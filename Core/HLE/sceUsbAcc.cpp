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
#include "Core/HLE/sceUsbAcc.h"

// Assuming https://github.com/joel16/usbacc/blob/master/usbacc.c is correct
// sceUsbAccGetAuthStat should return 0 or one of the two errors on failure
// sceUsbAccGetInfo should return 0 or one of the 3 errors on failure
// we don't have to deal with physical Usb connection, so let's just always pass

static int sceUsbAccGetAuthStat() {
	INFO_LOG(Log::HLE, "UNIMPL sceUsbAccGetAuthStat");
	return 0;
}

static int sceUsbAccGetInfo(u32 addr) {
	INFO_LOG(Log::HLE, "UNIMPL sceUsbAccGetInfo");
	return 0;
}

const HLEFunction sceUsbAcc[] =
{
	{0X79A1C743, &WrapI_V<sceUsbAccGetAuthStat>,     "sceUsbAccGetAuthStat",                    'i', ""   },
	{0X0CD7D4AA, &WrapI_U<sceUsbAccGetInfo>,         "sceUsbAccGetInfo",                        'i', "x" },
};

void Register_sceUsbAcc()
{
	RegisterModule("sceUsbAcc", ARRAY_SIZE(sceUsbAcc), sceUsbAcc);
}
