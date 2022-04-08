#pragma once

#include <condition_variable>
#include <mutex>

// Similar to C++20's std::barrier
class CountingBarrier {
public:
	CountingBarrier(size_t count) : threadCount_(count) {}

	void Arrive() {
		std::unique_lock<std::mutex> lk(m);
		counter++;
		waiting++;
		cv.wait(lk, [&] {return counter >= threadCount_; });
		cv.notify_one();
		waiting--;
		if (waiting == 0) {
			// Reset so it can be re-used.
			counter = 0;
		}
		lk.unlock();
	}

private:
	std::mutex m;
	std::condition_variable cv;
	size_t counter = 0;
	size_t waiting = 0;
	size_t threadCount_;
};
