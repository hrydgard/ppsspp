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


// POD so can be moved around freely.
// TODO: I can't quite get this to work with all the moves, the refcount blows up. I'll just use mailboxes directly.

/*
template<class T>
class Rx {
public:
	Rx() : mbx_(nullptr) {}
	Rx(Mailbox<T> *mbx) : mbx_(mbx) { mbx_->AddRef(); }
	Rx(Rx<T> &&rx) { mbx_ = rx.mbx_; }
	Rx<T>& operator=(Rx<T> &&rx) { mbx_ = rx.mbx_; return *this; }
	~Rx() {
		mbx_->Release();
	}
	bool Poll(T **data) {
		return mbx_->Poll(data);
	}
	T *Wait() {
		return mbx_->Wait();
	}

private:
	Mailbox<T> *mbx_;
};

// POD so can be moved around freely.
template<class T>
class Tx {
public:
	Tx() : mbx_(nullptr) {}
	Tx(Mailbox<T> *mbx) : mbx_(mbx) { mbx_->AddRef(); }
	Tx(Tx<T> &&rx) { mbx_ = rx.mbx_; }
	Tx<T>& operator=(Tx<T> &&rx) { mbx_ = rx.mbx_; return *this; }

	~Tx() {
		mbx_->Release();
	}
	bool Send(T *t) {
		return mbx_->Send(t);
	}

private:
	Mailbox<T> *mbx_;
};

template<class T>
std::pair<Rx<T>, Tx<T>> CreateChannel() {
	Mailbox<T> *mbx = new Mailbox<T>();
	auto retval = std::make_pair(Rx<T>(mbx), Tx<T>(mbx));
	mbx->Release();
	return retval;
}
*/
