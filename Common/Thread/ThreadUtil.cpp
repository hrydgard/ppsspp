#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS)

#include "Common/CommonWindows.h"

#ifdef __MINGW32__
#include <excpt.h>
#endif

#define TLS_SUPPORTED

#elif defined(__ANDROID__)

#include "android/jni/app-android.h"

#define TLS_SUPPORTED

#endif

// TODO: Many other platforms also support TLS, in fact probably nearly all that we support
// these days.

#include <cstring>
#include <cstdint>

#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"

AttachDetachFunc g_attach;
AttachDetachFunc g_detach;

void AttachThreadToJNI() {
	if (g_attach) {
		g_attach();
	}
}


void DetachThreadFromJNI() {
	if (g_detach) {
		g_detach();
	}
}

void RegisterAttachDetach(AttachDetachFunc attach, AttachDetachFunc detach) {
	g_attach = attach;
	g_detach = detach;
}

#if (PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(LINUX)) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#if !PPSSPP_PLATFORM(WINDOWS)
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pthread_np.h>
#elif defined(__NetBSD__)
#include <lwp.h>
#endif

#ifdef TLS_SUPPORTED
static thread_local const char *curThreadName;
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

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
typedef HRESULT (WINAPI *TSetThreadDescription)(HANDLE, PCWSTR);

static TSetThreadDescription g_pSetThreadDescription = nullptr;
static bool g_failedToFindSetThreadDescription = false;

static void InitializeSetThreadDescription() {
	if (g_pSetThreadDescription != nullptr || g_failedToFindSetThreadDescription) {
		return;
	}

	HMODULE hKernel32 = GetModuleHandle(L"kernelbase.dll");
	if (hKernel32 == nullptr) {
		// Failed to find the function. Windows version too old, most likely.
		g_failedToFindSetThreadDescription = true;
		return;
	}
	g_pSetThreadDescription = reinterpret_cast<TSetThreadDescription>(GetProcAddress(hKernel32, "SetThreadDescription"));
	if (g_pSetThreadDescription == nullptr) {
		g_failedToFindSetThreadDescription = true;
		return;
	}
}

void SetCurrentThreadNameThroughException(const char *threadName);
#endif

const char *GetCurrentThreadName() {
#ifdef TLS_SUPPORTED
	return "N/A"; //curThreadName;
#else
	return "";
#endif
}

void SetCurrentThreadName(const char *threadName) {
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	InitializeSetThreadDescription();
	if (g_pSetThreadDescription) {
		// Use the modern API
		wchar_t buffer[256];
		ConvertUTF8ToWString(buffer, ARRAY_SIZE(buffer), threadName);
		g_pSetThreadDescription(GetCurrentThread(), buffer);
	} else {
		// Use the old exception hack.
		SetCurrentThreadNameThroughException(threadName);
	}
#elif PPSSPP_PLATFORM(WINDOWS)
	wchar_t buffer[256];
	ConvertUTF8ToWString(buffer, ARRAY_SIZE(buffer), threadName);
	SetThreadDescription(GetCurrentThread(), buffer);
#elif PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(LINUX)
	// pthread_setname_np(pthread_self(), threadName);
#elif defined(__APPLE__)
	pthread_setname_np(threadName);
#elif defined(__DragonFly__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	pthread_set_name_np(pthread_self(), threadName);
#elif defined(__NetBSD__)
	pthread_setname_np(pthread_self(), "%s", (void*)threadName);
#endif

	// Set the locally known threadname using a thread local variable.
#ifdef TLS_SUPPORTED
	// curThreadName = threadName;
#endif
}

#if PPSSPP_PLATFORM(WINDOWS)

void SetCurrentThreadNameThroughException(const char *threadName) {
	// Set the debugger-visible threadname through an unholy magic hack
	static const DWORD MS_VC_EXCEPTION = 0x406D1388;

#if defined(__MINGW32__)
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
#else
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
#endif
}
#endif

void AssertCurrentThreadName(const char *threadName) {
#ifdef TLS_SUPPORTED
	if (strcmp(curThreadName, threadName) != 0) {
		ERROR_LOG(Log::System, "Thread name assert failed: Expected %s, was %s", threadName, curThreadName);
	}
#endif
}

int GetCurrentThreadIdForDebug() {
#if __LIBRETRO__
	// Not sure why gettid() would not be available, but it isn't.
	// The return value of this function is only used in unit tests anyway...
	return 1;
#elif PPSSPP_PLATFORM(WINDOWS)
	return (int)GetCurrentThreadId();
#elif PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	uint64_t tid = 0;
	pthread_threadid_np(NULL, &tid);
	return (int)tid;
#elif PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(LINUX)
	// See issue 14545
	return (int)syscall(__NR_gettid);
	// return (int)gettid();
#elif defined(__DragonFly__) || defined(__FreeBSD__)
	return pthread_getthreadid_np();
#elif defined(__NetBSD__)
	return _lwp_self();
#elif defined(__OpenBSD__)
	return getthrid();
#else
	return 1;
#endif
}
