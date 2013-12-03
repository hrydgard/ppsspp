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

#ifndef _COMMON_H_
#define _COMMON_H_

// DO NOT EVER INCLUDE <windows.h> directly _or indirectly_ from this file
// since it slows down the build a lot.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef _MSC_VER
#pragma warning (disable:4100)
#endif

#ifdef __arm__
#if !defined(ARM)
#define ARM
#endif
#endif

#if defined(ARM)
#define _M_ARM32
#endif

// Force enable logging in the right modes. For some reason, something had changed
// so that debugfast no longer logged.
#if defined(_DEBUG) || defined(DEBUGFAST)
#undef LOGGING
#define LOGGING 1
#endif

#define STACKALIGN

// An inheritable class to disallow the copy constructor and operator= functions
class NonCopyable
{
protected:
	NonCopyable() {}
private:
	NonCopyable(const NonCopyable&);
	void operator=(const NonCopyable&);
};

#include "Log.h"
#include "CommonTypes.h"
#include "MsgHandler.h"
#include "CommonFuncs.h"

#ifdef __APPLE__
// The Darwin ABI requires that stack frames be aligned to 16-byte boundaries.
// This is only needed on i386 gcc - x86_64 already aligns to 16 bytes.
#if defined __i386__ && defined __GNUC__
#undef STACKALIGN
#define STACKALIGN __attribute__((__force_align_arg_pointer__))
#endif

#elif defined _WIN32

// Check MSC ver
	#if !defined _MSC_VER || _MSC_VER <= 1000
		#error needs at least version 1000 of MSC
	#endif

// Memory leak checks
	#define CHECK_HEAP_INTEGRITY()

// Alignment
	#define MEMORY_ALIGNED16(x) __declspec(align(16)) x
	#define GC_ALIGNED32(x) __declspec(align(32)) x
	#define GC_ALIGNED64(x) __declspec(align(64)) x
	#define GC_ALIGNED128(x) __declspec(align(128)) x
	#define GC_ALIGNED16_DECL(x) __declspec(align(16)) x
	#define GC_ALIGNED64_DECL(x) __declspec(align(64)) x

// Debug definitions
	#if defined(_DEBUG)
		#include <crtdbg.h>
		#undef CHECK_HEAP_INTEGRITY
		#define CHECK_HEAP_INTEGRITY() {if (!_CrtCheckMemory()) PanicAlert("memory corruption detected. see log.");}
		// If you want to see how much a pain in the ass singletons are, for example:
		// {614} normal block at 0x030C5310, 188 bytes long.
		// Data: <Master Log      > 4D 61 73 74 65 72 20 4C 6F 67 00 00 00 00 00 00
		struct CrtDebugBreak { CrtDebugBreak(int spot) { _CrtSetBreakAlloc(spot); } };
		//CrtDebugBreak breakAt(614);
	#endif // end DEBUG/FAST

#endif

// Windows compatibility
#ifndef _WIN32
#include <limits.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#ifdef _LP64
#define _M_X64 1
#else
#ifndef _M_ARM32
#define _M_IX86 1
#endif
#endif

#define __forceinline inline __attribute__((always_inline))
#define MEMORY_ALIGNED16(x) __attribute__((aligned(16))) x
#define GC_ALIGNED32(x) __attribute__((aligned(32))) x
#define GC_ALIGNED64(x) __attribute__((aligned(64))) x
#define GC_ALIGNED128(x) __attribute__((aligned(128))) x
#define GC_ALIGNED16_DECL(x) __attribute__((aligned(16))) x
#define GC_ALIGNED64_DECL(x) __attribute__((aligned(64))) x
#endif

#ifdef _MSC_VER
#define __strdup _strdup
#define __getcwd _getcwd
#define __chdir _chdir
#else
#define __strdup strdup
#define __getcwd getcwd
#define __chdir chdir
#endif

#if defined __GNUC__
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
#elif ((_MSC_VER >= 1500) || __INTEL_COMPILER) && !defined(_XBOX) // Visual Studio 2008
# define _M_SSE 0x402
#endif


#ifdef _MSC_VER
inline unsigned long long bswap64(unsigned long long x) { return _byteswap_uint64(x); }
inline unsigned int bswap32(unsigned int x) { return _byteswap_ulong(x); }
inline unsigned int bswap16(unsigned int x) { return _byteswap_ushort(x); }
#else
// TODO: speedup
inline unsigned short bswap16(unsigned short x) { return (x << 8) | (x >> 8); }
inline unsigned int bswap32(unsigned int x) { return (x >> 24) | ((x & 0xFF0000) >> 8) | ((x & 0xFF00) << 8) | (x << 24);}
inline unsigned long long bswap64(unsigned long long x) {return ((unsigned long long)bswap32(x) << 32) | bswap32(x >> 32); }
#endif

inline float bswapf( float f )
{
  union
  {
    float f;
    unsigned int u32;
  } dat1, dat2;

  dat1.f = f;
  dat2.u32 = bswap32(dat1.u32);

  return dat2.f;
}

inline double bswapd( double f )
{
  union
  {
    double f;
    unsigned long long u64;
  } dat1, dat2;

  dat1.f = f;
  dat2.u64 = bswap64(dat1.u64);

  return dat2.f;
}

#include "Swap.h"

#endif // _COMMON_H_
