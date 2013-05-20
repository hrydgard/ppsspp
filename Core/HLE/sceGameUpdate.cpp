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


u32 sceGameUpdateInit()
{
	ERROR_LOG(HLE, "UNIMPL sceGameUpdateInit()");
	return 0;
}

u32 sceGameUpdateTerm()
{
	ERROR_LOG(HLE, "UNIMPL sceGameUpdateTerm()");
	return 0;
}

u32 sceGameUpdateRun()
{
	ERROR_LOG(HLE, "UNIMPL sceGameUpdateRun()");
	return 0;
}

u32 sceGameUpdateAbort()
{
	ERROR_LOG(HLE, "UNIMPL sceGameUpdateAbort()");
	return 0;
}

const HLEFunction sceGameUpdate[] =
{
	{0xCBE69FB3, WrapU_V<sceGameUpdateInit>, "sceGameUpdateInit"},
	{0xBB4B68DE, WrapU_V<sceGameUpdateTerm>, "sceGameUpdateTerm"},
	{0x596AD78C, WrapU_V<sceGameUpdateRun>, "sceGameUpdateRun"},
	{0x5F5D98A6, WrapU_V<sceGameUpdateAbort>, "sceGameUpdateAbort"},
};

void Register_sceGameUpdate()
{
	RegisterModule("sceGameUpdate", ARRAY_SIZE(sceGameUpdate), sceGameUpdate);
}
