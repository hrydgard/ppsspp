#pragma once
#include <cstdint>

#include "ppsspp_config.h"

void TimeInit();

// Seconds from app start.
double time_now_d();

// Raw time in nanoseconds.
// The only intended use is to match the timings from VK_GOOGLE_display_timing.
uint64_t time_now_raw();

// This is only interesting for Linux, in relation to VK_GOOGLE_display_timing.
double from_time_raw(uint64_t raw_time);
double from_time_raw_relative(uint64_t raw_time);
double from_mach_time_interval(double interval);

// Seconds, Unix UTC time
double time_now_unix_utc();

// Sleep for milliseconds. Does not necessarily have millisecond granularity, especially on Windows.
// Requires a "reason" since sleeping generally should be very sparingly used. This
// can be logged if desired to figure out where we're wasting time.
void sleep_ms(int ms, const char *reason);
// Sleep for microseconds. Does not necessarily have microsecond granularity, especially on Windows.
void sleep_us(int us, const char *reason);
// Precise sleep. Can consume a little bit of CPU on Windows at least.
void sleep_precise(double seconds, const char *reason);

// Random sleep, used for debugging.
void sleep_random(double minSeconds, double maxSeconds, const char *reason);

// Yield. Signals that this thread is busy-waiting but wants to allow other hyperthreads to run.
void yield();

void GetCurrentTimeFormatted(char formattedTime[13]);

// Most accurate timer possible - no extra double conversions. Only for spans.
class Instant {
public:
	Instant();
	static Instant Now() {
		return Instant();
	}
	double ElapsedSeconds() const;
	double ElapsedMs() const { return ElapsedSeconds() * 1000.0; }
	int64_t ElapsedNanos() const;
private:
	uint64_t nativeStart_;
#if !PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(MAC) && !PPSSPP_PLATFORM(IOS)
	int64_t nsecs_;
#endif
};

class TimeCollector {
public:
	TimeCollector(double *target, bool enable) : target_(enable ? target : nullptr) {
		if (enable)
			startTime_ = time_now_d();
	}
	~TimeCollector() {
		if (target_) {
			*target_ += time_now_d() - startTime_;
		}
	}
private:
	double startTime_;
	double *target_;
};
