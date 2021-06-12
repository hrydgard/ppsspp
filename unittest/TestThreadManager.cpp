#pragma once

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
	printf("range %d-%d (thread %d)\n", lower, upper, GetCurrentThreadIdForDebug());
}

// This always passes unless something is badly broken, the interesting thing is the
// logged output.
bool TestParallelLoop(ThreadManager *threadMan) {
	printf("tester thread ID: %d", GetCurrentThreadIdForDebug());

	WaitableCounter *waitable = ParallelRangeLoopWaitable(threadMan, rangeFunc, 0, 7, 1);
	// Can do stuff here if we like.
	waitable->WaitAndRelease();
	// Now it's done.

	ParallelRangeLoop(threadMan, rangeFunc, 0, 65);

	// Try a few synchronous loops (can be slightly more efficient) with various ranges.
	return true;
}

bool TestThreadManager() {
	if (!TestMailbox()) {
		return false;
	}

	ThreadManager manager;
	manager.Init(8, 8);

	Promise<ResultObject> *object(Promise<ResultObject>::Spawn(&manager, &ResultProducer, TaskType::IO_BLOCKING));

	if (!TestParallelLoop(&manager)) {
		return false;
	}
	sleep_ms(1000);

	ResultObject *result = object->BlockUntilReady();
	if (result) {
		// Note that the data is owned by the promise so we don't
		// delete it here.
		printf("Got result back!");
	}

	delete object;
	return true;
}
