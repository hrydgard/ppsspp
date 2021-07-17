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
const int EXTRA_THREADS = 4;  // For I/O limited tasks

struct GlobalThreadContext {
	std::mutex mutex; // associated with each respective condition variable
	std::deque<Task *> queue;
	std::vector<ThreadContext *> threads_;

	int roundRobin = 0;
};

struct ThreadContext {
	std::thread thread; // the worker thread
	std::condition_variable cond; // used to signal new work
	std::mutex mutex; // protects the local queue.
	std::atomic<int> queueSize;
	int index;
	std::atomic<bool> cancelled;
	std::deque<Task *> private_queue;
};

ThreadManager::ThreadManager() : global_(new GlobalThreadContext()) {

}

ThreadManager::~ThreadManager() {
	delete global_;
}

void ThreadManager::Teardown() {
	for (size_t i = 0; i < global_->threads_.size(); i++) {
		global_->threads_[i]->cancelled = true;
		global_->threads_[i]->cond.notify_one();
	}
	for (size_t i = 0; i < global_->threads_.size(); i++) {
		global_->threads_[i]->thread.join();
		delete global_->threads_[i];
	}
	global_->threads_.clear();
}

static void WorkerThreadFunc(GlobalThreadContext *global, ThreadContext *thread) {
	char threadName[16];
	snprintf(threadName, sizeof(threadName), "PoolWorker %d", thread->index);
	SetCurrentThreadName(threadName);
	while (!thread->cancelled) {
		Task *task = nullptr;

		// Check the global queue first, then check the private queue and wait if there's nothing to do.
		{
			// Grab one from the global queue if there is any.
			std::unique_lock<std::mutex> lock(global->mutex);
			if (!global->queue.empty()) {
				task = global->queue.front();
				global->queue.pop_front();
			}
		}

		if (!task) {
			std::unique_lock<std::mutex> lock(thread->mutex);
			if (!thread->private_queue.empty()) {
				task = thread->private_queue.front();
				thread->private_queue.pop_front();
				thread->queueSize.store((int)thread->private_queue.size());
			} else {
				thread->cond.wait(lock);
			}
		}
		// The task itself takes care of notifying anyone waiting on it. Not the
		// responsibility of the ThreadManager (although it could be!).
		if (task) {
			task->Run();
			delete task;
		}
	}
}

void ThreadManager::Init(int numRealCores, int numLogicalCoresPerCpu) {
	if (!global_->threads_.empty()) {
		Teardown();
	}

	numComputeThreads_ = std::min(numRealCores * numLogicalCoresPerCpu, MAX_CORES_TO_USE);
	int numThreads = numComputeThreads_ + EXTRA_THREADS;
	numThreads_ = numThreads;

	INFO_LOG(SYSTEM, "ThreadManager::Init(compute threads: %d, all: %d)", numComputeThreads_, numThreads_);

	for (int i = 0; i < numThreads; i++) {
		ThreadContext *thread = new ThreadContext();
		thread->cancelled.store(false);
		thread->thread = std::thread(&WorkerThreadFunc, global_, thread);
		thread->index = i;
		global_->threads_.push_back(thread);
	}
}

void ThreadManager::EnqueueTask(Task *task, TaskType taskType) {
	int maxThread;
	int threadOffset = 0;
	if (taskType == TaskType::CPU_COMPUTE) {
		// only the threads reserved for heavy compute.
		maxThread = numComputeThreads_;
		threadOffset = 0;
	} else {
		// any free thread
		maxThread = numThreads_;
		threadOffset = numComputeThreads_;
	}

	// Find a thread with no outstanding work.
	int threadNum = threadOffset;
	for (int i = 0; i < maxThread; i++, threadNum++) {
		if (threadNum >= global_->threads_.size()) {
			threadNum = 0;
		}
		ThreadContext *thread = global_->threads_[threadNum];
		if (thread->queueSize.load() == 0) {
			std::unique_lock<std::mutex> lock(thread->mutex);
			thread->private_queue.push_back(task);
			thread->queueSize.store((int)thread->private_queue.size());
			thread->cond.notify_one();
			// Found it - done.
			return;
		}
	}

	// Still not scheduled? Put it on the global queue and notify a thread chosen by round-robin.
	// Not particularly scientific, but hopefully we should not run into this too much.
	{
		std::unique_lock<std::mutex> lock(global_->mutex);
		global_->queue.push_back(task);
		global_->threads_[global_->roundRobin % maxThread]->cond.notify_one();
		global_->roundRobin++;
	}
}

void ThreadManager::EnqueueTaskOnThread(int threadNum, Task *task, TaskType taskType) {
	_assert_(threadNum >= 0 && threadNum < (int)global_->threads_.size());
	ThreadContext *thread = global_->threads_[threadNum];
	{
		std::unique_lock<std::mutex> lock(thread->mutex);
		thread->private_queue.push_back(task);
		thread->cond.notify_one();
	}
}

int ThreadManager::GetNumLooperThreads() const {
	return numComputeThreads_;
}

void ThreadManager::TryCancelTask(uint64_t taskID) {
	// Do nothing
}
