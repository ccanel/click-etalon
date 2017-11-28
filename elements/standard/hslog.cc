// -*- c-basic-offset: 4 -*-
/*
 * hslog.{cc,hh} -- Logs hybrid switch packet info
 * Eddie Kohler
 *
 * Copyright (c) 2004 Regents of the University of California
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
#include <click/handlercall.hh>
#include "hslog.hh"
#include <click/packet_anno.hh>
#include <click/args.hh>
#include <clicknet/tcp.h>
CLICK_DECLS

HSLog::HSLog()
{
}

int
HSLog::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_mp("NUM_HOSTS", _num_hosts)
	.complete() < 0)
	return -1;
    if (_num_hosts == 0)
	return -1;
    _fp = NULL;
    return 0;
}

int
HSLog::initialize(ErrorHandler* errh)
{
    _q12_len = new HandlerCall("hybrid_switch/q12/q.length");
    _q12_len->initialize(HandlerCall::f_read, this, errh);
    _q12_cap = new HandlerCall("hybrid_switch/q12/q.capacity");
    _q12_cap->initialize(HandlerCall::f_read, this, errh);

    _circuit_source = (HandlerCall **)malloc(sizeof(HandlerCall *) * _num_hosts + 1);
    for(int dst = 1; dst < _num_hosts + 1; dst++) {
	char handler[500];
	sprintf(handler, "hybrid_switch/coc%d.color", dst);
	_circuit_source[dst] = new HandlerCall(handler);
	_circuit_source[dst]->initialize(HandlerCall::f_read, this, errh);
    }
    
    if (open_log("/tmp/hslog.log"))
    	return 1;
    return 0;
}

int
HSLog::open_log(const char *fn)
{
    if (_fp) {
	fclose(_fp);
    }
    _fp = fopen(fn, "w");
    if (_fp == NULL) {
	printf("error initializing log file\n");
	return 1;
    }
    return 0;
}

Packet *
HSLog::simple_action(Packet *p)
{
    Timestamp now;
    now.assign_now();
    float latency = strtof((now - CONST_FIRST_TIMESTAMP_ANNO(p)).unparse().c_str(),
			   NULL);
    latency *= 1000000;
    latency /= 20; // TDF

    int len = atoi(_q12_len->call_read().c_str());
    int cap = atoi(_q12_cap->call_read().c_str());    

    int src = p->anno_u8(20);
    int dst = p->anno_u8(21);
    bool is_circuit = p->anno_u8(22) == 1 ? true : false;
    int dst_can_circuit_recv_from = p->anno_u8(23);

    // change ECN on the ACKS
    int c_src = atoi(_circuit_source[src]->call_read().c_str());
    bool have_circuit = c_src == dst;

    if (have_circuit) {
	if (WritablePacket *q = p->uniqueify()) {
	    if (q->ip_header()->ip_p == IP_PROTO_TCP) { // TCP
		q->tcp_header()->th_flags |= TH_ECE;
	    }
	    p = q;
	}
    }

    if (is_circuit) { // circuit
	fprintf(_fp, "%s: %d -> %d (%d bytes), circuit, %d %d, %d can recv from %d, " \
		"latency %fus, have_circuit %d\n",
		now.unparse().c_str(), src, dst,
		p->length(), len, cap, dst, dst_can_circuit_recv_from, latency,
		have_circuit);
    } else { // packet
	fprintf(_fp, "%s: %d -> %d (%d bytes), packet, %d %d, %d can recv from %d, " \
		"latency %fus, have_circuit %d\n",
		now.unparse().c_str(), src, dst,
		p->length(), len, cap, dst, dst_can_circuit_recv_from, latency,
		have_circuit);
    }
    return p;
}

int
HSLog::handler(const String &str, Element *e, void *, ErrorHandler *)
{
    HSLog *hsl = static_cast<HSLog *>(e);
    hsl->open_log(str.c_str());
    return 0;
}

void
HSLog::add_handlers()
{
    add_write_handler("openLog", handler, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HSLog)
ELEMENT_MT_SAFE(HSLog)
