#pragma once

// http://linux.die.net/man/3/clock_gettime

// This time implementation caches the time for max performance (call time_now() as much as you like).
// You need to call time_update() once per frame (or whenever you need the correct time right now).

void time_update();

// Seconds.
float time_now();
double time_now_d();

// Uncached time. Slower than the above cached time functions. Does not update cached time, call time_update for that.
double real_time_now();

int time_now_ms();


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