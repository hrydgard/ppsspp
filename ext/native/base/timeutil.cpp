#include <cstdio>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/timeutil.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

static double curtime = 0;
static float curtime_f = 0;

#ifdef _WIN32

LARGE_INTEGER frequency;
double frequencyMult;
LARGE_INTEGER startTime;

double real_time_now() {
	if (frequency.QuadPart == 0) {
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&startTime);
		curtime = 0.0;
		frequencyMult = 1.0 / static_cast<double>(frequency.QuadPart);
	}
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	double elapsed = static_cast<double>(time.QuadPart - startTime.QuadPart);
	return elapsed * frequencyMult;
}

#else

uint64_t _frequency = 0;
uint64_t _starttime = 0;

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
	Sleep(ms);
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

