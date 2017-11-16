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

#include "base/NativeApp.h"
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

	if (s >= 2) {
		p.Do(usbStarted);
		p.Do(usbConnected);
	} else {
		usbStarted = false;
		usbConnected = true;
	}
	p.Do(usbActivated);
}

static int sceUsbStart(const char* driverName, u32 argsSize, u32 argsPtr) {
	ERROR_LOG(HLE, "UNIMPL sceUsbStart(%s, %i, %08x)", driverName, argsSize, argsPtr);
	usbStarted = true;
	return 0;
}

static int sceUsbStop(const char* driverName, u32 argsSize, u32 argsPtr) {
	ERROR_LOG(HLE, "UNIMPL sceUsbStop(%s, %i, %08x)", driverName, argsSize, argsPtr);
	usbStarted = false;
	return 0;
}

static int sceUsbGetState() {
	int state = 0;
	if (!usbStarted) {
		state = 0x80243007;
	} else {
		state = USB_STATUS_STARTED
			| (usbConnected ? USB_STATUS_CONNECTED : USB_STATUS_DISCONNECTED)
			| (usbActivated ? USB_STATUS_ACTIVATED : USB_STATUS_DEACTIVATED);
	}
	ERROR_LOG(HLE, "UNIMPL sceUsbGetState: 0x%x", state);
	return state;
}

static int sceUsbActivate(u32 pid) {
	ERROR_LOG(HLE, "UNIMPL sceUsbActivate(%i)", pid);
	usbActivated = true;
	return 0;
}

static int sceUsbDeactivate(u32 pid) {
	ERROR_LOG(HLE, "UNIMPL sceUsbDeactivate(%i)", pid);
	usbActivated = false;
	return 0;
}

const HLEFunction sceUsb[] =
{
	{0XAE5DE6AF, &WrapI_CUU<sceUsbStart>,            "sceUsbStart",                             'i', "sxx"},
	{0XC2464FA0, &WrapI_CUU<sceUsbStop>,             "sceUsbStop",                              'i', "sxx"},
	{0XC21645A4, &WrapI_V<sceUsbGetState>,           "sceUsbGetState",                          'i', ""   },
	{0X4E537366, nullptr,                            "sceUsbGetDrvList",                        '?', ""   },
	{0X112CC951, nullptr,                            "sceUsbGetDrvState",                       '?', ""   },
	{0X586DB82C, &WrapI_U<sceUsbActivate>,           "sceUsbActivate",                          'i', "x"  },
	{0XC572A9C8, &WrapI_U<sceUsbDeactivate>,         "sceUsbDeactivate",                        'i', "x"  },
	{0X5BE0E002, nullptr,                            "sceUsbWaitState",                         '?', ""   },
	{0X616F2B61, nullptr,                            "sceUsbWaitStateCB",                       '?', ""   },
	{0X1C360735, nullptr,                            "sceUsbWaitCancel",                        '?', ""   },
};

const HLEFunction sceUsbstor[] =
{
	{0X60066CFE, nullptr,                            "sceUsbstorGetStatus",                     '?', ""   },
};

const HLEFunction sceUsbstorBoot[] =
{
	{0XE58818A8, nullptr,                            "sceUsbstorBootSetCapacity",               '?', ""   },
	{0X594BBF95, nullptr,                            "sceUsbstorBootSetLoadAddr",               '?', ""   },
	{0X6D865ECD, nullptr,                            "sceUsbstorBootGetDataSize",               '?', ""   },
	{0XA1119F0D, nullptr,                            "sceUsbstorBootSetStatus",                 '?', ""   },
	{0X1F080078, nullptr,                            "sceUsbstorBootRegisterNotify",            '?', ""   },
	{0XA55C9E16, nullptr,                            "sceUsbstorBootUnregisterNotify",          '?', ""   },
};

void Register_sceUsb()
{
	RegisterModule("sceUsbstor", ARRAY_SIZE(sceUsbstor), sceUsbstor);
	RegisterModule("sceUsbstorBoot", ARRAY_SIZE(sceUsbstorBoot), sceUsbstorBoot);
	RegisterModule("sceUsb", ARRAY_SIZE(sceUsb), sceUsb);
}
