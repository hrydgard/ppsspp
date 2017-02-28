#pragma once

#include <chrono>
#include <mutex>
#include <condition_variable>

// Note that name must be a global string that lives until the end of the process,
// for assertThreadName to work.
void setCurrentThreadName(const char *threadName);
void AssertCurrentThreadName(const char *threadName);

class event {
public:
	void notify_one() {
		cond_.notify_one();
	}

	// notify_all is not really possible to implement with win32 events?

	void wait(std::mutex &mtx) {
		// broken logic
		std::unique_lock<std::mutex> guard(mtx);
		cond_.wait(guard);
	}

	void wait_for(std::mutex &mtx, int milliseconds) {
		std::unique_lock<std::mutex> guard(mtx);
		cond_.wait_for(guard, std::chrono::milliseconds(milliseconds));
	}

	void reset() {
	}
private:
	std::condition_variable cond_;
};