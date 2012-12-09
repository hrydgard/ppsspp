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

#include "Globals.h"
#include "HLE.h"

u32 sceDmacMemcpy(u32 dst, u32 src, u32 size)
{
	DEBUG_LOG(HLE, "sceDmacMemcpy(dest=%08x, src=%08x, size=%i)", dst, src, size);
	// TODO: check the addresses.
	Memory::Memcpy(dst, Memory::GetPointer(src), size);
	return 0;
}

const HLEFunction sceDmac[] =
{
	{0x617f3fe6, &WrapU_UUU<sceDmacMemcpy>, "sceDmacMemcpy"},
};

void Register_sceDmac()
{
	RegisterModule("sceDmac", ARRAY_SIZE(sceDmac), sceDmac);
}
