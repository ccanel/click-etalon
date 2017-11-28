// -*- c-basic-offset: 4 -*-
/*
 * run_schedule.{cc,hh} -- Runs an optical switching schedule
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
#include "run_schedule.hh"
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>
#include <sys/select.h>
#include <pthread.h>

CLICK_DECLS

RunSchedule::RunSchedule() : new_sched(false), _task(this), _num_hosts(0),
                             _big_buffer_size(100), _small_buffer_size(10),
                             _print(0)
{
    pthread_mutex_init(&lock, NULL);
}

int
RunSchedule::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_mp("NUM_HOSTS", _num_hosts)
        .read_mp("RESIZE", do_resize)
        .complete() < 0)
        return -1;
    if (_num_hosts == 0)
        return -1;
    next_schedule = "";
    return 0;
}
 
int
RunSchedule::initialize(ErrorHandler *errh)
{
    ScheduleInfo::initialize_task(this, &_task, true, errh);
    
#if defined(__linux__)
    sched_setscheduler(getpid(), SCHED_RR, NULL);
#endif

    _queue_capacity = (HandlerCall **)malloc(sizeof(HandlerCall *) * _num_hosts * _num_hosts);
    for(int src = 0; src < _num_hosts; src++) {
        for(int dst = 0; dst < _num_hosts; dst++) {
            char handler[500];
            sprintf(handler, "hybrid_switch/q%d%d/q.resize_capacity", src+1, dst+1);
            // sprintf(handler, "hybrid_switch/q%d%d/q.mark_fraction", src+1, dst+1);
            _queue_capacity[src * _num_hosts + dst] = new HandlerCall(handler);
            _queue_capacity[src * _num_hosts + dst]->
                initialize(HandlerCall::f_read | HandlerCall::f_write,
                           this, errh);
        }
    }

    _queue_sizes = (int *)malloc(sizeof(int) * _num_hosts * _num_hosts);
    for(int src = 0; src < _num_hosts; src++) {
	for(int dst = 0; dst < _num_hosts; dst++) {
	    _queue_sizes[src * _num_hosts + dst] = -1;
	}
    }

    _pull_switch = (HandlerCall **)malloc(sizeof(Handler *) * _num_hosts);
    for(int dst = 0; dst < _num_hosts; dst++) {
        char handler[500];
        sprintf(handler, "hybrid_switch/circuit_link%d/ps.switch", dst+1);
        _pull_switch[dst] = new HandlerCall(handler);
        _pull_switch[dst]->initialize(HandlerCall::f_write, this, errh);
    }

    _packet_pull_switch = (HandlerCall **)malloc(sizeof(HandlerCall *) * \
    						 _num_hosts * _num_hosts);
    for(int src = 0; src < _num_hosts; src++) {
        for(int dst = 0; dst < _num_hosts; dst++) {
            char handler[500];
            sprintf(handler, "hybrid_switch/pps%d%d.switch", src+1, dst+1);
            _packet_pull_switch[src * _num_hosts + dst] = new HandlerCall(handler);
            _packet_pull_switch[src * _num_hosts + dst]->
                initialize(HandlerCall::f_write, this, errh);
        }
    }

    _circuit_label = (HandlerCall **)malloc(sizeof(Handler *) * _num_hosts);
    for(int dst = 0; dst < _num_hosts; dst++) {
	char handler[500];
	sprintf(handler, "hybrid_switch/coc%d.color", dst+1);
	_circuit_label[dst] = new HandlerCall(handler);
	_circuit_label[dst]->initialize(HandlerCall::f_write, this, errh);
    }

    _packet_label = (HandlerCall **)malloc(sizeof(Handler *) * _num_hosts);
    for(int dst = 0; dst < _num_hosts; dst++) {
	char handler[500];
	sprintf(handler, "hybrid_switch/cop%d.color", dst+1);
	_packet_label[dst] = new HandlerCall(handler);
	_packet_label[dst]->initialize(HandlerCall::f_write, this, errh);
    }

    return 0;
}

int
RunSchedule::handler(const String &str, Element *e, void *, ErrorHandler *)
{
    RunSchedule *rs = static_cast<RunSchedule *>(e);

    pthread_mutex_lock(&(rs->lock));
    if (rs->next_schedule != str) {
	rs->new_sched = true;
    }
    rs->next_schedule = String(str);
    pthread_mutex_unlock(&(rs->lock));
    return 0;
}

int
RunSchedule::resize_handler(const String &str, Element *e, void *, ErrorHandler *)
{
    RunSchedule *rs = static_cast<RunSchedule *>(e);

    pthread_mutex_lock(&(rs->lock));
    bool current = rs->do_resize;
    BoolArg::parse(str, rs->do_resize, ArgContext());
    if (rs->do_resize && rs->do_resize != current) {
        // get sizes based on queues sizes
        // rs->_big_buffer_size = atoi(rs->_queue_capacity[0]->call_read().c_str());
        rs->_big_buffer_size = atof(rs->_queue_capacity[0]->call_read().c_str());
        rs->_small_buffer_size = rs->_big_buffer_size / 4;
        if (rs->_small_buffer_size < 1) {
            rs->_small_buffer_size = 1;
        }
        printf("auto resizing: %d -> %d\n", rs->_small_buffer_size,
               rs->_big_buffer_size);
        // printf("auto resizing: %f -> %f\n", rs->_small_buffer_size,
        //        rs->_big_buffer_size);
    }
    pthread_mutex_unlock(&(rs->lock));
    return 0;
}

Vector<String>
RunSchedule::split(const String &s, char delim) {
    Vector<String> elems;
    int prev = 0;
    for(int i = 0; i < s.length(); i++) {
        if (s[i] == delim) {
            elems.push_back(s.substring(prev, i-prev));
            prev = i+1;
        }
    }
    elems.push_back(s.substring(prev, s.length()-prev));
    return elems;
}

int
RunSchedule::execute_schedule(ErrorHandler *)
{
    // parse schedule...

    // num_schedules [duration config]
    // confg = src_for_dst_0/src_for_dst_1/...
    // '2 180 1/2/3/0 20 -1/-1/-1/-1'
    pthread_mutex_lock(&lock);
    String current_schedule = String(next_schedule);
    bool resize = do_resize;
    int small_size = _small_buffer_size;
    int big_size = _big_buffer_size;
    bool new_s = new_sched;
    new_sched = false;
    pthread_mutex_unlock(&lock);

        
    _print = (_print+1) % 100;
    if (!_print) {
	if (current_schedule)
	    printf("running sched %s\n", current_schedule.c_str());
    }
    
    if (current_schedule == "")
        return 0;
    Vector<String> v = RunSchedule::split(current_schedule, ' ');

    int num_configurations = atoi(v[0].c_str());

    int *durations = (int *)malloc(sizeof(int) * num_configurations);
    Vector<int> *configurations = new Vector<int>[num_configurations];
    int j = 0;
    Vector<String> c;
    for(int i = 1; i < v.size(); i+=2) {
        durations[j] = atoi(v[i].c_str());
        c = RunSchedule::split(v[i+1], '/');
        for(int k = 0; k < c.size(); k++) {
            configurations[j].push_back(atoi(c[k].c_str()));
        }
        j++;
    }

    // re-enable packet switch
    if (new_s && num_configurations == 1) {
	for(int i = 0; i < _num_hosts; i++) {
	    for(int j = 0; j < _num_hosts; j++) {
		int dst = i;
		int src = j;
		if (configurations[0][i] == -1) {
		    _packet_pull_switch[src * _num_hosts + dst]->call_write(String(0));
		} else {
		    _packet_pull_switch[src * _num_hosts + dst]->call_write(String(-1));
		}
	    }
	}
    }


    // at the beginning of the 'week' set all the buffers to full size
    // int *buffer_times = (int *)malloc(sizeof(int) * _num_hosts * _num_hosts);
    // if (resize) {
    // 	memset(buffer_times, 0, sizeof(int) * _num_hosts * _num_hosts);
    // 	for(int m = 0; m < num_configurations; m++) {
    // 	    Vector<int> configuration = configurations[m];
    // 	    for(int i = 0; i < _num_hosts; i++) {
    // 		int dst = i;
    // 		int src = configuration[i];
    // 		if (src == -1)
    // 		    continue;
    // 		_queue_capacity[src * _num_hosts + dst]->call_write(String(big_size));
    // 		buffer_times[src * _num_hosts + dst]++;
    // 	    }
    // 	}
    // }

    // make first days buffers big
    int DAYS_OUT = 2;
    // if(resize) {
    // 	for(int k = 0; k < DAYS_OUT; k++) {
    // 	    for(int i = 0; i < _num_hosts; i++) {
    // 		int dst = i;
    // 		int src = configurations[k % num_configurations][i];
    // 		if (src == -1)
    // 		    continue;
    // 		_queue_capacity[src * _num_hosts + dst]->call_write(String(big_size));
    // 	    }
    // 	}
    // }

    // for each configuration in schedule
    for(int m = 0; m < num_configurations; m++) {
        // make next days buffer big
        if(resize) {
            // for(int k = -1 * DAYS_OUT; k <= DAYS_OUT; k++) {
            for(int k = 0; k <= DAYS_OUT; k++) {
                for(int i = 0; i < _num_hosts; i++) {
                    int dst = i;
		    int index = (m + k) % num_configurations;
		    index = index < 0 ? index + num_configurations : index;
                    int src = configurations[index][i];
                    if (src == -1)
                        continue;
		    if (_queue_sizes[src * _num_hosts + dst] != big_size) {
			_queue_capacity[src * _num_hosts + dst]->
			    call_write(String(big_size));
		    }
		    _queue_sizes[src * _num_hosts + dst] = big_size;
                }
            }
        }

        // if (!_print && m == 12) {
        //     int len = atoi(HandlerCall::call_read("hybrid_switch/q12/q.length",
        //                                           this).c_str());
        //     int cap = atoi(HandlerCall::call_read("hybrid_switch/q12/q.capacity",
        //                                           this).c_str());
        //     printf("config 12: %d %d\n", len, cap);
        // }

        Vector<int> configuration = configurations[m];
        int duration = durations[m]; // microseconds
        struct timespec start_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        // set configuration
        for(int i = 0; i < _num_hosts; i++) {
            int dst = i;
            int src = configuration[i];
            _pull_switch[dst]->call_write(String(src));

	    if (src == -1 && num_configurations > 1) {
		if (!new_s || m+1 < num_configurations) {
		    Vector<int> next_config = configurations[(m+1) % num_configurations];
		    int next_src = next_config[i];
		    _packet_pull_switch[next_src * _num_hosts + dst]->call_write(String(-1));
		}
	    }

	    _circuit_label[dst]->call_write(String(src+1));
	    _packet_label[dst]->call_write(String(src+1));


            // probably just remove this? we aren't signaling TCP to dump.
            // sprintf(handler, "hybrid_switch/ecnr%d/s.switch %d", dst, src);
            // HandlerCall::call_write(handler, this);
        }

        // wait duration
        long long elapsed_nano = 0;

        struct timespec ts_new;

        while (elapsed_nano < duration * 1000) {
            clock_gettime(CLOCK_MONOTONIC, &ts_new);
            elapsed_nano = (1000000000 * ts_new.tv_sec + ts_new.tv_nsec)
                - (1000000000 * start_time.tv_sec + start_time.tv_nsec);
        }    
        start_time = ts_new;

        // make this -DAYS_OUT buffers smaller
	// only if this (src, dst) pair isn't in the next k configs
        if(resize) {
            for(int i = 0; i < _num_hosts; i++) {
                int dst = i;
		int k = 0; //-1 * DAYS_OUT;
		int index = (m + k) % num_configurations;
		index = index < 0 ? index + num_configurations : index;
                int src = configurations[index][i];
		bool not_found = true;
		// for (k = -1 * DAYS_OUT + 1; k <= DAYS_OUT; k++) {
		for (k = 1; k <= DAYS_OUT; k++) {
		    index = (m + k) % num_configurations;
		    index = index < 0 ? index + num_configurations : index;
		    int src2 = configurations[index][i];
		    if (src == src2)
			not_found = false;
		}
                if (src == -1)
                    continue;
		if (not_found) {
		    if (_queue_sizes[src * _num_hosts + dst] != small_size) {
			_queue_capacity[src * _num_hosts + dst]->
			    call_write(String(small_size));
		    }
		    _queue_sizes[src * _num_hosts + dst] = small_size;
		}
            }
        }

        // resize buffer if needed
        // if(resize) {
        //     for(int i = 0; i < _num_hosts; i++) {
        //         int dst = i;
        //         int src = configuration[i];
        //         if (src == -1)
        //             continue;
        //         buffer_times[src * _num_hosts + dst]--;
        //         if (buffer_times[src * _num_hosts + dst] == 0) {
        //             _queue_capacity[src * _num_hosts + dst]->
        //                 call_write(String(small_size));
        //         }
        //     }
        // }

	// re-enable packet switch
        for(int i = 0; i < _num_hosts; i++) {
	    int dst = i;
            int src = configuration[i];
	    if (src != -1 && num_configurations > 1) {
		_packet_pull_switch[src * _num_hosts + dst]->call_write(String(0));
	    }
        }


    }    
    free(durations);
    // free(buffer_times);

    if(!do_resize && resize) {
        // reset all queues back to large size
        for(int i = 0; i < _num_hosts; i++) {
            for (int j = 0; j < _num_hosts; j++) {
		if (_queue_sizes[i * _num_hosts + j] != big_size) {
		    _queue_capacity[i * _num_hosts + j]->
			call_write(String(big_size));
		}
		_queue_sizes[i * _num_hosts + j] = big_size;
            }
        }
    }


    return 0;
}

bool
RunSchedule::run_task(Task *)
{
    while(1) {
        execute_schedule(ErrorHandler::default_handler());
        // if (rc)
        //     return;
    }
    return true;
}

void
RunSchedule::add_handlers()
{
    add_write_handler("setSchedule", handler, 0);
    add_write_handler("setDoResize", resize_handler, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RunSchedule)
ELEMENT_REQUIRES(userlevel)
