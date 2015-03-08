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

const HLEFunction sceUsbCam[] =
{
	{ 0x17F7B2FB, 0, "sceUsbCamSetupVideo" },
	{ 0xF93C4669, 0, "sceUsbCamAutoImageReverseSW" },
	{ 0x574A8C3F, 0, "sceUsbCamStartVideo" },
	{ 0x6CF32CB9, 0, "sceUsbCamStopVideo" },
	{ 0x03ED7A82, 0, "sceUsbCamSetupMic" },
	{ 0x82A64030, 0, "sceUsbCamStartMic" },
	{ 0x7DAC0C71, 0, "sceUsbCamReadVideoFrameBlocking" },
	{ 0x99D86281, 0, "sceUsbCamReadVideoFrame" },
	{ 0x41E73E95, 0, "sceUsbCamPollReadVideoFrameEnd" },
	{ 0xF90B2293, 0, "sceUsbCamWaitReadVideoFrameEnd" },
	{ 0x4C34F553, 0, "sceUsbCamGetLensDirection" },
	{ 0x3F0CF289, 0, "sceUsbCamSetupStill" },
	{ 0x0A41A298, 0, "sceUsbCamSetupStillEx" },
	{ 0x61BE5CAC, 0, "sceUsbCamStillInputBlocking" },
	{ 0xFB0A6C5D, 0, "sceUsbCamStillInput" },
	{ 0x7563AFA1, 0, "sceUsbCamStillWaitInputEnd" },
	{ 0x1A46CFE7, 0, "sceUsbCamStillPollInputEnd" },
	{ 0xA720937C, 0, "sceUsbCamStillCancelInput" },
	{ 0xE5959C36, 0, "sceUsbCamStillGetInputLength" },
	{ 0xCFE9E999, 0, "sceUsbCamSetupVideoEx" },
	{ 0xDF9D0C92, 0, "sceUsbCamGetReadVideoFrameSize" },
	{ 0x6E205974, 0, "sceUsbCamSetSaturation" },
	{ 0x4F3D84D5, 0, "sceUsbCamSetBrightness" },
	{ 0x09C26C7E, 0, "sceUsbCamSetContrast" },
	{ 0x622F83CC, 0, "sceUsbCamSetSharpness" },
	{ 0xD4876173, 0, "sceUsbCamSetImageEffectMode" },
	{ 0x1D686870, 0, "sceUsbCamSetEvLevel" },
	{ 0x951BEDF5, 0, "sceUsbCamSetReverseMode" },
	{ 0xC484901F, 0, "sceUsbCamSetZoom" },
	{ 0x383E9FA8, 0, "sceUsbCamGetSaturation" },
	{ 0x70F522C5, 0, "sceUsbCamGetBrightness" },
	{ 0xA063A957, 0, "sceUsbCamGetContrast" },
	{ 0xFDB68C23, 0, "sceUsbCamGetSharpness" },
	{ 0x994471E0, 0, "sceUsbCamGetImageEffectMode" },
	{ 0x2BCD50C0, 0, "sceUsbCamGetEvLevel" },
	{ 0xD5279339, 0, "sceUsbCamGetReverseMode" },
	{ 0x9E8AAF8D, 0, "sceUsbCamGetZoom" },
	{ 0x11A1F128, 0, "sceUsbCamGetAutoImageReverseState" },
	{ 0x08AEE98A, 0, "sceUsbCamSetMicGain" },
	{ 0x2E930264, 0, "sceUsbCamSetupMicEx" },
	{ 0x36636925, 0, "sceUsbCamReadMicBlocking" },
	{ 0x3DC0088E, 0, "sceUsbCamReadMic" },
	{ 0x41EE8797, 0, "sceUsbCamUnregisterLensRotationCallback" },
	{ 0x5145868A, 0, "sceUsbCamStopMic" },
	{ 0x5778B452, 0, "sceUsbCamGetMicDataLength" },
	{ 0x6784E6A8, 0, "sceUsbCamSetAntiFlicker" },
	{ 0xAA7D94BA, 0, "sceUsbCamGetAntiFlicker" },
	{ 0xB048A67D, 0, "sceUsbCamWaitReadMicEnd" },
	{ 0xD293A100, 0, "sceUsbCamRegisterLensRotationCallback" },
	{ 0xF8847F60, 0, "sceUsbCamPollReadMicEnd" },
};

const HLEFunction sceG729[] =
{
	{ 0x13f1028a, 0, "sceUsbstorBootSetCapacity" },
	{ 0x17c11696, 0, "sceUsbstorBootSetLoadAddr" },
	{ 0x3489d1f3, 0, "sceUsbstorBootGetDataSize" },
	{ 0x5a409d1b, 0, "sceUsbstorBootSetStatus" },
	{ 0x594BBF95, 0, "sceUsbstorBootRegisterNotify" },
	{ 0x594BBF95, 0, "sceUsbstorBootUnregisterNotify" },
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
