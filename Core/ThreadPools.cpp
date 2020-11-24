#include "ThreadPools.h"

#include "../Core/Config.h"
#include "Common/MakeUnique.h"

std::unique_ptr<ThreadPool> GlobalThreadPool::pool;
std::once_flag GlobalThreadPool::init_flag;

void GlobalThreadPool::Loop(const std::function<void(int,int)>& loop, int lower, int upper) {
	std::call_once(init_flag, Inititialize);
	pool->ParallelLoop(loop, lower, upper);
}

void GlobalThreadPool::Inititialize() {
	pool = make_unique<ThreadPool>(g_Config.iNumWorkerThreads);
}
