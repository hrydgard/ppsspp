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

#include "sceAudiocodec.h"

class PointerWrap;

void Register_sceAtrac3plus();
void __AtracInit();
void __AtracDoState(PointerWrap &p);
void __AtracShutdown();

typedef struct
{
    u32_le decodePos; // 0
    u32_le endSample; // 4
    u32_le loopStart; // 8
    u32_le loopEnd; // 12
    s32_le samplesPerChan; // 16
    char numFrame; // 20
    // 2: all the stream data on the buffer
    // 6: looping -> second buffer needed
    char state; // 21
    char unk22;
    char numChan; // 23
    u16_le sampleSize; // 24
    u16_le codec; // 26
    u32_le dataOff; // 28
    u32_le curOff; // 32
    u32_le dataEnd; // 36
    s32_le loopNum; // 40
    u32_le streamDataByte; // 44
    u32_le unk48;
    u32_le unk52;
    u32_le buffer; // 56
    u32_le secondBuffer; // 60
    u32_le bufferByte; // 64
    u32_le secondBufferByte; // 68
    // make sure the size is 128
	u8 unk[56];
} SceAtracIdInfo;

typedef struct
{
	// size 128
    SceAudiocodecCodec codec;
	// size 128
    SceAtracIdInfo info;
} SceAtracId;

// provide some decoder interface

u32 _AtracAddStreamData(int atracID, u32 bufPtr, u32 bytesToAdd);
u32 _AtracDecodeData(int atracID, u8* outbuf, u32 outbufPtr, u32 *SamplesNum, u32* finish, int *remains);
int _AtracGetIDByContext(u32 contextAddr);