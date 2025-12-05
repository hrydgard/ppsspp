#include "ppsspp_config.h"

#include <cstdio>
#include <cstdint>

#include "Common/TimeUtil.h"
#include "Common/Data/Random/Rng.h"
#include "Common/Log.h"

#ifdef HAVE_LIBNX
#include <switch.h>
#endif // HAVE_LIBNX

#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
#include <mach/mach_time.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif // __EMSCRIPTEN__

#ifdef _WIN32
#include "CommonWindows.h"
#include <mmsystem.h>
#include <sys/timeb.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

// for _mm_pause
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#include <emmintrin.h>
#endif

#include <ctime>

// TODO: https://github.com/floooh/sokol/blob/9a6237fcdf213e6da48e4f9201f144bcb2dcb46f/sokol_time.h#L229-L248

constexpr double micros = 1000000.0;
constexpr double nanos = 1000000000.0;

#if PPSSPP_PLATFORM(WINDOWS)

static LARGE_INTEGER frequency;
static double frequencyMult;
static LARGE_INTEGER startTime;

HANDLE Timer;
int SchedulerPeriodMs = 10;
INT64 QpcPerSecond;

void TimeInit() {
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&startTime);
	QpcPerSecond = frequency.QuadPart;
	frequencyMult = 1.0 / static_cast<double>(frequency.QuadPart);

	// The timer will be automatically deleted on process destruction. Don't need to CloseHandle.
	Timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#if !PPSSPP_PLATFORM(UWP)
	TIMECAPS caps;
	timeGetDevCaps(&caps, sizeof caps);
	timeBeginPeriod(caps.wPeriodMin);
	SchedulerPeriodMs = (int)caps.wPeriodMin;
#endif
}

double time_now_d() {
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	return static_cast<double>(time.QuadPart - startTime.QuadPart) * frequencyMult;
}

// Fake, but usable in a pinch. Don't, though.
uint64_t time_now_raw() {
	return (uint64_t)(time_now_d() * nanos);
}

double from_time_raw(uint64_t raw_time) {
	if (raw_time == 0) {
		return 0.0; // invalid time
	}
	return (double)raw_time * (1.0 / nanos);
}

double from_time_raw_relative(uint64_t raw_time) {
	return from_time_raw(raw_time);
}

double time_now_unix_utc() {
	const int64_t UNIX_TIME_START = 0x019DB1DED53E8000; //January 1, 1970 (start of Unix epoch) in "ticks"
	const double TICKS_PER_SECOND = 10000000; //a tick is 100ns
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft); //returns ticks in UTC
	// Copy the low and high parts of FILETIME into a LARGE_INTEGER
	LARGE_INTEGER li;
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;
	//Convert ticks since 1/1/1970 into seconds
	return (double)(li.QuadPart - UNIX_TIME_START) / TICKS_PER_SECOND;
}

void yield() {
	YieldProcessor();
}

Instant::Instant() {
	_dbg_assert_(frequencyMult != 0.0);
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&nativeStart_));
}

double Instant::ElapsedSeconds() const {
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	double elapsed = static_cast<double>(time.QuadPart - nativeStart_);
	return elapsed * frequencyMult;
}

int64_t Instant::ElapsedNanos() const {
	return (int64_t)(ElapsedSeconds() * 1000000000.0);
}

#elif PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)

static double g_machTimeConversion = 0.0;
static double g_machTimeConversionNanos = 0.0;
static int64_t g_startTime = 0;

void TimeInit() {
	mach_timebase_info_data_t info;
	mach_timebase_info(&info);
	g_startTime = mach_absolute_time();
	// nanoseconds per tick
	g_machTimeConversionNanos = (double)info.numer / (double)info.denom;
	g_machTimeConversion = (double)info.numer / (double)info.denom / 1e9;
}

double time_now_d() {
	return (double)(mach_absolute_time() - g_startTime) * g_machTimeConversion;
}

uint64_t time_now_raw() {
	return mach_absolute_time();
}

double from_time_raw(uint64_t raw_time) {
	return (double)(raw_time - g_startTime) * g_machTimeConversion;
}

