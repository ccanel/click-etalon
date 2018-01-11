// -*- c-basic-offset: 4 -*-
/*
 * solstice.{cc,hh} -- Computes OCS Solstice Schedules
 * Matt Mukerjee
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
#include <click/handlercall.hh>
#include <click/args.hh>
#include "solstice.hh"
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>
#include <sys/select.h>

CLICK_DECLS

Solstice::Solstice() : _task(this)
{
}

int
Solstice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int num_hosts, reconfig_delay, tdf;
    uint32_t circuit_bw, packet_bw;
    if (Args(conf, this, errh)
        .read_mp("NUM_HOSTS", num_hosts)
        .read_mp("CIRCUIT_BW", BandwidthArg(), circuit_bw)
        .read_mp("PACKET_BW", BandwidthArg(), packet_bw)
        .read_mp("RECONFIG_DELAY", reconfig_delay)
        .read_mp("TDF", tdf)
        .complete() < 0)
        return -1;
    _num_hosts = num_hosts;
    
    if (_num_hosts == 0)
        return -1;

    _traffic_matrix = (long long *)malloc(sizeof(long long)
                                          * _num_hosts * _num_hosts);
    bzero(_traffic_matrix, sizeof(long long) * _num_hosts * _num_hosts);

    sols_init(&_s, _num_hosts);
    _s.night_len = reconfig_delay * tdf;  // reconfiguration us
    _s.week_len = 2000 * tdf;  // schedule max length us
    _s.min_day_len = 2 * reconfig_delay * tdf;  // minimum configuration length us
    _s.skip_trim = true;
    _s.day_len_align = 1;  // ???
    _s.link_bw = int(circuit_bw / 1000000); // 4Gbps (in bytes / us)
    _s.pack_bw = int(packet_bw / 1000000); // 0.5Gbps (in bytes / us)

    _print = 0;
    _print2 = 0;

    enabled = true;

    return 0;
}
 
int
Solstice::initialize(ErrorHandler *errh)
{
    ScheduleInfo::initialize_task(this, &_task, true, errh);
    
#if defined(__linux__)
    sched_setscheduler(getpid(), SCHED_RR, NULL);
#endif

    _tm = new HandlerCall("traffic_matrix.getTraffic");
    _tm->initialize(HandlerCall::f_read, this, errh);

    _runner = new HandlerCall("runner.setSchedule");
    _runner->initialize(HandlerCall::f_write, this, errh);

    return 0;
}

bool
Solstice::run_task(Task *)
{
    while(1) {
        int stopped = 0;
        while(!enabled) { // if disabled externally
            if (!stopped) {
                printf("****solstice stopping...\n");
                stopped = 1;
            }
            sleep(1);
        }
        if (stopped) {
            printf("****soltice starting...\n");
        }

        // get traffic matrix from estimator and unparse.
        String tm = _tm->call_read();
        // if(_print == 0) {
        //     printf("tm = %s\n", tm.c_str());
        // }
        int start = 0;
        int sd = 0;
        for (int ind = 0; ind < tm.length(); ind++) {
            if (tm[ind] == ' ') {
                _traffic_matrix[sd]  = atoll(tm.substring(start, ind-start).c_str());
                start = ind+1;
                sd++;
            }
        }
        _traffic_matrix[_num_hosts * _num_hosts - 1] =
            atoll(tm.substring(start).c_str());

        /* setup the demand here */
        uint64_t cap = _s.week_len * (_s.link_bw + _s.pack_bw);
        for (int src = 0; src < _num_hosts; src++) {
            for (int dst = 0; dst < _num_hosts; dst++) {
                uint64_t v = _traffic_matrix[src * _num_hosts + dst];
                if (v > cap)
                    v = cap;
                sols_mat_set(&_s.future, src, dst, v);
            }
        }

	// if some demand is greater than 1,000,000,
	// ignore demand lower than 1,000,000
	bool big_demand = false;
	for (int src = 0; src < _num_hosts; src++) {
	    for (int dst = 0; dst < _num_hosts; dst++) {
		if (sols_mat_get(&_s.future, src, dst) >= 1000000)
		    big_demand = true;
	    }
	}
	if (big_demand) {
	    for (int src = 0; src < _num_hosts; src++) {
		for (int dst = 0; dst < _num_hosts; dst++) {
		    if (sols_mat_get(&_s.future, src, dst) < 1000000) {
			sols_mat_set(&_s.future, src, dst, 0);
		    }
		}
	    }
	}

        /* inflate demand to 1/2 weeklen */
        /* find largest row or column sum and increase demand to 1/2 weeklen */
        /* allows solstice to schedule small flows letting TCP grow */
        uint64_t max_col = 0;
        uint64_t max_row = 0;
        for (int src = 0; src < _num_hosts; src++) {
            uint64_t current_row = 0;
            for (int dst = 0; dst < _num_hosts; dst++)
                current_row += sols_mat_get(&_s.future, src, dst);
            if (current_row > max_row)
                max_row = current_row;
        }
        for (int dst = 0; dst < _num_hosts; dst++) {
            uint64_t current_col = 0;
            for (int src = 0; src < _num_hosts; src++)
                current_col += sols_mat_get(&_s.future, src, dst);
            if (current_col > max_col)
                max_col = current_col;
        }
        double min_demand = _s.week_len * _s.link_bw * 0.5;
        double scale_factor = 0;
        if (max_row || max_col) {
            scale_factor = max_row > max_col ?
                min_demand / max_row : min_demand / max_col;
        }
        if (scale_factor < 1)
            scale_factor = 1;
        int empty_demand = 1;
        for (int dst = 0; dst < _num_hosts; dst++) {
            for (int src = 0; src < _num_hosts; src++) {
                uint64_t current = sols_mat_get(&_s.future, src, dst);
                uint64_t v = current * scale_factor;
                sols_mat_set(&_s.future, src, dst, static_cast<uint64_t>(v));
                if (v)
                    empty_demand = 0;
            }
        }

        sols_schedule(&_s);
        sols_check(&_s);

        // each configuration string should be at most _num_hosts*3 length
        // each duration string should be at most 4 length
        // there are _s.nday days and nights
        // add an additional factor of 2 for number of days and delimiters
        char *schedule = (char *)malloc(sizeof(char) * _s.nday *
                                        2 * (4 + _num_hosts*3) * 2);
        sprintf(schedule, "%d ", _s.nday * 2);
        
        for (int i = 0; i < _s.nday; i++) {
            if (i > 0)
                sprintf(&(schedule[strlen(schedule)]), " ");
            sols_day_t *day = &_s.sched[i];
            sprintf(&(schedule[strlen(schedule)]), "%ld ", day->len - _s.night_len);

            for (int dst = 0; dst < _num_hosts; dst++) {
                int src = day->input_ports[dst];
                if (src < 0) {
                    printf("SOLSTICE BAD PORT\n");
                    return true;
                }
                sprintf(&(schedule[strlen(schedule)]), "%d/", src);
            }
            schedule[strlen(schedule)-1] = '\0';

            // nights
            sprintf(&(schedule[strlen(schedule)]), " %ld ", _s.night_len);
            for (int src = 0; src < _num_hosts; src++) {
                sprintf(&(schedule[strlen(schedule)]), "%d/", -1);
            }
            schedule[strlen(schedule)-1] = '\0';
        }

        _print = (_print+1) % 5000;
        _print2 = (_print2+1) % 50000;

        // print demand and scaled matrix
        if(!empty_demand && !_print) {
            printf("[demand]\t\t\t\t\t[scaled]\n");
            for (int src = 0; src < _num_hosts; src++) {
                for (int dst = 0; dst < _num_hosts; dst++) {
                    if (dst > 0) printf(" ");
                    uint64_t v = _traffic_matrix[src * _num_hosts + dst];
                    if (v == 0)
                        printf(".");
                    else
                        printf("%ld", v);
                }
                printf("\t\t\t\t\t");
                for (int dst = 0; dst < _num_hosts; dst++) {
                    if (dst > 0) printf(" ");
                    uint64_t v = sols_mat_get(&_s.future, src, dst);
                    if (v == 0)
                        printf(".");
                    else
                        printf("%ld", v);
                }
                printf("\n");
            }
            printf("schedule == %s\n", schedule);
        }

        if(!_print2) {
            printf("****Solstice still running...\n");
        }

        // tell schedule runner
        _runner->call_write(schedule);

        free(schedule);
    }
    return true;
}

int
Solstice::set_enabled(const String &str, Element *e, void *, ErrorHandler *)
{
    Solstice *s = static_cast<Solstice *>(e);
    BoolArg::parse(str, s->enabled, ArgContext());
    return 0;
}

void
Solstice::add_handlers()
{
    add_write_handler("setEnabled", set_enabled, 0);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(Solstice)
ELEMENT_REQUIRES(userlevel)
