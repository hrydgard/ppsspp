#include <cstdio>
#include <cstdint>

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

static LARGE_INTEGER frequency;
static double frequencyMult;
static LARGE_INTEGER startTime;

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

static uint64_t _frequency = 0;
static uint64_t _starttime = 0;

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
