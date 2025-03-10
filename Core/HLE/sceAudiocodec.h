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

typedef struct {
	s32_le unk0;
	s32_le unk4;
	s32_le err; // 8
	s32_le edramAddr; // 12  // presumably in ME memory?
	s32_le neededMem; // 16  // 0x102400 for Atrac3+
	s32_le unk20;
	u32_le inBuf; // 24  // This is updated for every frame that's decoded, to point to the start of the frame.
	s32_le unk28;
	u32_le outBuf; // 32
	s32_le unk36;
	s8 unk40;
	s8 unk41;
	s8 unk42;
	s8 unk43;
	s8 unk44;
	s8 unk45;
	s8 unk46;
	s8 unk47;
	s32_le unk48;
	s32_le unk52;
	s32_le unk56;
	s32_le unk60;
	s32_le unk64;
	s32_le unk68;
	s32_le unk72;
	s32_le unk76;
	s32_le unk80;
	s32_le unk84;
	s32_le unk88;
	s32_le unk92;
	s32_le unk96;
	s32_le unk100;
	u32_le allocMem; // 104
	// make sure the size is 128
	u8 unk[20];
} SceAudiocodecCodec;

void __AudioCodecInit();
void __AudioCodecShutdown();
void Register_sceAudiocodec();
void __sceAudiocodecDoState(PointerWrap &p);

class AudioDecoder;
extern std::map<u32, AudioDecoder *> g_audioDecoderContexts;
