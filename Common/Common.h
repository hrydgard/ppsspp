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

// DO NOT EVER INCLUDE <windows.h> directly _or indirectly_ from this file
// since it slows down the build a lot.

#include <stdarg.h>

#ifdef _MSC_VER
#pragma warning (disable:4100)
#endif

// Force enable logging in the right modes. For some reason, something had changed
// so that debugfast no longer logged.
#if defined(_DEBUG) || defined(DEBUGFAST)
#undef LOGGING
#define LOGGING 1
#endif

#define STACKALIGN

#include "Log.h"
#include "CommonTypes.h"
#include "CommonFuncs.h"

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(t) \
	t(const t &other) = delete;  \
	void operator =(const t &other) = delete;
#endif

#ifdef __APPLE__
// The Darwin ABI requires that stack frames be aligned to 16-byte boundaries.
// This is only needed on i386 gcc - x86_64 already aligns to 16 bytes.
#if defined __i386__ && defined __GNUC__
#undef STACKALIGN
#define STACKALIGN __attribute__((__force_align_arg_pointer__))
#endif

#define CHECK_HEAP_INTEGRITY()

#elif defined(_WIN32)

// Memory leak checks
	#define CHECK_HEAP_INTEGRITY()

	// Debug definitions
	#if defined(_DEBUG)
		#include <crtdbg.h>
		#undef CHECK_HEAP_INTEGRITY
		#define CHECK_HEAP_INTEGRITY() {if (!_CrtCheckMemory()) PanicAlert("memory corruption detected. see log.");}
	#endif
#else

#define CHECK_HEAP_INTEGRITY()

#endif

// Windows compatibility
#ifndef _WIN32
#include <limits.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif

#define __forceinline inline __attribute__((always_inline))
#endif

#if !defined(__GNUC__) && (defined(_M_X64) || defined(_M_IX86))
# define _M_SSE 0x402
#else
# if defined __SSE4_2__
#  define _M_SSE 0x402
# elif defined __SSE4_1__
#  define _M_SSE 0x401
# elif defined __SSSE3__
#  define _M_SSE 0x301
# elif defined __SSE3__
#  define _M_SSE 0x300
# elif defined __SSE2__
#  define _M_SSE 0x200
# endif
#endif