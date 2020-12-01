#pragma once

#include <functional>

#include "Common/Thread/Channel.h"
#include "Common/Thread/ThreadManager.h"

template<class T>
class PromiseTask : public Task {
public:
	PromiseTask() {}
	~PromiseTask() {
		tx_->Release();
	}

	void Run() override {
		T *value = fun_();
		tx_->Send(value);
		tx_->Release();
	}

	std::function<T *()> fun_;
	Mailbox<T> *tx_;
};

// Represents pending or actual data.
// Has ownership over the data.
// Single use.
// TODO: Split Mailbox (rx_ and tx_) up into separate proxy objects.
template<class T>
class Promise {
public:
	static Promise<T> *Spawn(ThreadManager *threadman, std::function<T *()> fun) {
		// std::pair<Rx<T>, Tx<T>> channel = CreateChannel<T>();
		Mailbox<T> *mailbox = new Mailbox<T>();

		PromiseTask<T> *task = new PromiseTask<T>();
		task->fun_ = fun;
		task->tx_ = mailbox;
		threadman->EnqueueTask(task);

		Promise<T> *promise = new Promise<T>();
		promise->rx_ = mailbox;
		mailbox->AddRef();
		return promise;
	}

	~Promise() {
		if (rx_) {
			rx_->Release();
		}
		delete data_;
	}

	// Returns *T if the data is ready, nullptr if it's not.
	T *Poll() {
		if (ready_) {
			return data_;
		} else {
			if (rx_->Poll(&data_)) {
				ready_ = true;
				return data_;
			} else {
				return nullptr;
			}
		}
	}

	T *BlockUntilReady() {
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

	T *data_ = nullptr;
	bool ready_ = false;
	Mailbox<T> *rx_;
};
