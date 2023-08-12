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

#include "ppsspp_config.h"

#include "CommonTypes.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

#if !defined(_WIN32)

#include <unistd.h>
#include <errno.h>

#if (PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)) && !defined(__EMSCRIPTEN__)
#define Crash() {asm ("int $3");}
#elif PPSSPP_PLATFORM(SWITCH)
// TODO: Implement Crash() for Switch, lets not use breakpoint for the time being
#define Crash() {*((volatile u32 *)0x0) = 0xDEADC0DE;}
#elif PPSSPP_ARCH(ARM)
#define Crash() {asm ("bkpt #0");}
#elif PPSSPP_ARCH(ARM64)
#define Crash() {asm ("brk #0");}
#elif PPSSPP_ARCH(RISCV64)
#define Crash() {asm ("ebreak");}
#else
#include <signal.h>
#define Crash() {kill(getpid(), SIGINT);}
#endif

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
#ifndef __MINGW32__
	#define strcasecmp _stricmp
	#define strncasecmp _strnicmp
#endif

#define unlink _unlink
#define __rotl _rotl
#define __rotl64 _rotl64
#define __rotr _rotr
#define __rotr64 _rotr64

// 64 bit offsets for windows
#ifndef __MINGW32__
	#define fseeko _fseeki64
	#define ftello _ftelli64
	#define atoll _atoi64
#endif
	#define Crash() {__debugbreak();}
#endif // WIN32 ndef
