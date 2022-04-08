#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/Barrier.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Thread/Channel.h"
#include "Common/Thread/Promise.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Thread/ThreadUtil.h"

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
	WaitableCounter *waitable = ParallelRangeLoopWaitable(threadMan, rangeFunc, 0, 7, 1);
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
	WaitableCounter *waitable2 = ParallelRangeLoopWaitable(threadMan, rangeFunc, 10, 30, 40);
	waitable2->WaitAndRelease();
	return true;
}

// This is some ugly stuff but realistic.
const size_t THREAD_COUNT = 6;  // Must match the number of threads in TestMultithreadedScheduling
const size_t ITERATIONS = 100000;

static std::atomic<int> g_atomicCounter;
static ThreadManager *g_threadMan;
static CountingBarrier g_barrier(THREAD_COUNT + 1);

class IncrementTask : public Task {
public:
	IncrementTask(TaskType type) : type_(type) {}
	~IncrementTask() {}
	virtual TaskType Type() const { return type_; }
	virtual void Run() {
		g_atomicCounter++;
	}
private:
	TaskType type_;
};

void ThreadFunc() {
	for (int i = 0; i < ITERATIONS; i++) {
		g_threadMan->EnqueueTask(new IncrementTask((i & 1) ? TaskType::CPU_COMPUTE : TaskType::IO_BLOCKING));
	}
	g_barrier.Arrive();
}

bool TestMultithreadedScheduling() {
	g_atomicCounter = 0;

	auto start = Instant::Now();

	std::thread thread1(ThreadFunc);
	std::thread thread2(ThreadFunc);
	std::thread thread3(ThreadFunc);
	std::thread thread4(ThreadFunc);
	std::thread thread5(ThreadFunc);
	std::thread thread6(ThreadFunc);

	// Just testing the barrier
	g_barrier.Arrive();
	// OK, all are done.

	EXPECT_EQ_INT(g_atomicCounter, THREAD_COUNT * ITERATIONS);

	thread1.join();
	thread2.join();
	thread3.join();
	thread4.join();
	thread5.join();
	thread6.join();

	printf("Stress test elapsed: %0.2f", start.Elapsed());

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
