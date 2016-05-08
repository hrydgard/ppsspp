#include "base/functional.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "thread/thread.h"
#include "thread/prioritizedworkqueue.h"

PrioritizedWorkQueue::~PrioritizedWorkQueue() {
	if (!done_) {
		ELOG("PrioritizedWorkQueue destroyed but not done!");
	}
}

void PrioritizedWorkQueue::Add(PrioritizedWorkQueueItem *item) {
	lock_guard guard(mutex_);
	queue_.push_back(item);
	notEmpty_.notify_one();
}

void PrioritizedWorkQueue::Stop() {
	lock_guard guard(mutex_);
	done_ = true;
	notEmpty_.notify_one();
}

void PrioritizedWorkQueue::Flush() {
	lock_guard guard(mutex_);
	int flush_count = 0;
	for (auto iter = queue_.begin(); iter != queue_.end(); ++iter) {
		delete *iter;
		flush_count++;
	}
	queue_.clear();
	ILOG("Flushed %d un-executed tasks", flush_count);
}

bool PrioritizedWorkQueue::WaitUntilDone(bool all) {
	// We'll lock drain this entire time, so make sure you follow that lock ordering.
	lock_guard guard(drainMutex_);
	if (AllItemsDone()) {
		return true;
	}

	while (!AllItemsDone()) {
		drain_.wait(drainMutex_);
		if (!all) {
			// Return whether empty or not, something just drained.
			return AllItemsDone();
		}
	}

	return true;
}

void PrioritizedWorkQueue::NotifyDrain() {
	lock_guard guard(drainMutex_);
	drain_.notify_one();
}

bool PrioritizedWorkQueue::AllItemsDone() {
	lock_guard guard(mutex_);
	return queue_.empty() && !working_;
}

// The worker should simply call this in a loop. Will block when appropriate.
PrioritizedWorkQueueItem *PrioritizedWorkQueue::Pop() {
	{
		lock_guard guard(mutex_);
		working_ = false;  // The thread only calls Pop if it's done.
	}

	// Important: make sure mutex_ is not locked while draining.
	NotifyDrain();

	lock_guard guard(mutex_);
	if (done_) {
		return 0;
	}

	while (queue_.empty()) {
		notEmpty_.wait(mutex_);
		if (done_) {
			return 0;
		}
	}

	// Find the top priority item (lowest value).
	float best_prio = std::numeric_limits<float>::infinity();
	std::vector<PrioritizedWorkQueueItem *>::iterator best = queue_.end();
	for (auto iter = queue_.begin(); iter != queue_.end(); ++iter) {
		if ((*iter)->priority() < best_prio) {
			best = iter;
			best_prio = (*iter)->priority();
		}
	}

	if (best != queue_.end()) {
		PrioritizedWorkQueueItem *poppedItem = *best;
		queue_.erase(best);
		working_ = true;  // This will be worked on.
		return poppedItem;
	} else {
		// Not really sure how this can happen, but let's be safe.
		return 0;
	}
}

// TODO: This feels ugly. Revisit later.

static std::thread *workThread;

static void threadfunc(PrioritizedWorkQueue *wq) {
	while (true) {
		PrioritizedWorkQueueItem *item = wq->Pop();
		if (!item) {
			if (wq->Done())
				break;
		} else {
			item->run();
			delete item;
		}
	}
}

void ProcessWorkQueueOnThreadWhile(PrioritizedWorkQueue *wq) {
	workThread = new std::thread(std::bind(&threadfunc, wq));
}

void StopProcessingWorkQueue(PrioritizedWorkQueue *wq) {
	wq->Stop();
	if (workThread) {
		workThread->join();
		delete workThread;
	}
	workThread = 0;
}
