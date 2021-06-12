#include <cstring>

#include "Common/Thread/ParallelLoop.h"
#include "Common/CPUDetect.h"

class LoopRangeTask : public Task {
public:
	LoopRangeTask(WaitableCounter *counter, const std::function<void(int, int)> &loop, int lower, int upper)
		: counter_(counter), loop_(loop), lower_(lower), upper_(upper) {}

	void Run() override {
		loop_(lower_, upper_);
		counter_->Count();
	}

	std::function<void(int, int)> loop_;
	WaitableCounter *counter_;

	int lower_;
	int upper_;
};

WaitableCounter *ParallelRangeLoopWaitable(ThreadManager *threadMan, const std::function<void(int, int)> &loop, int lower, int upper, int minSize) {
	if (minSize == -1) {
		minSize = 1;
	}

	int numTasks = threadMan->GetNumLooperThreads();

	int range = upper - lower;
	if (range <= 0) {
		// Bad range. A finished counter allocated.
		return new WaitableCounter(0);
	}

	if (range <= numTasks) {
		// Just assign one task per thread, as many as we have.
		WaitableCounter *counter = new WaitableCounter(range);
		for (int i = 0; i < range; i++) {
			threadMan->EnqueueTaskOnThread(i, new LoopRangeTask(counter, loop, i, i + 1), TaskType::CPU_COMPUTE);
		}
		return counter;
	} else {
		WaitableCounter *counter = new WaitableCounter(numTasks);
		// Split the range between threads. 
		double dx = (double)range / (double)numTasks;
		double d = 0.0;
		int lastEnd = 0;
		for (int i = 0; i < numTasks; i++) {
			int start = lastEnd;
			d += dx;
			int end = i == numTasks - 1 ? range : (int)d;
			threadMan->EnqueueTaskOnThread(i, new LoopRangeTask(counter, loop, start, end), TaskType::CPU_COMPUTE);
			lastEnd = end;
		}
		return counter;
	}
}

void ParallelRangeLoop(ThreadManager *threadMan, const std::function<void(int, int)> &loop, int lower, int upper, int minSize) {
	if (cpu_info.num_cores == 1) {
		// "Optimization" for single-core devices.
		// No point in adding threading overhead, let's just do it inline.
		loop(lower, upper);
		return;
	}

	if (minSize == -1) {
		minSize = 4;
	}

	WaitableCounter *counter = ParallelRangeLoopWaitable(threadMan, loop, lower, upper, minSize);
	// TODO: Optimize using minSize. We'll just compute whether there's a remainer, remove it from the call to ParallelRangeLoopWaitable,
	// and process the remainder right here. If there's no remainer, we'll steal a whole chunk.
	if (counter) {
		counter->WaitAndRelease();
	}
}

void ParallelMemcpy(ThreadManager *threadMan, void *dst, const void *src, size_t bytes) {
	// This threshold can probably be a lot bigger.
	if (bytes < 512) {
		memcpy(dst, src, bytes);
		return;
	}

	// 128 is the largest cacheline size on common CPUs.
	// Still I suspect that the optimal minSize is a lot higher.

	char *d = (char *)dst;
	char *s = (char *)src;
	ParallelRangeLoop(threadMan, [&](int l, int h) {
		memmove(d + l, s + l, h - l);
	}, 0, 128);
}


void ParallelMemset(ThreadManager *threadMan, void *dst, uint8_t value, size_t bytes) {
	// This threshold can probably be a lot bigger.
	if (bytes < 512) {
		memset(dst, 0, bytes);
		return;
	}

	// 128 is the largest cacheline size on common CPUs.
	// Still I suspect that the optimal minSize is a lot higher.

	char *d = (char *)dst;
	ParallelRangeLoop(threadMan, [&](int l, int h) {
		memset(d + l, value, h - l);
	}, 0, 128);
}
