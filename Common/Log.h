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

#pragma once

#include "CommonFuncs.h"

#define	NOTICE_LEVEL  1  // VERY important information that is NOT errors. Like startup and debugprintfs from the game itself.
#define	ERROR_LEVEL   2  // Important errors.
#define	WARNING_LEVEL 3  // Something is suspicious.
#define	INFO_LEVEL    4  // General information.
#define	DEBUG_LEVEL   5  // Detailed debugging - might make things slow.
#define	VERBOSE_LEVEL 6  // Noisy debugging - sometimes needed but usually unimportant.

// NOTE: Needs to be kept in sync with the g_logTypeNames array.
enum class Log {
	System = 0,  // Catch-all for uncategorized things
	Boot,
	Common,
	CPU,
	FileSystem,
	G3D,
	HLE,  // dumping ground that we should get rid of
	JIT,
	Loader,
	ME,
	MemMap,
	SasMix,
	SaveState,
	FrameBuf,
	Audio,
	IO,
	Achievements,
	HTTP,
	Printf,

	sceAudio,
	sceCtrl,
	sceDisplay,
	sceFont,
	sceGe,
	sceIntc,
	sceIo,
	sceKernel,
	sceModule,
	sceNet,
	sceRtc,
	sceSas,
	sceUtility,
	sceMisc,

	NUMBER_OF_LOGS,  // Must be last
};

enum class LogLevel : int {
	LNOTICE = NOTICE_LEVEL,
	LERROR = ERROR_LEVEL,
	LWARNING = WARNING_LEVEL,
	LINFO = INFO_LEVEL,
	LDEBUG = DEBUG_LEVEL,
	LVERBOSE = VERBOSE_LEVEL,
};

void GenericLog(LogLevel level, Log type, const char *file, int line, const char *fmt, ...)
#ifdef __GNUC__
		__attribute__((format(printf, 5, 6)))
#endif
		;
bool GenericLogEnabled(LogLevel level, Log type);

// Exception for Windows - enable more log levels in release mode than on other platforms.
#if defined(_DEBUG) || defined(_WIN32)

// Needs to be an int (and not use the enum) because it's used by the preprocessor!
#define MAX_LOGLEVEL DEBUG_LEVEL

#else

#ifndef MAX_LOGLEVEL
#define MAX_LOGLEVEL INFO_LEVEL
#endif // loglevel

#endif // logging

// Let the compiler optimize this out.
// TODO: Compute a dynamic max level as well that can be checked here.
#define GENERIC_LOG(t, v, ...) { \
	if ((int)v <= MAX_LOGLEVEL) \
		GenericLog(v, t, __FILE__, __LINE__, __VA_ARGS__); \
	}

#define ERROR_LOG(t,...)   do { GENERIC_LOG(t, LogLevel::LERROR,   __VA_ARGS__) } while (false)
#define WARN_LOG(t,...)    do { GENERIC_LOG(t, LogLevel::LWARNING, __VA_ARGS__) } while (false)
#define NOTICE_LOG(t,...)  do { GENERIC_LOG(t, LogLevel::LNOTICE,  __VA_ARGS__) } while (false)
#define INFO_LOG(t,...)    do { GENERIC_LOG(t, LogLevel::LINFO,    __VA_ARGS__) } while (false)
#define DEBUG_LOG(t,...)   do { GENERIC_LOG(t, LogLevel::LDEBUG,   __VA_ARGS__) } while (false)
#define VERBOSE_LOG(t,...) do { GENERIC_LOG(t, LogLevel::LVERBOSE, __VA_ARGS__) } while (false)

// Currently only actually shows a dialog box on Windows.
bool HandleAssert(const char *function, const char *file, int line, const char *expression, const char* format, ...)
#ifdef __GNUC__
__attribute__((format(printf, 5, 6)))
#endif
;

bool HitAnyAsserts();
void ResetHitAnyAsserts();
void SetExtraAssertInfo(const char *info);
void SetCleanExitOnAssert();

#if defined(__ANDROID__)
// Tricky macro to get the basename, that also works if *built* on Win32.
// Doesn't mean this macro can be used on Win32 though.
#define __FILENAME__ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : (__builtin_strrchr(__FILE__, '\\') ? __builtin_strrchr(__FILE__, '\\') + 1 : __FILE__))
#else
#define __FILENAME__ __FILE__
#endif

// If we're a debug build, _dbg_assert_ is active. Not otherwise, even on Windows.
#if defined(_DEBUG)

#define _dbg_assert_(_a_) \
	if (!(_a_)) {\
		if (!HandleAssert(__FUNCTION__, __FILENAME__, __LINE__, #_a_, "Assert!\n")) Crash(); \
	}

#define _dbg_assert_msg_(_a_, ...) \
	if (!(_a_)) { \
		if (!HandleAssert(__FUNCTION__, __FILENAME__, __LINE__, #_a_, __VA_ARGS__)) Crash(); \
	}

#else // not debug

#ifndef _dbg_assert_
#define _dbg_assert_(_a_) {}
#define _dbg_assert_msg_(_a_, _desc_, ...) {}
#endif // dbg_assert

#endif // MAX_LOGLEVEL DEBUG

#define _assert_(_a_) \
	if (!(_a_)) {\
		if (!HandleAssert(__FUNCTION__, __FILENAME__, __LINE__, #_a_, "Assert!\n")) Crash(); \
	}

#define _assert_msg_(_a_, ...) \
	if (!(_a_)) { \
		if (!HandleAssert(__FUNCTION__, __FILENAME__, __LINE__, #_a_, __VA_ARGS__)) Crash(); \
	}

// Just INFO_LOGs on nonWindows. On Windows it outputs to the VS output console.
void OutputDebugStringUTF8(const char *p);
