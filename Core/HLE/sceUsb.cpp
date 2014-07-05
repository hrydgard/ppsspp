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
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Common/ChunkFile.h"
#include "Core/HLE/sceUsb.h"

// TODO: Map by driver name
bool usbStarted = false;
// TODO: Check actual status
bool usbConnected = true;
// TODO: Activation by product id
bool usbActivated = false;

enum UsbStatus {
	USB_STATUS_STOPPED      = 0x001,
	USB_STATUS_STARTED      = 0x002,
	USB_STATUS_DISCONNECTED = 0x010,
	USB_STATUS_CONNECTED    = 0x020,
	USB_STATUS_DEACTIVATED  = 0x100,
	USB_STATUS_ACTIVATED    = 0x200,
};

void __UsbInit()
{
	usbStarted = false;
	usbConnected = true;
	usbActivated = false;
}

void __UsbDoState(PointerWrap &p)
{
	auto s = p.Section("sceUsb", 1, 2);
	if (!s)
		return;

	p.Do(usbStarted);
	p.Do(usbConnected);
	p.Do(usbActivated);
}

int sceUsbStart(const char* driverName, u32 argsSize, u32 argsPtr) {
	ERROR_LOG(HLE, "UNIMPL sceUsbStart(%s, %i, %08x)", driverName, argsSize, argsPtr);
	usbStarted = true;
	return 0;
}

int sceUsbStop(const char* driverName, u32 argsSize, u32 argsPtr) {
	ERROR_LOG(HLE, "UNIMPL sceUsbStop(%s, %i, %08x)", driverName, argsSize, argsPtr);
	usbStarted = false;
	return 0;
}

int sceUsbGetState() {
	ERROR_LOG(HLE, "UNIMPL sceUsbGetState");
	int state = (usbStarted ? USB_STATUS_STARTED : USB_STATUS_STOPPED)
	    | (usbConnected ? USB_STATUS_CONNECTED : USB_STATUS_DISCONNECTED)
	    | (usbActivated ? USB_STATUS_ACTIVATED : USB_STATUS_DEACTIVATED);
	return state;
}

int sceUsbActivate(u32 pid) {
	ERROR_LOG(HLE, "UNIMPL sceUsbActivate(%i)", pid);
	usbActivated = true;
	return 0;
}

int sceUsbDeactivate(u32 pid) {
	ERROR_LOG(HLE, "UNIMPL sceUsbDeactivate(%i)", pid);
	usbActivated = false;
	return 0;
}

const HLEFunction sceUsb[] =
{
	{0xae5de6af, WrapI_CUU<sceUsbStart>,     "sceUsbStart"},
	{0xc2464fa0, WrapI_CUU<sceUsbStop>,      "sceUsbStop"},
	{0xc21645a4, WrapI_V<sceUsbGetState>,    "sceUsbGetState"},
	{0x4e537366, 0, "sceUsbGetDrvList"},
	{0x112cc951, 0, "sceUsbGetDrvState"},
	{0x586db82c, WrapI_U<sceUsbActivate>,    "sceUsbActivate"},
	{0xc572a9c8, WrapI_U<sceUsbDeactivate>,  "sceUsbDeactivate"},
	{0x5be0e002, 0, "sceUsbWaitState"},
	{0x616f2b61, 0, "sceUsbWaitStateCB"},
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
