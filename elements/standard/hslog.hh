// -*- c-basic-offset: 4 -*-
#ifndef CLICK_HSLOG_HH
#define CLICK_HSLOG_HH
#include <click/element.hh>
CLICK_DECLS

/* =c
 * HSLog()
 * =s basicmod
 * Logs hybrid switch packet info
 * =d
 *
 * Logs hybrid switch packet info
 *
 * =a AlignmentInfo, click-align(1) */

class HSLog : public Element { public:

    HSLog() CLICK_COLD;

    const char *class_name() const		{ return "HSLog"; }
    const char *port_count() const		{ return PORTS_1_1; }

    int initialize(ErrorHandler *errh);
    void add_handlers() CLICK_COLD;
    
    Packet *simple_action(Packet *);

private:
    static int handler(const String&, Element*, void*, ErrorHandler*);
    int open_log(const char *);
    FILE *_fp;
    // int _circuit_dest[9];
    HandlerCall *_q12_len, *_q12_cap;
};

CLICK_ENDDECLS
#endif
