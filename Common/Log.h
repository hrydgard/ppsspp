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

#include <cassert>
#include <cstdio>

#include "CommonFuncs.h"
#include "Common/MsgHandler.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#define	NOTICE_LEVEL  1  // VERY important information that is NOT errors. Like startup and debugprintfs from the game itself.
#define	ERROR_LEVEL   2  // Important errors.
#define	WARNING_LEVEL 3  // Something is suspicious.
#define	INFO_LEVEL    4  // General information.
#define	DEBUG_LEVEL   5  // Detailed debugging - might make things slow.
#define	VERBOSE_LEVEL 6  // Noisy debugging - sometimes needed but usually unimportant.


namespace LogTypes {

enum LOG_TYPE {
	SYSTEM = 0,
	BOOT,
	COMMON,
	CPU,
	FILESYS,
	G3D,
	HLE,  // dumping ground that we should get rid of
	JIT,
	LOADER,
	ME,
	MEMMAP,
	SASMIX,
	SAVESTATE,
	FRAMEBUF,

	SCEAUDIO,
	SCECTRL,
	SCEDISPLAY,
	SCEFONT,
	SCEGE,
	SCEINTC,
	SCEIO,
	SCEKERNEL,
	SCEMODULE,
	SCENET,
	SCERTC,
	SCESAS,
	SCEUTILITY,
	SCEMISC,

	NUMBER_OF_LOGS,  // Must be last
};

enum LOG_LEVELS : int {
	LNOTICE = NOTICE_LEVEL,
	LERROR = ERROR_LEVEL,
	LWARNING = WARNING_LEVEL,
	LINFO = INFO_LEVEL,
	LDEBUG = DEBUG_LEVEL,
	LVERBOSE = VERBOSE_LEVEL,
};

}  // namespace

void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type,
		const char *file, int line, const char *fmt, ...)
#ifdef __GNUC__
		__attribute__((format(printf, 5, 6)))
#endif
		;
bool GenericLogEnabled(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type);

#if defined(LOGGING) || defined(_DEBUG) || defined(DEBUGFAST) || defined(_WIN32)
#define MAX_LOGLEVEL DEBUG_LEVEL
#else
#ifndef MAX_LOGLEVEL
#define MAX_LOGLEVEL INFO_LEVEL
#endif // loglevel
#endif // logging

// Let the compiler optimize this out
#define GENERIC_LOG(t, v, ...) { \
	if (v <= MAX_LOGLEVEL) \
		GenericLog(v, t, __FILE__, __LINE__, __VA_ARGS__); \
	}

#define ERROR_LOG(t,...)   do { GENERIC_LOG(LogTypes::t, LogTypes::LERROR, __VA_ARGS__) } while (false)
#define WARN_LOG(t,...)    do { GENERIC_LOG(LogTypes::t, LogTypes::LWARNING, __VA_ARGS__) } while (false)
#define NOTICE_LOG(t,...)  do { GENERIC_LOG(LogTypes::t, LogTypes::LNOTICE, __VA_ARGS__) } while (false)
#define INFO_LOG(t,...)    do { GENERIC_LOG(LogTypes::t, LogTypes::LINFO, __VA_ARGS__) } while (false)
#define DEBUG_LOG(t,...)   do { GENERIC_LOG(LogTypes::t, LogTypes::LDEBUG, __VA_ARGS__) } while (false)
#define VERBOSE_LOG(t,...) do { GENERIC_LOG(LogTypes::t, LogTypes::LVERBOSE, __VA_ARGS__) } while (false)

#if defined(__ANDROID__)

// Tricky macro to get the basename, that also works if *built* on Win32.
#define __FILENAME__ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : (__builtin_strrchr(__FILE__, '\\') ? __builtin_strrchr(__FILE__, '\\') + 1 : __FILE__))
void AndroidAssertLog(const char *func, const char *file, int line, const char *condition, const char *fmt, ...);

#endif

#if MAX_LOGLEVEL >= DEBUG_LEVEL
#define _dbg_assert_(_t_, _a_) \
	if (!(_a_)) {\
		ERROR_LOG(_t_, "Error...\n\n  Line: %d\n  File: %s\n\nIgnore and continue?", \
					   __LINE__, __FILE__); \
		if (!PanicYesNo("*** Assertion ***\n")) { Crash(); } \
	}

#if defined(__ANDROID__)

#define _dbg_assert_msg_(_t_, _a_, ...)\
	if (!(_a_)) {\
		printf(__VA_ARGS__); \
		ERROR_LOG(_t_, __VA_ARGS__); \
		if (!PanicYesNo(__VA_ARGS__)) AndroidAssertLog(__FUNCTION__, __FILENAME__, __LINE__, #_a_, __VA_ARGS__); \
	}

#else  // !defined(__ANDROID__)

#define _dbg_assert_msg_(_t_, _a_, ...)\
	if (!(_a_)) {\
		printf(__VA_ARGS__); \
		ERROR_LOG(_t_, __VA_ARGS__); \
		if (!PanicYesNo(__VA_ARGS__)) { Crash();} \
	}

#endif  // __ANDROID__

#define _dbg_update_() ; //Host_UpdateLogDisplay();

#else // not debug
#define _dbg_update_() ;

#ifndef _dbg_assert_
#define _dbg_assert_(_t_, _a_) {}
#define _dbg_assert_msg_(_t_, _a_, _desc_, ...) {}
#endif // dbg_assert
#endif // MAX_LOGLEVEL DEBUG

#if defined(__ANDROID__)

#define _assert_(_a_) \
	if (!(_a_)) {\
		AndroidAssertLog(__FUNCTION__, __FILENAME__, __LINE__, #_a_, "Assertion failed!"); \
	}

#define _assert_msg_(_t_, _a_, ...)		\
	if (!(_a_) && !PanicYesNo(__VA_ARGS__)) { \
		AndroidAssertLog(__FUNCTION__, __FILENAME__, __LINE__, #_a_, __VA_ARGS__); \
	}

#else  // __ANDROID__

#define _assert_(_a_) \
	if (!(_a_)) {\
		ERROR_LOG(SYSTEM, "Error...\n\n  Line: %d\n  File: %s\n\nIgnore and continue?", \
					   __LINE__, __FILE__); \
		if (!PanicYesNo("*** Assertion ***\n")) { Crash(); } \
	}

#define _assert_msg_(_t_, _a_, ...)		\
	if (!(_a_) && !PanicYesNo(__VA_ARGS__)) { \
    Crash(); \
	}

#endif  // __ANDROID__