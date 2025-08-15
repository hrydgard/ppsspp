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

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

class PointerWrap;

// MPEG statics.
static const u32 PSMF_MAGIC = 0x464D5350;
static const int PSMF_STREAM_VERSION_OFFSET = 0x4;
static const int PSMF_STREAM_OFFSET_OFFSET = 0x8;
static const int PSMF_STREAM_SIZE_OFFSET = 0xC;
static const int PSMF_FIRST_TIMESTAMP_OFFSET = 0x54;
static const int PSMF_LAST_TIMESTAMP_OFFSET = 0x5A;

static const int PSMF_VIDEO_STREAM_ID = 0xE0;
static const int PSMF_AUDIO_STREAM_ID = 0xBD;

struct SceMpegAu {
	s64_le pts;  // presentation time stamp
	s64_le dts;  // decode time stamp
	u32_le esBuffer;  // WARNING: We abuse this to keep track of the stream number!
	u32_le esSize;

	void read(u32 addr);
	void write(u32 addr);
};

// As native in PSP ram
struct SceMpegRingBuffer {
	// PSP info
	s32_le packets;
	// Misused: this is used as total read, but should be read offset (within ring.)
	s32_le packetsRead;
	s32_le packetsWritePos;
	s32_le packetsAvail; // pspsdk: unk2, noxa: iUnk0
	s32_le packetSize; // 2048
	u32_le data; // address, ring buffer
	u32_le callback_addr; // see sceMpegRingbufferPut
	s32_le callback_args;
	s32_le dataUpperBound;
	s32_le semaID; // unused? No, probably not, see #20084. Though when should we signal it? create it?
	u32_le mpeg; // pointer to mpeg struct, fixed up in sceMpegCreate
	// Note: not available in all versions.
	u32_le gp;
};

// Internal structure
struct AvcContext {
	int avcDetailFrameWidth;
	int avcDetailFrameHeight;
	int avcDecodeResult;
	int avcFrameStatus;
};

struct StreamInfo {
	int type;
	int num;
	int sid;
	bool needsReset;
};

static const int MPEG_DATA_ES_BUFFERS = 2;
typedef std::map<u32, StreamInfo> StreamInfoMap;
class MediaEngine;

// Internal structure
struct MpegContext {
	MpegContext();
	~MpegContext();

	MpegContext(const MpegContext &) = delete;
	void operator=(const MpegContext &) = delete;

	void DoState(PointerWrap &p);

	u8 mpegheader[2048];
	u32 defaultFrameWidth;
	int videoFrameCount;
	int audioFrameCount;
	bool endOfAudioReached;
	bool endOfVideoReached;
	int videoPixelMode;
	u32 mpegMagic;
	int mpegVersion;
	u32 mpegRawVersion;
	u32 mpegOffset;
	u32 mpegStreamSize;
	s64 mpegFirstTimestamp;
	s64 mpegLastTimestamp;
	u32 mpegFirstDate;
	u32 mpegLastDate;
	u32 mpegRingbufferAddr;
	int mpegwarmUp;
	bool esBuffers[MPEG_DATA_ES_BUFFERS];
	AvcContext avc;

	bool avcRegistered;
	bool atracRegistered;
	bool pcmRegistered;
	bool dataRegistered;

	bool ignoreAtrac;
	bool ignorePcm;
	bool ignoreAvc;

	bool isAnalyzed = false;
	bool ringbufferNeedsReverse = false;

	StreamInfoMap streamMap;
	MediaEngine *mediaengine = nullptr;
};

void __MpegInit();
void __MpegDoState(PointerWrap &p);
void __MpegShutdown();

void __MpegLoadModule(int version, u32 crc);

void Register_sceMpeg();

void Register_sceMpegbase();

void __VideoPmpInit();
void __VideoPmpDoState(PointerWrap &p);
void __VideoPmpShutdown();

const std::map<u32, MpegContext *> &__MpegGetContexts();
