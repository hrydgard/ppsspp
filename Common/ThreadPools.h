#pragma once

#include "thread/threadpool.h"

class GlobalThreadPool {
public:
	// will execute slices of "loop" from "lower" to "upper"
	// in parallel on the global thread pool
	static void Loop(const std::function<void(int,int)>& loop, int lower, int upper);

private:
	static std::shared_ptr<ThreadPool> pool;
	static bool initialized;
	static void Inititialize();
};
