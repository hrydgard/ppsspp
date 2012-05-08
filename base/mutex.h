#pragma once
// Simple cross platform mutex implementation.
// Similar to the new C++11 api.
// Windows and pthreads implementations in one.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <errno.h>
#endif


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
    pthread_mutex_init(&mut_, NULL);
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
    return TryEnterCriticalSection(&mut_) == TRUE;
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

private:
  mutexType mut_;
};

class lock_guard {
public:
  lock_guard(recursive_mutex &mtx) : mtx_(mtx) {mtx_.lock();}
  ~lock_guard() {mtx_.unlock();}

private:
  recursive_mutex &mtx_;
};

#undef p
#undef MIN
#undef MAX
#undef min
#undef max
#undef DrawText
#undef itoa