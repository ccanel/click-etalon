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
#include <pthread.h>
#include <tuple>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>

CLICK_DECLS

FullNoteLockQueue::FullNoteLockQueue()
{
    pthread_mutex_init(&_lock, NULL);
    enqueue_bytes = 0;
    dequeue_bytes = 0;
    dequeue_bytes_no_headers = 0;
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
    int r = NotifierQueue::live_reconfigure(conf, errh);
    if (r >= 0 && size() < capacity() && _q)
	_full_note.wake();
    return r;
}

void
FullNoteLockQueue::push(int, Packet *p)
{
    pthread_mutex_lock(&_lock);
    // Code taken from SimpleQueue::push().
    Storage::index_type h = head(), t = tail(), nt = next_i(t);

    if (nt != h) {
        push_success(h, t, nt, p);
        enqueue_bytes += p->length();
    }
    else {
        push_failure(p);
    }
    pthread_mutex_unlock(&_lock);
}

Packet *
FullNoteLockQueue::pull(int)
{
    pthread_mutex_lock(&_lock);
    // Code taken from SimpleQueue::deq.
    Storage::index_type h = head(), t = tail(), nh = next_i(h);

    if (h != t) {
        Packet *p = pull_success(h, nh);
        dequeue_bytes += p->length();

        if (p->has_transport_header()) {
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
		    if (seen_adu.find(info) == seen_adu.end()) {
			seen_adu[info] = tplen;
		    } else {
			seen_adu[info] += tplen;
		    }
		    seen_seq[t].assign_now();
		}

	    }
        }
	pthread_mutex_unlock(&_lock);
        return p;
    }
    else {
	pthread_mutex_unlock(&_lock);
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
String
FullNoteLockQueue::read_enqueue_bytes(Element *e, void *)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    return String(fq->enqueue_bytes);
}

String
FullNoteLockQueue::read_dequeue_bytes(Element *e, void *)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    return String(fq->dequeue_bytes);
}

long long
FullNoteLockQueue::get_seen_adu(struct traffic_info info)
{
    pthread_mutex_lock(&_lock);
    long long size = 0;
    if (seen_adu.find(info) != seen_adu.end())
	size = seen_adu[info];
    pthread_mutex_unlock(&_lock);
    return size;
}

String
FullNoteLockQueue::read_bytes(Element *e, void *)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);

    pthread_mutex_lock(&(fq->_lock));
    int byte_count = 0;
    Storage::index_type h = fq->head(), t = fq->tail();
    while (h != t) {
        byte_count += fq->_q[h]->length();
        h = fq->next_i(h);
    }
    pthread_mutex_unlock(&(fq->_lock));
    return String(byte_count);
}

int
FullNoteLockQueue::resize_capacity(const String &str, Element *e, void *,
				   ErrorHandler *errh)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    pthread_mutex_lock(&(fq->_lock));
    Vector<String> conf;
    conf.push_back("CAPACITY " + str);
    fq->live_reconfigure(conf, errh);
    pthread_mutex_unlock(&(fq->_lock));
    return 0;
}

int
FullNoteLockQueue::clear(const String &, Element *e, void *,
			 ErrorHandler *)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    pthread_mutex_lock(&(fq->_lock));
    fq->enqueue_bytes = 0;
    fq->dequeue_bytes = 0;
    fq->seen_adu = std::unordered_map<const struct traffic_info, long long,
				      info_key_hash, info_key_equal>();
    pthread_mutex_unlock(&(fq->_lock));
    return 0;
}

String
FullNoteLockQueue::get_resize_capacity(Element *e, void *)
{
    FullNoteLockQueue *fq = static_cast<FullNoteLockQueue *>(e);
    pthread_mutex_lock(&(fq->_lock));
    int cap = fq->_capacity;
    pthread_mutex_unlock(&(fq->_lock));
    return String(cap);
}

void
FullNoteLockQueue::add_handlers()
{
    NotifierQueue::add_handlers();
    add_read_handler("enqueue_bytes", read_enqueue_bytes, 0);
    add_read_handler("dequeue_bytes", read_dequeue_bytes, 0);
    add_read_handler("bytes", read_bytes, 0);
    add_write_handler("resize_capacity", resize_capacity, 0);
    add_read_handler("resize_capacity", get_resize_capacity, 0);
    add_write_handler("clear", clear, 0);
}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(NotifierQueue)
EXPORT_ELEMENT(FullNoteLockQueue FullNoteLockQueue-FullNoteLockQueue)
