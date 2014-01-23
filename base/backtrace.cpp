#include "base/backtrace.h"

// The mac check doesn't seem to work right.
#if (!defined(ANDROID) && defined(__linux__)) || (defined(__APPLE__) && (defined(_M_IX86) || defined(_M_X64)))

#include <execinfo.h>
#include <unistd.h>

static void *backtrace_buffer[128];

void PrintBacktraceToStderr() {
	int num_addrs = backtrace(backtrace_buffer, 128);
	backtrace_symbols_fd(backtrace_buffer, num_addrs, STDERR_FILENO);
}

#else

#include <stdio.h>

void PrintBacktraceToStderr() {
	fprintf(stderr, "No backtrace available to print on this platform\n");
}

#endif
