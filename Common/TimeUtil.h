#pragma once
#include <cstdint>

void TimeInit();

// Seconds.
double time_now_d();

// Raw time in nanoseconds.
// The only intended use is to match the timings from VK_GOOGLE_display_timing.
uint64_t time_now_raw();

// This is only interesting for Linux, in relation to VK_GOOGLE_display_timing.
double from_time_raw(uint64_t raw_time);
double from_time_raw_relative(uint64_t raw_time);

// Seconds, Unix UTC time
double time_now_unix_utc();

// Sleep. Does not necessarily have millisecond granularity, especially on Windows.
// Requires a "reason" since sleeping generally should be very sparingly used. This
// can be logged if desired to figure out where we're wasting time.
void sleep_ms(int ms, const char *reason);

// Precise sleep. Can consume a little bit of CPU on Windows at least.
void sleep_precise(double seconds);

// Yield. Signals that this thread is busy-waiting but wants to allow other hyperthreads to run.
void yield();

void GetCurrentTimeFormatted(char formattedTime[13]);

// Most accurate timer possible - no extra double conversions. Only for spans.
class Instant {
public:
	static Instant Now() {
		return Instant();
	}
	double ElapsedSeconds() const;
	int64_t ElapsedNanos() const;
private:
	Instant();
	uint64_t nativeStart_;
#ifndef _WIN32
	int64_t nsecs_;
#endif
};
