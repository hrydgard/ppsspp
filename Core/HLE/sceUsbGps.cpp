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
#include "Core/HLE/sceUsbGps.h"

enum UsbStatus {
	GPS_STATE_OFF = 0,
	GPS_STATE_ACTIVATING1 = 1,
	GPS_STATE_ACTIVATING2 = 2,
	GPS_STATE_ON = 3,
};

static int sceUsbGpsGetState(u32 stateAddr) {
	ERROR_LOG(HLE, "UNIMPL sceUsbGpsGetData(%08x)", stateAddr);
	return 0;
}

static int sceUsbGpsOpen() {
	ERROR_LOG(HLE, "UNIMPL sceUsbGpsOpen");
	return 0;
}

const HLEFunction sceUsbGps[] =
{
	{0X268F95CA, nullptr,                            "sceUsbGpsSetInitDataLocation",  '?', "" },
	{0X31F95CDE, nullptr,                            "sceUsbGpsGetPowerSaveMode",     '?', "" },
	{0X54D26AA4, nullptr,                            "sceUsbGpsGetInitDataLocation",  '?', "" },
	{0X63D1F89D, nullptr,                            "sceUsbGpsResetInitialPosition", '?', "" },
	{0X69E4AAA8, nullptr,                            "sceUsbGpsSaveInitData",         '?', "" },
	{0X6EED4811, nullptr,                            "sceUsbGpsClose",                '?', "" },
	{0X7C16AC3A, &WrapI_U<sceUsbGpsGetState>,        "sceUsbGpsGetState",             'i', "x"},
	{0X934EC2B2, nullptr,                            "sceUsbGpsGetData",              '?', "" },
	{0X9D8F99E8, nullptr,                            "sceUsbGpsSetPowerSaveMode",     '?', "" },
	{0X9F267D34, &WrapI_V<sceUsbGpsOpen>,            "sceUsbGpsOpen",                 'i', "" },
	{0XA259CD67, nullptr,                            "sceUsbGpsReset",                '?', "" },
};

void Register_sceUsbGps()
{
	RegisterModule("sceUsbGps", ARRAY_SIZE(sceUsbGps), sceUsbGps);
}
