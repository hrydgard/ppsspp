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
#include "FunctionWrappers.h"
#include "../MIPS/MIPS.h"

u32 sceImposeGetBatteryIconStatus(u32 chargingPtr, u32 iconStatusPtr)
{
	DEBUG_LOG(HLE,"%i=sceImposeGetBatteryIconStatus(%08x, %08x)", chargingPtr, iconStatusPtr);
	if (Memory::IsValidAddress(chargingPtr))
		Memory::Write_U32(1, chargingPtr);
	if (Memory::IsValidAddress(iconStatusPtr))
		Memory::Write_U32(3, iconStatusPtr);
	return 0;
}


//OSD stuff? home button?
const HLEFunction sceImpose[] =
{
	{0x36aa6e91, 0, "sceImposeSetLanguageMode"},  // Seen
	{0x381bd9e7, 0, "sceImposeHomeButton"},
	{0x24fd7bcf, 0, "sceImposeGetLanguageMode"},
	{0x8c943191, &WrapU_UU<sceImposeGetBatteryIconStatus>, "sceImposeGetBatteryIconStatus"},
};

void Register_sceImpose()
{
	RegisterModule("sceImpose", ARRAY_SIZE(sceImpose), sceImpose);
}
