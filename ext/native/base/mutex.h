#pragma once

// Simple cross platform mutex implementation.
// Similar to the new C++11 api.

// Windows and pthreads implementations in one.

// TODO: Need to clean up these primitives and put them in a reasonable namespace.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <limits.h>
#include <intrin.h>

// Zap stupid windows defines
// Should move these somewhere clever.
#undef p
#undef DrawText
#undef itoa
#undef min
#undef max

#else
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#endif

#include "base/basictypes.h"

struct atomic_flag_init {
	atomic_flag_init() : v(false) {
	}

	bool v;
};
static const atomic_flag_init NATIVE_ATOMIC_FLAG_INIT;

class atomic_flag {
public:
	atomic_flag() {
	}

	atomic_flag(const atomic_flag_init &v) : value(0) {
	}

	void clear() {
#if defined(_WIN32)
		_WriteBarrier();
		value = 0;
#else
		__sync_lock_release(&value);
#endif
	}

	// Returns the previous value.
	bool test_and_set() {
#if defined(_WIN32)
		return InterlockedExchange(&value, 1) != 0;
#else
		return __sync_lock_test_and_set(&value, 1) != 0;
#endif
	}

private:
	volatile unsigned int value;

	DISALLOW_COPY_AND_ASSIGN(atomic_flag);
};

class recursive_mutex {
#ifdef _WIN32
	typedef CRITICAL_SECTION mutexType;
#else
	typedef pthread_mutex_t mutexType;
#endif
public:
	recursive_mutex() {
#ifdef _WIN32
		InitializeCriticalSection(&mut_);
#else
		// Critical sections are recursive so let's make these recursive too.
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&mut_, &attr);
#endif
	}
	~recursive_mutex() {
#ifdef _WIN32
		DeleteCriticalSection(&mut_);
#else
		pthread_mutex_destroy(&mut_);
#endif
	}

	bool trylock() {
#ifdef _WIN32
		return TryEnterCriticalSection(&mut_) != FALSE;
#else
		return pthread_mutex_trylock(&mut_) != EBUSY;
#endif
	}

	void lock() {
#ifdef _WIN32
		EnterCriticalSection(&mut_);
#else
		pthread_mutex_lock(&mut_);
#endif
	}

	void unlock() {
#ifdef _WIN32
		LeaveCriticalSection(&mut_);
#else
		pthread_mutex_unlock(&mut_);
#endif
	}

	mutexType &native_handle() {
		return mut_;
	}

private:
	mutexType mut_;
	recursive_mutex(const recursive_mutex &other);
	recursive_mutex& operator=(const recursive_mutex& other);
};

class lock_guard {
public:
	lock_guard(recursive_mutex &mtx) : mtx_(mtx) {mtx_.lock();}
	~lock_guard() {mtx_.unlock();}

private:
	recursive_mutex &mtx_;
};


// Like a Windows event, or a modern condition variable.

class event {
public:
#ifdef _WIN32
#else
#endif
	event() {
#ifdef _WIN32
		event_ = CreateEvent(0, FALSE, FALSE, 0);
#else
		pthread_cond_init(&event_, NULL);
#endif
	}
	~event() {
#ifdef _WIN32
		CloseHandle(event_);
#else
		pthread_cond_destroy(&event_);
#endif
	}

	void notify_one() {
#ifdef _WIN32
		SetEvent(event_);
#else
		pthread_cond_signal(&event_);
#endif
	}

	// notify_all is not really possible to implement with win32 events?

	void wait(recursive_mutex &mtx) {
	// broken
#ifdef _WIN32
		// This has to be horribly racy.
		mtx.lock();
		WaitForSingleObject(event_, INFINITE);
		ResetEvent(event_); // necessary?
		mtx.unlock();
#else
		pthread_mutex_lock(&mtx.native_handle());
		pthread_cond_wait(&event_, &mtx.native_handle());
		pthread_mutex_unlock(&mtx.native_handle());
#endif
	}

