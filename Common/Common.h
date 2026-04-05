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
#pragma warning(disable:4100)
#pragma warning(disable:4244)
#endif

#include "CommonTypes.h"
#include "CommonFuncs.h"

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(t) \
	t(const t &other) = delete;  \
	void operator =(const t &other) = delete;
#endif

#ifndef ENUM_CLASS_BITOPS
#define ENUM_CLASS_BITOPS(T) \
	static inline constexpr T operator |(const T &lhs, const T &rhs) { \
		return T((int)lhs | (int)rhs); \
	} \
	static inline T &operator |= (T &lhs, const T &rhs) { \
		lhs = lhs | rhs; \
		return lhs; \
	} \
	static inline constexpr bool operator &(const T &lhs, const T &rhs) { \
		return ((int)lhs & (int)rhs) != 0; \
	} \
	static inline T &operator &= (T &lhs, const T &rhs) { \
		lhs = (T)((int)lhs & (int)rhs); \
		return lhs; \
	} \
	static inline constexpr T operator ~(const T &rhs) { \
		return (T)(~((int)rhs)); \
	}
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

#if defined(_WIN32)

// Memory leak checks
	#define CHECK_HEAP_INTEGRITY()

	// Debug definitions
	#if defined(_DEBUG)
		#include <crtdbg.h>
		#undef CHECK_HEAP_INTEGRITY
		#define CHECK_HEAP_INTEGRITY() {if (!_CrtCheckMemory()) _assert_msg_(false, "Memory corruption detected. See log.");}
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

// Easy way to printf string_views (note: The formatting specifier is "%.*s", not "%s")
#define STR_VIEW(sv) (int)(sv).size(), (sv).data()

// Restrict qualifier
#if defined(_MSC_VER)
#define RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#else
#define RESTRICT
#endif
