#pragma once

#include <mutex>

// Note that name must be a global string that lives until the end of the process,
// for AssertCurrentThreadName to work.
void SetCurrentThreadName(const char *threadName);
void AssertCurrentThreadName(const char *threadName);

// Just gets a cheap thread identifier so that you can see different threads in debug output,
// exactly what it is is badly specified and not useful for anything.
int GetCurrentThreadIdForDebug();
