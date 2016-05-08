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
	{0X13F1028A, nullptr,                            "sceG729DecodeExit",         '?', ""},
	{0X17C11696, nullptr,                            "sceG729DecodeInitResource", '?', ""},
	{0X3489D1F3, nullptr,                            "sceG729DecodeCore",         '?', ""},
	{0X55E14F75, nullptr,                            "sceG729DecodeInit",         '?', ""},
	{0X5A409D1B, nullptr,                            "sceG729EncodeExit",         '?', ""},
	{0X74804D93, nullptr,                            "sceG729DecodeReset",        '?', ""},
	{0X890B86AE, nullptr,                            "sceG729DecodeTermResource", '?', ""},
	{0X8C87A2CA, nullptr,                            "sceG729EncodeReset",        '?', ""},
	{0X94714D50, nullptr,                            "sceG729EncodeTermResource", '?', ""},
	{0XAA1E5462, nullptr,                            "sceG729EncodeInitResource", '?', ""},
	{0XCFCD367C, nullptr,                            "sceG729EncodeInit",         '?', ""},
	{0XDB7259D5, nullptr,                            "sceG729EncodeCore",         '?', ""},
};

void Register_sceG729()
{
	RegisterModule("sceG729", ARRAY_SIZE(sceG729), sceG729);
}
