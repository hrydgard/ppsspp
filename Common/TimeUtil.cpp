#include "ppsspp_config.h"

#include <cstdio>
#include <cstdint>

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

#ifdef _WIN32

static LARGE_INTEGER frequency;
static double frequencyMult;
static LARGE_INTEGER startTime;

double time_now_d() {
	if (frequency.QuadPart == 0) {
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&startTime);
		frequencyMult = 1.0 / static_cast<double>(frequency.QuadPart);
	}
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	double elapsed = static_cast<double>(time.QuadPart - startTime.QuadPart);
	return elapsed * frequencyMult;
}

// Fake, but usable in a pinch. Don't, though.
uint64_t time_now_raw() {
	return (uint64_t)(time_now_d() * 1000000000.0);
}

#elif PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)

// The only intended use is to match the timings in VK_GOOGLE_display_timing
uint64_t time_now_raw() {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
}

double time_now_d() {
	static uint64_t start;
	uint64_t raw_time = time_now_raw();
	if (start == 0) {
		start = raw_time;
	}
	return (double)(raw_time - start) * (1.0 / 1000000000.0);
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

// Fake, but usable in a pinch. Don't, though.
uint64_t time_now_raw() {
	return (uint64_t)(time_now_d() * 1000000000.0);
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

	uint32_t milliseconds;
#ifdef _WIN32
	struct timeb tp;
	(void)::ftime(&tp);
	milliseconds = tp.millitm;
#else
	struct timeval t;
	(void)gettimeofday(&t, NULL);
	milliseconds = (int)(t.tv_usec / 1000);
#endif

	struct tm *gmTime = localtime(&sysTime);
	char tmp[6];
	strftime(tmp, sizeof(tmp), "%M:%S", gmTime);

	// Now tack on the milliseconds
	snprintf(formattedTime, 11, "%s:%03u", tmp, milliseconds % 1000);
}
