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

#include <map>

class PointerWrap;

struct SceAudiocodecCodec {
	s32 unk_init;
	s32 unk4;
	s32 err; // 8
	s32 edramAddr; // c  // presumably in ME memory?
	s32 neededMem; // 10  // 0x102400 for Atrac3+
	s32 inited;  // 14
	u32 inBuf; // 18  // Before decoding, set this to the start of the raw frame.
	s32 srcFrameSize; // 1c
	u32 outBuf; // 20  // This is where the decoded data is written.
	s32 dstBytesWritten; // 24
	s8 unk40;  // 28  format or looping related   // Probably, from here on out is a union with different fields for different codecs.
	s8 unk41;  // 29  format or looping related
	s16 unk42; // 2a
	s8 unk44;
	s8 unk45;
	s8 unk46;
	s8 unk47;
	s32 unk48;  // 30 Atrac3 (non-+) related. Zero with Atrac3+.
	s32 unk52;  // 34
	s32 unk56;
	s32 unk60;
	s32 unk64;
	s32 unk68;
	s32 unk72;
	s32 unk76;
	s32 unk80;
	s32 unk84;
	s32 unk88;
	s32 unk92;
	s32 unk96;
	s32 unk100;
	u32 allocMem; // 104
	// make sure the size is 128
	u8 unk[20];
};

void __AudioCodecInit();
void __AudioCodecShutdown();
void Register_sceAudiocodec();
void __sceAudiocodecDoState(PointerWrap &p);

class AudioDecoder;
extern std::map<u32, AudioDecoder *> g_audioDecoderContexts;
