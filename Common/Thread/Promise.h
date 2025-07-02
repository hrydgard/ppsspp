#pragma once

#include <functional>
#include <mutex>

#include "Common/Log.h"
#include "Common/Thread/Channel.h"
#include "Common/Thread/ThreadManager.h"

// Nobody needs to wait for this (except threadpool shutdown).
template<class T>
class IndependentTask : public Task {
public:
	IndependentTask(TaskType type, TaskPriority prio, T func) : func_(std::move(func)), type_(type), prio_(prio) {}
	TaskType Type() const override { return type_; }
	TaskPriority Priority() const override { return prio_; }
	void Run() override {
		func_();
	}
private:
	T func_;
	TaskType type_;
	TaskPriority prio_;
};

template<class T>
class PromiseTask : public Task {
public:
	PromiseTask(std::function<T ()> fun, Mailbox<T> *tx, TaskType t, TaskPriority p)
		: fun_(fun), tx_(tx), type_(t), priority_(p) {
		tx_->AddRef();
	}
	~PromiseTask() {
		tx_->Release();
	}

	TaskType Type() const override {
		return type_;
	}

	TaskPriority Priority() const override {
		return priority_;
	}

	void Run() override {
		T value = fun_();
		if (!cancelled_) {
			tx_->Send(value);
		} else {
			INFO_LOG(Log::System, "PromiseTask ended after cancellation");
		}
	}

	bool Cancellable() const override {
		return true;
	}

	void Cancel() override {
		INFO_LOG(Log::System, "PromiseTask cancelled");
		cancelled_ = true;
	}

	std::function<T ()> fun_;
	Mailbox<T> *tx_;
	const TaskType type_;
	const TaskPriority priority_;

	std::atomic<bool> cancelled_{};
};

// Represents pending or actual data.
// Has ownership over the data. Single use.
// TODO: Split Mailbox (rx_ and tx_) up into separate proxy objects.
// NOTE: Poll/BlockUntilReady should only be used from one thread.
// TODO: Make movable?
template<class T>
class Promise {
public:
	// Never fails.
	static Promise<T> *Spawn(ThreadManager *threadman, std::function<T()> fun, TaskType taskType, TaskPriority taskPriority = TaskPriority::NORMAL) {
		Mailbox<T> *mailbox = new Mailbox<T>();

		Promise<T> *promise = new Promise<T>();
		promise->rx_ = mailbox;

		PromiseTask<T> *task = new PromiseTask<T>(fun, mailbox, taskType, taskPriority);
		threadman->EnqueueTask(task);
		promise->task_ = task;
		return promise;
	}

	static Promise<T> *AlreadyDone(T data) {
		Promise<T> *promise = new Promise<T>();
		promise->data_ = data;
		promise->ready_ = true;
		return promise;
	}

	static Promise<T> *CreateEmpty() {
		Mailbox<T> *mailbox = new Mailbox<T>();
		Promise<T> *promise = new Promise<T>();
		promise->rx_ = mailbox;
		return promise;
	}

	// Allow an empty promise to spawn, too, in case we want to delay it.
	void SpawnEmpty(ThreadManager *threadman, std::function<T()> fun, TaskType taskType, TaskPriority taskPriority = TaskPriority::NORMAL) {
		task_ = new PromiseTask<T>(fun, rx_, taskType, taskPriority);
		threadman->EnqueueTask(task_);
	}

	~Promise() {
		std::lock_guard<std::mutex> guard(readyMutex_);
		// A promise should have been fulfilled before it's destroyed.
		_assert_(ready_);
		_assert_(!rx_);
		sentinel_ = 0xeeeeeeee;
	}

	// Returns T if the data is ready, nullptr if it's not.
	// Obviously, can only be used if T is nullable, otherwise it won't compile.
	T Poll() {
		uint32_t sentinel = sentinel_;
		_assert_msg_(sentinel == 0xffc0ffee, "%08x", sentinel);
		std::lock_guard<std::mutex> guard(readyMutex_);
		if (ready_) {
			return data_;
		} else {
			_dbg_assert_(rx_);
			if (rx_->Poll(&data_)) {
				rx_->Release();
				rx_ = nullptr;
				ready_ = true;
				task_ = nullptr;
				return data_;
			} else {
				return nullptr;
			}
		}
	}

	T BlockUntilReady() {
		uint32_t sentinel = sentinel_;
		_assert_msg_(sentinel == 0xffc0ffee, "%08x", sentinel);
		std::lock_guard<std::mutex> guard(readyMutex_);
		if (ready_) {
			return data_;
		} else {
			_dbg_assert_(rx_);
			data_ = rx_->Wait();
			rx_->Release();
			rx_ = nullptr;
			ready_ = true;
			task_ = nullptr;
			return data_;
		}
	}

	// For outside injection of data, when not using Spawn.
	void Post(T data) {
		rx_->Send(data);
	}

	void Cancel() {
		std::lock_guard<std::mutex> guard(readyMutex_);
		if (!ready_) {
			ready_ = true;
			_dbg_assert_(task_);
			if (task_) {
				task_->Cancel();
			}
			rx_->Release();
			rx_ = nullptr;
		}
	}

private:
	Promise() {}

	// Promise can only be constructed in Spawn (or AlreadyDone).
	T data_{};
	bool ready_ = false;
	std::mutex readyMutex_;
	Mailbox<T> *rx_ = nullptr;
	uint32_t sentinel_ = 0xffc0ffee;
	PromiseTask<T> *task_ = nullptr;
};
