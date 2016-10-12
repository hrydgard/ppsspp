#ifdef _WIN32
#include <windows.h>
#define TLS_SUPPORTED
#elif defined(__ANDROID__)
#define TLS_SUPPORTED
#endif

#include "base/basictypes.h"
#include "base/logging.h"
#include "thread/threadutil.h"

#ifdef __ANDROID__
#include <pthread.h>
#endif

#ifdef TLS_SUPPORTED
static __THREAD const char *curThreadName;
#endif

void setCurrentThreadName(const char* threadName) {
#ifdef _WIN32
	// Set the debugger-visible threadname through an unholy magic hack
	static const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push,8)
	struct THREADNAME_INFO {
		DWORD dwType; // must be 0x1000
		LPCSTR szName; // pointer to name (in user addr space)
		DWORD dwThreadID; // thread ID (-1=caller thread)
		DWORD dwFlags; // reserved for future use, must be zero
	} info;
#pragma pack(pop)

	info.dwType = 0x1000;
	info.szName = threadName;
	info.dwThreadID = -1; //dwThreadID;
	info.dwFlags = 0;

	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except(EXCEPTION_CONTINUE_EXECUTION)
	{}
#else

#if defined(__ANDROID__)
	pthread_setname_np(pthread_self(), threadName);
// #else
//	pthread_setname_np(thread_name);
#endif

	// Do nothing
#endif
	// Set the locally known threadname using a thread local variable.
#ifdef TLS_SUPPORTED
	curThreadName = threadName;
#endif
}

void AssertCurrentThreadName(const char *threadName) {
#ifdef TLS_SUPPORTED
	if (strcmp(curThreadName, threadName) != 0) {
		ELOG("Thread name assert failed: Expected %s, was %s", threadName, curThreadName);
	}
#endif
}