double from_time_raw_relative(uint64_t raw_time) {
	return (double)raw_time * g_machTimeConversion;
}

double from_mach_time_interval(double interval) {
	return interval - g_startTime * g_machTimeConversion;
}

double time_now_unix_utc() {
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
}

void yield() {
	#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	_mm_pause();
	#elif PPSSPP_ARCH(ARM64)
	// Took this out for now. See issue #17877
	// __builtin_arm_isb(15);
	#endif
}

Instant::Instant() {
	nativeStart_ = mach_absolute_time();
}

int64_t Instant::ElapsedNanos() const {
	uint64_t now = mach_absolute_time();
	return (int64_t)((double)(now - nativeStart_) * g_machTimeConversionNanos);
}

double Instant::ElapsedSeconds() const {
	uint64_t now = mach_absolute_time();
	return (double)(now - nativeStart_) * g_machTimeConversion;
}

#elif PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(LINUX)

void TimeInit() {
	// Nothing to do.
}

// The only intended use is to match the timings in VK_GOOGLE_display_timing
uint64_t time_now_raw() {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
}

static uint64_t g_startTime;

double from_time_raw(uint64_t raw_time) {
	return (double)(raw_time - g_startTime) * (1.0 / nanos);
}

double time_now_d() {
	uint64_t raw_time = time_now_raw();
	if (g_startTime == 0) {
		g_startTime = raw_time;
	}
	return from_time_raw(raw_time);
}

double from_time_raw_relative(uint64_t raw_time) {
	return (double)raw_time * (1.0 / nanos);
}

double time_now_unix_utc() {
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
}

void yield() {
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	_mm_pause();
#elif PPSSPP_ARCH(ARM64)
	// Took this out for now. See issue #17877
	// __builtin_arm_isb(15);
#endif
}

Instant::Instant() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	nativeStart_ = ts.tv_sec;
	nsecs_ = ts.tv_nsec;
}

int64_t Instant::ElapsedNanos() const {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	int64_t secs = ts.tv_sec - nativeStart_;
	int64_t nsecs = ts.tv_nsec - nsecs_;
	if (nsecs < 0) {
		secs--;
		nsecs += 1000000000;
	}
	return secs * 1000000000ULL + nsecs;
}

double Instant::ElapsedSeconds() const {
	return (double)ElapsedNanos() * (1.0 / nanos);
}

#else

void TimeInit() {
	// Nothing to do.
}

static time_t start;

double time_now_d() {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	if (start == 0) {
		start = tv.tv_sec;
	}
	return (double)(tv.tv_sec - start) + (double)tv.tv_usec * (1.0 / micros);
}

uint64_t time_now_raw() {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	if (start == 0) {
		start = tv.tv_sec;
	}
	return (double)tv.tv_sec + (double)tv.tv_usec * (1.0 / micros);
}

double from_time_raw(uint64_t raw_time) {
	return (double)raw_time * (1.0 / nanos);
}

double from_time_raw_relative(uint64_t raw_time) {
	return from_time_raw(raw_time);
}

void yield() {}

double time_now_unix_utc() {
	return time_now_raw();
}

Instant::Instant() {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	nativeStart_ = tv.tv_sec;
	nsecs_ = tv.tv_usec;
}

int64_t Instant::ElapsedNanos() const {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	int64_t secs = ts.tv_sec - nativeStart_;
	int64_t usecs = ts.tv_nsec - nsecs_;
	if (usecs < 0) {
		secs--;
		usecs += 1000000;
	}
	return secs * 1000000000 + usecs * 1000;
}

double Instant::ElapsedSeconds() const {
	return (double)ElapsedNanos() * (1.0 / 1000000000.0);
}

#endif

#define SLEEP_LOG_ENABLED 0

void sleep_ms(int ms, const char *reason) {
	if (ms <= 0) {
		return;
	}
#if SLEEP_LOG_ENABLED
	INFO_LOG(Log::System, "Sleep %d ms: %s", ms, reason);
#endif
#ifdef _WIN32
	Sleep(ms);
#elif defined(HAVE_LIBNX)
	svcSleepThread(ms * 1000000);
#elif defined(__EMSCRIPTEN__)
	emscripten_sleep(ms);
#else
	usleep(ms * 1000);
#endif
}

