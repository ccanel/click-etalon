// -*- c-basic-offset: 4 -*-
/*
 * icmptdnupdate.{cc,hh} -- Send ICMP ping packets.
 * Weiyang Wang
 *
 * Copyright (c) 1999-2020 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "icmptdnupdate.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/etheraddress.hh>
#include <clicknet/ether.h>
#include <clicknet/ip.h>
#include <clicknet/icmp.h>
#include <click/packet_anno.hh>
#include <click/integers.hh>
#include <click/handlercall.hh>
#include <click/straccum.hh>
#if CLICK_LINUXMODULE
# include <click/cxxprotect.h>
CLICK_CXX_PROTECT
# include <linux/vmalloc.h>
CLICK_CXX_UNPROTECT
# include <click/cxxunprotect.h> 
#endif
CLICK_DECLS

#define DST_HOST_IP(base, rackid, hostid) \
  ((struct in_addr){.s_addr = htonl(ntohl((base).s_addr) | ((uint32_t)(rackid) << 8) | (hostid + 1))})
#define PACKET_KEY(addr, tdn_id) \
  ((uint64_t)((addr).s_addr) << 32 | tdn_id)

#define HOST_FROM_IP(base, addr) \
  (uint8_t)(ntohl((addr).s_addr & (~(base).s_addr)) & (uint32_t)(0x000000FF))
#define RACK_FROM_IP(base, addr) \
  (uint8_t)((ntohl((addr).s_addr & (~(base).s_addr)) & (uint32_t)(0x0000FF00)) >> 8)

ICMPTDNUpdate::ICMPTDNUpdate() : _verbose(true), _test(false), _timer(this), h(nullptr), n_rack(0), n_host(0), n_tdn(0) {

}

ICMPTDNUpdate::~ICMPTDNUpdate() {

}

int ICMPTDNUpdate::configure(Vector<String> &conf, ErrorHandler *errh) {
  if (Args(conf, this, errh)
      .read_mp("IPSRC", src_addr)
      .read_mp("ETHSRC", EtherAddressArg(), _ethh.ether_shost)
      .read_mp("IPBASE", base_addr)
      .read_mp("ETHBASE", EtherAddressArg(), _ethh.ether_dhost)
      .read_mp("CTRLNIC", ctrlnic)
      .read("NTDN", n_tdn)
      .read("NRACK", n_rack)
      .read("NHOST", n_host)
      .read("TEST", _test)
      .complete() < 0) {

	  return -1;
  }
  return 0;
}

int ICMPTDNUpdate::initialize(ErrorHandler * errh) {
  // preconstruct packet if parameters are specified
  // this also assumes the address assignment to be very deterministic:
  // rack i, host j <-> base_addr || ()
  if (_test) {
    _timer.initialize(this);
    _timer.schedule_after_msec(1000);
    if (n_rack == 0) n_rack = 2;
    if (n_host == 0) n_rack = 2;
    if (n_tdn == 0) n_tdn = 2;
    char handler_char[64] = "t/source.updateAll";
    h = new HandlerCall(handler_char);
    int result = h->initialize(HandlerCall::f_write, this, errh);
    click_chatter("h: %p; res: %d\n", h, result);
  }
  _ethh.ether_type = htons(0x0800);

  if (n_rack > 0 && n_host > 0 && n_tdn > 0 && base_addr.s_addr != 0) {
    for (uint8_t i = 0; i < n_rack; i++)
      for (uint8_t j = 0; j < n_host; j++)
        for (uint8_t k = 0; k < n_tdn; k++) {
          struct in_addr dst_ip = DST_HOST_IP(base_addr, i, j);
          Packet * update_packet = generate_packet_by_ip_tdn(dst_ip, k);
          cache_packets[PACKET_KEY(dst_ip, k)] = update_packet;
          if (_verbose) {
            click_chatter("Key: %llu", PACKET_KEY(dst_ip, k));
		        click_chatter("Constructed TDN update pacekt for %s:::%u", IPAddress(dst_ip).unparse().c_str(), k);
          }
        }
  }
  return 0;
}

void ICMPTDNUpdate::cleanup(CleanupStage) {
  // destructor is private...
  // for (auto & item: cache_packets) {
  //   delete item->second;
  // } 
}

/*
 * For debugging purpose...
 */
void ICMPTDNUpdate::run_timer(Timer *) {
  static uint8_t curr_tdn = 0;
  // send_update_all(curr_tdn, nullptr);
  h->call_write(String((int)curr_tdn));
  curr_tdn = (curr_tdn + 1) % n_tdn; 
  if (_verbose)
    click_chatter("Timer fired. curr_tdn: %u", curr_tdn);
  _timer.reschedule_after_msec(1000); 
}

// this is a source module - output port should be "Push", 
// but there can be a "pull" version?
void ICMPTDNUpdate::push(int, Packet *) {

}

Packet* ICMPTDNUpdate::pull(int) {
  return nullptr; 
}

int ICMPTDNUpdate::send_update_host(struct in_addr host_ip, uint8_t new_tdn, ErrorHandler* errh) {
  auto packet_iter = cache_packets.find(PACKET_KEY(host_ip, new_tdn));
  if (packet_iter != cache_packets.end()) {
    Packet * p = packet_iter->second->clone();
    if (_verbose)
      click_chatter("Packet ptr: %p", p);
    output(0).push(p);
    return 0;
  }

  // no cache found
  Packet * p = generate_packet_by_ip_tdn(host_ip, new_tdn);
  if (!p) {
    errh->error("Failed to constrct packet");
  }

  // add to cache
  cache_packets[PACKET_KEY(host_ip, new_tdn)] = p->clone();
  output(0).push(p);
  return 0;
}

