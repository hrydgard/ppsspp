#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "Common/Thread/ThreadManager.h"

class LimitedWaitable : public Waitable {
public:
	LimitedWaitable() {
		triggered_ = false;
	}

	~LimitedWaitable() {
		// Make sure no one is still waiting, and any notify lock is released.
		Notify();
	}

	void Wait() override {
		if (triggered_)
			return;

		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [&] { return triggered_.load(); });
	}

	bool WaitFor(double budget) {
		if (triggered_)
			return true;

		uint32_t us = budget > 0 ? (uint32_t)(budget * 1000000.0) : 0;
		if (us == 0)
			return false;

		std::unique_lock<std::mutex> lock(mutex_);
		return cond_.wait_for(lock, std::chrono::microseconds(us), [&] { return triggered_.load(); });
	}

	void Notify() {
		std::unique_lock<std::mutex> lock(mutex_);
		triggered_ = true;
		cond_.notify_all();
	}

	// For simple polling.
	bool Ready() const {
		return triggered_;
	}

private:
	std::condition_variable cond_;
	std::mutex mutex_;
	std::atomic<bool> triggered_;
};
