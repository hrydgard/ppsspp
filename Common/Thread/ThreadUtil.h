#pragma once

// Note that name must be a global string that lives until the end of the process,
// for AssertCurrentThreadName to work.
void SetCurrentThreadName(const char *threadName);
void AssertCurrentThreadName(const char *threadName);
