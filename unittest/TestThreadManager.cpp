#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Thread/Channel.h"
#include "Common/Thread/Promise.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Thread/ThreadUtil.h"

struct ResultObject {
	bool ok;
};

ResultObject *ResultProducer() {
	sleep_ms(250);
	printf("result produced: thread %d\n", GetCurrentThreadIdForDebug());
	return new ResultObject{ true };
}

bool TestMailbox() {
	Mailbox<ResultObject> *mailbox = new Mailbox<ResultObject>();
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

bool TestThreadManager() {
	ThreadManager manager;
	manager.Init(8, 1);

	Promise<ResultObject> *object(Promise<ResultObject>::Spawn(&manager, &ResultProducer, TaskType::IO_BLOCKING));

	if (!TestParallelLoop(&manager)) {
		return false;
	}
	sleep_ms(1000);

	ResultObject *result = object->BlockUntilReady();
	if (result) {
		printf("Got result back!");
	}

	delete object;

	if (!TestMailbox()) {
		return false;
	}

	return true;
}
