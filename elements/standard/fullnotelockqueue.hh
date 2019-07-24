// -*- c-basic-offset: 4 -*-
#ifndef CLICK_FULLNOTELOCKQUEUE_HH
#define CLICK_FULLNOTELOCKQUEUE_HH
#include "notifierqueue.hh"
#include <unordered_map>
#include <tuple>
#include <atomic>
CLICK_DECLS

/*
=c

LockQueue([CAPACITY, THRESH])

=s storage

stores packets in a FIFO queue

=d

Stores incoming packets in a first-in-first-out queue. Drops incoming packets if
the queue already holds CAPACITY packets. The default for CAPACITY is 1000. If
adding a packet to the queue would cause the queue's length to exceed THRESHOLD
packets, then the packet's THRESH_EXCEEDED_ANNO user annotation is set to 1. The
default for THRESHOLD is 40.

B<Note:> This element works in conjuction with MarkIPCE to provide ECN marking
for DCTCP. The actual ECN marking is performed by B<ECEMark>.

B<Note:> Threshold-based marking must be explicitly enabled using the
"marking_enabled" write handler (see below).

Queue notifies interested parties when it becomes empty and when a
formerly-empty queue receives a packet.  The empty notification takes place
some time after the queue goes empty, to prevent thrashing for queues that
hover around 1 or 2 packets long.  This behavior is the same as that of
NotifierQueue.  (See QuickNoteQueue for an alternative.)  Queue additionally
notifies interested parties that it is non-full, and when a formerly-full
queue gains some free space.  In all respects but notification, Queue behaves
exactly like SimpleQueue.

You may also use the old element name "FullNoteLockQueue".

B<Multithreaded Click note:> Queue is designed to be used in an environment
with at most one concurrent pusher and at most one concurrent puller.  Thus,
at most one thread pushes to the Queue at a time and at most one thread pulls
from the Queue at a time.  Different threads can push to and pull from the
Queue concurrently, however.  See ThreadSafeQueue for a queue that can support
multiple concurrent pushers and pullers.

=h length read-only

Returns the current number of packets in the queue.

=h highwater_length read-only

Returns the maximum number of packets that have ever been in the queue at once.

=h capacity read/write

Returns or sets the queue's capacity.

=h drops read-only

Returns the number of packets dropped by the queue so far.

=h reset_counts write-only

When written, resets the C<drops> and C<highwater_length> counters.

=h reset write-only

When written, drops all packets in the queue.

=h marking_enabled read/write

"true" or "false". When read, returns whether threshold-based marking is
enabled. When written, enables or disables marking.

=h marking_threshold read/write

When read, returns the current marking threshold. When written, modifies the
marking threshold.

=a ThreadSafeQueue, QuickNoteQueue, SimpleQueue, NotifierQueue, MixedQueue,
FrontDropQueue, LockQueue */

struct traffic_info {
    struct in_addr src;
    struct in_addr dst;
    uint8_t proto;
    uint16_t sport;
    uint16_t dport;
    long size;
};

struct info_key_hash : public std::unary_function<struct traffic_info, std::size_t>
{
    std::size_t operator()(const traffic_info& k) const
    {
	return k.src.s_addr ^ k.dst.s_addr ^ k.proto ^ k.sport ^ k.dport;
    }
};

struct info_key_equal : public std::binary_function<struct traffic_info, struct traffic_info, bool>
{
    bool operator()(const struct traffic_info& v0, const struct traffic_info& v1) const
    {
	return (
		v0.src.s_addr == v1.src.s_addr &&
		v0.dst.s_addr == v1.dst.s_addr &&
		v0.proto == v1.proto &&
		v0.sport == v1.sport &&
		v0.dport == v1.dport
		);
    }
};

typedef std::tuple<struct in_addr, struct in_addr, uint16_t, uint16_t, uint32_t> tcp_and_seq;

struct seq_key_hash : public std::unary_function<tcp_and_seq, std::size_t>
{
    std::size_t operator()(const tcp_and_seq& k) const
    {
	uint32_t src = IPAddress(std::get<0>(k)).addr();
	uint32_t dst = IPAddress(std::get<1>(k)).addr();
	uint32_t sport = (uint32_t)std::get<2>(k);
	uint32_t dport = (uint32_t)std::get<3>(k);
	uint32_t seq = std::get<4>(k);
	return src ^ dst ^ sport ^ dport ^ seq;
    }
};

