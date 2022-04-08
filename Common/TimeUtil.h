#pragma once

// Seconds.
double time_now_d();

// Sleep. Does not necessarily have millisecond granularity, especially on Windows.
void sleep_ms(int ms);

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
