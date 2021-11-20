#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cassert>

// Named Channel.h because I originally intended to support a multi item channel as
// well as a simple blocking mailbox. Let's see if we get there.

// Single item mailbox.
// T is copyable. Often T will itself just be a pointer or smart pointer of some sort.
template<class T>
struct Mailbox {
	Mailbox() : refcount_(1) {}
	~Mailbox() {
		assert(refcount_ == 0);
	}

	std::mutex mutex_;
	std::condition_variable condvar_;
	T data_ = nullptr;

	T Wait() {
		std::unique_lock<std::mutex> lock(mutex_);
		while (!data_) {
			condvar_.wait(lock);
		}
		return data_;
	}

	bool Poll(T *data) {
		std::unique_lock<std::mutex> lock(mutex_);
		if (data_) {
			*data = data_;
			return true;
		} else {
			return false;
		}
	}

	bool Send(T data) {
		std::unique_lock<std::mutex> lock(mutex_);
		if (!data_) {
			data_ = data;
			condvar_.notify_one();
			return true;
		} else {
			// Already has value.
			return false;
		}
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
