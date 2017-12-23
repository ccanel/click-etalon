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
                             _big_buffer_size(128), _small_buffer_size(16),
                             _print(0), _in_advance(12000), _next_time(0)
{
    pthread_mutex_init(&lock, NULL);
    clock_gettime(CLOCK_MONOTONIC, &_start_time);
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

    _queue_capacity = (HandlerCall **)malloc(sizeof(HandlerCall *) *
                                             _num_hosts * _num_hosts);
    for(int src = 0; src < _num_hosts; src++) {
        for(int dst = 0; dst < _num_hosts; dst++) {
            char handler[500];
            sprintf(handler, "hybrid_switch/q%d%d/q.resize_capacity", src+1, dst+1);
            _queue_capacity[src * _num_hosts + dst] = new HandlerCall(handler);
            _queue_capacity[src * _num_hosts + dst]->
                initialize(HandlerCall::f_read | HandlerCall::f_write,
                           this, errh);
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

    _ece_map = new HandlerCall("ecem.setECE");
    _ece_map->initialize(HandlerCall::f_write, this, errh);

    _log_config = new HandlerCall("hsl.circuitEvent");
    _log_config->initialize(HandlerCall::f_write, this, errh);

    return 0;
}

int
RunSchedule::set_schedule_handler(const String &str, Element *e, void *,
                                  ErrorHandler *)
{
    RunSchedule *rs = static_cast<RunSchedule *>(e);

    pthread_mutex_lock(&(rs->lock));
    if (rs->next_schedule != str)
        rs->new_sched = true;
    rs->next_schedule = String(str);
    pthread_mutex_unlock(&(rs->lock));
    return 0;
}

int
RunSchedule::resize_handler(const String &str, Element *e, void *, ErrorHandler *)
{
    RunSchedule *rs = static_cast<RunSchedule *>(e);

    pthread_mutex_lock(&(rs->lock));
    BoolArg::parse(str, rs->do_resize, ArgContext());
    if (rs->do_resize) {
        // get sizes based on queues sizes
        rs->_small_buffer_size = atoi(rs->_queue_capacity[0]->call_read().c_str());
        rs->_big_buffer_size = rs->_small_buffer_size * 8;

        printf("auto resizing: %d -> %d\n", rs->_small_buffer_size,
               rs->_big_buffer_size);
    }
    pthread_mutex_unlock(&(rs->lock));
    return 0;
}

int
RunSchedule::in_advance_handler(const String &str, Element *e, void *, ErrorHandler *)
{
    RunSchedule *rs = static_cast<RunSchedule *>(e);

    pthread_mutex_lock(&(rs->lock));
    rs->_in_advance = atoi(str.c_str());
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
    int in_advance = _in_advance;
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

    // cleanup from previous run during a new schedule roll over
    if (new_s && resize) {
        bool *qbig = (bool *)malloc(sizeof(bool) * _num_hosts * _num_hosts);
        bzero(qbig, sizeof(bool) * _num_hosts * _num_hosts);

        int remaining = in_advance;
        for(int k = 0; remaining >= 0; k++) {
            for(int dst = 0; dst < _num_hosts; dst++) {
                int src = configurations[k % num_configurations][dst];
                if (src == -1)
                    continue;
                _queue_capacity[src * _num_hosts + dst]->call_write(String(big_size));
                qbig[src * _num_hosts + dst] = true;
            }
            remaining -= durations[k % num_configurations];
        }
        for(int dst = 0; dst < _num_hosts; dst++) {
            for(int src = 0; src < _num_hosts; src++) {
                if(!qbig[src * _num_hosts + dst])
                    _queue_capacity[src * _num_hosts + dst]->
                        call_write(String(small_size));
            }
        }
        free(qbig);
    }

    // turn on / off packet switch for special case schedules
    // (e.g., circuit only, packet only)
    // and during a schedule roll over
    if (new_s || num_configurations == 1) {
        for(int dst = 0; dst < _num_hosts; dst++) {
            for(int src = 0; src < _num_hosts; src++) {
                // packet off only if this (src, dst) pair has circuit
                int val = configurations[0][dst] == src ? -1 : 0;
                _packet_pull_switch[src * _num_hosts + dst]->call_write(String(val));
            }
        }
    }


    // for each configuration in schedule
    for(int m = 0; m < num_configurations; m++) {
        // set configuration
        for(int dst = 0; dst < _num_hosts; dst++) {
            int src = configurations[m][dst];
            _pull_switch[dst]->call_write(String(src));

            if (src == -1) {
                int next_src = configurations[(m+1) % num_configurations][dst];
                _packet_pull_switch[next_src * _num_hosts + dst]->
                    call_write(String(-1));
            }

            _circuit_label[dst]->call_write(String(src+1));
            _packet_label[dst]->call_write(String(src+1));
        }

        char conf[500];
        bzero(conf, 500);
        for (int i = 0; i < configurations[m].size(); i++) {
            sprintf(&(conf[strlen(conf)]), "%d/", configurations[m][i]);
        }
        conf[strlen(conf)-1] = 0;
        _log_config->call_write(String(conf));

        // wait duration
        long long elapsed_nano = 0;
        struct timespec ts_new;
        while (elapsed_nano < durations[m] * 1e3) { // duration in microseconds
            clock_gettime(CLOCK_MONOTONIC, &ts_new);
            long long current_nano = 1e9 * ts_new.tv_sec + ts_new.tv_nsec;

            if (current_nano > _next_time) {
                // set ECE
                char ecem[500];
                bzero(ecem, 500);
                int q = 0;
                int remaining = in_advance + elapsed_nano / 1e3;
                for(int k = 0; remaining >= 0; k++) {
                    for(int dst = 0; dst < _num_hosts; dst++) {
                        int src = configurations[(m+k) % num_configurations][dst];
                        if (src == -1)
                            continue;
                        ecem[q] = src + 1 + '0';
                        ecem[q+1] = dst + 1 + '0';
                        ecem[q+2] = ' ';
                        q += 3;

                        if(resize) { // make the next few days buffer big
                            _queue_capacity[src * _num_hosts + dst]->
                                call_write(String(big_size));
                        }
                    }
                    remaining -= durations[(m+k) % num_configurations];
                }
                _ece_map->call_write(String(ecem));
                _next_time = current_nano - remaining * 1e3;
            }

            elapsed_nano = (1e9 * ts_new.tv_sec + ts_new.tv_nsec)
                - (1e9 * _start_time.tv_sec + _start_time.tv_nsec);
        }
        _start_time = ts_new;

        // make this days buffers smaller
        // only if this (src, dst) pair isn't in the next k configs
        if(resize) {
            for(int dst = 0; dst < _num_hosts; dst++) {
                int src = configurations[m][dst];
                if (src == -1)
                    continue;
                bool not_found = true;
                int remaining = in_advance;
                for (int k = 1; remaining >= 0; k++) {
                    int src2 = configurations[(m + k) % num_configurations][dst];
                    if (src == src2)
                        not_found = false;
                    remaining -= durations[(m+k) % num_configurations];
                }
                if (not_found) {
                    _queue_capacity[src * _num_hosts + dst]->
                        call_write(String(small_size));
                }
            }
        }

        // re-enable packet switch
        for(int dst = 0; dst < _num_hosts; dst++) {
            int src = configurations[m][dst];
            if (src != -1) {
                _packet_pull_switch[src * _num_hosts + dst]->call_write(String(0));
            }
        }
    }    
    free(durations);
    return 0;
}

bool
RunSchedule::run_task(Task *)
{
    while(1) {
        execute_schedule(ErrorHandler::default_handler());
    }
    return true;
}

void
RunSchedule::add_handlers()
{
    add_write_handler("setSchedule", set_schedule_handler, 0);
    add_write_handler("setDoResize", resize_handler, 0);
    add_write_handler("setInAdvance", in_advance_handler, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RunSchedule)
ELEMENT_REQUIRES(userlevel)
