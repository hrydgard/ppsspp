#include <thread>
#include <vector>
#include <cstdio>

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/Barrier.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Thread/Channel.h"
#include "Common/Thread/Promise.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Thread/Waitable.h"

#include "UnitTest.h"

struct ResultObject {
	bool ok;
};

ResultObject *ResultProducer() {
	sleep_ms(250);
	printf("result produced: thread %d\n", GetCurrentThreadIdForDebug());
	return new ResultObject{ true };
}

bool TestMailbox() {
	Mailbox<ResultObject *> *mailbox = new Mailbox<ResultObject *>();
	mailbox->Send(new ResultObject{ true });
	ResultObject *data;
	data = mailbox->Wait();
	_assert_(data && data->ok);
	delete data;
	mailbox->Release();
	return true;
}

void rangeFunc(int lower, int upper) {
	sleep_ms(30);
	printf(" - range %d-%d (thread %d)\n", lower, upper, GetCurrentThreadIdForDebug());
}

// This always passes unless something is badly broken, the interesting thing is the
// logged output.
bool TestParallelLoop(ThreadManager *threadMan) {
	printf("tester thread ID: %d\n", GetCurrentThreadIdForDebug());

	printf("waitable test\n");
	WaitableCounter *waitable = ParallelRangeLoopWaitable(threadMan, rangeFunc, 0, 7, 1, TaskPriority::HIGH);
	// Can do stuff here if we like.
	waitable->WaitAndRelease();
	// Now it's done.

	// Try a loop with stragglers.
	printf("blocking test #1 [0-65)\n");
	ParallelRangeLoop(threadMan, rangeFunc, 0, 65, 1);
	// Try a loop with a relatively large minimum size.
	printf("blocking test #2 [0-100)\n");
	ParallelRangeLoop(threadMan, rangeFunc, 0, 100, 40);
	// Try a loop with minimum size larger than range.
	printf("waitable test [10-30)\n");
	WaitableCounter *waitable2 = ParallelRangeLoopWaitable(threadMan, rangeFunc, 10, 30, 40, TaskPriority::LOW);
	waitable2->WaitAndRelease();
	return true;
}

const size_t THREAD_COUNT = 9;
const size_t ITERATIONS = 40000;

static std::atomic<int> g_atomicCounter;
static ThreadManager *g_threadMan;
static CountingBarrier g_barrier(THREAD_COUNT + 1);

class IncrementTask : public Task {
public:
	IncrementTask(TaskType type, LimitedWaitable *waitable) : type_(type), waitable_(waitable) {}
	~IncrementTask() {}
	TaskType Type() const override { return type_; }
	TaskPriority Priority() const override {
		return TaskPriority::NORMAL;
	}
	void Run() override {
		g_atomicCounter++;
		waitable_->Notify();
	}
private:
	TaskType type_;
	LimitedWaitable *waitable_;
};

void ThreadFunc() {
	for (int i = 0; i < ITERATIONS; i++) {
		auto threadWaitable = new LimitedWaitable();
		g_threadMan->EnqueueTask(new IncrementTask((i & 1) ? TaskType::CPU_COMPUTE : TaskType::IO_BLOCKING, threadWaitable));
		threadWaitable->WaitAndRelease();
	}
	g_barrier.Arrive();
}

bool TestMultithreadedScheduling() {
	g_atomicCounter = 0;

	auto start = Instant::Now();

	std::vector<std::thread> threads;
	for (int i = 0; i < THREAD_COUNT; i++) {
		threads.push_back(std::thread(ThreadFunc));
	}

	// Just testing the barrier
	g_barrier.Arrive();
	// OK, all are done.

	EXPECT_EQ_INT(g_atomicCounter, THREAD_COUNT * ITERATIONS);

	for (int i = 0; i < THREAD_COUNT; i++) {
		threads[i].join();
	}

	threads.clear();

	printf("Stress test elapsed: %0.2f", start.ElapsedSeconds());

	return true;
}

bool TestThreadManager() {
	ThreadManager manager;
	manager.Init(8, 1);

	g_threadMan = &manager;

	Promise<ResultObject *> *object(Promise<ResultObject *>::Spawn(&manager, &ResultProducer, TaskType::IO_BLOCKING));

	if (!TestParallelLoop(&manager)) {
		return false;
	}
	sleep_ms(100);

	ResultObject *result = object->BlockUntilReady();
	if (result) {
		printf("Got result back!\n");
	}

	delete object;

	if (!TestMailbox()) {
		return false;
	}

	if (!TestMultithreadedScheduling()) {
		return false;
	}

	return true;
}
