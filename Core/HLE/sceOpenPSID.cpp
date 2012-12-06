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

#include "HLE.h"

#include "sceOpenPSID.h"

int sceOpenPSIDGetOpenPSID(u32 OpenPSIDPtr)
{
	ERROR_LOG(HLE, "UNTESTED sceOpenPSIDGetOpenPSID(%d)", OpenPSIDPtr);
	u8 dummyOpenPSID[16] = {0x10, 0x02, 0xA3, 0x44, 0x13, 0xF5, 0x93, 0xB0, 0xCC, 0x6E, 0xD1, 0x32, 0x27, 0x85, 0x0F, 0x9D};

	if (Memory::IsValidAddress(OpenPSIDPtr))
	{
		for (int i = 0; i < 16; i++) 
		{
			Memory::Write_U8(dummyOpenPSID[i], OpenPSIDPtr+i);
		}
	}
	return 0;
}

const HLEFunction sceOpenPSID[] = 
{
	{0xc69bebce, WrapI_U<sceOpenPSIDGetOpenPSID>, "sceOpenPSIDGetOpenPSID"},
};

void Register_sceOpenPSID()
{
	RegisterModule("sceOpenPSID", ARRAY_SIZE(sceOpenPSID), sceOpenPSID);
}
