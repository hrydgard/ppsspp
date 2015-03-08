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

const HLEFunction sceG729[] =
{
	{ 0x13F1028A, 0, "sceG729DecodeExit" },
	{ 0x17C11696, 0, "sceG729DecodeInitResource" },
	{ 0x3489D1F3, 0, "sceG729DecodeCore" },
	{ 0x55E14F75, 0, "sceG729DecodeInit" },
	{ 0x5A409D1B, 0, "sceG729EncodeExit" },
	{ 0x74804D93, 0, "sceG729DecodeReset" },
	{ 0x890B86AE, 0, "sceG729DecodeTermResource" },
	{ 0x8C87A2CA, 0, "sceG729EncodeReset" },
	{ 0x94714D50, 0, "sceG729EncodeTermResource" },
	{ 0xAA1E5462, 0, "sceG729EncodeInitResource" },
	{ 0xCFCD367C, 0, "sceG729EncodeInit" },
	{ 0xDB7259D5, 0, "sceG729EncodeCore" },
};

void Register_sceG729()
{
	RegisterModule("sceG729", ARRAY_SIZE(sceG729), sceG729);
}
