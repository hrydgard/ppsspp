#ifndef __LOG_WINDOWS_H
#define __LOG_WINDOWS_H

#include <stdio.h>

#define LOG(...) { \
	fprintf(stderr, __VA_ARGS__); \
	fflush(stderr); \
}

#endif