	void wait_for(recursive_mutex &mtx, int milliseconds) {
#ifdef _WIN32
		//mtx.unlock();
		WaitForSingleObject(event_, milliseconds);
		ResetEvent(event_); // necessary?
		// mtx.lock();
#else
		timespec timeout;
#ifdef __APPLE__
		timeval tv;
		gettimeofday(&tv, NULL);
		timeout.tv_sec = tv.tv_sec;
		timeout.tv_nsec = tv.tv_usec * 1000;
#else
		clock_gettime(CLOCK_REALTIME, &timeout);
#endif
		timeout.tv_sec += milliseconds / 1000;
		timeout.tv_nsec += (milliseconds % 1000) * 1000000;
		pthread_mutex_lock(&mtx.native_handle());
		pthread_cond_timedwait(&event_, &mtx.native_handle(), &timeout);
		pthread_mutex_unlock(&mtx.native_handle());
#endif
	}

	void reset() {
#ifdef _WIN32
		ResetEvent(event_);
#endif
	}
private:
#ifdef _WIN32
	HANDLE event_;
#else
	pthread_cond_t event_;
#endif
};

// Win32 CONDITION_VARIABLE is Vista+ only. We thus limit it to our 64-bit builds where
// we can be almost certain that the user isn't running XP.

class condition_variable {
public:
#ifdef _WIN32
#else
#endif
	condition_variable() {
#ifdef _WIN32
#ifdef _WIN64
		InitializeConditionVariable(&cond_);
#else
		sema_ = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
		waiting_ = 0;
#endif
#else
		pthread_cond_init(&event_, NULL);
#endif
	}
	~condition_variable() {
#ifdef _WIN32
#ifdef _WIN64
#else
		CloseHandle(sema_);
#endif
#else
		pthread_cond_destroy(&event_);
#endif
	}

	void notify_one() {
#ifdef _WIN32
#ifdef _M_X64
		WakeConditionVariable(&cond_);
#else
		// Should be locked at this time.
		if (waiting_ != 0) {
			ReleaseSemaphore(sema_, 1, NULL);
		}
#endif
#else
		pthread_cond_signal(&event_);
#endif
	}

	// notify_all is not really possible to implement with win32 events?
	// Can be done just fine using WakeAllConditionVariable though.

	void wait(recursive_mutex &mtx) {
		// broken http://msdn.microsoft.com/en-us/library/windows/desktop/ms686301(v=vs.85).aspx
#ifdef _WIN32
#ifdef _M_X64
		SleepConditionVariableCS(&cond_, &mtx.native_handle(), INFINITE);
#else
		// Since a semaphore keeps a count, the window between unlock and lock won't cause us to deadlock.
		// However, we could wake early incorrectly (if the semaphore is signalled already.)
		// That's better than deadlocking or forgetting to wake.
		waiting_++;
		mtx.unlock();
		WaitForSingleObject(sema_, INFINITE);
		mtx.lock();
		waiting_--;
#endif
#else
		pthread_cond_wait(&event_, &mtx.native_handle());
#endif
	}

	void wait_for(recursive_mutex &mtx, int milliseconds) {
#ifdef _WIN32
#ifdef _M_X64
		SleepConditionVariableCS(&cond_, &mtx.native_handle(), milliseconds);
#else
		// Since a semaphore keeps a count, the window between unlock and lock won't cause us to deadlock.
		// However, we could wake early incorrectly (if the semaphore is signalled already.)
		// That's better than deadlocking or forgetting to wake.
		waiting_++;
		mtx.unlock();
		WaitForSingleObject(sema_, milliseconds);
		mtx.lock();
		waiting_--;
#endif
#else
		timespec timeout;
#ifdef __APPLE__
		timeval tv;
		gettimeofday(&tv, NULL);
		timeout.tv_sec = tv.tv_sec;
		timeout.tv_nsec = tv.tv_usec * 1000;
#else
		clock_gettime(CLOCK_REALTIME, &timeout);
#endif
		timeout.tv_sec += milliseconds / 1000;
		timeout.tv_nsec += (milliseconds % 1000) * 1000000;
		pthread_cond_timedwait(&event_, &mtx.native_handle(), &timeout);
#endif
	}

private:
#ifdef _WIN32
#ifdef _M_X64
	CONDITION_VARIABLE cond_;
#else
	HANDLE sema_;
	volatile int waiting_;
#endif
#else
	pthread_cond_t event_;
#endif
};
