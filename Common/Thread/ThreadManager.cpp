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
static constexpr size_t TASK_PRIORITY_COUNT = (size_t)TaskPriority::COUNT;

struct GlobalThreadContext {
	std::mutex mutex;
	std::deque<Task *> compute_queue[TASK_PRIORITY_COUNT];
	std::atomic<int> compute_queue_size;
	std::deque<Task *> io_queue[TASK_PRIORITY_COUNT];
	std::atomic<int> io_queue_size;
	std::vector<TaskThreadContext *> threads_;

	std::atomic<int> roundRobin;
};

struct TaskThreadContext {
	std::atomic<int> queue_size;
	std::deque<Task *> private_queue[TASK_PRIORITY_COUNT];
	std::thread thread; // the worker thread
	std::condition_variable cond; // used to signal new work
	std::mutex mutex; // protects the local queue.
	int index;
	TaskType type;
	std::atomic<bool> cancelled;
	char name[16];
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
	for (TaskThreadContext *&threadCtx : global_->threads_) {
		std::unique_lock<std::mutex> lock(threadCtx->mutex);
		threadCtx->cancelled = true;
		threadCtx->cond.notify_one();
	}

	// Purge any cancellable tasks while the threads shut down.
	if (global_->compute_queue_size > 0 || global_->io_queue_size > 0) {
		auto drainQueue = [&](std::deque<Task *> queue[TASK_PRIORITY_COUNT], std::atomic<int> &size) {
			for (size_t i = 0; i < TASK_PRIORITY_COUNT; ++i) {
				for (auto it = queue[i].begin(); it != queue[i].end(); ++it) {
					if (TeardownTask(*it, false)) {
						queue[i].erase(it);
						size--;
						return false;
					}
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

	for (TaskThreadContext *&threadCtx : global_->threads_) {
		threadCtx->thread.join();
		// TODO: Is it better to just delete these?
		for (size_t i = 0; i < TASK_PRIORITY_COUNT; ++i) {
			for (Task *task : threadCtx->private_queue[i]) {
				TeardownTask(task, true);
			}
		}
		delete threadCtx;
	}
	global_->threads_.clear();

	if (global_->compute_queue_size > 0 || global_->io_queue_size > 0) {
		WARN_LOG(Log::System, "ThreadManager::Teardown() with tasks still enqueued");
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
		size_t queueIndex = (size_t)task->Priority();
		if (task->Type() == TaskType::CPU_COMPUTE) {
			global_->compute_queue[queueIndex].push_back(task);
			global_->compute_queue_size++;
		} else if (task->Type() == TaskType::IO_BLOCKING) {
			global_->io_queue[queueIndex].push_back(task);
			global_->io_queue_size++;
		} else {
			_assert_(false);
		}
	}
	return false;
}

static void WorkerThreadFunc(GlobalThreadContext *global, TaskThreadContext *thread) {
	if (thread->type == TaskType::CPU_COMPUTE) {
		snprintf(thread->name, sizeof(thread->name), "PoolWorker %d", thread->index);
	} else {
		_assert_(thread->type == TaskType::IO_BLOCKING);
		snprintf(thread->name, sizeof(thread->name), "PoolWorkerIO %d", thread->index);
	}
	SetCurrentThreadName(thread->name);

	if (thread->type == TaskType::IO_BLOCKING) {
		AttachThreadToJNI();
	}

	const bool isCompute = thread->type == TaskType::CPU_COMPUTE;
	const auto global_queue_size = [isCompute, &global]() -> int {
		return isCompute ? global->compute_queue_size.load() : global->io_queue_size.load();
	};

	while (!thread->cancelled) {
		Task *task = nullptr;

		// Check the global queue first, then check the private queue and wait if there's nothing to do.
		if (global_queue_size() > 0) {
			// Grab one from the global queue if there is any.
			std::unique_lock<std::mutex> lock(global->mutex);
			auto queue = isCompute ? global->compute_queue : global->io_queue;
			auto &queue_size = isCompute ? global->compute_queue_size : global->io_queue_size;

			for (size_t p = 0; p < TASK_PRIORITY_COUNT; ++p) {
				if (!queue[p].empty()) {
					task = queue[p].front();
					queue[p].pop_front();
					queue_size--;

					// We are processing one now, so mark that.
					thread->queue_size++;
					break;
				} else if (thread->queue_size != 0) {
					// Check the thread, as we prefer a HIGH thread task to a global NORMAL task.
					std::unique_lock<std::mutex> lock(thread->mutex);
					if (!thread->private_queue[p].empty()) {
						task = thread->private_queue[p].front();
						thread->private_queue[p].pop_front();
						break;
					}
				}
			}
		}

		if (!task) {
			// We didn't have any global, do we have anything on the thread?
			std::unique_lock<std::mutex> lock(thread->mutex);
			for (size_t p = 0; p < TASK_PRIORITY_COUNT; ++p) {
				if (thread->private_queue[p].empty())
					continue;

				task = thread->private_queue[p].front();
				thread->private_queue[p].pop_front();
				break;
			}

			// We must check both queue and single again, while locked.
			bool wait = !thread->cancelled && !task && global_queue_size() == 0;

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
			// _dbg_assert_(thread->queue_size == thread->private_queue[0].size() + thread->private_queue[1].size() + thread->private_queue[2].size());
		}
	}

	// In case it got attached to JNI, detach it. Don't think this has any side effects if called redundantly.
	if (thread->type == TaskType::IO_BLOCKING) {
		DetachThreadFromJNI();
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

	INFO_LOG(Log::System, "ThreadManager::Init(compute threads: %d, all: %d)", numComputeThreads_, numThreads_);

	for (int i = 0; i < numThreads; i++) {
		TaskThreadContext *thread = new TaskThreadContext();
		thread->cancelled.store(false);
		thread->type = i < numComputeThreads_ ? TaskType::CPU_COMPUTE : TaskType::IO_BLOCKING;
		thread->index = i;
		thread->thread = std::thread(&WorkerThreadFunc, global_, thread);
		global_->threads_.push_back(thread);
	}
}

void ThreadManager::EnqueueTask(Task *task) {
	if (task->Type() == TaskType::DEDICATED_THREAD) {
		std::thread th([=](Task *task) {
			SetCurrentThreadName("DedicatedThreadTask");
			task->Run();
			task->Release();
		}, task);
		th.detach();
		return;
	}

	_assert_msg_(IsInitialized(), "ThreadManager not initialized");

	size_t queueIndex = (size_t)task->Priority();
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
		TaskThreadContext *thread = global_->threads_[threadNum];
		if (thread->queue_size.load() == 0) {
			std::unique_lock<std::mutex> lock(thread->mutex);
			thread->private_queue[queueIndex].push_back(task);
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
			global_->compute_queue[queueIndex].push_back(task);
			global_->compute_queue_size++;
		} else if (task->Type() == TaskType::IO_BLOCKING) {
			global_->io_queue[queueIndex].push_back(task);
			global_->io_queue_size++;
		} else {
			_assert_(false);
		}
	}

	int chosenIndex = global_->roundRobin++;
	chosenIndex = minThread + (chosenIndex % (maxThread - minThread));
	TaskThreadContext *&chosenThread = global_->threads_[chosenIndex];

	// Lock the thread to ensure it gets the message.
	std::unique_lock<std::mutex> lock(chosenThread->mutex);
	chosenThread->cond.notify_one();
}

void ThreadManager::EnqueueTaskOnThread(int threadNum, Task *task) {
	_assert_msg_(task->Type() != TaskType::DEDICATED_THREAD, "Dedicated thread tasks can't be put on specific threads");

	_assert_msg_(threadNum >= 0 && threadNum < (int)global_->threads_.size(), "Bad threadnum or not initialized");
	TaskThreadContext *thread = global_->threads_[threadNum];
	size_t queueIndex = (size_t)task->Priority();

	thread->queue_size++;

	std::unique_lock<std::mutex> lock(thread->mutex);
	thread->private_queue[queueIndex].push_back(task);
	thread->cond.notify_one();
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
