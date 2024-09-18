#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>

#include "Common/Thread/ThreadManager.h"

// Same as the latch from C++21.
struct WaitableCounter : public Waitable {
public:
	WaitableCounter(int count) : count_(count) {}

	void Count() {
		std::unique_lock<std::mutex> lock(mutex_);
		if (count_ == 0) {
			return;
		}
		count_--;
		if (count_ == 0) {
			// We were the last one to increment
			cond_.notify_all();
		}
	}

	void Wait() override {
		std::unique_lock<std::mutex> lock(mutex_);
		while (count_ != 0) {
			cond_.wait(lock);
		}
	}

	int count_;
	std::mutex mutex_;
	std::condition_variable cond_;
};

// Note that upper bounds are non-inclusive: range is [lower, upper)
WaitableCounter *ParallelRangeLoopWaitable(ThreadManager *threadMan, const std::function<void(int, int)> &loop, int lower, int upper, int minSize, TaskPriority priority);

// Note that upper bounds are non-inclusive: range is [lower, upper)
void ParallelRangeLoop(ThreadManager *threadMan, const std::function<void(int, int)> &loop, int lower, int upper, int minSize, TaskPriority priority = TaskPriority::NORMAL);

// Common utilities for large (!) memory copies.
// Will only fall back to threads if it seems to make sense.
// NOTE: These support a max of 2GB.
void ParallelMemcpy(ThreadManager *threadMan, void *dst, const void *src, size_t bytes, TaskPriority priority = TaskPriority::NORMAL);
void ParallelMemset(ThreadManager *threadMan, void *dst, uint8_t value, size_t bytes, TaskPriority priority = TaskPriority::NORMAL);
