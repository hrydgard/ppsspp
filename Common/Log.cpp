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
#include <retro_atomic.h>

#include "ppsspp_config.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"
#include "Common/System/System.h"

#if PPSSPP_PLATFORM(ANDROID)
#include <android/log.h>
#elif PPSSPP_PLATFORM(WINDOWS)
#include "CommonWindows.h"
static HWND g_dialogParent;
#endif

#define LOG_BUF_SIZE 2048

static bool hitAnyAsserts = false;

// Rotating slots: the writer fills one, then publishes its index. A reader racing a
// writer several updates ahead may see a torn string; it's only assert decoration.
struct AssertInfoSlot {
	char text[64];
	double time;
};
static AssertInfoSlot g_extraAssertInfo[4] = { { "menu", 0.0 } };
static retro_atomic_int_t g_extraAssertInfoIndex;
static retro_atomic_int_t g_extraAssertInfoNext;
static bool g_exitOnAssert;
static AssertNoCallbackFunc g_assertCancelCallback = 0;
static void *g_assertCancelCallbackUserData = 0;

u8 g_debugCounters[8];

void SetDebugValue(DebugCounter counter, int value) {
	if (value > 15)
		value = 15;
	if (value < 0)
		value = 0;
	g_debugCounters[(int)counter] = value;
}

void IncrementDebugCounter(DebugCounter counter) {
	int value = g_debugCounters[(int)counter] + 1;
	if (value > 15)
		value = 15;
	if (value < 0)
		value = 0;
	g_debugCounters[(int)counter] = value;
}

void SetAssertDialogParent(void *handle) {
#if PPSSPP_PLATFORM(WINDOWS)
	// I thought this would be nice, but for some reason the dialog becomes invisible.
	// g_dialogParent = (HWND)handle;
#endif
}

void SetExtraAssertInfo(const char *info) {
	const int idx = retro_atomic_fetch_add_int(&g_extraAssertInfoNext, 1) & 3;
	AssertInfoSlot &slot = g_extraAssertInfo[idx];
	truncate_cpy(slot.text, sizeof(slot.text), info ? info : "menu");
	slot.time = time_now_d();
	retro_atomic_store_release_int(&g_extraAssertInfoIndex, idx);
}

void SetAssertCancelCallback(AssertNoCallbackFunc callback, void *userdata) {
	g_assertCancelCallback = callback;
	g_assertCancelCallbackUserData = userdata;
}

void SetCleanExitOnAssert() {
	g_exitOnAssert = true;
}

void BreakIntoPSPDebugger(const char *reason) {
	if (g_assertCancelCallback) {
		g_assertCancelCallback(reason, g_assertCancelCallbackUserData);
	}
}

bool HandleAssert(bool isDebugAssert, const char *function, const char *file, int line, const char *expression, const char* format, ...) {
	// Read message and write it to the log
	char text[LOG_BUF_SIZE];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);

	// Secondary formatting. Wonder if this can be combined into the vsnprintf somehow.
	char formatted[LOG_BUF_SIZE + 128];
	{
		const AssertInfoSlot &slot = g_extraAssertInfo[retro_atomic_load_acquire_int(&g_extraAssertInfoIndex) & 3];
		char info[sizeof(slot.text)];
		memcpy(info, slot.text, sizeof(info));
		info[sizeof(info) - 1] = '\0';
		double delta = time_now_d() - slot.time;
		u32 debugCounters = 0;
		for (int i = 0; i < 8; i++) {
			debugCounters |= g_debugCounters[7 - i] << (i * 4);
		}
		snprintf(formatted, sizeof(formatted), "(%s:%s:%d:%08x): [%s] (%s, %0.1fs) %s", file, function, line, debugCounters, expression, info, delta, text);
	}

	// Normal logging (will also log to Android log)
	ERROR_LOG(Log::System, "%s", formatted);
	// Also do a simple printf for good measure, in case logging of System is disabled (should we disallow that?)
	fprintf(stderr, "%s\n", formatted);

	hitAnyAsserts = true;

	// Avoid hanging on CI or on headless builds in general.
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	if (!getenv("CI")) {
		const int msgBoxStyle = MB_ICONINFORMATION | MB_YESNOCANCEL;
		std::string text = formatted;
		text += "\n\nTry to continue?";
		if (IsDebuggerPresent()) {
			text += "\n\nNo: break directly into the native debugger";
			text += "\n\nCancel: skip and break into PPSSPP debugger";
		} else {
			text += "\n\nNo: exit";
			text += "\n\nCancel: skip and break into PPSSPP debugger";
		}
		const char *threadName = GetCurrentThreadName();
		OutputDebugStringA(formatted);
		printf("%s\n", formatted);
		static const std::string caption = isDebugAssert ? "Debug Assert" : "Critical Assert";
		std::wstring wcaption = ConvertUTF8ToWString(caption + " " + (threadName ? threadName : "(unknown thread)"));
		switch (MessageBox(g_dialogParent, ConvertUTF8ToWString(text).c_str(), wcaption.c_str(), msgBoxStyle)) {
		case IDYES:
			return true;
		case IDNO:
			if (g_exitOnAssert || !IsDebuggerPresent()) {
				// Hard exit.
				ExitProcess(1);
			}
			return false;  // Break into the native debugger.
		case IDCANCEL:
			// Via the helper, which null-checks. EmuScreen clears the callback when a game is
			// unloaded, so an assert after returning to the menu was jumping through null - from
			// the one button whose whole purpose is surviving the assert.
			BreakIntoPSPDebugger(formatted);
			return true;  // don't crash!
		}
	}
	return false;
#elif PPSSPP_PLATFORM(ANDROID)
	__android_log_assert(expression, "PPSSPP", "%s", formatted);
	// Doesn't matter what we return here.
	return false;
#else
	INFO_LOG(Log::System, "%s", formatted);
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
