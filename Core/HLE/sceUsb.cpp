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
	ERROR_LOG(HLE, "UNIMPL sceUsbGetState");
	int state = (usbStarted ? USB_STATUS_STARTED : USB_STATUS_STOPPED)
	    | (usbConnected ? USB_STATUS_CONNECTED : USB_STATUS_DISCONNECTED)
	    | (usbActivated ? USB_STATUS_ACTIVATED : USB_STATUS_DEACTIVATED);
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

const HLEFunction sceUsbCam[] =
{
	{0X17F7B2FB, nullptr,                            "sceUsbCamSetupVideo",                     '?', ""   },
	{0XF93C4669, nullptr,                            "sceUsbCamAutoImageReverseSW",             '?', ""   },
	{0X574A8C3F, nullptr,                            "sceUsbCamStartVideo",                     '?', ""   },
	{0X6CF32CB9, nullptr,                            "sceUsbCamStopVideo",                      '?', ""   },
	{0X03ED7A82, nullptr,                            "sceUsbCamSetupMic",                       '?', ""   },
	{0X82A64030, nullptr,                            "sceUsbCamStartMic",                       '?', ""   },
	{0X7DAC0C71, nullptr,                            "sceUsbCamReadVideoFrameBlocking",         '?', ""   },
	{0X99D86281, nullptr,                            "sceUsbCamReadVideoFrame",                 '?', ""   },
	{0X41E73E95, nullptr,                            "sceUsbCamPollReadVideoFrameEnd",          '?', ""   },
	{0XF90B2293, nullptr,                            "sceUsbCamWaitReadVideoFrameEnd",          '?', ""   },
	{0X4C34F553, nullptr,                            "sceUsbCamGetLensDirection",               '?', ""   },
	{0X3F0CF289, nullptr,                            "sceUsbCamSetupStill",                     '?', ""   },
	{0X0A41A298, nullptr,                            "sceUsbCamSetupStillEx",                   '?', ""   },
	{0X61BE5CAC, nullptr,                            "sceUsbCamStillInputBlocking",             '?', ""   },
	{0XFB0A6C5D, nullptr,                            "sceUsbCamStillInput",                     '?', ""   },
	{0X7563AFA1, nullptr,                            "sceUsbCamStillWaitInputEnd",              '?', ""   },
	{0X1A46CFE7, nullptr,                            "sceUsbCamStillPollInputEnd",              '?', ""   },
	{0XA720937C, nullptr,                            "sceUsbCamStillCancelInput",               '?', ""   },
	{0XE5959C36, nullptr,                            "sceUsbCamStillGetInputLength",            '?', ""   },
	{0XCFE9E999, nullptr,                            "sceUsbCamSetupVideoEx",                   '?', ""   },
	{0XDF9D0C92, nullptr,                            "sceUsbCamGetReadVideoFrameSize",          '?', ""   },
	{0X6E205974, nullptr,                            "sceUsbCamSetSaturation",                  '?', ""   },
	{0X4F3D84D5, nullptr,                            "sceUsbCamSetBrightness",                  '?', ""   },
	{0X09C26C7E, nullptr,                            "sceUsbCamSetContrast",                    '?', ""   },
	{0X622F83CC, nullptr,                            "sceUsbCamSetSharpness",                   '?', ""   },
	{0XD4876173, nullptr,                            "sceUsbCamSetImageEffectMode",             '?', ""   },
	{0X1D686870, nullptr,                            "sceUsbCamSetEvLevel",                     '?', ""   },
	{0X951BEDF5, nullptr,                            "sceUsbCamSetReverseMode",                 '?', ""   },
	{0XC484901F, nullptr,                            "sceUsbCamSetZoom",                        '?', ""   },
	{0X383E9FA8, nullptr,                            "sceUsbCamGetSaturation",                  '?', ""   },
	{0X70F522C5, nullptr,                            "sceUsbCamGetBrightness",                  '?', ""   },
	{0XA063A957, nullptr,                            "sceUsbCamGetContrast",                    '?', ""   },
	{0XFDB68C23, nullptr,                            "sceUsbCamGetSharpness",                   '?', ""   },
	{0X994471E0, nullptr,                            "sceUsbCamGetImageEffectMode",             '?', ""   },
	{0X2BCD50C0, nullptr,                            "sceUsbCamGetEvLevel",                     '?', ""   },
	{0XD5279339, nullptr,                            "sceUsbCamGetReverseMode",                 '?', ""   },
	{0X9E8AAF8D, nullptr,                            "sceUsbCamGetZoom",                        '?', ""   },
	{0X11A1F128, nullptr,                            "sceUsbCamGetAutoImageReverseState",       '?', ""   },
	{0X08AEE98A, nullptr,                            "sceUsbCamSetMicGain",                     '?', ""   },
	{0X2E930264, nullptr,                            "sceUsbCamSetupMicEx",                     '?', ""   },
	{0X36636925, nullptr,                            "sceUsbCamReadMicBlocking",                '?', ""   },
	{0X3DC0088E, nullptr,                            "sceUsbCamReadMic",                        '?', ""   },
	{0X41EE8797, nullptr,                            "sceUsbCamUnregisterLensRotationCallback", '?', ""   },
	{0X5145868A, nullptr,                            "sceUsbCamStopMic",                        '?', ""   },
	{0X5778B452, nullptr,                            "sceUsbCamGetMicDataLength",               '?', ""   },
	{0X6784E6A8, nullptr,                            "sceUsbCamSetAntiFlicker",                 '?', ""   },
	{0XAA7D94BA, nullptr,                            "sceUsbCamGetAntiFlicker",                 '?', ""   },
	{0XB048A67D, nullptr,                            "sceUsbCamWaitReadMicEnd",                 '?', ""   },
	{0XD293A100, nullptr,                            "sceUsbCamRegisterLensRotationCallback",   '?', ""   },
	{0XF8847F60, nullptr,                            "sceUsbCamPollReadMicEnd",                 '?', ""   },
};

void Register_sceUsb()
{
	RegisterModule("sceUsbstor", ARRAY_SIZE(sceUsbstor), sceUsbstor);
	RegisterModule("sceUsbstorBoot", ARRAY_SIZE(sceUsbstorBoot), sceUsbstorBoot);
	RegisterModule("sceUsb", ARRAY_SIZE(sceUsb), sceUsb);
}

void Register_sceUsbCam()
{
	RegisterModule("sceUsbCam", ARRAY_SIZE(sceUsbCam), sceUsbCam);
}
