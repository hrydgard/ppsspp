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

#include "base/compat.h"
#include "CommonTypes.h"

#if defined(IOS) || defined(MIPS)
#include <signal.h>
#endif

template <bool> struct CompileTimeAssert;
template<> struct CompileTimeAssert<true> {};

#ifndef _WIN32

#include <unistd.h>
#include <errno.h>

#if defined(_M_IX86) || defined(_M_X86)
	#define Crash() {asm ("int $3");}
#else
  #define Crash() {kill(getpid(), SIGINT);}
#endif

#define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))

inline u32 __rotl(u32 x, int shift) {
    shift &= 31;
    if (!shift) return x;
    return (x << shift) | (x >> (32 - shift));
}

inline u64 __rotl64(u64 x, unsigned int shift){
	unsigned int n = shift % 64;
	return (x << n) | (x >> (64 - n));
}

inline u32 __rotr(u32 x, int shift) {
    shift &= 31;
    if (!shift) return x;
    return (x >> shift) | (x << (32 - shift));
}

inline u64 __rotr64(u64 x, unsigned int shift){
	unsigned int n = shift % 64;
	return (x >> n) | (x << (64 - n));
}

#else // WIN32

// Function Cross-Compatibility
	#define strcasecmp _stricmp
	#define strncasecmp _strnicmp
	#define unlink _unlink
	#define __rotl _rotl
	#define __rotl64 _rotl64
	#define __rotr _rotr
	#define __rotr64 _rotr64

// 64 bit offsets for windows
	#define fseeko _fseeki64
	#define ftello _ftelli64
	#define atoll _atoi64
	#define stat64 _stat64
	#define fstat64 _fstat64
	#define fileno _fileno
#ifndef _XBOX
	#if _M_IX86
		#define Crash() {__asm int 3}
	#else
extern "C" {
	__declspec(dllimport) void __stdcall DebugBreak(void);
}
		#define Crash() {DebugBreak();}
	#endif // M_IX86
#else
	#define Crash() {DebugBreak();}
#endif // _XBOX ndef
#endif // WIN32 ndef

// Generic function to get last error message.
// Call directly after the command or use the error num.
// This function might change the error code.
// Defined in Misc.cpp.
const char *GetLastErrorMsg();
const char *GetStringErrorMsg(int errCode);

