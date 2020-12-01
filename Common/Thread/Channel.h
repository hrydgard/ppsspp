#pragma once

#include <mutex>
#include <condition_variable>

// Single item mailbox.
template<class T>
struct Mailbox {
	Mailbox() : refcount_(1) {}

	std::mutex mutex_;
	std::condition_variable condvar_;
	T *data_ = nullptr;

	T *Wait() {
		T *data;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			while (!data_) {
				condvar_.wait(lock);
			}
			data = data_;
		}
		return data;
	}

	bool Poll(T **data) {
		bool retval = false;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			if (data_) {
				*data = data_;
				retval = true;
			}
		}
		return retval;
	}

	bool Send(T *data) {
		bool success = false;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			if (!data_) {
				data_ = data;
				success = true;
			}
			condvar_.notify_one();
		}
		return success;
	}

	void AddRef() {
		refcount_.fetch_add(1);
	}

	void Release() {
		int count = refcount_.fetch_sub(1);
		if (count == 1) {  // was definitely decreased to 0
			delete this;
		}
	}

private:
	std::atomic<int> refcount_;
};
