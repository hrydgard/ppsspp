#include <functional>

#include "base/logging.h"
#include "thread/thread.h"
#include "thread/prioritizedworkqueue.h"

#if defined(IOS) || (defined(__APPLE__) && !defined(__MAC_10_7))
namespace std {
	using tr1::bind;
}
#endif

PrioritizedWorkQueue::~PrioritizedWorkQueue() {
	if (!done_) {
		ELOG("PrioritizedWorkQueue destroyed but not done!");
	}
}

void PrioritizedWorkQueue::Add(PrioritizedWorkQueueItem *item) {
	mutex_.lock();
	queue_.push_back(item);
	mutex_.unlock();
	notEmpty_.notify_one();
}

void PrioritizedWorkQueue::Stop() {
	mutex_.lock();
	done_ = true;
	notEmpty_.notify_one();
	mutex_.unlock();
}

// The worker should simply call this in a loop. Will block when appropriate.
PrioritizedWorkQueueItem *PrioritizedWorkQueue::Pop() {
	mutex_.lock();
	while (queue_.empty()) {
		notEmpty_.wait(mutex_);
		if (done_)
			return 0;
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
	PrioritizedWorkQueueItem *poppedItem = *best;
	queue_.erase(best);
	mutex_.unlock();
	return poppedItem;
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
		}
	}
}

void ProcessWorkQueueOnThreadWhile(PrioritizedWorkQueue *wq) {
	workThread = new std::thread(std::bind(&threadfunc, wq));
}

void StopProcessingWorkQueue(PrioritizedWorkQueue *wq) {
	wq->Stop();
	workThread->join();
	delete workThread;
	workThread = 0;
}