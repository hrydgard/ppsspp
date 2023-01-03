#pragma once

#include <mutex>

// Note that the string pointed to must be have a lifetime until the end of the thread,
// for AssertCurrentThreadName to work.
void SetCurrentThreadName(const char *threadName);
void AssertCurrentThreadName(const char *threadName);

// If TLS is not supported, this will return an empty string.
const char *GetCurrentThreadName();

// Just gets a cheap thread identifier so that you can see different threads in debug output,
// exactly what it is is badly specified and not useful for anything.
int GetCurrentThreadIdForDebug();

// Call when leaving threads. On Android, calls DetachCurrentThread.
// Threads that use scoped storage I/O end up attached as JNI threads, and will thus
// need this in order to follow the rules correctly. Some devices seem to enforce this.
void DetachThreadFromJNI();

class AndroidJNIThreadContext {
public:
	~AndroidJNIThreadContext() {
		DetachThreadFromJNI();
	}
};
