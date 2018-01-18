// -*- c-basic-offset: 4 -*-
/*
 * fullnotelockqueue.{cc,hh} -- queue element that notifies on full
 * Eddie Kohler
 *
 * Copyright (c) 2004-2007 Regents of the University of California
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
#include "fullnotelockqueue.hh"
#include <tuple>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>

CLICK_DECLS

FullNoteLockQueue::FullNoteLockQueue()
{
    _xadu_access = _xdeq = _xenq = 0;
    use_adus = false;
}

void *
FullNoteLockQueue::cast(const char *n)
{
    if (strcmp(n, "FullNoteLockQueue") == 0)
	return (FullNoteLockQueue *)this;
    else if (strcmp(n, Notifier::FULL_NOTIFIER) == 0)
	return static_cast<Notifier*>(&_full_note);
    else
	return NotifierQueue::cast(n);
}

int
FullNoteLockQueue::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _full_note.initialize(Notifier::FULL_NOTIFIER, router());
    _full_note.set_active(true, false);
    return NotifierQueue::configure(conf, errh);
}

int
FullNoteLockQueue::live_reconfigure(Vector<String> &conf, ErrorHandler *errh)
{
    do {
    } while (_xdeq.compare_swap(0, 1) != 0);
    do {
    } while (_xenq.compare_swap(0, 1) != 0);
    int r = NotifierQueue::live_reconfigure(conf, errh);
    if (r >= 0 && size() < capacity() && _q)
	_full_note.wake();
    _xenq = 0;
    _xdeq = 0;
    return r;
}

void
FullNoteLockQueue::push(int, Packet *p)
{
    do {
    } while (_xenq.compare_swap(0, 1) != 0);

    Storage::index_type h = head(), t = tail(), nt = next_i(t);
    if (nt != h) {
	push_success(h, t, nt, p);
	_xenq = 0;
	_byte_count += p->length();
    }
    else {
	_xenq = 0;
	push_failure(p);
    }
}

Packet *
FullNoteLockQueue::pull(int)
{
    do {
    } while (_xdeq.compare_swap(0, 1) != 0);

    Storage::index_type h = head(), t = tail(), nh = next_i(h);
    if (h != t) {
        Packet *p = pull_success(h, nh);
	_xdeq = 0;
	_byte_count -= p->length();

        if (use_adus && p->has_transport_header()) {
            const click_ip *ipp = p->ip_header();
	    struct in_addr src_ip = ipp->ip_src;
	    struct in_addr dst_ip = ipp->ip_dst;
	    unsigned int tplen = p->length() - (ipp->ip_hl * 4);
	    uint16_t sport = 0;
	    uint16_t dport = 0;
	    uint32_t seq = 0;
	    bool not_tcp = true;
            if (ipp->ip_p == IP_PROTO_TCP) { // TCP
                tplen -= p->tcp_header()->th_off * 4;
		sport = p->tcp_header()->th_sport;
		dport = p->tcp_header()->th_dport;
		seq = (uint32_t)p->tcp_header()->th_seq;
		not_tcp = false;
            }
            else if (ipp->ip_p == IP_PROTO_UDP) { // UDP
                tplen -= 8;
		sport = p->udp_header()->uh_sport;
		dport = p->udp_header()->uh_dport;
            }
	    if (p->length() < tplen) {
		printf("error: data segment larger than packet?\n");
		exit(EXIT_FAILURE);
	    }
	    if (tplen) { // data packet
		tcp_and_seq t = std::make_tuple(src_ip, dst_ip, sport, dport, seq);
		Timestamp now;
		now.assign_now();
		if (not_tcp || seen_seq.find(t) == seen_seq.end() ||
		    (now - seen_seq[t]).doubleval() > 1.0) {
		    // seq not found or seq seen before but not for a second
		    traffic_info info;
		    info.src = src_ip;
		    info.dst = dst_ip;
		    info.proto = ipp->ip_p;
		    info.sport = sport;
		    info.dport = dport;
		    info.size = 0;
		    do {
		    } while (_xadu_access.compare_swap(0, 1) != 0);
		    if (seen_adu.find(info) == seen_adu.end()) {
			seen_adu[info] = tplen;
		    } else {
			seen_adu[info] += tplen;
		    }
		    seen_seq[t].assign_now();
		    _xadu_access = 0;
		}
	    }
        }
        return p;
    }
    else {
	_xdeq = 0;
        return pull_failure();
    }
}

#if CLICK_DEBUG_SCHEDULING
String
FullNoteLockQueue::read_handler(Element *e, void *)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    return "nonempty " + fq->_empty_note.unparse(fq->router())
	+ "\nnonfull " + fq->_full_note.unparse(fq->router());
}

void
FullNoteLockQueue::add_handlers()
{
    NotifierQueue::add_handlers();
    add_read_handler("notifier_state", read_handler, 0);
}
#else

long long
FullNoteLockQueue::get_seen_adu(struct traffic_info info)
{
    long long size = 0;
    do {
    } while (_xadu_access.compare_swap(0, 1) != 0);
    if (seen_adu.find(info) != seen_adu.end())
	size = seen_adu[info];
    _xadu_access = 0;
    return size;
}

long long
FullNoteLockQueue::get_bytes()
{
    return _byte_count;
}

int
FullNoteLockQueue::resize_capacity(const String &str, Element *e, void *,
				   ErrorHandler *errh)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    Vector<String> conf;
    conf.push_back("CAPACITY " + str);
    fq->live_reconfigure(conf, errh);
    return 0;
}

int
FullNoteLockQueue::clear(const String &, Element *e, void *,
			 ErrorHandler *)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    do {
    } while (fq->_xadu_access.compare_swap(0, 1) != 0);
    fq->seen_adu = std::unordered_map<const struct traffic_info, long long,
				      info_key_hash, info_key_equal>();
    fq->_xadu_access = 0;
    return 0;
}

String
FullNoteLockQueue::get_resize_capacity(Element *e, void *)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    int cap = fq->_capacity;
    return String(cap);
}

void
FullNoteLockQueue::add_handlers()
{
    NotifierQueue::add_handlers();
    add_write_handler("resize_capacity", resize_capacity, 0);
    add_read_handler("resize_capacity", get_resize_capacity, 0);
    add_write_handler("clear", clear, 0);
}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(NotifierQueue)
EXPORT_ELEMENT(FullNoteLockQueue FullNoteLockQueue-FullNoteLockQueue)
