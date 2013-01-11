#include "base/basictypes.h"
#include "base/logging.h"
#include "base/timeutil.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif
#include <stdio.h>

static double curtime = 0;
static float curtime_f = 0;

#ifdef _WIN32

__int64 _frequency = 0;
__int64 _starttime = 0;

double real_time_now(){
	if (_frequency == 0) {
		QueryPerformanceFrequency((LARGE_INTEGER*)&_frequency);
		QueryPerformanceCounter((LARGE_INTEGER*)&_starttime);
		curtime=0;
	}
	__int64 time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return ((double) (time - _starttime) / (double) _frequency);
}

#else

double real_time_now() {
	static time_t start;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (start == 0) {
		start = tv.tv_sec;
	}
	tv.tv_sec -= start;
	return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

#endif

void time_update() {
	curtime = real_time_now();
	curtime_f = (float)curtime;

	//printf("curtime: %f %f\n", curtime, curtime_f);
	// also smooth time.
	//curtime+=float((double) (time-_starttime) / (double) _frequency);
	//curtime*=0.5f;
	//curtime+=1.0f/60.0f;
	//lastTime=curtime;
	//curtime_f = (float)curtime;
}

float time_now() {
	return curtime_f;
}

double time_now_d() {
	return curtime;
}

int time_now_ms() {
	return int(curtime*1000.0);
}

void sleep_ms(int ms) {
#ifdef _WIN32
#ifndef METRO
	Sleep(ms);
#endif
#else
	usleep(ms * 1000);
#endif
}

LoggingDeadline::LoggingDeadline(const char *name, int ms) : name_(name), endCalled_(false) {
	totalTime_ = (double)ms * 0.001;
	time_update();
	endTime_ = time_now_d() + totalTime_;
}

LoggingDeadline::~LoggingDeadline() {
	if (!endCalled_)
		End();
}

bool LoggingDeadline::End() {
	endCalled_ = true;
	time_update();
	if (time_now_d() > endTime_) {
		double late = (time_now_d() - endTime_);
		double totalTime = late + totalTime_;
		ELOG("===== %0.2fms DEADLINE PASSED FOR %s at %0.2fms - %0.2fms late =====", totalTime_ * 1000.0, name_, 1000.0 * totalTime, 1000.0 * late);
		return false;
	}
	return true;
}

