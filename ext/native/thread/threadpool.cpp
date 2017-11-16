#include "base/logging.h"
#include "thread/threadpool.h"
#include "thread/threadutil.h"

///////////////////////////// WorkerThread

WorkerThread::WorkerThread() : active(true), started(false) {
	thread.reset(new std::thread(std::bind(&WorkerThread::WorkFunc, this)));
	while(!started) { };
}

WorkerThread::~WorkerThread() {
	mutex.lock();
	active = false;
	signal.notify_one();
	mutex.unlock();
	thread->join();
}

void WorkerThread::Process(const std::function<void()>& work) {
	mutex.lock();
	work_ = work;
	jobsTarget = jobsDone + 1;
	signal.notify_one();
	mutex.unlock();
}

void WorkerThread::WaitForCompletion() {
	std::unique_lock<std::mutex> guard(doneMutex);
	if (jobsDone < jobsTarget) {
		done.wait(guard);
	}
}

void WorkerThread::WorkFunc() {
	setCurrentThreadName("Worker");
	std::unique_lock<std::mutex> guard(mutex);
	started = true;
	while (active) {
		signal.wait(guard);
		if (active) {
			work_();
			doneMutex.lock();
			done.notify_one();
			jobsDone++;
			doneMutex.unlock();
		}
	}
}

LoopWorkerThread::LoopWorkerThread() : WorkerThread(true) {
	thread.reset(new std::thread(std::bind(&LoopWorkerThread::WorkFunc, this)));
	while (!started) { };
}

void LoopWorkerThread::Process(const std::function<void(int, int)> &work, int start, int end) {
	std::lock_guard<std::mutex> guard(mutex);
	work_ = work;
	start_ = start;
	end_ = end;
	jobsTarget = jobsDone + 1;
	signal.notify_one();
}

void LoopWorkerThread::WorkFunc() {
	setCurrentThreadName("LoopWorker");
	std::unique_lock<std::mutex> guard(mutex);
	started = true;
	while (active) {
		signal.wait(guard);
		if (active) {
			work_(start_, end_);
			doneMutex.lock();
			done.notify_one();
			jobsDone++;
			doneMutex.unlock();
		}
	}
}

///////////////////////////// ThreadPool

ThreadPool::ThreadPool(int numThreads) : workersStarted(false) {
	if (numThreads <= 0) {
		numThreads_ = 1;
		ILOG("ThreadPool: Bad number of threads %i", numThreads);
	} else if (numThreads > 8) {
		ILOG("ThreadPool: Capping number of threads to 8 (was %i)", numThreads);
		numThreads_ = 8;
	} else {
		numThreads_ = numThreads;
	}
}

void ThreadPool::StartWorkers() {
	if (!workersStarted) {
		for(int i = 0; i < numThreads_; ++i) {
			workers.push_back(std::make_shared<LoopWorkerThread>());
		}
		workersStarted = true;
	}
}

void ThreadPool::ParallelLoop(const std::function<void(int,int)> &loop, int lower, int upper) {
	int range = upper - lower;
	if (range >= numThreads_ * 2) { // don't parallelize tiny loops (this could be better, maybe add optional parameter that estimates work per iteration)
		std::lock_guard<std::mutex> guard(mutex);
		StartWorkers();

		// could do slightly better load balancing for the generic case, 
		// but doesn't matter since all our loops are power of 2
		int chunk = range / numThreads_;
		int s = lower;
		for (int i = 0; i < numThreads_ - 1; ++i) {
			workers[i]->Process(loop, s, s+chunk);
			s+=chunk;
		}
		// This is the final chunk.
		loop(s, upper);
		for (int i = 0; i < numThreads_ - 1; ++i) {
			workers[i]->WaitForCompletion();
		}
	} else {
		loop(lower, upper);
	}
}

