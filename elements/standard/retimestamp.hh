// -*- c-basic-offset: 4 -*-
#ifndef CLICK_RETIMESTAMP_HH
#define CLICK_RETIMESTAMP_HH
#include <click/element.hh>
CLICK_DECLS

/* =c
 * ReTimestamp()
 * =s basicmod
 * Changes flowgrind timestamp to current time
 * =d
 *
 * Changes flowgrind timestamp in TCP packet to current time
 *
 * =a AlignmentInfo, click-align(1) */

class ReTimestamp : public Element { public:

    ReTimestamp() CLICK_COLD;

    const char *class_name() const		{ return "ReTimestamp"; }
    const char *port_count() const		{ return PORTS_1_1; }
    
    Packet *simple_action(Packet *);
};

CLICK_ENDDECLS
#endif
