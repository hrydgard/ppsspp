#pragma once

#include <cmath>

// Very simple stat for convenience. Keeps track of min, max, smoothed.
struct SimpleStat {
	SimpleStat(const char *name) : name_(name) { Reset(); }

	void Update(double value) {
		value_ = value;
		if (min_ == INFINITY) {
			smoothed_ = value;
		} else {
			// TODO: Make factor adjustable?
			smoothed_ = 0.99 * smoothed_ + 0.01 * value;
		}
		if (value < min_) {
			min_ = value;
		}
		if (value > max_) {
			max_ = value;
		}
	}

	void Reset() {
		value_ = 0.0;
		smoothed_ = 0.0;  // doens't really need init
		min_ = INFINITY;
		max_ = -INFINITY;
	}

	void Format(char *buffer, size_t sz);

private:
	SimpleStat() {}
	const char *name_;

	// These are initialized in Reset().
	double value_;
	double min_;
	double max_;
	double smoothed_;
};