void sleep_us(int us, const char *reason) {
	if (us <= 0) {
		return;
	}
#if SLEEP_LOG_ENABLED
	INFO_LOG(Log::System, "Sleep %d us: %s", us, reason);
#endif
#ifdef _WIN32
	Sleep(us / 1000);
#elif defined(HAVE_LIBNX)
	svcSleepThread(us * 1000);
#elif defined(__EMSCRIPTEN__)
	emscripten_sleep(us / 1000);
#else
	usleep(us);
#endif
}

// This can be a little more expensive in some circumstances, so only use when necessary.
void sleep_precise(double seconds, const char *reason) {
	if (seconds <= 0.0) {
		return;
	}
#if SLEEP_LOG_ENABLED
	INFO_LOG(Log::System, "Sleep precise %f s: %s", seconds, reason);
#endif
#ifdef _WIN32
	// Precise Windows sleep function from: https://github.com/blat-blatnik/Snippets/blob/main/precise_sleep.c
	// Described in: https://blog.bearcats.nl/perfect-sleep-function/
	LARGE_INTEGER qpc;
	QueryPerformanceCounter(&qpc);
	INT64 targetQpc = (INT64)(qpc.QuadPart + seconds * QpcPerSecond);

	if (Timer) { // Try using a high resolution timer first.
		const double TOLERANCE = 0.001'02;
		INT64 maxTicks = (INT64)SchedulerPeriodMs * 9'500;
		for (;;) // Break sleep up into parts that are lower than scheduler period.
		{
			double remainingSeconds = (targetQpc - qpc.QuadPart) / (double)QpcPerSecond;
			INT64 sleepTicks = (INT64)((remainingSeconds - TOLERANCE) * 10'000'000);
			if (sleepTicks <= 0) {
				break;
			}
			LARGE_INTEGER due;
			due.QuadPart = -(sleepTicks > maxTicks ? maxTicks : sleepTicks);
			// Note: SetWaitableTimerEx is not available on Vista.
			if (!SetWaitableTimer(Timer, &due, 0, NULL, NULL, FALSE)) {
				_dbg_assert_(false);
				break;
			}
			WaitForSingleObject(Timer, INFINITE);
			QueryPerformanceCounter(&qpc);
		}
	} else { // Fallback to Sleep.
		const double TOLERANCE = 0.000'02;
		double sleepMs = (seconds - TOLERANCE) * 1000 - SchedulerPeriodMs; // Sleep for 1 scheduler period less than requested.
		int sleepSlices = (int)(sleepMs / SchedulerPeriodMs);
		if (sleepSlices > 0) {
			Sleep((DWORD)sleepSlices * SchedulerPeriodMs);
		}
		QueryPerformanceCounter(&qpc);
	}
	while (qpc.QuadPart < targetQpc) { // Spin for any remaining time.
		YieldProcessor();
		QueryPerformanceCounter(&qpc);
	}
	// On other platforms, we just do a conversion with more input precision than in sleep_ms which is restricted to whole milliseconds.
#elif defined(HAVE_LIBNX)
	svcSleepThread((int64_t)(seconds * 1000000000.0));
#elif defined(__EMSCRIPTEN__)
	emscripten_sleep(seconds * 1000.0);
#else
	usleep(seconds * 1000000.0);
#endif
}

// Return the current time formatted as Minutes:Seconds:Milliseconds
// in the form 00:00:000.
void GetCurrentTimeFormatted(char formattedTime[13]) {
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

// We don't even bother synchronizing this, it's fine if threads stomp a bit.
static GMRng g_sleepRandom;

void sleep_random(double minSeconds, double maxSeconds, const char *reason) {
	const double waitSeconds = minSeconds + (maxSeconds - minSeconds) * g_sleepRandom.F();
	sleep_precise(waitSeconds, reason);
}
