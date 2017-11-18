#ifndef CLICK_FRONTRESIZEQUEUE_HH
#define CLICK_FRONTRESIZEQUEUE_HH
#include "fullnotequeue.hh"
CLICK_DECLS

/*
=c

FrontResizeQueue
FrontResizeQueue(CAPACITY)

=s storage

stores packets in FIFO queue that drops packets from the front during resize

=d

Stores incoming packets in a first-in-first-out queue. Drops packets from the front
when changing CAPACITY if the new capacity is smaller than the old one. The default
for CAPACITY is 1000.

=h length read-only

Returns the current number of packets in the queue.

=h highwater_length read-only

Returns the maximum number of packets that have ever been in the queue at once.

=h capacity read/write

Returns or sets the queue's capacity.

=h drops read-only

Returns the number of packets dropped by the Queue so far.  Dropped packets
are emitted on output 1 if output 1 exists.

=h reset_counts write-only

When written, resets the C<drops> and C<highwater_length> counters.

=h reset write-only

When written, drops all packets in the Queue.

=a Queue, SimpleQueue, MixedQueue, RED
*/

typedef struct five_tuple_list five_tuple_list;
struct five_tuple_list {
  struct in_addr ip_src, ip_dst;
  uint16_t sport, dport;
  int transport;
  int count;
  five_tuple_list *next;
};


class FrontResizeQueue : public FullNoteQueue { public:

  FrontResizeQueue() CLICK_COLD;

  const char *class_name() const		{ return "FrontResizeQueue"; }
  void *cast(const char *);
  void add_handlers() CLICK_COLD;

  // int live_reconfigure(Vector<String> &, ErrorHandler *);
  void take_state(Element *, ErrorHandler *);

  five_tuple_list *get_ft(Packet*);
  five_tuple_list *get_ft_in_list(five_tuple_list*, five_tuple_list*);
  bool same_ft(five_tuple_list*, five_tuple_list*);
  void add_ft_if_not_in_list(five_tuple_list**, five_tuple_list*);
  void add_ft_count_in_list(five_tuple_list**, five_tuple_list*);

  static int change_mark_fraction(const String&, Element*, void*, ErrorHandler*);
  static String get_mark_fraction(Element *e, void *user_data);

  void push(int, Packet*);

  float _mark_fraction;
};

CLICK_ENDDECLS
#endif
