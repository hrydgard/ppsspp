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

// NOTE: a graduation candidate, but not graduated. sceParseUri is a small library with no
// dependencies beyond the kernel, and most games that use it ship their own copy - but it is also
// in the firmware, and sceUtility can load it (module 0x103), so a game may import it without
// carrying one. Letting the game's module win would hand that game unresolved imports. It can
// follow once sceUtility loads the firmware copy the way it does for sceMp3 and sceMp4.

#pragma once

#ifdef _MSC_VER
#define PACK  // on MSVC we use #pragma pack() instead so let's kill this.
#pragma pack(push, 1)
#else
#define PACK __attribute__((packed))
#endif

typedef struct PSPParsedUri {
	s32 noSlash;
	s32 schemeAddr;
	s32 userInfoUserNameAddr;
	s32 userInfoPasswordAddr;
	s32 hostAddr;
	s32 pathAddr;
	s32 queryAddr;
	s32 fragmentAddr;
	u16 port;
	u8 unknown[10]; // padding might be included here?
} PACK PSPParsedUri;

#ifdef _MSC_VER 
#pragma pack(pop)
#endif

void Register_sceParseUri();
