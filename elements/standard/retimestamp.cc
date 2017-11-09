// -*- c-basic-offset: 4 -*-
/*
 * retimestamp.{cc,hh} -- Changes flowgrind timestamp to current time
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
#include "retimestamp.hh"
#include <click/args.hh>
#include <clicknet/tcp.h>
CLICK_DECLS

ReTimestamp::ReTimestamp()
{
}

Packet *
ReTimestamp::simple_action(Packet *p)
{
    if (p->length() <= 8000 || !p->has_transport_header()) {
        return p;
    }
    const click_ip *ipp = p->ip_header();
    if (ipp->ip_p == IP_PROTO_UDP) {
	return p;
    } // at this point, TCP packet > 8000 bytes
    if (WritablePacket *q = p->uniqueify()) {
	int start = q->transport_header_offset() + q->tcp_header()->th_off * 4;
	unsigned char *d = q->data();
	unsigned int x = start;
	// int total = 0;
	for(; x < q->length(); x++) {
	    if(d[x] == 0x22) {
		const int offset = 6;
		if (x + offset + sizeof(struct timespec) >= q->length()) {
		    printf("problems\n");
		    break;
		}
		struct timespec *tp = (struct timespec *)(&(d[x + offset]));
		// unsigned long long np = tp->tv_sec * 1000000000 + tp->tv_nsec;
		// printf("curr = %llu, %ld, %ld\n", np, tp->tv_sec, tp->tv_nsec);
		// printf("d[x] = %d\n", d[x]);
		
		clock_gettime(CLOCK_REALTIME, tp);

		// // Time dilation
		unsigned long long ns = tp->tv_sec * 1000000000 + tp->tv_nsec;
		ns /= 20; // TDF
		tp->tv_sec = ns / 1000000000;
		tp->tv_nsec = ns % 1000000000;
		
		x += (offset + sizeof(struct timespec) - 1);
		// total++;
	    }
	}
	// if (total > 1) {
	//     printf("total = %d\n", total);
	// }
        return q;
    } else
        return 0;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(ReTimestamp)
ELEMENT_MT_SAFE(ReTimestamp)
