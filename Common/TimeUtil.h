#pragma once

// Seconds.
double time_now_d();

// Sleep. Does not necessarily have millisecond granularity, especially on Windows.
void sleep_ms(int ms);

void GetTimeFormatted(char formattedTime[13]);

// Rust-style Instant for clear and easy timing.
// TODO: Optimize to not convert to seconds until calling Elapsed (Now() should just store
// the raw time value from the functions, depending on platform).
class Instant {
public:
	Instant() {}
	static Instant Now() {
		return Instant(time_now_d());
	}
	double Elapsed() const {
		return time_now_d() - instantTime_;
	}
	static Instant FromSeconds(double seconds) {
		return Instant{ seconds };
	}
	double ToSeconds() const {
		return instantTime_;
	}
	static Instant FromSecondsAgo(double seconds) {
		return Instant{ time_now_d() - seconds };
	}
	double DifferenceInSeconds(const Instant &later) const {
		return later.instantTime_ - instantTime_;
	}

	Instant AddSeconds(double seconds) {
		return Instant{ instantTime_ + seconds };
	}

	bool operator >(const Instant &other) const {
		return instantTime_ > other.instantTime_;
	}
	bool operator <(const Instant &other) const {
		return instantTime_ < other.instantTime_;
	}
	bool operator >=(const Instant &other) const {
		return instantTime_ >= other.instantTime_;
	}
	bool operator <=(const Instant &other) const {
		return instantTime_ <= other.instantTime_;
	}
private:
	explicit Instant(double initTime) : instantTime_(initTime) {}
	double instantTime_;
};
