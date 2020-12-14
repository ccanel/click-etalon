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
    const char *port_count() const		{ return "0/1"; }
    const char *processing() const		{ return "a/a"; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    void run_timer(Timer *);
    void push(int, Packet *);
    Packet* pull(int);

  private:

    unordered_map<uint64_t, Packet*> cache_packets;  // key: tdn << 32 | ip

    // most of these are just for cache construction
    uint8_t n_rack;
    uint8_t n_host;
    uint8_t n_tdn;
    struct in_addr base_addr;
    struct in_addr src_addr;

    Packet* generate_packet_by_ip_tdn(struct in_addr host_ip, uint8_t new_tdn);
    Vector<String> split(const String &s, char delim);

    int send_update_host(struct in_addr host_ip, uint8_t new_tdn, ErrorHandler* errh);
    int send_update_rack(uint8_t rack_id, uint8_t new_tdn, ErrorHandler* errh);
    int send_update_all(uint8_t new_tdn, ErrorHandler* errh);

    // static String read_handler(Element*, void*) CLICK_COLD;
    static int update_all(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
    static int update_host(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
    static int update_rack(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
