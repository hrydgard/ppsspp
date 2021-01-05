#pragma once

#include "Common/Thread/ThreadPool.h"

class GlobalThreadPool {
public:
	// will execute slices of "loop" from "lower" to "upper"
	// in parallel on the global thread pool
	static void Loop(const std::function<void(int,int)>& loop, int lower, int upper);

private:
	static std::unique_ptr<ThreadPool> pool;
	static std::once_flag init_flag;
	static void Inititialize();
};
