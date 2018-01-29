// -*- c-basic-offset: 4 -*-
#ifndef CLICK_SOLSTICE_HH
#define CLICK_SOLSTICE_HH
#include <click/sols.h>
#include <click/element.hh>
#include <click/timer.hh>
CLICK_DECLS

/*
=c

Solstice()

=s control

Computes OCS Solstice Schedules

=d

TODO

=h

*/

class Solstice : public Element {
  public:
    Solstice() CLICK_COLD;

    const char *class_name() const	{ return "Solstice"; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    bool run_task(Task *);

    bool enabled;
    bool use_adus;

  private:
    static int set_enabled(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
    static int set_thresh(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;

    sols_t _s;
    long long *_traffic_matrix;
    Task _task;
    int _num_hosts;
    int _print;
    int _print2;
    unsigned int _thresh;

    HandlerCall *_tm;
    HandlerCall *_runner;
};

CLICK_ENDDECLS
#endif
