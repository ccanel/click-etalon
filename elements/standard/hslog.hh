// -*- c-basic-offset: 4 -*-
#ifndef CLICK_HSLOG_HH
#define CLICK_HSLOG_HH
#include <click/element.hh>
#include <pthread.h>
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
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    
    Packet *simple_action(Packet *);

    pthread_mutex_t lock;

private:
    static int handler(const String&, Element*, void*, ErrorHandler*);
    static int set_ece(const String&, Element*, void*, ErrorHandler*);
    static int set_circuit_event(const String&, Element*, void*, ErrorHandler*);
    static Vector<String> split(const String&, char);
    int open_log(const char *);
    FILE *_fp;
    HandlerCall *_q12_len, *_q12_cap;
    int *ece_map;
    int *current_circuits;
    int _num_hosts;
};

CLICK_ENDDECLS
#endif
