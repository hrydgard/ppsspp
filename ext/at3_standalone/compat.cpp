#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ext/at3_standalone/compat.h"

void av_log(int level, const char *fmt, ...) {}

int av_get_cpu_flags(void) {
	return 0;
}

size_t av_strlcpy(char *dst, const char *src, size_t size)
{
	size_t len = 0;
	while (++len < size && *src)
		*dst++ = *src++;
	if (len <= size)
		*dst = 0;
	return len + strlen(src) - 1;
}
