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

ICMPTDNUpdate::ICMPTDNUpdate() {

}

ICMPTDNUpdate::~ICMPTDNUpdate() {

}

int ICMPTDNUpdate::configure(Vector<String> &conf, ErrorHandler *errh) {

}

int ICMPTDNUpdate::initialize(ErrorHandler *errh) {

}

void ICMPTDNUpdate::cleanup(CleanupStage) {

}

void ICMPTDNUpdate::add_handlers() {

}

/*
 * For debugging purpose...
 */
void ICMPTDNUpdate::run_timer(Timer *) {

}

void ICMPTDNUpdate::push(int, Packet *p) {

}

Packet* ICMPTDNUpdate::pull(int) {

}

void ICMPTDNUpdate::send_update_host(struct in_addr host_ip, uint8_t new_tdn) {

}

void ICMPTDNUpdate::send_update_rack(int rack_id, uint8_t new_tdn) {

}

Packet* ICMPTDNUpdate::generate_packet_by_ip_tdn(struct in_addr host_ip, uint8_t new_tdn) {

}

Packet* ICMPTDNUpdate::make_packet(WritablePacket *q) {

}

CLICK_ENDDECLS
EXPORT_ELEMENT(ICMPPingSource ICMPPingSource-ICMPSendPings)
