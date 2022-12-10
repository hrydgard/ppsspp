#include <cstdio>
#include <algorithm>
#include <thread>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <atomic>

#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Thread/ThreadManager.h"

// Threads and task scheduling
//
// * The threadpool should contain a number of threads that's the the number of cores,
//   plus a fixed number more for I/O-limited background tasks.
// * Parallel compute-limited loops should use as many threads as there are cores.
//   They should always be scheduled to the first N threads.
// * For some tasks, splitting the input values up linearly between the threads
//   is not fair. However, we ignore that for now.

const int MAX_CORES_TO_USE = 16;
const int MIN_IO_BLOCKING_THREADS = 4;

struct GlobalThreadContext {
	std::mutex mutex; // associated with each respective condition variable
	std::deque<Task *> compute_queue;
	std::atomic<int> compute_queue_size;
	std::deque<Task *> io_queue;
	std::atomic<int> io_queue_size;
	std::vector<ThreadContext *> threads_;

	std::atomic<int> roundRobin;
};

struct ThreadContext {
	std::thread thread; // the worker thread
	std::condition_variable cond; // used to signal new work
	std::mutex mutex; // protects the local queue.
	std::atomic<int> queue_size;
	int index;
	TaskType type;
	std::atomic<bool> cancelled;
	std::atomic<Task *> private_single;
	std::deque<Task *> private_queue;
};

ThreadManager::ThreadManager() : global_(new GlobalThreadContext()) {
	global_->compute_queue_size = 0;
	global_->io_queue_size = 0;
	global_->roundRobin = 0;
}

ThreadManager::~ThreadManager() {
	delete global_;
}

void ThreadManager::Teardown() {
	for (ThreadContext *&threadCtx : global_->threads_) {
		threadCtx->cancelled = true;
		std::unique_lock<std::mutex> lock(threadCtx->mutex);
		threadCtx->cond.notify_one();
	}

	// Purge any cancellable tasks while the threads shut down.
	if (global_->compute_queue_size > 0 || global_->io_queue_size > 0) {
		auto drainQueue = [&](std::deque<Task *> &queue, std::atomic<int> &size) {
			for (auto it = queue.begin(); it != queue.end(); ++it) {
				if (TeardownTask(*it, false)) {
					queue.erase(it);
					size--;
					return false;
				}
			}
			return true;
		};

		std::unique_lock<std::mutex> lock(global_->mutex);
		while (!drainQueue(global_->compute_queue, global_->compute_queue_size))
			continue;
		while (!drainQueue(global_->io_queue, global_->io_queue_size))
			continue;
	}

	for (ThreadContext *&threadCtx : global_->threads_) {
		threadCtx->thread.join();
		// TODO: Is it better to just delete these?
		TeardownTask(threadCtx->private_single, true);
		for (Task *task : threadCtx->private_queue) {
			TeardownTask(threadCtx->private_single, true);
		}
		delete threadCtx;
	}
	global_->threads_.clear();

	if (global_->compute_queue_size > 0 || global_->io_queue_size > 0) {
		WARN_LOG(SYSTEM, "ThreadManager::Teardown() with tasks still enqueued");
	}
}

bool ThreadManager::TeardownTask(Task *task, bool enqueue) {
	if (!task)
		return true;

	if (task->Cancellable()) {
		task->Cancel();
		task->Release();
		return true;
	}

	if (enqueue) {
		if (task->Type() == TaskType::CPU_COMPUTE) {
			global_->compute_queue.push_back(task);
			global_->compute_queue_size++;
		} else if (task->Type() == TaskType::IO_BLOCKING) {
			global_->io_queue.push_back(task);
			global_->io_queue_size++;
		} else {
			_assert_(false);
		}
	}
	return false;
}

static void WorkerThreadFunc(GlobalThreadContext *global, ThreadContext *thread) {
	char threadName[16];
	if (thread->type == TaskType::CPU_COMPUTE) {
		snprintf(threadName, sizeof(threadName), "PoolWorker %d", thread->index);
	} else {
		_assert_(thread->type == TaskType::IO_BLOCKING);
		snprintf(threadName, sizeof(threadName), "PoolWorkerIO %d", thread->index);
	}
	SetCurrentThreadName(threadName);

	const bool isCompute = thread->type == TaskType::CPU_COMPUTE;
	const auto global_queue_size = [isCompute, &global]() -> int {
		return isCompute ? global->compute_queue_size.load() : global->io_queue_size.load();
	};

	while (!thread->cancelled) {
		Task *task = thread->private_single.exchange(nullptr);

		// Check the global queue first, then check the private queue and wait if there's nothing to do.
		if (!task && global_queue_size() > 0) {
			// Grab one from the global queue if there is any.
			std::unique_lock<std::mutex> lock(global->mutex);
			auto &queue = isCompute ? global->compute_queue : global->io_queue;
			auto &queue_size = isCompute ? global->compute_queue_size : global->io_queue_size;

			if (!queue.empty()) {
				task = queue.front();
				queue.pop_front();
				queue_size--;

				// We are processing one now, so mark that.
				thread->queue_size++;
			}
		}

		if (!task) {
			std::unique_lock<std::mutex> lock(thread->mutex);
			// We must check both queue and single again, while locked.
			bool wait = true;
			if (!thread->private_queue.empty()) {
				task = thread->private_queue.front();
				thread->private_queue.pop_front();
				wait = false;
			} else if (thread->private_single || thread->cancelled) {
				wait = false;
			} else {
				wait = global_queue_size() == 0;
			}

			if (wait)
				thread->cond.wait(lock);
		}
		// The task itself takes care of notifying anyone waiting on it. Not the
		// responsibility of the ThreadManager (although it could be!).
		if (task) {
			task->Run();
			task->Release();

			// Reduce the queue size once complete.
			thread->queue_size--;
		}
	}
}

