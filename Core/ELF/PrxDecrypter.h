// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/Swap.h"  // PSP_Header below is all _le types.

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
typedef struct
{
	u32_le      signature;       // 0
	u16_le      attribute;       // 4  modinfo
	u16_le      comp_attribute;  // 6
	u8          module_ver_lo;   // 8
	u8          module_ver_hi;   // 9
	char        modname[28];     // 0A
	u8          version;         // 26
	u8          nsegments;       // 27
	u32_le      elf_size;        // 28
	u32_le      psp_size;        // 2C
	u32_le      entry;           // 30
	u32_le      modinfo_offset;  // 34
	s32_le      bss_size;        // 38
	u16_le      seg_align[4];    // 3C
	u32_le      seg_address[4];  // 44
	s32_le      seg_size[4];     // 54
	u32_le      reserved[5];     // 64
	u32_le      devkitversion;   // 78
	u32_le      decrypt_mode;    // 7C 
	u8          key_data0[0x30]; // 80
	s32_le      comp_size;       // B0
	s32_le      _80;             // B4
	s32_le      reserved2[2];    // B8
	u8          key_data1[0x10]; // C0
	u32_le      tag;             // D0
	u8          scheck[0x58];    // D4
	u32_le      key_data2;       // 12C
	u32_le      oe_tag;          // 130
	u8          key_data3[0x1C]; // 134
#ifdef _MSC_VER
} PSP_Header;
#else
} __attribute__((packed)) PSP_Header;
#endif

#ifdef _MSC_VER
#pragma pack(pop)
#endif

// fuseId is the PSP's Fuse ID, the words at 0xBC100090 and 0xBC100094. Only a game patch installed
// on a PSP needs it (seed is its secure install ID then).
int pspDecryptPRX(const u8 *inbuf, u8 *outbuf, u32 size, const u8 *seed = nullptr, const u32 *fuseId = nullptr);

// A module installed by the DRM BB install utility (sceKernelLoadModuleDNAS) is a PGD around a type 3
// PRX, both keyed to the console by its Fuse ID and to the game by the secure install ID it passes.
// Replaces data with the PGD's contents; false if it doesn't open with these.
bool pspUnwrapDNASPGD(std::vector<u8> &data, const u8 *secureId, const u32 *fuseId);

// A DRM BB install package is the EBOOT.PBP of a game patch download, which the install utility
// (psheet) unpacks onto the memory stick. A .PRX in it comes out as the type 3 module before the
// install's console-specific step, which pspDecryptPRX can still decrypt if it knows that step's
// result for the module. secureId is the game's secure install ID, or null to take it from the
// package's MAC; it's returned in secureIdOut if that's given. False if the package doesn't check out.
struct DrmBBFile {
	std::string name;
	std::vector<u8> data;
};
bool pspReadDrmBBPackage(const std::vector<u8> &pbp, const u8 *secureId, std::vector<DrmBBFile> &files, u8 *secureIdOut = nullptr);

// A Fuse ID as PSP tools show it, 12 (or 16) hex digits with the 0xBC100094 word first.
bool pspParseFuseId(std::string_view text, u32 fuseId[2]);
