#include "ThreadPools.h"

#include "../Core/Config.h"

std::shared_ptr<ThreadPool> GlobalThreadPool::pool;
bool  GlobalThreadPool::initialized = false;

void GlobalThreadPool::Loop(const std::function<void(int,int)>& loop, int lower, int upper) {
	Inititialize();
	pool->ParallelLoop(loop, lower, upper);
}

void GlobalThreadPool::Inititialize() {
	if(!initialized) {
		pool = std::make_shared<ThreadPool>(g_Config.iNumWorkerThreads);
		initialized = true;
	}
}

