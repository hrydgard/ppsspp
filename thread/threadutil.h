#pragma once

#include <functional>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
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


#ifdef _WIN32
#define THREAD_HANDLE HANDLE
#else
#define THREAD_HANDLE pthread_t
#endif


// TODO: replace this abomination with std::thread

class thread {
public:
	virtual void Run();
};