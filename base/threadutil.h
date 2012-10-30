#pragma once

#include <functional>

#ifdef _WIN32
#include <windows.h>
#else

#include <unistd.h>

#ifndef _POSIX_THREADS
#error unsupported platform (no pthreads?)
#endif

#include <pthread.h>

#endif

void setCurrentThreadName(const char *name);

/*
class thread {
private:
#ifdef _WIN32
	typedef HANDLE thread_;
#else
	typedef pthread_t thread_;
#endif

public:
	//void run(std::function<void()> threadFunc) {
	//	 func_ = 
	//}

	void wait() {

	}
};*/
