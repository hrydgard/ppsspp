// Copyright (c) 2017- PPSSPP Project.

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

#include "Common/CommonTypes.h"

namespace GPURecord {

struct Header {
	char magic[8];
	uint32_t version;
	char gameID[9];
	uint8_t pad[3];
};

static const char * const HEADER_MAGIC = "PPSSPPGE";
// Version 1: Uncompressed
// Version 2: Uses snappy
// Version 3: Adds FRAMEBUF0-FRAMEBUF9
// Version 4: Expanded header with game ID
// Version 5: Uses zstd
// Version 6: Corrects dirty VRAM flag
static const int VERSION = 6;
static const int MIN_VERSION = 2;

enum class CommandType : u8 {
	INIT = 0,
	REGISTERS = 1,
	VERTICES = 2,
	INDICES = 3,
	CLUT = 4,
	TRANSFERSRC = 5,
	MEMSET = 6,
	MEMCPYDEST = 7,
	MEMCPYDATA = 8,
	DISPLAY = 9,
	CLUTADDR = 10,
	EDRAMTRANS = 11,

	TEXTURE0 = 0x10,
	TEXTURE1 = 0x11,
	TEXTURE2 = 0x12,
	TEXTURE3 = 0x13,
	TEXTURE4 = 0x14,
	TEXTURE5 = 0x15,
	TEXTURE6 = 0x16,
	TEXTURE7 = 0x17,

	FRAMEBUF0 = 0x18,
	FRAMEBUF1 = 0x19,
	FRAMEBUF2 = 0x1A,
	FRAMEBUF3 = 0x1B,
	FRAMEBUF4 = 0x1C,
	FRAMEBUF5 = 0x1D,
	FRAMEBUF6 = 0x1E,
	FRAMEBUF7 = 0x1F,
};

#pragma pack(push, 1)

struct Command {
	CommandType type;
	u32 sz;
	u32 ptr;
};

#pragma pack(pop)

};
