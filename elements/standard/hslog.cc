// -*- c-basic-offset: 4 -*-
/*
 * hslog.{cc,hh} -- Logs hybrid switch packet info
 * Matt Mukerjee
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
CLICK_DECLS

HSLog::HSLog()
{
    _enabled = true;
    _xfile_access = 0;
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
HSLog::initialize(ErrorHandler*)
{
    current_circuits = (int *)malloc(sizeof(int) * (_num_hosts + 1));
    for (int i = 0; i < _num_hosts + 1; i++) {
        current_circuits[i] = 0;
    }

    circuit_event_buffer = (hsl_s *)malloc(sizeof(hsl_s) * _num_hosts * 2);
    bzero(circuit_event_buffer, sizeof(hsl_s) * _num_hosts * 2);
    
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
    if (_enabled) {
	hsl_s msg;
	bzero(&msg, sizeof(msg));
	Timestamp now;
	now.assign_now();
	strncpy(msg.ts, now.unparse().c_str(), 31);
	float latency = strtof((now - CONST_FIRST_TIMESTAMP_ANNO(p)).unparse().c_str(),
			       NULL);
	latency *= 1e6;
	latency /= 20; // TDF
	msg.latency = (int)latency;

	memcpy(msg.data, p->data(), 64);
	do {
	} while(_xfile_access.compare_swap(0, 1) != 0);
	fwrite(&msg, sizeof(msg), 1, _fp);
	_xfile_access = 0;
    }
    return p;
}

int
HSLog::set_log(const String &str, Element *e, void *, ErrorHandler *)
{
    HSLog *hsl = static_cast<HSLog *>(e);
    do {
    } while(hsl->_xfile_access.compare_swap(0, 1) != 0);
    hsl->open_log(str.c_str());
    hsl->_xfile_access = 0;
    hsl->_enabled = true;
    return 0;
}

int
HSLog::disable_log(const String&, Element *e, void *, ErrorHandler *)
{
    HSLog *hsl = static_cast<HSLog *>(e);
    hsl->_enabled = false;
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
    if (hsl->_enabled) {
	int hosts = hsl->_num_hosts + 1;
	Timestamp now;
	now.assign_now();
	const char *now_str = now.unparse().c_str();
	int nmemb = 0;
	hsl_s *msg;
	for(int dst = 1; dst < hosts; dst++) {
	    int src = hsl->current_circuits[dst];
	    if (src != 0) {
		msg = &(hsl->circuit_event_buffer[nmemb]);
		msg->type = 2;
		strncpy(msg->ts, now_str, 31);
		msg->src = src;
		msg->dst = dst;
		nmemb++;
	    }
	}
	Vector<String> c = HSLog::split(str, '/');
	for(int dst = 1; dst < hosts; dst++) {
	    int src = atoi(c[dst-1].c_str()) + 1;
	    hsl->current_circuits[dst] = src;
	    if (src != 0) {
		msg = &(hsl->circuit_event_buffer[nmemb]);
		msg->type = 1;
		strncpy(msg->ts, now_str, 31);
		msg->src = src;
		msg->dst = dst;
		nmemb++;
	    }
	}
	if (nmemb) {
	    do {
	    } while(hsl->_xfile_access.compare_swap(0, 1) != 0);
	    fwrite(hsl->circuit_event_buffer, sizeof(hsl_s), nmemb, hsl->_fp);
	    hsl->_xfile_access = 0;
	}
    }
    return 0;
}

void
HSLog::add_handlers()
{
    add_write_handler("openLog", set_log, 0);
    add_write_handler("disableLog", disable_log, 0);
    add_write_handler("circuitEvent", set_circuit_event, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HSLog)
ELEMENT_MT_SAFE(HSLog)
