#pragma once

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Thread/Channel.h"
#include "Common/Thread/Promise.h"
#include "Common/Thread/ParallelLoop.h"

struct ResultObject {
	bool ok;
};

ResultObject *ResultProducer() {
	sleep_ms(250);
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
	printf("%d-%d\n", lower, upper);
}

bool TestParallelLoop(ThreadManager *threadMan) {
	WaitableCounter *waitable = ParallelRangeLoopWaitable(threadMan, rangeFunc, 0, 7, 1);

	// Can do stuff here if we like.

	waitable->WaitAndRelease();
	// Now it's done.
	return true;
}

bool TestThreadManager() {
	if (!TestMailbox()) {
		return false;
	}

	ThreadManager manager;
	manager.Init(8);

	Promise<ResultObject> *object(Promise<ResultObject>::Spawn(&manager, &ResultProducer));

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
