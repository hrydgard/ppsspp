#pragma once

#include <functional>

#include "Common/Thread/Channel.h"
#include "Common/Thread/ThreadManager.h"

template<class T>
class PromiseTask : public Task {
public:
	PromiseTask(std::function<T ()> fun, Mailbox<T> *tx, TaskType t) : fun_(fun), tx_(tx), type_(t) {
		tx_->AddRef();
	}
	~PromiseTask() {
		tx_->Release();
	}

	TaskType Type() const override {
		return type_;
	}

	void Run() override {
		T value = fun_();
		tx_->Send(value);
	}

	std::function<T ()> fun_;
	Mailbox<T> *tx_;
	TaskType type_;
};

// Represents pending or actual data.
// Has ownership over the data. Single use.
// TODO: Split Mailbox (rx_ and tx_) up into separate proxy objects.
// NOTE: Poll/BlockUntilReady should only be used from one thread.
template<class T>
class Promise {
public:
	static Promise<T> *Spawn(ThreadManager *threadman, std::function<T()> fun, TaskType taskType) {
		Mailbox<T> *mailbox = new Mailbox<T>();

		Promise<T> *promise = new Promise<T>();
		promise->rx_ = mailbox;

		PromiseTask<T> *task = new PromiseTask<T>(fun, mailbox, taskType);
		threadman->EnqueueTask(task);
		return promise;
	}

	~Promise() {
		// A promise should have been fulfilled before it's destroyed.
		_assert_(ready_);
		_assert_(!rx_);
		delete data_;
	}

	// Returns T if the data is ready, nullptr if it's not.
	T Poll() {
		if (ready_) {
			return data_;
		} else {
			if (rx_->Poll(&data_)) {
				rx_->Release();
				rx_ = nullptr;
				ready_ = true;
				return data_;
			} else {
				return nullptr;
			}
		}
	}

	T BlockUntilReady() {
		if (ready_) {
			return data_;
		} else {
			data_ = rx_->Wait();
			rx_->Release();
			rx_ = nullptr;
			ready_ = true;
			return data_;
		}
	}

private:
	Promise() {}

	T data_ = nullptr;
	bool ready_ = false;
	Mailbox<T> *rx_;
};
