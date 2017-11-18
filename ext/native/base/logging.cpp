#include "base/logging.h"

const char *GetFn(const char *fn) {
	const char *p = strrchr(fn, '\\');
	if (p)
		return p + 1;
	else
		return fn;
}
