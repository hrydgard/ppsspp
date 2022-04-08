#pragma once

#include "Common/Thread/ThreadManager.h"

#include <condition_variable>
#include <mutex>

struct Event : public Waitable {
public:
	Event() {
		triggered_ = false;
	}

	void Wait() override {
		std::unique_lock<std::mutex> lock;
		if (!triggered_) {
			cond_.wait(lock, [&] { return triggered_.load(); });
		}
	}

	void Notify() {
		std::unique_lock<std::mutex> lock;
		triggered_ = true;
		cond_.notify_one();
	}

private:
	std::condition_variable cond_;
	std::mutex mutex_;
	std::atomic<bool> triggered_;
};
