#include <cstdio>
#include <thread>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <atomic>

#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Thread/ThreadManager.h"

struct GlobalThreadContext {
	std::mutex mutex; // associated with each respective condition variable
	std::deque<Task *> queue;
	std::vector<ThreadContext *> threads_;
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
	for (size_t i = 0; i < global_->threads_.size(); i++) {
		global_->threads_[i]->cancelled = true;
		global_->threads_[i]->cond.notify_one();
	}
	for (size_t i = 0; i < global_->threads_.size(); i++) {
		global_->threads_[i]->thread.join();
	}
	global_->threads_.clear();
	delete global_;
}

static void WorkerThreadFunc(GlobalThreadContext *global, ThreadContext *thread) {
	char threadName[16];
	snprintf(threadName, sizeof(threadName), "PoolWorker %d", thread->index);
	SetCurrentThreadName(threadName);
	while (!thread->cancelled) {
		Task *task = nullptr;
		// Check the thread-private queue first, then check the global queue.
		{
			std::unique_lock<std::mutex> lock(thread->mutex);
			if (!thread->private_queue.empty()) {
				task = thread->private_queue.front();
				thread->private_queue.pop_front();
				thread->queueSize.store((int)thread->private_queue.size());
			} else {
				thread->cond.wait(lock);
			}
		}
		if (!task) {
			// Grab one from the global queue if there is any.
			std::unique_lock<std::mutex> lock(global->mutex);
			if (!global->queue.empty()) {
				task = global->queue.front();
				global->queue.pop_front();
			}
		}  // Don't try to else here!

		// The task itself takes care of notifying anyone waiting on it. Not the
		// responsibility of the ThreadManager (although it could be!).
		if (task) {
			task->run();
			delete task;
		}
	}
}

void ThreadManager::Init(int numThreads) {
	for (int i = 0; i < numThreads; i++) {
		ThreadContext *thread = new ThreadContext();
		thread->cancelled.store(false);
		thread->thread = std::thread(&WorkerThreadFunc, global_, thread);
		thread->index = i;
		global_->threads_.push_back(thread);
	}
}

void ThreadManager::EnqueueTask(Task *task, TaskType taskType) {
	// Find a thread with no outstanding work.
	for (int i = 0; i < global_->threads_.size(); i++) {
		ThreadContext *thread = global_->threads_[i];
		if (thread->queueSize.load() == 0) {
			std::unique_lock<std::mutex> lock(thread->mutex);
			thread->private_queue.push_back(task);
			thread->queueSize.store((int)thread->private_queue.size());
			thread->cond.notify_one();
			// Found it - done.
			return;
		}
	}

	// Still not scheduled? Put it on the global queue and notify a random thread.
	{
		std::unique_lock<std::mutex> lock(global_->mutex);
		global_->queue.push_back(task);
		global_->threads_[0]->cond.notify_one();
	}
}

void ThreadManager::EnqueueTaskOnThread(int threadNum, Task *task, TaskType taskType) {
	_assert_(threadNum >= 0 && threadNum < (int)global_->threads_.size())
	ThreadContext *thread = global_->threads_[threadNum];
	{
		std::unique_lock<std::mutex> lock(thread->mutex);
		thread->private_queue.push_back(task);
		thread->cond.notify_one();
	}
}

int ThreadManager::GetNumLooperThreads() const {
	return (int)(global_->threads_.size() / 2);
}

void ThreadManager::TryCancelTask(Task *task) {
	// Do nothing
}
