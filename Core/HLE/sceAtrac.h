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
void __AtracLoadModule(int version, u32 crc);

enum AtracStatus : u8 {
	ATRAC_STATUS_NO_DATA = 1,
	ATRAC_STATUS_ALL_DATA_LOADED = 2,
	ATRAC_STATUS_HALFWAY_BUFFER = 3,
	ATRAC_STATUS_STREAMED_WITHOUT_LOOP = 4,
	ATRAC_STATUS_STREAMED_LOOP_FROM_END = 5,
	// This means there's additional audio after the loop.
	// i.e. ~~before loop~~ [ ~~this part loops~~ ] ~~after loop~~
	// The "fork in the road" means a second buffer is needed for the second path.
	ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER = 6,
	ATRAC_STATUS_LOW_LEVEL = 8,
	ATRAC_STATUS_FOR_SCESAS = 16,

	ATRAC_STATUS_STREAMED_MASK = 4,
};

const char *AtracStatusToString(AtracStatus status);
inline bool AtracStatusIsStreaming(AtracStatus status) {
	switch (status) {
	case ATRAC_STATUS_STREAMED_WITHOUT_LOOP:
	case ATRAC_STATUS_STREAMED_LOOP_FROM_END:
	case ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER:
		return true;
	default:
		return false;
	}
}

typedef AtracStatus AtracStatus_le;

struct SceAtracIdInfo {
    u32_le decodePos; // 0
    u32_le endSample; // 4
    u32_le loopStart; // 8
    u32_le loopEnd; // 12
    s32_le samplesPerChan; // 16   // This rather seems to be the number of skipped samples at the start. (plus one frame?)
    char numFrame; // 20  // This seems to just stay at zero.
    AtracStatus_le state; // 21
    char unk22;
    char numChan; // 23
    u16_le sampleSize; // 24
    u16_le codec; // 26
    u32_le dataOff; // 28
    u32_le curOff; // 32
    u32_le dataEnd; // 36
    s32_le loopNum; // 40
    u32_le streamDataByte; // 44
    u32_le streamOff;  // Previously unk48. this appears to possibly be the offset inside the buffer for streaming.
    u32_le unk52;
    u32_le buffer; // 56
    u32_le secondBuffer; // 60
    u32_le bufferByte; // 64
    u32_le secondBufferByte; // 68
    // make sure the size is 128
    u32_le unk[13];
    u32_le atracID;
};

struct SceAtracContext {
	// size 128
    SceAudiocodecCodec codec;
	// size 128
    SceAtracIdInfo info;
};

const int PSP_NUM_ATRAC_IDS = 6;

class AtracBase;

const AtracBase *__AtracGetCtx(int i, u32 *type);

// External interface used by sceSas.
u32 AtracSasAddStreamData(int atracID, u32 bufPtr, u32 bytesToAdd);
u32 AtracSasDecodeData(int atracID, u8* outbuf, u32 outbufPtr, u32 *SamplesNum, u32* finish, int *remains);
int AtracSasGetIDByContext(u32 contextAddr);
