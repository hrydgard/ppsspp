#pragma once

#include <cstdint>

// The new threadpool.

// To help future smart scheduling.
enum class TaskType {
	CPU_COMPUTE,
	IO_BLOCKING,
};

// Implement this to make something that you can run on the thread manager.
class Task {
public:
	virtual ~Task() {}
	virtual void Run() = 0;
	virtual bool Cancellable() { return false; }
	virtual void Cancel() {}
	virtual float Priority() { return 1.0f; }
	virtual uint64_t id() { return 0; }
};

class Waitable {
public:
	virtual ~Waitable() {}

	virtual void Wait() = 0;

	void WaitAndRelease() {
		Wait();
		delete this;
	}
};

struct ThreadContext;
struct GlobalThreadContext;

class ThreadManager {
public:
	ThreadManager();
	~ThreadManager();

	// The distinction here is to be able to take hyper-threading into account.
	// It gets even trickier when you think about mobile chips with BIG/LITTLE, but we'll
	// just ignore it and let the OS handle it.
	void Init(int numRealCores, int numLogicalCores);
	void EnqueueTask(Task *task, TaskType taskType);
	void EnqueueTaskOnThread(int threadNum, Task *task, TaskType taskType);

	// Currently does nothing. It will always be best-effort - maybe it cancels,
	// maybe it doesn't. Note that the id is the id() returned by the task. You need to make that
	// something meaningful yourself.
	void TryCancelTask(uint64_t id);

	// Parallel loops get to use half the threads,
	// so we still have some worker threads for other tasks.
	int GetNumLooperThreads() const;

private:
	void Teardown();

	GlobalThreadContext *global_ = nullptr;

	int numThreads_ = 0;
	int numComputeThreads_ = 0;

	friend struct ThreadContext;
};

extern ThreadManager g_threadManager;
