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

#include "../../Globals.h"
#include "../MIPS/MIPS.h"

class PointerWrap;

enum {
	ERROR_MPEG_BAD_VERSION                              = 0x80610002,
	ERROR_MPEG_NO_MEMORY                                = 0x80610022,
	ERROR_MPEG_INVALID_ADDR                             = 0x80610103,
	ERROR_MPEG_INVALID_VALUE                            = 0x806101fe,

	ERROR_PSMF_NOT_INITIALIZED                          = 0x80615001,
	ERROR_PSMF_BAD_VERSION                              = 0x80615002,
	ERROR_PSMF_NOT_FOUND                                = 0x80615025,
	ERROR_PSMF_INVALID_ID                               = 0x80615100,
	ERROR_PSMF_INVALID_VALUE                            = 0x806151fe,
	ERROR_PSMF_INVALID_TIMESTAMP                        = 0x80615500,
	ERROR_PSMF_INVALID_PSMF                             = 0x80615501,

	ERROR_PSMFPLAYER_NOT_INITIALIZED                    = 0x80616001,
	ERROR_PSMFPLAYER_NO_MORE_DATA                       = 0x8061600c,

	ERROR_MPEG_NO_DATA                                  = 0x80618001,
	ERROR_MPEG_ALREADY_INIT                             = 0x80618005,
	ERROR_MPEG_NOT_YET_INIT                             = 0x80618009,
};

// MPEG statics.
static const u32 PSMF_MAGIC = 0x464D5350;
static const int PSMF_VERSION_0012 = 0x32313030;
static const int PSMF_VERSION_0013 = 0x33313030;
static const int PSMF_VERSION_0014 = 0x34313030;
static const int PSMF_VERSION_0015 = 0x35313030;
static const int PSMF_STREAM_VERSION_OFFSET = 0x4;
static const int PSMF_STREAM_OFFSET_OFFSET = 0x8;
static const int PSMF_STREAM_SIZE_OFFSET = 0xC;
static const int PSMF_FIRST_TIMESTAMP_OFFSET = 0x54;
static const int PSMF_LAST_TIMESTAMP_OFFSET = 0x5A;

struct SceMpegAu {
	s64 pts;  // presentation time stamp
	s64 dts;  // decode time stamp
	u32 esBuffer;
	u32 esSize;

	void read(u32 addr) {
		Memory::ReadStruct(addr, this);
		pts = (pts & 0xFFFFFFFFULL) << 32 | (((u64)pts) >> 32);
		dts = (dts & 0xFFFFFFFFULL) << 32 | (((u64)dts) >> 32);
	}

	void write(u32 addr) {
		pts = (pts & 0xFFFFFFFFULL) << 32 | (((u64)pts) >> 32);
		dts = (dts & 0xFFFFFFFFULL) << 32 | (((u64)dts) >> 32);
		Memory::WriteStruct(addr, this);
	}
};

// As native in PSP ram
struct SceMpegRingBuffer {
	// PSP info
  int packets;
  int packetsRead;
  int packetsWritten;
  int packetsFree; // pspsdk: unk2, noxa: iUnk0
  int packetSize; // 2048
  int data; // address, ring buffer
  u32 callback_addr; // see sceMpegRingbufferPut
  int callback_args;
  int dataUpperBound;
  int semaID; // unused?
  u32 mpeg; // pointer to mpeg struct, fixed up in sceMpegCreate
};

void __MpegInit();
void __MpegDoState(PointerWrap &p);
void __MpegShutdown();

void Register_sceMpeg();