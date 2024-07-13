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
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceGameUpdate.h"

static u32 sceGameUpdateInit()
{
	ERROR_LOG(Log::sceUtility, "UNIMPL sceGameUpdateInit()");
	return 0;
}

static u32 sceGameUpdateTerm()
{
	ERROR_LOG(Log::sceUtility, "UNIMPL sceGameUpdateTerm()");
	return 0;
}

static u32 sceGameUpdateRun()
{
	ERROR_LOG(Log::sceUtility, "UNIMPL sceGameUpdateRun()");
	return 0;
}

static u32 sceGameUpdateAbort()
{
	ERROR_LOG(Log::sceUtility, "UNIMPL sceGameUpdateAbort()");
	return 0;
}

const HLEFunction sceGameUpdate[] =
{
	{0XCBE69FB3, &WrapU_V<sceGameUpdateInit>,        "sceGameUpdateInit",  'x', ""},
	{0XBB4B68DE, &WrapU_V<sceGameUpdateTerm>,        "sceGameUpdateTerm",  'x', ""},
	{0X596AD78C, &WrapU_V<sceGameUpdateRun>,         "sceGameUpdateRun",   'x', ""},
	{0X5F5D98A6, &WrapU_V<sceGameUpdateAbort>,       "sceGameUpdateAbort", 'x', ""},
};

void Register_sceGameUpdate()
{
	RegisterModule("sceGameUpdate", ARRAY_SIZE(sceGameUpdate), sceGameUpdate);
}
