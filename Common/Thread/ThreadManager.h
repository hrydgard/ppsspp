#pragma once

// The new threadpool.

// To help future smart scheduling.
enum class TaskType {
	CPU_COMPUTE,
	IO_BLOCKING,
};

class Task {
public:
	virtual ~Task() {}
	virtual void run() = 0;
	virtual bool cancellable() { return false; }
	virtual void cancel() {}
};

struct ThreadContext;
struct GlobalThreadContext;

class ThreadManager {
public:
	ThreadManager();
	~ThreadManager();

	void Init(int numWorkerThreads);
	void EnqueueTask(Task *task, TaskType taskType = TaskType::CPU_COMPUTE);
	void EnqueueTaskOnThread(int threadNum, Task *task, TaskType taskType = TaskType::CPU_COMPUTE);

	// Currently does nothing. It will always be best-effort - maybe it cancels,
	// maybe it doesn't.
	void TryCancelTask(Task *task);

	// Parallel loops get to use half the threads,
	// so we still have some worker threads for other tasks.
	int GetNumLooperThreads() const;

private:
	GlobalThreadContext *global_;

	friend struct ThreadContext;
};

extern ThreadManager g_threadManager;
