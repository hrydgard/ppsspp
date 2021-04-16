#include <algorithm>
#include <cstring>
#include "Common/Thread/ThreadPool.h"
#include "Common/Thread/ThreadUtil.h"

#include "Common/Log.h"
#include "Common/MakeUnique.h"

///////////////////////////// WorkerThread

WorkerThread::~WorkerThread() {
	{
		std::lock_guard<std::mutex> guard(mutex);
		active = false;
		signal.notify_one();
	}
	if (thread.joinable()) {
		thread.join();
	}
}

void WorkerThread::StartUp() {
	thread = std::thread(std::bind(&WorkerThread::WorkFunc, this));
}

void WorkerThread::Process(std::function<void()> work) {
	std::lock_guard<std::mutex> guard(mutex);
	work_ = std::move(work);
	jobsTarget = jobsDone + 1;
	signal.notify_one();
}

void WorkerThread::WaitForCompletion() {
	std::unique_lock<std::mutex> guard(doneMutex);
	while (jobsDone < jobsTarget) {
		done.wait(guard);
	}
}

void WorkerThread::WorkFunc() {
	setCurrentThreadName("Worker");
	std::unique_lock<std::mutex> guard(mutex);
	while (active) {
		// 'active == false' is one of the conditions for signaling,
		// do not "optimize" it
		while (active && jobsTarget <= jobsDone) {
			signal.wait(guard);
		}
		if (active) {
			work_();

			std::lock_guard<std::mutex> doneGuard(doneMutex);
			jobsDone++;
			done.notify_one();
		}
	}
}

void LoopWorkerThread::ProcessLoop(std::function<void(int, int)> work, int start, int end) {
	std::lock_guard<std::mutex> guard(mutex);
	loopWork_ = std::move(work);
	work_ = [this]() {
		loopWork_(start_, end_);
	};
	start_ = start;
	end_ = end;
	jobsTarget = jobsDone + 1;
	signal.notify_one();
}

///////////////////////////// ThreadPool

ThreadPool::ThreadPool(int numThreads) {
	if (numThreads <= 0) {
		numThreads_ = 1;
		INFO_LOG(JIT, "ThreadPool: Bad number of threads %d", numThreads);
	} else if (numThreads > 16) {
		INFO_LOG(JIT, "ThreadPool: Capping number of threads to 16 (was %d)", numThreads);
		numThreads_ = 16;
	} else {
		numThreads_ = numThreads;
	}
}

void ThreadPool::StartWorkers() {
	if (!workersStarted) {
		workers.reserve(numThreads_ - 1);
		for(int i = 0; i < numThreads_ - 1; ++i) { // create one less worker thread as the thread calling ParallelLoop will also do work
			auto workerPtr = make_unique<LoopWorkerThread>();
			workerPtr->StartUp();
			workers.push_back(std::move(workerPtr));
		}
		workersStarted = true;
	}
}

void ThreadPool::ParallelLoop(const std::function<void(int,int)> &loop, int lower, int upper, int minSize) {
	// Don't parallelize tiny loops.
	if (minSize == -1)
		minSize = 4;

	int range = upper - lower;
	if (range >= minSize) {
		std::lock_guard<std::mutex> guard(mutex);
		StartWorkers();

		// could do slightly better load balancing for the generic case, 
		// but doesn't matter since all our loops are power of 2
		int chunk = std::max(minSize, range / numThreads_);
		int s = lower;
		for (auto &worker : workers) {
			// We'll do the last chunk on the current thread.
			if (s + chunk >= upper) {
				break;
			}
			worker->ProcessLoop(loop, s, s + chunk);
			s += chunk;
		}
		// This is the final chunk.
		if (s < upper)
			loop(s, upper);
		for (auto &worker : workers) {
			worker->WaitForCompletion();
		}
	} else {
		loop(lower, upper);
	}
}

void ThreadPool::ParallelMemcpy(void *dest, const void *src, int size) {
	static const int MIN_SIZE = 128 * 1024;
	ParallelLoop([&](int l, int h) {
		memmove((uint8_t *)dest + l, (const uint8_t *)src + l, h - l);
	}, 0, size, MIN_SIZE);
}