int ICMPTDNUpdate::send_update_rack(uint8_t rack_id, uint8_t new_tdn, ErrorHandler* errh) {

  // do the samething but iterate thru a rack
  for (uint8_t host_id = 0; host_id < n_host; host_id++) {
    struct in_addr host_ip = DST_HOST_IP(base_addr, rack_id, host_id);
    send_update_host(host_ip, new_tdn, errh);
  }
  return 0;

}

int ICMPTDNUpdate::send_update_all(uint8_t new_tdn, ErrorHandler* errh) {

  // do the samething but iterate everything...
  for (uint8_t rack_id = 0; rack_id < n_rack; rack_id++) {
    send_update_rack(rack_id, new_tdn, errh);
  }
  return 0;

}

Packet* ICMPTDNUpdate::generate_packet_by_ip_tdn(struct in_addr host_ip, uint8_t new_tdn) {

  WritablePacket * q = nullptr;
  
  size_t hsz = sizeof(click_ip) + sizeof(click_icmp_tdn) + sizeof(click_ether);
	q = Packet::make(hsz);
  if (!q)
	  return 0;

  memset(q->data(), 0, hsz);

  click_ether *neth = reinterpret_cast<click_ether *>(q->data());
  memcpy(neth, &_ethh, 14);
  neth->ether_dhost[3] = RACK_FROM_IP(base_addr, host_ip);
  neth->ether_dhost[4] = HOST_FROM_IP(base_addr, host_ip);
  neth->ether_dhost[5] = ctrlnic;

  click_ip *nip = reinterpret_cast<click_ip *>(q->data()+14);
  nip->ip_v = 4;
  nip->ip_hl = sizeof(click_ip) >> 2;
  nip->ip_len = htons(q->length() - 14);
  uint16_t ip_id = (new_tdn % 0xFFFF) + 1; // ensure ip_id != 0
  nip->ip_id = htons(ip_id);
  nip->ip_p = IP_PROTO_ICMP; /* icmp */
  nip->ip_ttl = 200;
  nip->ip_src = src_addr;
  nip->ip_dst = host_ip;
  nip->ip_sum = click_in_cksum((unsigned char *)nip, sizeof(click_ip));

  click_icmp_tdn *icp = (struct click_icmp_tdn *) (nip + 1);
  icp->icmp_type = ICMP_ACTIVE_TDN_ID;
  icp->icmp_code = 0;
  icp->newnet_id = new_tdn;

  icp->icmp_cksum = click_in_cksum((const unsigned char *)icp, sizeof(click_icmp_tdn));

  q->set_dst_ip_anno(IPAddress(host_ip));
  q->set_ip_header(nip, sizeof(click_ip));
  q->timestamp_anno().assign_now();

  return q;

}

Vector<String> ICMPTDNUpdate::split(const String &s, char delim) {
  Vector<String> elems;
  int prev = 0;
  for(int i = 0; i < s.length(); i++) {
    if (s[i] == delim) {
      elems.push_back(s.substring(prev, i-prev));
      prev = i + 1;
    }
  }
  elems.push_back(s.substring(prev, s.length()-prev));
  return elems;
}

int ICMPTDNUpdate::update_all(const String& str, Element* e, void*, ErrorHandler* errh) {
  printf("fda, %s\n", str.c_str());
  uint8_t new_tdn;
  if(!IntArg().parse(str, new_tdn)) {
    return errh->error("tdn needs to be an integer!");
  }
  printf("%u\n", new_tdn);
  printf("%s\n", e->name().c_str());
  ICMPTDNUpdate * itu = static_cast<ICMPTDNUpdate *>(e);
  printf("%p\n", itu);
  return itu->send_update_all(new_tdn, errh);
}

int ICMPTDNUpdate::update_rack(const String& str, Element* e, void*, ErrorHandler* errh) {
  uint8_t new_tdn;
  uint8_t rack_id;
  
  Vector<String> args = split(str, ',');
  if (args.size() != 2) {
    return errh->error("Rack update takes <RACKID, NEWTDN>");
  }

  if(!IntArg().parse(args[0], rack_id)) {
    return errh->error("rack_id needs to be an integer!");
  }
  if(!IntArg().parse(args[1], new_tdn)) {
    return errh->error("tdn needs to be an integer!");
  }

  ICMPTDNUpdate * itu = static_cast<ICMPTDNUpdate *>(e);
  return itu->send_update_all(new_tdn, errh);
}

int ICMPTDNUpdate::update_host(const String& str, Element* e, void*, ErrorHandler* errh) {
  struct in_addr host_ip;
  uint8_t new_tdn;
  
  Vector<String> args = split(str, ',');
  if (args.size() != 2) {
    return errh->error("host update takes <HOST_IP, NEWTDN>");
  }

  if(!IPAddressArg().parse(args[0], host_ip)) {
    return errh->error("host_ip needs to be an IPV4 address!");
  }
  if(!IntArg().parse(args[1], new_tdn)) {
    return errh->error("tdn needs to be an integer!");
  }
  ICMPTDNUpdate * itu = static_cast<ICMPTDNUpdate *>(e);
  return itu->send_update_all(new_tdn, errh);
}

void ICMPTDNUpdate::add_handlers() {
  add_write_handler("updateAll", update_all, 0);
  add_write_handler("updateRack", update_rack, 0);
  add_write_handler("updateHost", update_host, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(ICMPTDNUpdate ICMPTDNUpdate-ICMPTDNUpdate)
