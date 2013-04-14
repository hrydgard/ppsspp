#pragma once

#include <vector>
#include <limits>

#include "base/basictypes.h"
#include "base/mutex.h"
#include "thread/threadutil.h"

// Priorities can change dynamically.
// Try to make priority() fast, it will be called a lot.

class PrioritizedWorkQueueItem {
public:
	PrioritizedWorkQueueItem() {}
	virtual ~PrioritizedWorkQueueItem() {}
	virtual void run() = 0;
	virtual float priority() = 0;  // Low priority value = high priority!

private:
	DISALLOW_COPY_AND_ASSIGN(PrioritizedWorkQueueItem);
};

class PrioritizedWorkQueue {
public:
	PrioritizedWorkQueue() : done_(false) {}
	~PrioritizedWorkQueue();
	// Takes ownership.
	void Add(PrioritizedWorkQueueItem *item);

	// The worker should simply call this in a loop. Will block when appropriate.
	PrioritizedWorkQueueItem *Pop();

	void Flush();
	bool Done() { return done_; }
	void Stop();

private:
	bool done_;
	recursive_mutex mutex_;
	condition_variable notEmpty_;

	std::vector<PrioritizedWorkQueueItem *> queue_;

	DISALLOW_COPY_AND_ASSIGN(PrioritizedWorkQueue);
};


// Starts up a thread that keeps trying to run this workqueue.
// TODO: This feels ugly. Revisit later.
void ProcessWorkQueueOnThreadWhile(PrioritizedWorkQueue *wq);
void StopProcessingWorkQueue(PrioritizedWorkQueue *wq);