struct seq_key_equal : public std::binary_function<tcp_and_seq, tcp_and_seq, bool>
{
    bool operator()(const tcp_and_seq& v0, const tcp_and_seq& v1) const
    {
	return (
		std::get<0>(v0) == std::get<0>(v1) &&
		std::get<1>(v0) == std::get<1>(v1) &&
		std::get<2>(v0) == std::get<2>(v1) &&
		std::get<3>(v0) == std::get<3>(v1) &&
		std::get<4>(v0) == std::get<4>(v1)
		);
    }
};

class FullNoteLockQueue : public NotifierQueue { public:

    FullNoteLockQueue() CLICK_COLD;

    const char *class_name() const		{ return "LockQueue"; }
    void *cast(const char *);

    int configure(Vector<String> &conf, ErrorHandler *) CLICK_COLD;
    int live_reconfigure(Vector<String> &conf, ErrorHandler *errh);
    void add_handlers() CLICK_COLD;

    void push(int port, Packet *p);
    Packet *pull(int port);

    long long get_bytes();
    long long get_seen_adu(struct traffic_info);
    int clear_adus();
    bool use_adus;

  protected:

    ActiveNotifier _full_note;

    inline void push_success(Storage::index_type h, Storage::index_type t,
			     Storage::index_type nt, Packet *p);
    inline void push_failure(Packet *p);
    inline Packet *pull_success(Storage::index_type h,
				Storage::index_type nh);
    inline Packet *pull_failure();


#if CLICK_DEBUG_SCHEDULING
    static String read_handler(Element *e, void *user_data) CLICK_COLD;
#endif
    static int resize_capacity(const String&, Element*, void*, ErrorHandler*);
    static String get_resize_capacity(Element *e, void *user_data);
    static int clear(const String&, Element*, void*, ErrorHandler*);
    static int set_marking_enabled(const String&, Element*, void*, ErrorHandler*);
    static String get_marking_enabled(Element *e, void *user_data);
    static int set_marking_thresh(const String&, Element*, void*, ErrorHandler*);
    static String get_marking_thresh(Element *e, void *user_data);

    std::unordered_map<const tcp_and_seq, Timestamp, seq_key_hash, seq_key_equal> seen_seq;
    std::unordered_map<const struct traffic_info, long long, info_key_hash, info_key_equal> seen_adu;

    int _thresh;
    bool _marking_enabled;

private:
    atomic_uint32_t _xadu_access;
    atomic_uint32_t _xdeq;
    atomic_uint32_t _xenq;

    /** @brief Validates whether a threshold value is well-formed.
     * @param thresh threshold to validate
     * @return true if thresh is well-formed, false otherwise */
    static bool validate_thresh(int thresh);
};

inline void
FullNoteLockQueue::push_success(Storage::index_type h, Storage::index_type t,
			    Storage::index_type nt, Packet *p)
{
    _q[t] = p;
    set_tail(nt);

    int s = size(h, nt);
    if (s > _highwater_length)
	_highwater_length = s;

    _empty_note.wake();

    if (s == capacity()) {
	_full_note.sleep();
#if HAVE_MULTITHREAD
	// Work around race condition between push() and pull().
	// We might have just undone pull()'s Notifier::wake() call.
	// Easiest lock-free solution: check whether we should wake again!
	if (size() < capacity())
	    _full_note.wake();
#endif
    }
}

inline void
FullNoteLockQueue::push_failure(Packet *p)
{
    if (_drops == 0 && _capacity > 0)
	click_chatter("%p{element}: overflow", this);
    _drops++;
    checked_output_push(1, p);
}

inline Packet *
FullNoteLockQueue::pull_success(Storage::index_type h,
			    Storage::index_type nh)
{
    Packet *p = _q[h];
    set_head(nh);

    _sleepiness = 0;
    _full_note.wake();
    return p;
}

inline Packet *
FullNoteLockQueue::pull_failure()
{
    if (_sleepiness >= SLEEPINESS_TRIGGER) {
        _empty_note.sleep();
#if HAVE_MULTITHREAD
	// Work around race condition between push() and pull().
	// We might have just undone push()'s Notifier::wake() call.
	// Easiest lock-free solution: check whether we should wake again!
	if (size())
	    _empty_note.wake();
#endif
    } else
	++_sleepiness;
    return 0;
}

CLICK_ENDDECLS
#endif
