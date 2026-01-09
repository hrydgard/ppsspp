#ifndef __LOG_LINUX_H
#define __LOG_LINUX_H

#include <stdio.h>

#define LOG(...) { \
	fprintf(stderr, __VA_ARGS__); \
	fflush(stderr); \
}

#endif
