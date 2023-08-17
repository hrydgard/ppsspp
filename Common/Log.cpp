// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <string>
#include <mutex>

#include "ppsspp_config.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "StringUtils.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Thread/ThreadUtil.h"

#if PPSSPP_PLATFORM(ANDROID)
#include <android/log.h>
#elif PPSSPP_PLATFORM(WINDOWS)
#include "CommonWindows.h"
#endif

#define LOG_BUF_SIZE 2048

static bool hitAnyAsserts = false;

std::mutex g_extraAssertInfoMutex;
std::string g_extraAssertInfo = "menu";

void SetExtraAssertInfo(const char *info) {
	std::lock_guard<std::mutex> guard(g_extraAssertInfoMutex);
	g_extraAssertInfo = info ? info : "menu";
}

bool HandleAssert(const char *function, const char *file, int line, const char *expression, const char* format, ...) {
	// Read message and write it to the log
	char text[LOG_BUF_SIZE];
	const char *caption = "Critical";
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);

	// Secondary formatting. Wonder if this can be combined into the vsnprintf somehow.
	char formatted[LOG_BUF_SIZE + 128];
	{
		std::lock_guard<std::mutex> guard(g_extraAssertInfoMutex);
		snprintf(formatted, sizeof(formatted), "(%s:%s:%d): [%s] (%s) %s", file, function, line, expression, g_extraAssertInfo.c_str(), text);
	}

	// Normal logging (will also log to Android log)
	ERROR_LOG(SYSTEM, "%s", formatted);
	// Also do a simple printf for good measure, in case logging of SYSTEM is disabled (should we disallow that?)
	fprintf(stderr, "%s\n", formatted);

	hitAnyAsserts = true;

#if defined(USING_WIN_UI)
	// Avoid hanging on CI.
	if (!getenv("CI")) {
		int msgBoxStyle = MB_ICONINFORMATION | MB_YESNO;
		std::wstring wtext = ConvertUTF8ToWString(formatted) + L"\n\nTry to continue?";
		std::wstring wcaption = ConvertUTF8ToWString(std::string(caption) + " " + GetCurrentThreadName());
		OutputDebugString(wtext.c_str());
		if (IDYES != MessageBox(0, wtext.c_str(), wcaption.c_str(), msgBoxStyle)) {
			return false;
		} else {
			return true;
		}
	}
	return false;
#elif PPSSPP_PLATFORM(ANDROID)
	__android_log_assert(expression, "PPSSPP", "%s", formatted);
	// Doesn't matter what we return here.
	return false;
#else
	OutputDebugStringUTF8(text);
	return false;
#endif
}

// These are mainly used for unit testing.
bool HitAnyAsserts() {
	return hitAnyAsserts;
}
void ResetHitAnyAsserts() {
	hitAnyAsserts = false;
}