void ThreadManager::Init(int numRealCores, int numLogicalCoresPerCpu) {
	if (IsInitialized()) {
		Teardown();
	}

	numComputeThreads_ = std::min(numRealCores * numLogicalCoresPerCpu, MAX_CORES_TO_USE);
	// Double it for the IO blocking threads.
	int numThreads = numComputeThreads_ + std::max(MIN_IO_BLOCKING_THREADS, numComputeThreads_);
	numThreads_ = numThreads;

	INFO_LOG(SYSTEM, "ThreadManager::Init(compute threads: %d, all: %d)", numComputeThreads_, numThreads_);

	for (int i = 0; i < numThreads; i++) {
		ThreadContext *thread = new ThreadContext();
		thread->cancelled.store(false);
		thread->private_single.store(nullptr);
		thread->type = i < numComputeThreads_ ? TaskType::CPU_COMPUTE : TaskType::IO_BLOCKING;
		thread->index = i;
		thread->thread = std::thread(&WorkerThreadFunc, global_, thread);
		global_->threads_.push_back(thread);
	}
}

void ThreadManager::EnqueueTask(Task *task) {
	_assert_msg_(IsInitialized(), "ThreadManager not initialized");

	int minThread;
	int maxThread;
	if (task->Type() == TaskType::CPU_COMPUTE) {
		// only the threads reserved for heavy compute.
		minThread = 0;
		maxThread = numComputeThreads_;
	} else {
		// Only IO blocking threads (to avoid starving compute threads.)
		minThread = numComputeThreads_;
		maxThread = numThreads_;
	}

	// Find a thread with no outstanding work.
	_assert_(maxThread <= (int)global_->threads_.size());
	for (int threadNum = minThread; threadNum < maxThread; threadNum++) {
		ThreadContext *thread = global_->threads_[threadNum];
		if (thread->queue_size.load() == 0) {
			std::unique_lock<std::mutex> lock(thread->mutex);
			thread->private_queue.push_back(task);
			thread->queue_size++;
			thread->cond.notify_one();
			// Found it - done.
			return;
		}
	}

	// Still not scheduled? Put it on the global queue and notify a thread chosen by round-robin.
	// Not particularly scientific, but hopefully we should not run into this too much.
	{
		std::unique_lock<std::mutex> lock(global_->mutex);
		if (task->Type() == TaskType::CPU_COMPUTE) {
			global_->compute_queue.push_back(task);
			global_->compute_queue_size++;
		} else if (task->Type() == TaskType::IO_BLOCKING) {
			global_->io_queue.push_back(task);
			global_->io_queue_size++;
		} else {
			_assert_(false);
		}
	}

	int chosenIndex = global_->roundRobin++;
	chosenIndex = minThread + (chosenIndex % (maxThread - minThread));
	ThreadContext *&chosenThread = global_->threads_[chosenIndex];

	// Lock the thread to ensure it gets the message.
	std::unique_lock<std::mutex> lock(chosenThread->mutex);
	chosenThread->cond.notify_one();
}

void ThreadManager::EnqueueTaskOnThread(int threadNum, Task *task, bool enforceSequence) {
	_assert_msg_(threadNum >= 0 && threadNum < (int)global_->threads_.size(), "Bad threadnum or not initialized");
	ThreadContext *thread = global_->threads_[threadNum];

	// Try first atomically, as highest priority.
	Task *expected = nullptr;
	bool queued = !enforceSequence && thread->private_single.compare_exchange_weak(expected, task);
	// Whether we got that or will have to wait, increase the queue counter.
	thread->queue_size++;

	if (queued) {
		std::unique_lock<std::mutex> lock(thread->mutex);
		thread->cond.notify_one();
	} else {
		std::unique_lock<std::mutex> lock(thread->mutex);
		thread->private_queue.push_back(task);
		thread->cond.notify_one();
	}
}

int ThreadManager::GetNumLooperThreads() const {
	return numComputeThreads_;
}

void ThreadManager::TryCancelTask(uint64_t taskID) {
	// Do nothing for now, just let it finish.
}

bool ThreadManager::IsInitialized() const {
	return !global_->threads_.empty();
}
