// -*- c-basic-offset: 4 -*-
/*
 * ecemark.{cc,hh} -- Marks ECE bits when a rack has/is going to have a circuit.
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
#include "ecemark.hh"
#include <click/args.hh>
#include <clicknet/tcp.h>
CLICK_DECLS

ECEMark::ECEMark()
{
}

int
ECEMark::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_mp("NUM_HOSTS", _num_hosts)
        .complete() < 0)
        return -1;
    if (_num_hosts == 0)
        return -1;
    _enabled = false;
    return 0;
}

int
ECEMark::initialize(ErrorHandler*)
{
  ece_map = (int *)malloc(sizeof(int) * (_num_hosts + 1) * (_num_hosts + 1));
  for (int i = 0; i < _num_hosts + 1; i++) {
    for (int j = 0; j < _num_hosts + 1; j++) {
	    ece_map[i * (_num_hosts + 1) + j] = 0;
    }
  }

  return 0;
}

Packet *
ECEMark::simple_action(Packet *p)
{
    if (p->ip_header()->ip_p != IP_PROTO_TCP)
	return p;
    if (!_enabled)
	return p;

    int src = p->anno_u8(20);
    int dst = p->anno_u8(21);

    // change ECN on the ACKS
    bool have_circuit = ece_map[dst * (_num_hosts + 1) + src];

    if (have_circuit) {
        if (WritablePacket *q = p->uniqueify()) {
	    click_tcp *tcph = q->tcp_header();
	    tcph->th_flags |= TH_ECE;

	    // new_sum = ~(~old_sum + ~old_halfword + new_halfword)
	    //         = ~(~old_sum + ~old_halfword + old_halfword + 0x0040)
	    //         = ~(~old_sum + ~0 + 0x0040)
	    //         = ~(~old_sum + 0x0040)
	    unsigned long sum = (~ntohs(tcph->th_sum) & 0xFFFF) + 0x0040;
	    tcph->th_sum = ~htons(sum + (sum >> 16));
	    return q;
        }
    }
    return p;
}

int
ECEMark::set_ece(const String &str, Element *e, void *, ErrorHandler *)
{
    ECEMark *ece = static_cast<ECEMark *>(e);
    int hosts = ece->_num_hosts + 1;
    int *emap = (int *)malloc(sizeof(int) * hosts * hosts);
    bzero(emap, sizeof(int) * hosts * hosts);
    const char *s = str.c_str();
    for(unsigned int i = 0; i < strlen(s); i += 3) {
        int src = s[i] - '0'; // convert char digit to int;
        int dst = s[i+1] - '0';
        emap[src * hosts + dst] = 1;
    }
    int *temp = ece->ece_map;
    // Set the new ece map. Do this before freeing the old ece map so that the ece
    // map is always valid.
    ece->ece_map = emap;
    if (temp != nullptr) {
        // Free the old ece map, if it had been set.
	free(temp);
    }
    return 0;
}

void
ECEMark::add_handlers()
{
    add_write_handler("setECE", set_ece, 0);
    add_write_handler("enabled", set_enabled, 0);
    add_read_handler("enabled", get_enabled, 0);
}

int
ECEMark::set_enabled(const String &str, Element *e, void *,
			     ErrorHandler *errh)
{
    ArgContext ctx(errh);
    bool new_enabled = false;
    if (!BoolArg().parse(str, new_enabled, ctx)) {
	return -1;
    }
    ECEMark *ece = static_cast<ECEMark *>(e);
    ece->_enabled = new_enabled;
    return 0;
}

String
ECEMark::get_enabled(Element *e, void *)
{
    ECEMark *ece = static_cast<ECEMark *>(e);
    return String(ece->_enabled);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(ECEMark)
ELEMENT_MT_SAFE(ECEMark)
