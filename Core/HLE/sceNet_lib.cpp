// Copyright (c) 2025- PPSSPP Project.

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


#include "Core/HLE/sceNet_lib.h"

#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/HLE.h"



const HLEFunction sceNet_lib[] = {
	{0X1858883D, nullptr,           "sceNetRand",                  'i', ""       },
	{0X2A73ADDC, nullptr,           "sceNetStrtoul",               'i', ""       },
	{0X4753D878, nullptr,           "sceNetMemmove",               'i', ""       },
	{0X80C9F02A, nullptr,           "sceNetStrcpy",                'i', ""       },
	{0X94DCA9F0, nullptr,           "sceNetStrncmp",               'i', ""       },
	{0X9CFBC7E3, nullptr,           "sceNetStrcasecmp",            'i', ""       },
	{0XA0F16ABD, nullptr,           "sceNetStrcmp",                'i', ""       },
	{0XB5CE388A, nullptr,           "sceNetStrncpy",               'i', ""       },
	{0XBCBE14CF, nullptr,           "sceNetStrchr",                'i', ""       },
	{0XCF705E46, nullptr,           "sceNetSprintf",               'i', ""       },
	{0XD8722983, nullptr,           "sceNetStrlen",                'i', ""       },
	{0XE0A81C7C, nullptr,           "sceNetMemcmp",                'i', ""       },
};


void Register_sceNet_lib() {
	RegisterModule("sceNet_lib", ARRAY_SIZE(sceNet_lib), sceNet_lib);
}
