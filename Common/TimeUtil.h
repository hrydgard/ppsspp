#pragma once

#include <cstdint>

// Seconds.
double time_now_d();

// Raw time in nanoseconds.
// The only intended use is to match the timings from VK_GOOGLE_display_timing.
uint64_t time_now_raw();

// This is only interesting for Linux, in relation to VK_GOOGLE_display_timing.
double from_time_raw(uint64_t raw_time);
double from_time_raw_relative(uint64_t raw_time);

// Sleep. Does not necessarily have millisecond granularity, especially on Windows.
void sleep_ms(int ms);

// Yield. Signals that this thread is busy-waiting but wants to allow other hyperthreads to run.
void yield();

void GetTimeFormatted(char formattedTime[13]);

// Rust-style Instant for clear and easy timing.
class Instant {
public:
	static Instant Now() {
		return Instant(time_now_d());
	}
	double Elapsed() const {
		return time_now_d() - instantTime_;
	}
private:
	explicit Instant(double initTime) : instantTime_(initTime) {}
	double instantTime_;
};
