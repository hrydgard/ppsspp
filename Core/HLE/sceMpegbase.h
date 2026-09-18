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

// Called per boot, from __MpegInit.
void __MpegBaseInit();

void __MpegBaseDoState(PointerWrap &p);

// Takes the PES payload sceMpegBasePESpacketCopy gathered for a given destination. On hardware that
// copy lands in Media Engine memory, which sceVideocodec would then read back; we keep it here
// instead. The payload is moved out and dropped from the table, so each one is decoded once.
// Empty if nothing was copied to that address.
std::vector<u8> MpegBaseTakePESPacket(u32 dest);

// Un-tiles a decoded frame from the eight buffers the Media Engine lays it out in into three
// planes. The buffers are in sceVideocodec's order: four luma, then four chroma. cb and cr come
// out at half width and half height, as YUV420 does.
bool ReadTiledYCbCr(const u32 *buffers, int width, int height,
	std::vector<u8> &luma, std::vector<u8> &cb, std::vector<u8> &cr);

// Converts a rectangle of a planar YCbCr420 frame to RGB, the way the DMACPLUS does on the way to
// the screen. Pure, so it can be measured and checked on its own - see TestMpegCsc.
//
// luma is width by height; cb and cr are half that in both directions. dest is destStride pixels
// wide in the format pixelMode names (a GEBufferFormat), and the range lands at its origin.
void MpegCscRange(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight);
