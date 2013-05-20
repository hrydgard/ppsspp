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

#include "sceParseUri.h"

const HLEFunction sceParseUri[] =
{
	{0x49E950EC, 0, "sceUriEscape"},
	{0x062BB07E, 0, "sceUriUnescape"},
	{0x568518C9, 0, "sceUriParse"},
	{0x7EE318AF, 0, "sceUriBuild"},
};

void Register_sceParseUri()
{
	RegisterModule("sceParseUri", ARRAY_SIZE(sceParseUri), sceParseUri);
}