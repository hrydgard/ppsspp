// Copyright (C) 2003 Dolphin Project / 2012 PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include <cstring>

#include "Common/CommonTypes.h"
#include "Common/StringUtils.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"

// To avoid pulling in the entire HLE.h.
extern MIPSState *currentMIPS;

namespace Memory
{

inline void Memcpy(const u32 to_address, const void *from_data, const u32 len, const char *tag, size_t tagLen) {
	u8 *to = GetPointerWriteRange(to_address, len);
	if (to) {
		memcpy(to, from_data, len);
		if (!tag) {
			tag = "Memcpy";
			tagLen = sizeof("Memcpy") - 1;
		}
		NotifyMemInfo(MemBlockFlags::WRITE, to_address, len, tag, tagLen);
	}
	// if not, GetPointer will log.
}

inline void Memcpy(void *to_data, const u32 from_address, const u32 len, const char *tag, size_t tagLen) {
	const u8 *from = GetPointerRange(from_address, len);
	if (from) {
		memcpy(to_data, from, len);
		if (!tag) {
			tag = "Memcpy";
			tagLen = sizeof("Memcpy") - 1;
		}
		NotifyMemInfo(MemBlockFlags::READ, from_address, len, tag, tagLen);
	}
	// if not, GetPointer will log.
}

inline void Memcpy(const u32 to_address, const u32 from_address, const u32 len, const char *tag, size_t tagLen) {
	u8 *to = GetPointerWriteRange(to_address, len);
	// If not, GetPointer will log.
	if (!to)
		return;
	const u8 *from = GetPointerRange(from_address, len);
	if (!from)
		return;

	memcpy(to, from, len);

	if (MemBlockInfoDetailed(len)) {
		if (!tag) {
			NotifyMemInfoCopy(to_address, from_address, len, "Memcpy/");
		} else {
			NotifyMemInfo(MemBlockFlags::READ, from_address, len, tag, tagLen);
			NotifyMemInfo(MemBlockFlags::WRITE, to_address, len, tag, tagLen);
		}
	}
}

template<size_t tagLen>
inline void Memcpy(const u32 to_address, const void *from_data, const u32 len, const char(&tag)[tagLen]) {
	Memcpy(to_address, from_data, len, tag, tagLen);
}

template<size_t tagLen>
inline void Memcpy(void *to_data, const u32 from_address, const u32 len, const char(&tag)[tagLen]) {
	Memcpy(to_data, from_address, len, tag, tagLen);
}

template<size_t tagLen>
inline void Memcpy(const u32 to_address, const u32 from_address, const u32 len, const char(&tag)[tagLen]) {
	Memcpy(to_address, from_address, len, tag, tagLen);
}

inline void Memcpy(const u32 to_address, const void *from_data, const u32 len) {
	Memcpy(to_address, from_data, len, nullptr, 0);
}

inline void Memcpy(void *to_data, const u32 from_address, const u32 len) {
	Memcpy(to_data, from_address, len, nullptr, 0);
}

inline void Memcpy(const u32 to_address, const u32 from_address, const u32 len) {
	Memcpy(to_address, from_address, len, nullptr, 0);
}

void Memset(const u32 _Address, const u8 _Data, const u32 _iLength, const char *tag = "Memset");

}
