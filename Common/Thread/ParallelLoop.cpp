#include <algorithm>
#include <cstring>

#include "Common/Thread/ParallelLoop.h"
#include "Common/CPUDetect.h"

class LoopRangeTask : public Task {
public:
	LoopRangeTask(WaitableCounter *counter, const std::function<void(int, int)> &loop, int lower, int upper, TaskPriority p)
		: counter_(counter), loop_(loop), lower_(lower), upper_(upper), priority_(p) {}

	TaskType Type() const override {
		return TaskType::CPU_COMPUTE;
	}

	TaskPriority Priority() const override {
		return priority_;
	}

	void Run() override {
		loop_(lower_, upper_);
		counter_->Count();
	}

	std::function<void(int, int)> loop_;
	WaitableCounter *counter_;

	int lower_;
	int upper_;
	const TaskPriority priority_;
};

WaitableCounter *ParallelRangeLoopWaitable(ThreadManager *threadMan, const std::function<void(int, int)> &loop, int lower, int upper, int minSize, TaskPriority priority) {
	if (minSize == -1) {
		minSize = 1;
	}

	int numTasks = threadMan->GetNumLooperThreads();
	int range = upper - lower;
	if (range <= 0) {
		// Nothing to do. A finished counter allocated to keep the API.
		return new WaitableCounter(0);
	} else if (range <= minSize) {
		// Single background task.
		WaitableCounter *waitableCounter = new WaitableCounter(1);
		threadMan->EnqueueTaskOnThread(0, new LoopRangeTask(waitableCounter, loop, lower, upper, priority));
		return waitableCounter;
	} else {
		// Split the range between threads. Allow for some fractional bits.
		const int fractionalBits = 8;

		int64_t totalFrac = (int64_t)range << fractionalBits;
		int64_t delta = totalFrac / numTasks;

		delta = std::max(delta, (int64_t)minSize << fractionalBits);

		// Now we can compute the actual number of tasks.
		// Remember that stragglers are done on the current thread
		// so we don't round up.
		numTasks = (int)(totalFrac / delta);

		WaitableCounter *waitableCounter = new WaitableCounter(numTasks);
		int64_t counter = (int64_t)lower << fractionalBits;

		// Split up tasks as equitable as possible.
		for (int i = 0; i < numTasks; i++) {
			int start = (int)(counter >> fractionalBits);
			int end = (int)((counter + delta) >> fractionalBits);
			if (end > upper) {
				// Let's do the stragglers on the current thread.
				break;
			}
			threadMan->EnqueueTaskOnThread(i, new LoopRangeTask(waitableCounter, loop, start, end, priority));
			counter += delta;
			if ((counter >> fractionalBits) >= upper) {
				break;
			}
		}

		// Run stragglers on the calling thread directly.
		// We might add a flag later to avoid this for some cases.
		int stragglerStart = (int)(counter >> fractionalBits);
		int stragglerEnd = upper;
		if (stragglerStart < stragglerEnd) {
			loop(stragglerStart, stragglerEnd);
		}
		return waitableCounter;
	}
}

void ParallelRangeLoop(ThreadManager *threadMan, const std::function<void(int, int)> &loop, int lower, int upper, int minSize, TaskPriority priority) {
	if (cpu_info.num_cores == 1 || (minSize >= (upper - lower) && upper > lower)) {
		// "Optimization" for single-core devices, or minSize larger than the range.
		// No point in adding threading overhead, let's just do it inline (since this is the blocking variant).
		loop(lower, upper);
		return;
	}

	if (minSize < 1) {
		// There's no obvious value to default to.
		minSize = 1;
	}

	WaitableCounter *counter = ParallelRangeLoopWaitable(threadMan, loop, lower, upper, minSize, priority);
	// TODO: Optimize using minSize. We'll just compute whether there's a remainer, remove it from the call to ParallelRangeLoopWaitable,
	// and process the remainder right here. If there's no remainer, we'll steal a whole chunk.
	if (counter) {
		counter->WaitAndRelease();
	}
}

// NOTE: Supports a max of 2GB.
void ParallelMemcpy(ThreadManager *threadMan, void *dst, const void *src, size_t bytes, TaskPriority priority) {
	// This threshold should be the same as the minimum split below, 128kb.
	if (bytes < 128 * 1024) {
		memcpy(dst, src, bytes);
		return;
	}

	// unknown's testing showed that 128kB is an appropriate minimum size.

	char *d = (char *)dst;
	const char *s = (const char *)src;
	ParallelRangeLoop(threadMan, [&](int l, int h) {
		memmove(d + l, s + l, h - l);
	}, 0, (int)bytes, 128 * 1024, priority);
}

// NOTE: Supports a max of 2GB.
void ParallelMemset(ThreadManager *threadMan, void *dst, uint8_t value, size_t bytes, TaskPriority priority) {
	// This threshold can probably be a lot bigger.
	if (bytes < 128 * 1024) {
		memset(dst, 0, bytes);
		return;
	}

	// unknown's testing showed that 128kB is an appropriate minimum size.

	char *d = (char *)dst;
	ParallelRangeLoop(threadMan, [&](int l, int h) {
		memset(d + l, value, h - l);
	}, 0, (int)bytes, 128 * 1024, priority);
}
