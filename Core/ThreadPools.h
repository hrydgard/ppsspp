#pragma once

#include "Common/Thread/ThreadPool.h"

class GlobalThreadPool {
public:
	// will execute slices of "loop" from "lower" to "upper"
	// in parallel on the global thread pool
	static void Loop(const std::function<void(int,int)>& loop, int lower, int upper, int minSize = -1);
	static void Memcpy(void *dest, const void *src, int size);

private:
	static std::unique_ptr<ThreadPool> pool;
	static std::once_flag init_flag;
	static void Inititialize();
};
