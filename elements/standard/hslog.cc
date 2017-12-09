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
#include <pthread.h>
CLICK_DECLS

HSLog::HSLog()
{
    pthread_mutex_init(&lock, NULL);
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
    _q12_len = new HandlerCall("hybrid_switch/q13/q.length");
    _q12_len->initialize(HandlerCall::f_read, this, errh);
    _q12_cap = new HandlerCall("hybrid_switch/q13/q.capacity");
    _q12_cap->initialize(HandlerCall::f_read, this, errh);

    ece_map = (int *)malloc(sizeof(int) * (_num_hosts + 1) * (_num_hosts + 1));
    for (int i = 0; i < _num_hosts + 1; i++) {
	for (int j = 0; j < _num_hosts + 1; j++) {
	    ece_map[i * (_num_hosts + 1) + j] = 0;
	}
    }

    current_circuits = (int *)malloc(sizeof(int) * (_num_hosts + 1));
    for (int i = 0; i < _num_hosts + 1; i++) {
	current_circuits[i] = 0;
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
    bool is_circuit = p->anno_u8(22) == 1;
    int dst_can_circuit_recv_from = p->anno_u8(23);

    pthread_mutex_lock(&lock);
    // change ECN on the ACKS
    bool have_circuit = ece_map[dst * (_num_hosts + 1) + src];

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
    pthread_mutex_unlock(&lock);
    return p;
}

int
HSLog::handler(const String &str, Element *e, void *, ErrorHandler *)
{
    HSLog *hsl = static_cast<HSLog *>(e);
    pthread_mutex_lock(&(hsl->lock));
    hsl->open_log(str.c_str());
    pthread_mutex_unlock(&(hsl->lock));
    return 0;
}

int
HSLog::set_ece(const String &str, Element *e, void *, ErrorHandler *)
{
    HSLog *hsl = static_cast<HSLog *>(e);
    int hosts = hsl->_num_hosts + 1;
    pthread_mutex_lock(&(hsl->lock));
    for(int i = 1; i < hosts; i++) {
	for(int j = 1; j < hosts; j++) {
	    hsl->ece_map[i * hosts + j] = 0;
	}
    }
    const char *s = str.c_str();
    for(unsigned int i = 0; i < strlen(s); i += 3) {
	int src = s[i] - '0'; // convert char digit to int;
	int dst = s[i+1] - '0';
	hsl->ece_map[src * hosts + dst] = 1;
    }
    pthread_mutex_unlock(&(hsl->lock));
    return 0;
}

Vector<String>
HSLog::split(const String &s, char delim) {
    Vector<String> elems;
    int prev = 0;
    for(int i = 0; i < s.length(); i++) {
        if (s[i] == delim) {
            elems.push_back(s.substring(prev, i-prev));
            prev = i+1;
        }
    }
    elems.push_back(s.substring(prev, s.length()-prev));
    return elems;
}

int
HSLog::set_circuit_event(const String &str, Element *e, void *, ErrorHandler *)
{
    HSLog *hsl = static_cast<HSLog *>(e);
    int hosts = hsl->_num_hosts + 1;
    Timestamp now;
    now.assign_now();
    pthread_mutex_lock(&(hsl->lock));
    for(int dst = 1; dst < hosts; dst++) {
	int src = hsl->current_circuits[dst];
	if (src != 0) {
	    fprintf(hsl->_fp, "%s: closing circuit %d -> %d\n",
		    now.unparse().c_str(), src, dst);
	}
    }
    Vector<String> c = HSLog::split(str, '/');
    for(int dst = 1; dst < hosts; dst++) {
	int src = atoi(c[dst-1].c_str()) + 1;
	hsl->current_circuits[dst] = src;
	if (src != 0) {
	    fprintf(hsl->_fp, "%s: starting circuit %d -> %d\n",
		    now.unparse().c_str(), src, dst);
	}
    }
    pthread_mutex_unlock(&(hsl->lock));
    return 0;
}

void
HSLog::add_handlers()
{
    add_write_handler("openLog", handler, 0);
    add_write_handler("setECE", set_ece, 0);
    add_write_handler("circuitEvent", set_circuit_event, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HSLog)
ELEMENT_MT_SAFE(HSLog)
