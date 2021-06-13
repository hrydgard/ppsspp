#pragma once

#include "Common/Thread/ThreadManager.h"

#include <condition_variable>
#include <mutex>

struct Event : public Waitable {
public:
	void Wait() override {
		if (triggered_) {
			return;
		}
		std::unique_lock<std::mutex> lock;
		cond_.wait(lock, [&] { return !triggered_; });
	}

	void Notify() {
		std::unique_lock<std::mutex> lock;
		triggered_ = true;
		cond_.notify_one();
	}

private:
	std::condition_variable cond_;
	std::mutex mutex_;
	bool triggered_ = false;
};
