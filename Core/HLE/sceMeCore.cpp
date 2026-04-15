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

static u32 sceMeBootStartStub(u32 arg) { return 0; }

const HLEFunction sceMeCore_driver[] = {
	{0X47DB48C2, &WrapU_U<sceMeBootStartStub>, "sceMeBootStart",    'x', "x" },
	{0XC287AD90, &WrapU_U<sceMeBootStartStub>, "sceMeBootStart371", 'x', "x" },
	{0XD857CF93, &WrapU_U<sceMeBootStartStub>, "sceMeBootStart380", 'x', "x" },
	{0X8988AD49, &WrapU_U<sceMeBootStartStub>, "sceMeBootStart395", 'x', "x" },
	{0X051C1601, &WrapU_U<sceMeBootStartStub>, "sceMeBootStart500", 'x', "x" },
	{0X3A2E60BB, &WrapU_U<sceMeBootStartStub>, "sceMeBootStart620", 'x', "x" },
	{0X99E4DBFA, &WrapU_U<sceMeBootStartStub>, "sceMeBootStart635", 'x', "x" },
	{0X5DFF5C50, &WrapU_U<sceMeBootStartStub>, "sceMeBootStart660", 'x', "x" },
};

void Register_sceMeCore_driver() {
	RegisterHLEModule("sceMeCore_driver", ARRAY_SIZE(sceMeCore_driver), sceMeCore_driver);
}
