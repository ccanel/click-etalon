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

typedef struct {
    int type;
    char ts[32];
    int latency;
    int src;
    int dst;
    char data[64];
} hsl_s;

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
    static int set_log(const String&, Element*, void*, ErrorHandler*);
    static int disable_log(const String&, Element*, void*, ErrorHandler*);
    static int set_circuit_event(const String&, Element*, void*, ErrorHandler*);
    static Vector<String> split(const String&, char);
    int open_log(const char *);
    FILE *_fp;
    HandlerCall *_q12_len, *_q12_cap;
    int *current_circuits;
    int _num_hosts;
    bool _enabled;
};

CLICK_ENDDECLS
#endif
