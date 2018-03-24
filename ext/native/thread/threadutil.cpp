#ifdef _WIN32
#include <windows.h>
#ifdef __MINGW32__
#include <excpt.h>
#endif
#define TLS_SUPPORTED
#elif defined(__ANDROID__)
#define TLS_SUPPORTED
#endif

#include "base/basictypes.h"
#include "base/logging.h"
#include "thread/threadutil.h"

#if defined(__ANDROID__) || defined(__APPLE__) || (defined(__GLIBC__) && defined(_GNU_SOURCE))
#include <pthread.h>
#endif

#ifdef TLS_SUPPORTED
static __THREAD const char *curThreadName;
#endif

#ifdef __MINGW32__
#include <pshpack8.h>
typedef struct {
	DWORD dwType;
	LPCSTR szName;
	DWORD dwThreadID;
	DWORD dwFlags;
} THREADNAME_INFO;
#include <poppack.h>

static EXCEPTION_DISPOSITION NTAPI ignore_handler(EXCEPTION_RECORD *rec,
                                                  void *frame, CONTEXT *ctx,
                                                  void *disp)
{
	return ExceptionContinueExecution;
}
#endif

void setCurrentThreadName(const char* threadName) {
#ifdef _WIN32
	// Set the debugger-visible threadname through an unholy magic hack
	static const DWORD MS_VC_EXCEPTION = 0x406D1388;
#endif

#if defined(_WIN32) && defined(__MINGW32__)
	// Thread information for VS compatible debugger. -1 sets current thread.
	THREADNAME_INFO ti;
	ti.dwType = 0x1000;
	ti.szName = threadName;
	ti.dwThreadID = -1;

	// Push an exception handler to ignore all following exceptions
	NT_TIB *tib = ((NT_TIB*)NtCurrentTeb());
	EXCEPTION_REGISTRATION_RECORD rec;
	rec.Next = tib->ExceptionList;
	rec.Handler = ignore_handler;
	tib->ExceptionList = &rec;

	// Visual Studio and compatible debuggers receive thread names from the
	// program through a specially crafted exception
	RaiseException(MS_VC_EXCEPTION, 0, sizeof(ti) / sizeof(ULONG_PTR),
	               (ULONG_PTR*)&ti);

	// Pop exception handler
	tib->ExceptionList = tib->ExceptionList->Next;
#elif defined(_WIN32)
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

#ifdef __MINGW32__
	__try1 (ehandler)
#else
	__try
#endif
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
#ifdef __MINGW32__
	__except1
#else
	__except(EXCEPTION_CONTINUE_EXECUTION)
#endif
	{}
#else

#if defined(__ANDROID__) || (defined(__GLIBC__) && defined(_GNU_SOURCE))
	pthread_setname_np(pthread_self(), threadName);
#elif defined(__APPLE__)
	pthread_setname_np(threadName);
// #else
//	pthread_setname_np(threadName);
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
