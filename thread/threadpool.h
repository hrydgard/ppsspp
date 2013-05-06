#pragma once

#include "thread.h"
#include "base/mutex.h"

#include <functional>
#ifdef __SYMBIAN32__
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/weak_ptr.hpp>
using namespace boost;
#else
#include <memory>
using namespace std;
#endif
#include <vector>

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MACGNUSTD)
#ifndef __SYMBIAN32__
#include <tr1/functional>
#include <tr1/memory>
#endif
namespace std {
#ifndef __SYMBIAN32__
	using tr1::bind;
	using tr1::function;
	using tr1::shared_ptr;
#endif

	template <typename T>
	inline shared_ptr<T> make_shared()
	{
		return shared_ptr<T>(new T());
	}

	template <typename T, typename Arg1>
	inline shared_ptr<T> make_shared(Arg1& arg1)
	{
		return shared_ptr<T>(new T(arg1));
	}
}
#endif

// This is the simplest possible worker implementation I can think of
// but entirely sufficient for the given purpose.
// Only handles a single item of work at a time.
class WorkerThread {
public:
	WorkerThread();
	~WorkerThread();

	// submit a new work item
	void Process(const function<void()>& work);
	// wait for a submitted work item to be completed
	void WaitForCompletion();

private:
	std::thread *thread; // the worker thread
	::condition_variable signal; // used to signal new work
	::condition_variable done; // used to signal work completion
	::recursive_mutex mutex, doneMutex; // associated with each respective condition variable
	volatile bool active, started;
	function<void()> work_; // the work to be done by this thread

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

	void ParallelLoop(function<void(int,int)> loop, int lower, int upper);

private:
	const int numThreads;
	std::vector<shared_ptr<WorkerThread>> workers;
	::recursive_mutex mutex; // used to sequentialize loop execution

	bool workersStarted;
	void StartWorkers();
	
	ThreadPool(const ThreadPool& other); // prevent copies
	void operator =(const WorkerThread &other);
};
