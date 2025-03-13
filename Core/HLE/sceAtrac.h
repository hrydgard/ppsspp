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

void __AtracNotifyLoadModule(int version, u32 crc, u32 bssAddr, int bssSize);
void __AtracNotifyUnloadModule();

// The "state" member of SceAtracIdInfo.
enum AtracStatus : u8 {
	ATRAC_STATUS_NO_DATA = 1,

	// The entire file is loaded into memory, no further file access needed.
	ATRAC_STATUS_ALL_DATA_LOADED = 2,

	// The buffer is sized to fit the entire file, but it's only partially loaded, so you can start playback before loading the whole file.
	ATRAC_STATUS_HALFWAY_BUFFER = 3,

	// In these ones, the buffer is smaller than the file, and data is streamed into it as needed for playback.
	// These are the most complex modes, both to implement and use.
	ATRAC_STATUS_STREAMED_WITHOUT_LOOP = 4,
	ATRAC_STATUS_STREAMED_LOOP_FROM_END = 5,
	// This means there's additional audio after the loop.
	// i.e. ~~before loop~~ [ ~~this part loops~~ ] ~~after loop~~
	// The "fork in the road" means a second buffer is needed for the second path.
	ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER = 6,

	// In this mode, the only API to call is sceAtracLowLevelDecode, which decodes a stream packet by packet without any other metadata.
	ATRAC_STATUS_LOW_LEVEL = 8,

	// This mode is for using an Atrac context as the audio source for an sceSas channel. Not used a lot (Sol Trigger).
	ATRAC_STATUS_FOR_SCESAS = 16,

	// Bitwise-and the status with this to check for any of the streaming modes in a single test.
	ATRAC_STATUS_STREAMED_MASK = 4,
};

const char *AtracStatusToString(AtracStatus status);

inline bool AtracStatusIsStreaming(AtracStatus status) {
	return (status & ATRAC_STATUS_STREAMED_MASK) != 0;
}

struct SceAtracIdInfo {
	u32 decodePos;        // Sample position in the song that we'll next be decoding from. 
	u32 endSample;        // Last sample index of the track.
	u32 loopStart;        // Start of the loop (sample index)
	u32 loopEnd;          // End of the loop (sample index)
	s32 firstValidSample; // Seems to be the number of skipped samples at the start. After SetID, decodePos will match this.
	char numFrame;        // This is 1 for a single frame when a loop is triggered, otherwise seems to stay at 0. Likely mis-named.
	AtracStatus state;    // State enum, see AtracStatus.
	char unk22;           // UNKNOWN: Always zero?
	char numChan;         // Number of audio channels, usually 2 but 1 is possible.
	u16 sampleSize;       // Size in bytes of an encoded audio frame.
	u16 codec;            // Codec. 0x1000 is Atrac3+, 0x1001 is Atrac3. See the PSP_CODEC_ enum (only these two are supported).
	u32 dataOff;          // File offset in bytes where the Atrac3+ frames start appearing. The first dummy packet starts here.
	u32 curOff;           // File offset in bytes corresponding to the start of next packet that will be *decoded* (on the next call to sceAtracDecodeData).
	u32 dataEnd;          // File size in bytes.
	s32 loopNum;          // Current loop counter. If 0, will not loop. -1 loops for ever, positive numbers get decremented on the loop end. So to play a song 3 times and then end, set this to 2.
	u32 streamDataByte;   // Number of bytes of queued/buffered/uploaded data. In full and half-way modes, this isn't decremented as you decode.
	u32 streamOff;        // Streaming modes only: The byte offset inside the RAM buffer where sceAtracDecodeData will read from next. ONLY points to even packet boundaries.
	u32 unk52;            // UNKNOWN: Always zero?
	u32 buffer;           // Address in RAM of the main buffer.
	u32 secondBuffer;     // Address in RAM of the second buffer, or 0 if not used.
	u32 bufferByte;       // Size in bytes of the main buffer.
	u32 secondBufferByte; // Size in bytes of the second buffer.
	// Offset 72 here.
	// make sure the size is 128
	u32 unk[13];
	u32 atracID;          // The atrac context number is stored here, for some reason.
};

// One of these structs is allocated for each Atrac context.
// The raw codec state is stored in 'codec'.
// The internal playback state is stored in 'info', and that is used for all state keeping in the Atrac2 implementation,
// imitating what happens on hardware as closely as possible.
struct SceAtracContext {
	// size 128
	SceAudiocodecCodec codec;
	// size 128
	SceAtracIdInfo info;
};

constexpr int PSP_MAX_ATRAC_IDS = 6;

class AtracBase;

const AtracBase *__AtracGetCtx(int i, u32 *type);

// External interface used by sceSas, see ATRAC_STATUS_FOR_SCESAS.
u32 AtracSasAddStreamData(int atracID, u32 bufPtr, u32 bytesToAdd);
u32 AtracSasDecodeData(int atracID, u8* outbuf, u32 outbufPtr, u32 *SamplesNum, u32* finish, int *remains);
int AtracSasGetIDByContext(u32 contextAddr);
