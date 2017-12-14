// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ECEMARK_HH
#define CLICK_ECEMARK_HH
#include <click/element.hh>
#include <pthread.h>
CLICK_DECLS

/* =c
 * ECEMark()
 * =s basicmod
 * Marks ECE bits when a rack has/is going to have a circuit.
 * =d
 *
 * Marks ECE bits when a rack has/is going to have a circuit.
 *
 * =a AlignmentInfo, click-align(1) */

class ECEMark : public Element { public:

    ECEMark() CLICK_COLD;

    const char *class_name() const		{ return "ECEMark"; }
    const char *port_count() const		{ return PORTS_1_1; }

    int initialize(ErrorHandler *errh);
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    
    Packet *simple_action(Packet *);

    pthread_mutex_t lock;

private:
    static int set_ece(const String&, Element*, void*, ErrorHandler*);
    int *ece_map;
    int _num_hosts;
};

CLICK_ENDDECLS
#endif
