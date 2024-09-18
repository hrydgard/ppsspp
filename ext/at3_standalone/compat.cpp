#include <cstdarg>
#include <cstdio>

#include "compat.h"
#include "Common/Log.h"

void av_log(int level, const char *fmt, ...) {
	char buffer[512];
	va_list vl;
	va_start(vl, fmt);
	size_t retval = vsnprintf(buffer, sizeof(buffer), fmt, vl);
	va_end(vl);

	// lazy
	switch (level) {
	case AV_LOG_DEBUG:
	case AV_LOG_TRACE:
	case AV_LOG_VERBOSE: DEBUG_LOG(Log::ME, "Atrac3/3+: %s", buffer); break;
	case AV_LOG_ERROR: ERROR_LOG(Log::ME, "Atrac3/3+: %s", buffer); break;
	case AV_LOG_INFO: INFO_LOG(Log::ME, "Atrac3/3+: %s", buffer); break;
	case AV_LOG_WARNING:
	default:
		WARN_LOG(Log::ME, "Atrac3/3+: %s", buffer); break;
	}
}
