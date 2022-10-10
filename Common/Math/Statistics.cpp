#include <cstdio>

#include "Common/Math/Statistics.h"

void SimpleStat::Format(char *buffer, size_t sz) {
	if (min_ == INFINITY) {
		snprintf(buffer, sz, "%s: N/A\n", name_);
	} else {
		snprintf(buffer, sz, "%s: %0.2f (%0.2f..%0.2f, avg %0.2f)\n", name_, value_, min_, max_, smoothed_);
	}
}
