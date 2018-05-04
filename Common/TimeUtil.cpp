#include <cstdio>
#include <cstdint>
#include <ctime>

#include "ppsspp_config.h"

#include "Common/TimeUtil.h"

#ifdef HAVE_LIBNX
#include <switch.h>
#endif // HAVE_LIBNX

#ifdef _WIN32
#include "CommonWindows.h"
#include <mmsystem.h>
#include <sys/timeb.h>
#else
#if defined __wiiu__
#include <wiiu/os/time.h>
#endif
#include <sys/time.h>
#include <unistd.h>
#endif

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
#elif defined(__wiiu__)
double time_now_d() {
	static OSTime start;
	if(!start)
		start = OSGetSystemTime();
	return (double)(OSGetSystemTime() - start) * (1.0 / (double) wiiu_timer_clock);
}
#else

double time_now_d() {
	static time_t start;
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	if (start == 0) {
		start = tv.tv_sec;
	}
	return (double)(tv.tv_sec - start) + (double)tv.tv_usec * (1.0 / 1000000.0);
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

// Return the current time formatted as Minutes:Seconds:Milliseconds
// in the form 00:00:000.
void GetTimeFormatted(char formattedTime[13]) {
	time_t sysTime;
	struct tm * gmTime;
	char tmp[13];

	time(&sysTime);
	gmTime = localtime(&sysTime);

	strftime(tmp, 6, "%M:%S", gmTime);

	// Now tack on the milliseconds
#ifdef _WIN32
	struct timeb tp;
	(void)::ftime(&tp);
	snprintf(formattedTime, 13, "%s:%03i", tmp, tp.millitm);
#else
	struct timeval t;
	(void)gettimeofday(&t, NULL);
	snprintf(formattedTime, 13, "%s:%03d", tmp, (int)(t.tv_usec / 1000));
#endif
}
