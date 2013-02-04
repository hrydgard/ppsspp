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
#include "../MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "ChunkFile.h"
#include "sceUsb.h"

bool usbActivated = false;

void __UsbInit()
{
	usbActivated = false;
}

void __UsbDoState(PointerWrap &p)
{
	p.Do(usbActivated);
	p.DoMarker("sceUsb");
}

u32 sceUsbActivate() {
	ERROR_LOG(HLE, "UNIMPL sceUsbActivate");
	usbActivated = true;
	return 0;
}

const HLEFunction sceUsb[] =
{
	{0xae5de6af, 0, "sceUsbStart"},
	{0xc2464fa0, 0, "sceUsbStop"},
	{0xc21645a4, 0, "sceUsbGetState"},
	{0x4e537366, 0, "sceUsbGetDrvList"},
	{0x112cc951, 0, "sceUsbGetDrvState"},
	{0x586db82c, WrapU_V<sceUsbActivate>, "sceUsbActivate"},
	{0xc572a9c8, 0, "sceUsbDeactivate"},
	{0x5be0e002, 0, "sceUsbWaitState"},
	{0x1c360735, 0, "sceUsbWaitCancel"},
};

const HLEFunction sceUsbstor[] =
{
	{0x60066CFE, 0, "sceUsbstorGetStatus"},
};

const HLEFunction sceUsbstorBoot[] =
{
	{0xE58818A8, 0, "sceUsbstorBootSetCapacity"},
	{0x594BBF95, 0, "sceUsbstorBootSetLoadAddr"},
	{0x6D865ECD, 0, "sceUsbstorBootGetDataSize"},
	{0xA1119F0D, 0, "sceUsbstorBootSetStatus"},
	{0x1F080078, 0, "sceUsbstorBootRegisterNotify"},
	{0xA55C9E16, 0, "sceUsbstorBootUnregisterNotify"},
};

void Register_sceUsb()
{
	RegisterModule("sceUsbstor", ARRAY_SIZE(sceUsbstor), sceUsbstor);
	RegisterModule("sceUsbstorBoot", ARRAY_SIZE(sceUsbstorBoot), sceUsbstorBoot);
	RegisterModule("sceUsb", ARRAY_SIZE(sceUsb), sceUsb);
}
