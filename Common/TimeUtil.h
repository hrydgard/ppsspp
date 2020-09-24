#pragma once

// http://linux.die.net/man/3/clock_gettime

// Seconds.
double time_now_d();

// Sleep. Does not necessarily have millisecond granularity, especially on Windows.
void sleep_ms(int ms);

// Can be sprinkled around the code to make sure that thing don't take
// unexpectedly long. What that means is up to you.
class LoggingDeadline {
public:
	LoggingDeadline(const char *name, int deadline_in_ms);
	~LoggingDeadline();
	bool End();

private:
	const char *name_;
	bool endCalled_;
	double totalTime_;
	double endTime_;
};
