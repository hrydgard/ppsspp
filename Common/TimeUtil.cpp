#include <cstdio>
#include <cstdint>

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
#include <sys/time.h>
#include <unistd.h>
#endif
#include <ctime>

// TODO: https://github.com/floooh/sokol/blob/9a6237fcdf213e6da48e4f9201f144bcb2dcb46f/sokol_time.h#L229-L248

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
	time(&sysTime);

	struct tm *gmTime = localtime(&sysTime);
	char tmp[6];
	strftime(tmp, sizeof(tmp), "%M:%S", gmTime);

	// Now tack on the milliseconds
#ifdef _WIN32
	struct timeb tp;
	(void)::ftime(&tp);
	snprintf(formattedTime, 11, "%s:%03i", tmp, tp.millitm);
#else
	struct timeval t;
	(void)gettimeofday(&t, NULL);
	snprintf(formattedTime, 11, "%s:%03d", tmp, (int)(t.tv_usec / 1000));
#endif
}
