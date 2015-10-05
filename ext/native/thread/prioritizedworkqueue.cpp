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
	if (queue_.empty())
		return;
	lock_guard guard(mutex_);
	for (auto iter = queue_.begin(); iter != queue_.end(); ++iter) {
		delete *iter;
	}
	queue_.clear();
}

void PrioritizedWorkQueue::WaitUntilDone() {
	if (queue_.empty())
		return;
	// This could be made more elegant..
	while (true) {
		bool empty;
		{
			lock_guard guard(mutex_);
			empty = queue_.empty();
		}
		if (empty) {
			break;
		}
		sleep_ms(10);
	}
}


// The worker should simply call this in a loop. Will block when appropriate.
PrioritizedWorkQueueItem *PrioritizedWorkQueue::Pop() {
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
