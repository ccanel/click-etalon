// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ICMPTDN_UPDATE_HH
#define CLICK_ICMPTDN_UPDATE_HH
#include <click/element.hh>
#include <click/timer.hh>
#include <click/ipaddress.hh>
CLICK_DECLS

class ICMPTDNUpdate : public Element { public:

    ICMPTDNUpdate() CLICK_COLD;
    ~ICMPTDNUpdate() CLICK_COLD;

    const char *class_name() const		{ return "ICMPTDNUpdate"; }
    const char *port_count() const		{ return "0-1/1"; }
    const char *processing() const		{ return "h/a"; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    void run_timer(Timer *);
    void push(int, Packet *);
    Packet* pull(int);

    void send_update_host(struct in_addr host_ip, uint8_t new_tdn);
    void send_update_rack(int rack_id, uint8_t new_tdn);

  private:

    unordered_map<uint64_t, Packet*> cache_packets;  // key: tdn << 32 | ip

    // most of these are just for cache construction
    int n_rack = 0;
    int n_host = 0;
    int n_tdn = 0;
    struct in_addr base_addr;

    Packet* generate_packet_by_ip_tdn(struct in_addr host_ip, uint8_t new_tdn);

    Packet* make_packet(WritablePacket *q);
    static String read_handler(Element*, void*) CLICK_COLD;
    // static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
