// Copyright (c) 2015- PPSSPP Project.

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

#include "ppsspp_config.h"
#include "Common/CommonTypes.h"

inline u8 Convert4To8(u8 v) {
	// Swizzle bits: 00001234 -> 12341234
	return (v << 4) | (v);
}

inline u8 Convert5To8(u8 v) {
	// Swizzle bits: 00012345 -> 12345123
	return (v << 3) | (v >> 2);
}

inline u8 Convert6To8(u8 v) {
	// Swizzle bits: 00123456 -> 12345612
	return (v << 2) | (v >> 4);
}

inline u16 RGBA8888toRGB565(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 5) & 0x07E0) | ((px >> 8) & 0xF800);
}

inline u16 RGBA8888toRGBA4444(u32 px) {
	return ((px >> 4) & 0x000F) | ((px >> 8) & 0x00F0) | ((px >> 12) & 0x0F00) | ((px >> 16) & 0xF000);
}

inline u16 BGRA8888toRGB565(u32 px) {
	return ((px >> 19) & 0x001F) | ((px >> 5) & 0x07E0) | ((px << 8) & 0xF800);
}

inline u16 BGRA8888toRGBA4444(u32 px) {
	return ((px >> 20) & 0x000F) | ((px >> 8) & 0x00F0) | ((px << 4) & 0x0F00) | ((px >> 16) & 0xF000);
}

inline u16 BGRA8888toRGBA5551(u32 px) {
	return ((px >> 19) & 0x001F) | ((px >> 6) & 0x03E0) | ((px << 7) & 0x7C00) | ((px >> 16) & 0x8000);
}

inline u16 RGBA8888toRGBA5551(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 6) & 0x03E0) | ((px >> 9) & 0x7C00) | ((px >> 16) & 0x8000);
}

inline u32 RGBA4444ToRGBA8888(u16 src) {
	const u32 r = (src & 0x000F) << 0;
	const u32 g = (src & 0x00F0) << 4;
	const u32 b = (src & 0x0F00) << 8;
	const u32 a = (src & 0xF000) << 12;
	const u32 c = r | g | b | a;
	return c | (c << 4);
}

inline u32 RGBA5551ToRGBA8888(u16 src) {
	u32 dark = ((src & 0x1F) << 3) | ((src & 0x3E0) << 6) | ((src & 0x7C00) << 9);
	// Replicate the top 3 upper bits into the missing lower bits.
	u32 full = (dark | ((dark >> 5) & 0x070707));
	if (src >> 15) {
		full |= 0xFF000000;
	}
	return full;
}

inline u32 RGB565ToRGBA8888(u16 src) {
	u32 dark_rb = ((src & 0x1F) << 3) | ((src & 0xF800) << 8);
	// Replicate the top 3 upper bits into the missing lower bits.
	u32 full_rb = (dark_rb | ((dark_rb >> 5) & 0x070007));
	// Add in green (6 bits instead of 5).
	u32 dark_g = ((src & 0x7E0) << 5);
	u32 full_g = dark_g | ((dark_g >> 6) & 0x300);
	return full_rb | full_g | 0xFF000000;
}

inline u16 RGBA8888ToRGB565(u32 value) {
	u32 r = (value >> 3) & 0x1F;
	u32 g = (value >> 5) & (0x3F << 5);
	u32 b = (value >> 8) & (0x1F << 11);
	return (u16)(r | g | b);
}

inline u16 RGBA8888ToRGBA5551(u32 value) {
	u32 r = (value >> 3) & 0x1F;
	u32 g = (value >> 6) & (0x1F << 5);
	u32 b = (value >> 9) & (0x1F << 10);
	u32 a = (value >> 16) & 0x8000;
	return (u16)(r | g | b | a);
}

// Used in fast sprite path.
inline u16 RGBA8888ToRGBA555X(u32 value) {
	u32 r = (value >> 3) & 0x1F;
	u32 g = (value >> 6) & (0x1F << 5);
	u32 b = (value >> 9) & (0x1F << 10);
	return (u16)(r | g | b);
}

inline u16 RGBA8888ToRGBA4444(u32 value) {
	const u32 c = value >> 4;
	const u16 r = (c >> 0) & 0x000F;
	const u16 g = (c >> 4) & 0x00F0;
	const u16 b = (c >> 8) & 0x0F00;
	const u16 a = (c >> 12) & 0xF000;
	return r | g | b | a;
}

inline u16 RGBA8888ToRGBA444X(u32 value) {
	const u32 c = value >> 4;
	const u16 r = (c >> 0) & 0x000F;
	const u16 g = (c >> 4) & 0x00F0;
	const u16 b = (c >> 8) & 0x0F00;
	return r | g | b;
}

// "Complete" set of color conversion functions between the usual formats.

// TODO: Need to revisit the naming convention of these. Seems totally backwards
// now that we've standardized on Draw::DataFormat.
// 
// The functions that have the same bit width of input and output can generally
// tolerate being called with src == dst, which is used a lot for ReverseColors
// in the GLES backend.

void ConvertBGRA8888ToRGBA8888(u32 *dst, const u32 *src, u32 numPixels);
#define ConvertRGBA8888ToBGRA8888 ConvertBGRA8888ToRGBA8888
void ConvertBGRA8888ToRGB888(u8 *dst, const u32 *src, u32 numPixels);

void ConvertRGBA8888ToRGBA5551(u16 *dst, const u32 *src, u32 numPixels);
void ConvertRGBA8888ToRGB565(u16 *dst, const u32 *src, u32 numPixels);
void ConvertRGBA8888ToRGBA4444(u16 *dst, const u32 *src, u32 numPixels);
void ConvertRGBA8888ToRGB888(u8 *dst, const u32 *src, u32 numPixels);

void ConvertBGRA8888ToRGBA5551(u16 *dst, const u32 *src, u32 numPixels);
void ConvertBGRA8888ToRGB565(u16 *dst, const u32 *src, u32 numPixels);
void ConvertBGRA8888ToRGBA4444(u16 *dst, const u32 *src, u32 numPixels);

void ConvertRGB565ToRGBA8888(u32 *dst, const u16 *src, u32 numPixels);
void ConvertRGBA5551ToRGBA8888(u32 *dst, const u16 *src, u32 numPixels);
void ConvertRGBA4444ToRGBA8888(u32 *dst, const u16 *src, u32 numPixels);

void ConvertBGR565ToRGBA8888(u32 *dst, const u16 *src, u32 numPixels);
void ConvertABGR1555ToRGBA8888(u32 *dst, const u16 *src, u32 numPixels);
void ConvertABGR4444ToRGBA8888(u32 *dst, const u16 *src, u32 numPixels);

void ConvertRGBA4444ToBGRA8888(u32 *dst, const u16 *src, u32 numPixels);
void ConvertRGBA5551ToBGRA8888(u32 *dst, const u16 *src, u32 numPixels);
void ConvertRGB565ToBGRA8888(u32 *dst, const u16 *src, u32 numPixels);

void ConvertRGBA4444ToABGR4444(u16 *dst, const u16 *src, u32 numPixels);
void ConvertRGBA5551ToABGR1555(u16 *dst, const u16 *src, u32 numPixels);
void ConvertRGB565ToBGR565(u16 *dst, const u16 *src, u32 numPixels);
void ConvertBGRA5551ToABGR1555(u16 *dst, const u16 *src, u32 numPixels);
