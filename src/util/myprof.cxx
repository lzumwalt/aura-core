#include "python/pyprops.hxx"

#include <stdio.h>

#include "comms/logging.hxx"
#include "init/globals.hxx"
#include "timing.h"
#include "myprof.hxx"

myprofile::myprofile() {
    init_time = 0.0;
    count = 0;
    sum_time = 0.0;
    max_interval = 0.0;
    min_interval = 1000.0;
    enabled = false;
}

myprofile::~myprofile() {
}

void myprofile::set_name( const string _name ) {
    name = _name;
}

void myprofile::start() {
    if ( !enabled ) {
	return;
    }
    
    if ( init_time <= 0.0001 ) {
	init_time = get_Time();
    }
    start_time = get_Time();
    count++;
}

void myprofile::stop() {
    if ( !enabled ) {
	return;
    }
    
    double stop_time = get_Time();
    last_interval = stop_time - start_time;
    sum_time += last_interval;
    
    // log situations where a module took longer that 0.10 sec to execute
    if ( last_interval > 0.10 ) {
	char msg[256];
	snprintf(msg, 256, "t1 = %.3f t2 = %.3f int = %.3f",
		 start_time, stop_time, last_interval);
	events->log( name.c_str(), msg );
    }

    if ( last_interval < min_interval ) {
	min_interval = last_interval;
    }
    if ( last_interval > max_interval ) {
	max_interval = last_interval;
    }
}

void myprofile::stats() {
    if ( !enabled ) {
	return;
    }
    
    double total_time = get_Time() - init_time;
    double avg_hz = 0.0;
    if ( total_time > 1.0 ) {
	avg_hz = (double)count / total_time;
    }
    printf( "%s avg: %.4f num: %d tot: %.4f (range: %.4f-%.4f) hz: %.3f\n",
	    name.c_str(), sum_time / (double)count,
	    count, sum_time, min_interval, max_interval, avg_hz );
}


// global profiling structures
myprofile imu_prof;
myprofile gps_prof;
myprofile air_prof;
myprofile pilot_prof;
myprofile filter_prof;
myprofile mission_prof;
myprofile control_prof;
myprofile health_prof;
myprofile datalog_prof;
myprofile main_prof;
myprofile sync_prof;
