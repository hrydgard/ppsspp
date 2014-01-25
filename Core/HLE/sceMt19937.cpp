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

#include "Core/HLE/HLE.h"
#include "Core/Reporting.h"

u32 sceMt19937Init(u32 mt19937Addr, u32 seed)
{
	WARN_LOG(HLE, "sceMt19937Init(%08x, %08x)", mt19937Addr, seed);
	if (!Memory::IsValidAddress(mt19937Addr))
		return -1;
	void *ptr = Memory::GetPointer(mt19937Addr);
	// This is made to match the memory layout of a PSP MT structure exactly.
	// Let's just construct it in place with placement new. Elite C++ hackery FTW.
	new (ptr) MersenneTwister(seed);
	return 0;
}

u32 sceMt19937UInt(u32 mt19937Addr)
{
	WARN_LOG(HLE, "sceMt19937UInt(%08x)", mt19937Addr);
	if (!Memory::IsValidAddress(mt19937Addr))
		return -1;
	MersenneTwister *mt = (MersenneTwister *)Memory::GetPointer(mt19937Addr);
	return mt->R32();
}

const HLEFunction sceMt19937[] =
{
	{0xECF5D379, WrapU_UU<sceMt19937Init>, "sceMt19937Init"},
	{0xF40C98E6, WrapU_U<sceMt19937UInt>, "sceMt19937UInt"},
};

void Register_sceMt19937()
{
	RegisterModule("sceMt19937", ARRAY_SIZE(sceMt19937), sceMt19937);
}
