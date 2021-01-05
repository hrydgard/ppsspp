#pragma once

// Note that name must be a global string that lives until the end of the process,
// for assertThreadName to work.
void setCurrentThreadName(const char *threadName);
void AssertCurrentThreadName(const char *threadName);