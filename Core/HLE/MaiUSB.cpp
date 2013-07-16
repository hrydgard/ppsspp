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
#include "Common.h"





const HLEFunction MaiUSB[] =
{
	{0x533FE3D0, 0, "MaiUSB_533FE3D0"},
	{0x720B2453, 0, "MaiUSB_720B2453"},
	{0x37125015, 0, "MaiUSB_37125015"},
	{0xA35DE087, 0, "MaiUSB_A35DE087"},
	{0x67D8CCF2, 0, "MaiUSB_67D8CCF2"},

};

void Register_MaiUSB()
{
	RegisterModule("MaiUSB", ARRAY_SIZE(MaiUSB), MaiUSB);
}
