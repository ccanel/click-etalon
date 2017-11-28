// -*- c-basic-offset: 4 -*-
/*
 * frontresizequeue.{cc,hh} -- queue element that drops from front on resize
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "frontresizequeue.hh"
#include <click/confparse.hh>
#include <click/etheraddress.hh>
#include <clicknet/ether.h>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
CLICK_DECLS

static const unsigned char default_destination[6] = {
    0x01, 0x80, 0xC2, 0x00, 0x00, 0x01
};

static const unsigned char default_source[6] = {
    0xF4, 0x52, 0x14, 0x15, 0x6D, 0x31
};

FrontResizeQueue::FrontResizeQueue()
{
    _mark_fraction = 0.5;
    _circuit = false;
}

void *
FrontResizeQueue::cast(const char *n)
{
  if (strcmp(n, "FrontResizeQueue") == 0)
    return (FrontResizeQueue *)this;
  else
    return NotifierQueue::cast(n);
}

// int
// FrontResizeQueue::live_reconfigure(Vector<String> &conf, ErrorHandler *errh)
// {
//   // printf("resizing\n");
//   // change the maximum queue length at runtime
//   Storage::index_type old_capacity = _capacity;
//   int old_len = size();
//   if (configure(conf, errh) < 0)
//     return -1;
//   if (_capacity == old_capacity || !_q)
//     return 0;
//   Storage::index_type new_capacity = _capacity;
//   _capacity = old_capacity;

//   Packet **new_q = (Packet **) CLICK_LALLOC(sizeof(Packet *) * (new_capacity + 1));
//   if (new_q == 0)
//     return errh->error("out of memory");


//   // // printf("building\n");
//   // // build new ft list
//   // five_tuple_list *ftl = NULL;
//   // for (Storage::index_type k = head(); k != tail(); k = next_i(k)) {
//   //     // printf("getting ft\n");
//   //     five_tuple_list *ft = get_ft(_q[k]);
//   //     // printf("adding ft\n");
//   //     add_ft_if_not_in_list(&ftl, ft);
//   // }
//   // // printf("done building\n");

//   // // printf("dropping\n");
//   // // create new queue by selectively dropping packets
//   // Storage::index_type i = tail(), j = new_capacity;
//   // while (j != 0 && i != head()) {
//   //     i = prev_i(i);
//   //     five_tuple_list *ft = get_ft(_q[i]);
//   //     five_tuple_list *p = get_ft_in_list(ftl, ft);
//   //     free(ft);
//   //     if (p == NULL) {
//   // 	  if (name() == "hybrid_switch/q12/q") {
//   // 	      printf("RESIZE BROKEN, %p\n", ftl);
//   // 	  }
//   // 	  continue;
//   //     }
//   //     if (_capacity > old_capacity || p->count < 4) {
//   // 	  --j;
//   // 	  new_q[j] = _q[i];
//   //     } else {
//   // 	  _q[i]->kill();
//   //     }
//   // }
//   // // printf("done dropping\n");

//   Storage::index_type i = tail(), j = new_capacity;
//   while (j != 0 && i != head()) {
//       i = prev_i(i);
//       --j;
//       new_q[j] = _q[i];
//   }
//   while (i != head()) {
//       i = prev_i(i);
//       _q[i]->kill();
//   }

//   // printf("normal\n");
//   CLICK_LFREE(_q, sizeof(Packet *) * (_capacity + 1));
//   _q = new_q;
//   set_head(j);
//   set_tail(new_capacity);
//   _capacity = new_capacity;
//   // printf("done normal\n");

//   // // printf("clearing\n");
//   // // clear out old ft list
//   // for(five_tuple_list *p = ftl; p != NULL;) {
//   //     five_tuple_list *old = p;
//   //     p = p->next;
//   //     free(old);
//   // }
//   // // printf("done clear\n");

//   if (name() == "hybrid_switch/q12/q") {
//       printf("name = %s, old_len = %d, len = %d, cap = %d\n", name().c_str(), old_len, size(), _capacity);
//   }

//   // printf("done resizing\n");
//   return 0;
// }

five_tuple_list *FrontResizeQueue::get_ft(Packet *p) {
    five_tuple_list *ft = (five_tuple_list*)malloc(sizeof(five_tuple_list));
    ft->transport = 0;
    ft->next = 0;
    ft->count = 0;
	  
    const click_ip *ipp = p->ip_header();
    if (ipp->ip_p == IP_PROTO_TCP) { // TCP
	ft->sport = p->tcp_header()->th_sport;
	ft->dport = p->tcp_header()->th_dport;
	ft->transport = 1;
    } else if (ipp->ip_p == IP_PROTO_UDP) { // UDP
	ft->sport = p->udp_header()->uh_sport;
	ft->dport = p->udp_header()->uh_dport;
	ft->transport = 2;
    }
    ft->ip_src = ipp->ip_src;
    ft->ip_dst = ipp->ip_dst;
    return ft;
}

five_tuple_list *FrontResizeQueue::get_ft_in_list(five_tuple_list* ftl,
						  five_tuple_list* ft) {
    for(five_tuple_list *p = ftl; p != NULL; p = p->next) {
	if (same_ft(p, ft))
	    return p;
    }
    return NULL;
}

bool FrontResizeQueue::same_ft(five_tuple_list* ft, five_tuple_list* other) {
    if (ft->ip_src == other->ip_src &&
	ft->ip_dst == other->ip_dst &&
	ft->sport == other->sport &&
	ft->dport == other->dport &&
	ft->transport == other->transport)
	return true;
    return false;
}

void FrontResizeQueue::add_ft_if_not_in_list(five_tuple_list** ftl,
					     five_tuple_list* ft) {
    if (*ftl == NULL) {
	*ftl = ft;
    } else {
	five_tuple_list *p;
	for(p = *ftl; p != NULL; p = p->next) {
	    if (same_ft(p, ft))
		break;
	}
	if (p == NULL) { // didn't find ft in list
	    for(p = *ftl; p->next != NULL; p = p->next) {}
	    p->next = ft;
	} else {
	    free(ft);
	}
    }
}

void FrontResizeQueue::add_ft_count_in_list(five_tuple_list** ftl,
					    five_tuple_list* ft) {
    if (*ftl == NULL) {
	*ftl = ft;
	ft->count = 1;
    } else {
	five_tuple_list *p;
	for(p = *ftl; p != NULL; p = p->next) {
	    if (same_ft(p, ft))
		break;
	}
	if (p == NULL) { // didn't find ft in list
	    for(p = *ftl; p->next != NULL; p = p->next) {}
	    p->next = ft;
	    ft->count = 1;
	} else {
	    p->count++;
	    free(ft);
	}
    }
}

void
FrontResizeQueue::take_state(Element *e, ErrorHandler *errh)
{
    SimpleQueue *q = (SimpleQueue *)e->cast("SimpleQueue");
    if (!q)
	return;

    if (tail() != head() || head() != 0) {
	errh->error("already have packets enqueued, can%,t take state");
	return;
    }

    set_tail(_capacity);
    Storage::index_type i = _capacity, j = q->tail();
    while (i > 0 && j != q->head()) {
	i--;
	j = q->prev_i(j);
	_q[i] = q->packet(j);
    }
    set_head(i);
    _highwater_length = size();

    if (j != q->head())
	errh->warning("some packets lost (old length %d, new capacity %d)",
		      q->size(), _capacity);
    while (j != q->head()) {
	j = q->prev_i(j);
	q->packet(j)->kill();
    }
    q->set_head(0);
    q->set_tail(0);
}

void
FrontResizeQueue::push(int, Packet *p)
{
    pthread_mutex_lock(&_lock);
    // Code taken from SimpleQueue::push().
    Storage::index_type h = head(), t = tail(), nt = next_i(t);

    // // float r = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
    // float r = 0.5;
    float s = (float)size();
    int d = _capacity;
    // if (r < s / d) {
    if (s > _mark_fraction * d) {
    	// if (WritablePacket *q = p->uniqueify()) {
    	//     q->ip_header()->ip_tos |= IP_ECN_CE;
    	//     p = q;
    	//     // printf("set ECN, %f, %f, %d, %f\n", r, s, d, s/d);
    	// }
	
	// build PAUSE frame
	int packets_over = round(s - _mark_fraction*d);
	int bw = !_circuit ? 0.5*pow(10,9) : 4*pow(10,9);
	float ns_per_packet = (1.0 / bw) * 8 * 9000 * pow(10,9);
	float quanta_in_ns = (1.0 / (40*pow(10,9))) * 512 * pow(10,9);
	float quanta_per_packet = ns_per_packet / quanta_in_ns;
	
	EtherAddress src(default_source), dst(default_destination);
	uint16_t pausetime = 65535;
	if (quanta_per_packet * packets_over <= 65535)
	    pausetime = quanta_per_packet * packets_over;
	WritablePacket *q;
	if (!(q = Packet::make(64))) {
	    // return errh->error("out of memory!"), -ENOMEM;
	    printf("couldn't create pause frame, oom\n");
	} else {
	    // printf("building pause frame, quanta=%d\n", pausetime);
	    q->set_mac_header(q->data(), sizeof(click_ether));
	    click_ether *ethh = q->ether_header();
	    memcpy(ethh->ether_dhost, &dst, 6);
	    memcpy(ethh->ether_shost, &src, 6);
	    ethh->ether_type = htons(ETHERTYPE_MACCONTROL);

	    click_ether_macctl *emch = (click_ether_macctl *) q->network_header();
	    emch->ether_macctl_opcode = htons(ETHER_MACCTL_OP_PAUSE);
	    emch->ether_macctl_param = htons(pausetime);
	    memset(emch->ether_macctl_reserved, 0, sizeof(emch->ether_macctl_reserved));
	    output(1).push(q);
	}
    }

    // five_tuple_list *ftl = NULL;
    // for (Storage::index_type k = head(); k != tail(); k = next_i(k)) {
    // 	// printf("getting ft\n");
    // 	five_tuple_list *ft = get_ft(_q[k]);
    // 	// printf("adding ft\n");
    // 	add_ft_count_in_list(&ftl, ft);
    // }

    // five_tuple_list *ft = get_ft_in_list(ftl, get_ft(p));
    if ((nt != h)) { //&& (!ft || _capacity >= 100 || ft->count < 4)) {
        push_success(h, t, nt, p);
        enqueue_bytes += p->length();
    }
    else {
	push_failure(p);
    }
    pthread_mutex_unlock(&_lock);
}

// Packet *
// FrontResizeQueue::pull(int)
// {
//     return deq();
// }

int
FrontResizeQueue::change_mark_fraction(const String &str, Element *e, void *, ErrorHandler *)
{
    FrontResizeQueue *frq = static_cast<FrontResizeQueue *>(e);
    pthread_mutex_lock(&(frq->_lock));
    frq->_mark_fraction = atof(str.c_str());
    pthread_mutex_unlock(&(frq->_lock));
    return 0;
}

String
FrontResizeQueue::get_mark_fraction(Element *e, void *)
{
    FrontResizeQueue *frq = static_cast<FrontResizeQueue *>(e);
    pthread_mutex_lock(&(frq->_lock));
    int mf = frq->_mark_fraction;
    pthread_mutex_unlock(&(frq->_lock));
    return String(mf);
}

int
FrontResizeQueue::change_circuit(const String &str, Element *e, void *, ErrorHandler *)
{
    FrontResizeQueue *frq = static_cast<FrontResizeQueue *>(e);
    pthread_mutex_lock(&(frq->_lock));
    frq->_circuit = (str == "1");
    pthread_mutex_unlock(&(frq->_lock));
    return 0;
}

void
FrontResizeQueue::add_handlers()
{
    FullNoteQueue::add_handlers();
    add_write_handler("mark_fraction", change_mark_fraction, 0);
    add_read_handler("mark_fraction", get_mark_fraction, 0);
    add_write_handler("circuit", change_circuit, 0);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(FullNoteQueue)
EXPORT_ELEMENT(FrontResizeQueue)
