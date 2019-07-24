// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ECEMARK_HH
#define CLICK_ECEMARK_HH
#include <click/element.hh>
CLICK_DECLS

/* =c
 *
 * ECEMark()
 *
 * =s basicmod
 *
 * Marks ECE bits when a rack has/is going to have a circuit.
 *
 * =d
 *
 * Marks ECE bits when a rack has/is going to have a circuit.
 *
 * B<Note:> This element must be explicitly enabled using the "enabled" write
 * handler (see below).
 *
 * =h enabled read/write
 *
 * "true" or "false". When read, returns whether ECE marking is enabled. When
 * written, enables or disables marking.
 *
 * =h setECE write-only
 *
 * Set new ECE map. */
class ECEMark : public Element { public:

    ECEMark() CLICK_COLD;

    const char *class_name() const		{ return "ECEMark"; }
    const char *port_count() const		{ return PORTS_1_1; }

    int initialize(ErrorHandler *errh);
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    
    Packet *simple_action(Packet *);

protected:
    bool _enabled;

private:
    static int set_ece(const String&, Element*, void*, ErrorHandler*);
    static int set_enabled(const String&, Element*, void*, ErrorHandler*);
    static String get_enabled(Element *e, void *user_data);
    int *ece_map;
    int _num_hosts;
};

CLICK_ENDDECLS
#endif
