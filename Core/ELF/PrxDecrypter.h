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

#include "../../Globals.h"

#define MISSING_KEY -10

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
typedef struct
{
	u32     signature;  // 0
	u16     attribute; // 4  modinfo
	u16     comp_attribute; // 6
	u8      module_ver_lo;  // 8
	u8      module_ver_hi;  // 9
	char    modname[28]; // 0A
	u8      version; // 26
	u8      nsegments; // 27
	u32     elf_size; // 28
	u32     psp_size; // 2C
	u32     entry;  // 30
	u32     modinfo_offset; // 34
	int     bss_size; // 38
	u16     seg_align[4]; // 3C
	u32     seg_address[4]; // 44
	int     seg_size[4]; // 54
	u32     reserved[5]; // 64
	u32     devkitversion; // 78
	u32     decrypt_mode; // 7C 
	u8      key_data0[0x30]; // 80
	int     comp_size; // B0
	int     _80;    // B4
	int     reserved2[2];   // B8
	u8      key_data1[0x10]; // C0
	u32     tag; // D0
	u8      scheck[0x58]; // D4
	u32     key_data2; // 12C
	u32     oe_tag; // 130
	u8      key_data3[0x1C]; // 134
#ifdef _MSC_VER
} PSP_Header;
#else
} __attribute__((packed)) PSP_Header;
#endif

#ifdef _MSC_VER
#pragma pack(pop)
#endif

int pspDecryptPRX(const u8 *inbuf, u8 *outbuf, u32 size);

