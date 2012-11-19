#include "base/backtrace.h"

#ifdef __linux__

// LINUX ONLY

#include <execinfo.h>
#include <unistd.h>

static void *backtrace_buffer[128];

void PrintBacktraceToStderr() {
	int num_addrs = backtrace(backtrace_buffer, 128);
	backtrace_symbols_fd(backtrace_buffer, num_addrs, STDERR_FILENO);
}

#endif
