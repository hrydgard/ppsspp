#include <cstdio>

#include "base/basictypes.h"
#include "Common/TimeUtil.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef HAVE_LIBNX
#include <switch.h>
#endif // HAVE_LIBNX

#include "Common/Log.h"

static double curtime = 0;

#ifdef _WIN32

LARGE_INTEGER frequency;
double frequencyMult;
LARGE_INTEGER startTime;

double time_now_d() {
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

double time_now_d() {
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

void sleep_ms(int ms) {
#ifdef _WIN32
	Sleep(ms);
#elif defined(HAVE_LIBNX)
	svcSleepThread(ms * 1000000);
#else
	usleep(ms * 1000);
#endif
}

LoggingDeadline::LoggingDeadline(const char *name, int ms) : name_(name), endCalled_(false) {
	totalTime_ = (double)ms * 0.001;
	endTime_ = time_now_d() + totalTime_;
}

LoggingDeadline::~LoggingDeadline() {
	if (!endCalled_)
		End();
}

bool LoggingDeadline::End() {
	endCalled_ = true;
	double now = time_now_d();
	if (now > endTime_) {
		double late = (now - endTime_);
		double totalTime = late + totalTime_;
		ERROR_LOG(SYSTEM, "===== %0.2fms DEADLINE PASSED FOR %s at %0.2fms - %0.2fms late =====", totalTime_ * 1000.0, name_, 1000.0 * totalTime, 1000.0 * late);
		return false;
	}
	return true;
}

