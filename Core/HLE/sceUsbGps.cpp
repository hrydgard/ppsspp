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
#include "Core/HLE/sceUsbGps.h"

enum UsbStatus {
	GPS_STATE_OFF = 0,
	GPS_STATE_ACTIVATING1 = 1,
	GPS_STATE_ACTIVATING2 = 2,
	GPS_STATE_ON = 3,
};

const HLEFunction sceUsbGps[] =
{
	{0x268F95CA, 0, "sceUsbGpsSetInitDataLocation"},
	{0x31F95CDE, 0, "sceUsbGpsGetPowerSaveMode"},
	{0x54D26AA4, 0, "sceUsbGpsGetInitDataLocation"},
	{0x63D1F89D, 0, "sceUsbGpsResetInitialPosition"},
	{0x69E4AAA8, 0, "sceUsbGpsSaveInitData"},
	{0x6EED4811, 0, "sceUsbGpsClose"},
	{0x7C16AC3A, 0, "sceUsbGpsGetState"},
	{0x934EC2B2, 0, "sceUsbGpsGetData"},
	{0x9D8F99E8, 0, "sceUsbGpsSetPowerSaveMode"},
	{0x9F267D34, 0, "sceUsbGpsOpen"},
	{0xA259CD67, 0, "sceUsbGpsReset"},
};

void Register_sceUsbGps()
{
	RegisterModule("sceUsbGps", ARRAY_SIZE(sceUsbGps), sceUsbGps);
}
