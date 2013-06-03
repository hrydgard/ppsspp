// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <utility>

#include "Log.h"

#include "CommonTypes.h"

#define IS_LITTLE_ENDIAN (*(const u16 *)"\0\xff" >= 0x100)
#define IS_BIG_ENDIAN (*(const u16 *)"\0\xff" < 0x100)

#ifndef _WIN32

inline u32 _byteswap_ulong(u32 data)
{
  return ((data << 24)) | ((data >> 24)) | ((data >> 8) & 0x0000FF00) | ((data << 8) & 0x00FF0000);
}

#endif

inline u8 Convert4To8(u8 v)
{
	// Swizzle bits: 00012345 -> 12345123
	return (v << 4) | (v);
}

inline u8 Convert5To8(u8 v)
{
	// Swizzle bits: 00012345 -> 12345123
	return (v << 3) | (v >> 2);
}

inline u8 Convert6To8(u8 v)
{
	// Swizzle bits: 00123456 -> 12345612
	return (v << 2) | (v >> 4);
}

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(t) \
 private: \
 t(const t &other);  \
 void operator =(const t &other);
#endif
