#pragma once

#include <mutex>
#include <condition_variable>

#include "Common/Thread/ThreadManager.h"

class LimitedWaitable : public Waitable {
public:
	LimitedWaitable() {
		triggered_ = false;
	}

	void Wait() override {
		std::unique_lock<std::mutex> lock(mutex_);
		if (!triggered_) {
			cond_.wait(lock, [&] { return triggered_.load(); });
		}
	}

	bool WaitFor(double budget) {
		uint32_t us = budget > 0 ? (uint32_t)(budget * 1000000.0) : 0;
		std::unique_lock<std::mutex> lock(mutex_);
		if (!triggered_) {
			if (us == 0)
				return false;
			cond_.wait_for(lock, std::chrono::microseconds(us), [&] { return triggered_.load(); });
		}
		return triggered_;
	}

	void Notify() {
		std::unique_lock<std::mutex> lock(mutex_);
		triggered_ = true;
		cond_.notify_all();
	}

private:
	std::condition_variable cond_;
	std::mutex mutex_;
	std::atomic<bool> triggered_;
};
