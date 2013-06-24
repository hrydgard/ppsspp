#pragma once

#include "thread.h"
#include "base/mutex.h"
#include "base/functional.h"

// This is the simplest possible worker implementation I can think of
// but entirely sufficient for the given purpose.
// Only handles a single item of work at a time.
class WorkerThread {
public:
	WorkerThread();
	~WorkerThread();

	// submit a new work item
	void Process(const std::function<void()>& work);
	// wait for a submitted work item to be completed
	void WaitForCompletion();

private:
	std::thread *thread; // the worker thread
	::condition_variable signal; // used to signal new work
	::condition_variable done; // used to signal work completion
	::recursive_mutex mutex, doneMutex; // associated with each respective condition variable
	volatile bool active, started;
	std::function<void()> work_; // the work to be done by this thread

	void WorkFunc();

	WorkerThread(const WorkerThread& other); // prevent copies
	void operator =(const WorkerThread &other);
};

// A thread pool manages a set of worker threads, and allows the execution of parallel loops on them
// individual parallel loops are fully sequentialized to simplify synchronization, which should not 
// be a problem as they should each use the entire system
class ThreadPool {
public:
	ThreadPool(int numThreads);
	// don't need a destructor, "workers" is cleared on delete, 
	// leading to the stopping and joining of all worker threads (RAII and all that)

	void ParallelLoop(std::function<void(int,int)> loop, int lower, int upper);

private:
	const int numThreads;
	std::vector<std::shared_ptr<WorkerThread>> workers;
	::recursive_mutex mutex; // used to sequentialize loop execution

	bool workersStarted;
	void StartWorkers();
	
	ThreadPool(const ThreadPool& other); // prevent copies
	void operator =(const WorkerThread &other);
};

