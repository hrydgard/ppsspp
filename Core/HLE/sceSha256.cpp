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
#include "Core/HLE/sceSha256.h"

int sceSha256_unknow(u32 unknow1, int unknow2, u32 unknow3) {
	ERROR_LOG(HLE, "UNIMPL sceSha256_unknow (%08x,%08x,%08x)", unknow1, unknow2, unknow3);
	return 0;
}

const HLEFunction sceSha256[] =
{
	{ 0x318A350C, WrapI_UIU<sceSha256_unknow>, "sceSha256_unknow" },
};

void Register_sceSha256()
{
	RegisterModule("sceSha256", ARRAY_SIZE(sceSha256), sceSha256);
}
