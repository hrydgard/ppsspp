#pragma once

#include "Common/Thread/ThreadManager.h"

#include <condition_variable>
#include <mutex>

struct Event : public Waitable {
public:
	Event() {
		triggered_ = false;
	}

	~Event() {
		// Make sure no one is still waiting, and any notify lock is released.
		Notify();
	}

	void Wait() override {
		if (triggered_) {
			return;
		}
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [&] { return triggered_.load(); });
	}

	void Notify() {
		std::unique_lock<std::mutex> lock(mutex_);
		triggered_ = true;
		cond_.notify_one();
	}

private:
	std::condition_variable cond_;
	std::mutex mutex_;
	std::atomic<bool> triggered_;
};
