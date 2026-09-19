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

void __VideocodecInit();
void __VideocodecShutdown();
void __VideocodecDoState(PointerWrap &p);

void Register_sceVideocodec();

// The state of each open decoder, for the debugger. A game can have several - Silent Hill Origins
// runs one context for the EDRAM and another for the decoding.
struct VideocodecCtxInfo {
	u32 ctxAddr;
	int type;
	bool hasDecoder;
	int frameCount;
	// The token sceVideocodecGetEDRAM handed back, and how much it stands for. Not an address -
	// the block is ours, not the game's, the way the Media Engine's memory is on hardware.
	u32 edramToken;
	u32 edramSize;
	u32 frameBuffers;
	u32 frameBuffersSize;
	int width;
	int height;
};
void VideocodecGetCtxInfo(std::vector<VideocodecCtxInfo> *infos);

// The Media Engine's address space. It is the ME's own, so Memory::IsValidAddress and the rest,
// which answer for the Allegrex's memory, have nothing to say about it. These are the equivalents
// for the code standing in for the ME (the decoder, the DMA, the colour conversion).
//
// An address is only meaningful together with the space it came from, and mpeg.prx hands us bare
// integers. Where we have to work it out from the value, ask the ME first: the addresses mpeg.prx
// uses for ME memory sit well down in the first megabyte, and the ones it means for PSP RAM are
// 0x08000000 and up, so the two sets stay apart. That is what these callers happen to pass rather
// than a rule (a PSP address small enough to pass for an ME one, like the scratchpad, would be
// taken for one).
bool MEIsValidRange(u32 addr, u32 size);
u8 *MEGetPointerRange(u32 addr, u32 size);

// The eight buffers of the frame starting at `firstBuffer`, and its size. Returns false if that
// isn't the start of an allocation we handed out (the normal answer once sceMpegBaseYCrCbCopy has
// moved a frame into the game's memory).
bool VideocodecGetFrameBuffers(u32 firstBuffer, u32 buffers[8], int *width = nullptr, int *height = nullptr);

// How the eight buffers a frame is delivered in are sized and laid out, in the order the
// descriptor lists them: four luma (left/right half of a 32-pixel band, even/odd rows) then four
// chroma. Everything that writes, reads or allocates them has to agree, so it lives in one place.
// `offsets` is each buffer's start within a single allocation, 64-byte aligned; either array may
// be null. Returns the total allocation size.
u32 VideocodecFrameBufferLayout(int width, int height, int sizes[8], u32 offsets[8]);
