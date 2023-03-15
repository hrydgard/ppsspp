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
	T data_{};
	bool dataReceived_ = false;

	T Wait() {
		std::unique_lock<std::mutex> lock(mutex_);
		condvar_.wait(lock, [&] {return dataReceived_;});
		return data_;
	}

	bool Poll(T *data) {
		std::unique_lock<std::mutex> lock(mutex_);
		if (dataReceived_) {
			*data = data_;
			return true;
		} else {
			return false;
		}
	}

	bool Send(T data) {
		std::unique_lock<std::mutex> lock(mutex_);
		if (!dataReceived_) {
			data_ = data;
			dataReceived_ = true;
			condvar_.notify_all();
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
