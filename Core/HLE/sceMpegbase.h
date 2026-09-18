// Copyright (c) 2026- PPSSPP Project.

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

#include <vector>

#include "Common/CommonTypes.h"

class PointerWrap;

void Register_sceMpegbase();

// Called per boot, from __KernelInit and __KernelShutdown, around sceMpeg's own pair.
void __MpegBaseInit();
void __MpegBaseShutdown();

void __MpegBaseDoState(PointerWrap &p);

// Takes the PES payload sceMpegBasePESpacketCopy gathered for a given destination. On hardware that
// copy lands in Media Engine memory, which sceVideocodec would then read back; we keep it here
// instead. The payload is moved out and dropped from the table, so each one is decoded once.
// Empty if nothing was copied to that address.
std::vector<u8> MpegBaseTakePESPacket(u32 dest);

// Un-tiles a decoded frame from the eight buffers the Media Engine lays it out in into three
// planes. The buffers are in sceVideocodec's order: four luma, then four chroma. cb and cr come
// out at half width and half height, as YUV420 does.
//
// The planes point into scratch that is reused by the next call, so read them before calling again.
bool ReadTiledYCbCr(const u32 *buffers, int width, int height,
	const u8 **luma, const u8 **cb, const u8 **cr);

// The de-tiling on its own, taking the eight buffers already resolved to host pointers, so it can
// be measured and checked without a game - see TestMpegCsc. A null entry in src leaves that
// buffer's share of the output alone.
void UntileYCbCr(u8 *luma, u8 *cb, u8 *cr, const u8 *const src[8], const int sizes[8],
	int width, int height);

// Converts a rectangle of a planar YCbCr420 frame to RGB, the way the DMACPLUS does on the way to
// the screen. Pure, so it can be measured and checked on its own - see TestMpegCsc.
//
// luma is width by height; cb and cr are half that in both directions. dest is destStride pixels
// wide in the format pixelMode names (a GEBufferFormat), and the range lands at its origin.
void MpegCscRange(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight);

// The two implementations behind it, exposed so TestMpegCsc can measure and compare them.
// The scalar one handles anything; the swscale one refuses what it cannot express and is then
// not used. They do not agree to the bit - swscale rounds its own way - so the scalar one is
// what the reference in the test is checked against.
void MpegCscRangeScalar(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight);
bool MpegCscRangeSws(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight);

// Frees the cached swscale context.
void MpegCscShutdown();
