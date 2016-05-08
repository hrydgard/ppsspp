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

#include "Common/CommonTypes.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"

// To avoid pulling in the entire HLE.h.
extern MIPSState *currentMIPS;

namespace Memory
{

inline void Memcpy(const u32 to_address, const void *from_data, const u32 len)
{
	u8 *to = GetPointer(to_address);
	if (to) {
		memcpy(to, from_data, len);
#ifndef MOBILE_DEVICE
		CBreakPoints::ExecMemCheck(to_address, true, len, currentMIPS->pc);
#endif
	}
	// if not, GetPointer will log.
}

inline void Memcpy(void *to_data, const u32 from_address, const u32 len)
{
	const u8 *from = GetPointer(from_address);
	if (from) {
		memcpy(to_data, from, len);
#ifndef MOBILE_DEVICE
		CBreakPoints::ExecMemCheck(from_address, false, len, currentMIPS->pc);
#endif
	}
	// if not, GetPointer will log.
}

inline void Memcpy(const u32 to_address, const u32 from_address, const u32 len)
{
	Memcpy(GetPointer(to_address), from_address, len);
#ifndef MOBILE_DEVICE
	CBreakPoints::ExecMemCheck(to_address, true, len, currentMIPS->pc);
#endif
}

void Memset(const u32 _Address, const u8 _Data, const u32 _iLength);

template<class T>
void ReadStruct(u32 address, T *ptr)
{
	const u32 sz = (u32)sizeof(*ptr);
	Memcpy(ptr, address, sz);
}

template<class T>
void ReadStructUnchecked(u32 address, T *ptr)
{
	const u32 sz = (u32)sizeof(*ptr);
	MemcpyUnchecked(ptr, address, sz);
}

template<class T>
void WriteStruct(u32 address, T *ptr)
{
	const u32 sz = (u32)sizeof(*ptr);
	Memcpy(address, ptr, sz);
}

template<class T>
void WriteStructUnchecked(u32 address, T *ptr)
{
	const u32 sz = (u32)sizeof(*ptr);
	MemcpyUnchecked(address, ptr, sz);
}
}
