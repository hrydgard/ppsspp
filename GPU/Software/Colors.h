// Copyright (c) 2013- PPSSPP Project.

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

#include "CommonTypes.h"

static inline u32 DecodeRGBA4444(u16 src)
{
	u8 r = (src>>12) & 0x0F;
	u8 g = (src>>8) & 0x0F;
	u8 b = (src>>4) & 0x0F;
	u8 a = (src>>0) & 0x0F;
	r = (r << 4) | r;
	g = (g << 4) | g;
	b = (b << 4) | b;
	a = (a << 4) | a;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static inline u32 DecodeRGBA5551(u16 src)
{
	u8 r = src & 0x1F;
	u8 g = (src >> 5) & 0x1F;
	u8 b = (src >> 10) & 0x1F;
	u8 a = (src >> 15) & 0x1;
	r = (r << 3) | (r >> 2);
	g = (g << 3) | (g >> 2);
	b = (b << 3) | (b >> 2);
	a = (a) ? 0xff : 0;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static inline u32 DecodeRGB565(u16 src)
{
	u8 r = src & 0x1F;
	u8 g = (src >> 5) & 0x3F;
	u8 b = (src >> 11) & 0x1F;
	u8 a = 0xFF;
	r = (r << 3) | (r >> 2);
	g = (g << 2) | (g >> 4);
	b = (b << 3) | (b >> 2);
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static inline u32 DecodeRGBA8888(u32 src)
{
	u8 r = src & 0xFF;
	u8 g = (src >> 8) & 0xFF;
	u8 b = (src >> 16) & 0xFF;
	u8 a = (src >> 24) & 0xFF;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static inline u16 RGBA8888To565(u32 value)
{
	u8 r = value & 0xFF;
	u8 g = (value >> 8) & 0xFF;
	u8 b = (value >> 16) & 0xFF;
	r >>= 3;
	g >>= 2;
	b >>= 3;
	return (u16)r | ((u16)g << 5) | ((u16)b << 11);
}

static inline u16 RGBA8888To5551(u32 value)
{
	u8 r = value & 0xFF;
	u8 g = (value >> 8) & 0xFF;
	u8 b = (value >> 16) & 0xFF;
	u8 a = (value >> 24) & 0xFF;
	r >>= 3;
	g >>= 3;
	b >>= 3;
	a >>= 7;
	return (u16)r | ((u16)g << 5) | ((u16)b << 10) | ((u16)a << 15);
}

static inline u16 RGBA8888To4444(u32 value)
{
	u8 r = value & 0xFF;
	u8 g = (value >> 8) & 0xFF;
	u8 b = (value >> 16) & 0xFF;
	u8 a = (value >> 24) & 0xFF;
	r >>= 4;
	g >>= 4;
	b >>= 4;
	a >>= 4;
	return (u16)r | ((u16)g << 4) | ((u16)b << 8) | ((u16)a << 12);
}
