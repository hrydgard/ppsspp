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

#include "stdafx.h"
#include <initguid.h>
#include <cstddef>
#include <limits.h>
#include <algorithm>
#include <mmsystem.h>
#include <XInput.h>
#include <wrl/client.h>

#include <wbemidl.h>
#include <comdef.h>
#include <set>
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/StringUtils.h"
#include "Common/System/NativeApp.h"
#include "Core/KeyMap.h"
#include "Windows/DinputDevice.h"
#include "Windows/Hid/HidInputDevice.h"

#pragma comment(lib,"dinput8.lib")

using Microsoft::WRL::ComPtr;

// static members of DinputDevice
unsigned int                  DinputDevice::pInstances = 0;
Microsoft::WRL::ComPtr<IDirectInput8> DinputDevice::pDI;
std::vector<DIDEVICEINSTANCE> DinputDevice::devices;
std::set<u32> DinputDevice::ignoreDevices_;

bool DinputDevice::needsCheck_ = true;

// In order from 0.  There can be 128, but most controllers do not have that many.
static const InputKeyCode dinput_buttons[] = {
	NKCODE_BUTTON_1,
	NKCODE_BUTTON_2,
	NKCODE_BUTTON_3,
	NKCODE_BUTTON_4,
	NKCODE_BUTTON_5,
	NKCODE_BUTTON_6,
	NKCODE_BUTTON_7,
	NKCODE_BUTTON_8,
	NKCODE_BUTTON_9,
	NKCODE_BUTTON_10,
	NKCODE_BUTTON_11,
	NKCODE_BUTTON_12,
	NKCODE_BUTTON_13,
	NKCODE_BUTTON_14,
	NKCODE_BUTTON_15,
	NKCODE_BUTTON_16,
};

#define DIFF  (JOY_POVRIGHT - JOY_POVFORWARD) / 2
#define JOY_POVFORWARD_RIGHT	JOY_POVFORWARD + DIFF
#define JOY_POVRIGHT_BACKWARD	JOY_POVRIGHT + DIFF
#define JOY_POVBACKWARD_LEFT	JOY_POVBACKWARD + DIFF
#define JOY_POVLEFT_FORWARD		JOY_POVLEFT + DIFF

static std::set<u32> DetectXInputVIDPIDs();

LPDIRECTINPUT8 DinputDevice::getPDI()
{
	if (pDI == nullptr)
	{
		if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&pDI, NULL)))
		{
			pDI = nullptr;
		}
	}
	return pDI.Get();
}

BOOL CALLBACK DinputDevice::DevicesCallback(LPCDIDEVICEINSTANCE lpddi, LPVOID pvRef) {
	//check if a device with the same Instance guid is already saved
	auto res = std::find_if(devices.begin(), devices.end(), 
		[lpddi](const DIDEVICEINSTANCE &to_consider){
			return lpddi->guidInstance == to_consider.guidInstance;
		});
	if (res == devices.end()) {
		// not yet in the devices list
		// Ignore if device supports XInput - we'll get the input through there instead.
		const u32 vidpid = lpddi->guidProduct.Data1;
		const bool isXinputDevice = ignoreDevices_.find(vidpid) != ignoreDevices_.end();
		if (!isXinputDevice) {
			devices.push_back(*lpddi);
		}
	}
	return DIENUM_CONTINUE;
}

void DinputDevice::getDevices(bool refresh) {
	if (refresh) {
		// We don't want duplicate reporting from XInput devices through DInput.
		ignoreDevices_ = DetectXInputVIDPIDs();
		HidInputDevice::AddSupportedDevices(&ignoreDevices_);
		getPDI()->EnumDevices(DI8DEVCLASS_GAMECTRL, &DinputDevice::DevicesCallback, NULL, DIEDFL_ATTACHEDONLY);
	}
}

DinputDevice::DinputDevice(int devnum) {
	pInstances++;
	pDevNum = devnum;
	pJoystick = nullptr;
	last_lX_ = 0;
	last_lY_ = 0;
	last_lZ_ = 0;
	last_lRx_ = 0;
	last_lRy_ = 0;
	last_lRz_ = 0;

	if (!getPDI()) {
		return;
	}

	if (devnum >= MAX_NUM_PADS) {
		return;
	}

	getDevices(needsCheck_);
	if ( (devnum >= (int)devices.size()) || FAILED(getPDI()->CreateDevice(devices.at(devnum).guidInstance, &pJoystick, NULL)))
	{
		return;
	}

	wchar_t guid[64];
	if (StringFromGUID2(devices.at(devnum).guidProduct, guid, ARRAY_SIZE(guid)) != 0) {
		KeyMap::NotifyPadConnected(DEVICE_ID_PAD_0 + pDevNum, StringFromFormat("%S: %S", devices.at(devnum).tszProductName, guid));
	}

	if (FAILED(pJoystick->SetDataFormat(&c_dfDIJoystick2))) {
		pJoystick = nullptr;
		return;
	}

	DIPROPRANGE diprg; 
	diprg.diph.dwSize       = sizeof(DIPROPRANGE); 
	diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	diprg.diph.dwHow        = DIPH_DEVICE; 
	diprg.diph.dwObj        = 0;
	diprg.lMin              = -10000; 
	diprg.lMax              = 10000;

	analog = FAILED(pJoystick->SetProperty(DIPROP_RANGE, &diprg.diph)) ? false : true;

	// Other devices suffer if the deadzone is not set. 
	// TODO: The dead zone will be made configurable in the Control dialog.
	DIPROPDWORD dipw;
	dipw.diph.dwSize       = sizeof(DIPROPDWORD);
	dipw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipw.diph.dwHow        = DIPH_DEVICE;
	dipw.diph.dwObj        = 0;
	// dwData 10000 is deadzone(0% - 100%), multiply by config scalar
	dipw.dwData            = 0;

	analog |= FAILED(pJoystick->SetProperty(DIPROP_DEADZONE, &dipw.diph)) ? false : true;
}

DinputDevice::~DinputDevice() {
	KeyMap::NotifyPadDisconnected(DEVICE_ID_PAD_0 + pDevNum);
	ReleaseAllKeys();

	if (pJoystick) {
		pJoystick = nullptr;
	}

	pInstances--;

	//the whole instance counter is obviously highly thread-unsafe
	//but I don't think creation and destruction operations will be
	//happening at the same time and other values like pDI are
	//unsafe as well anyway
	if (pInstances == 0 && pDI) {
		pDI = nullptr;
	}
}

void DinputDevice::ReleaseAllKeys() {
	KeyInput key;
	key.deviceId = DEVICE_ID_PAD_0 + pDevNum;
	key.flags = KeyInputFlags::UP;
	for (int i = 0; i < ARRAY_SIZE(dinput_buttons); ++i) {
		if (lastButtons_[i] != 0) {
			key.keyCode = dinput_buttons[i];
			NativeKey(key);
			lastButtons_[i] = 0;
		}
	}

	// Release DPad
	static const InputKeyCode dpadCodes[] = {
		NKCODE_DPAD_UP,
		NKCODE_DPAD_DOWN,
		NKCODE_DPAD_LEFT,
		NKCODE_DPAD_RIGHT
	};
	for (int i = 0; i < ARRAY_SIZE(dpadCodes); ++i) {
		key.keyCode = dpadCodes[i];
		NativeKey(key);
	}

	// Release axes
	static const InputAxis axes[] = {
		JOYSTICK_AXIS_X,
		JOYSTICK_AXIS_Y,
		JOYSTICK_AXIS_Z,
		JOYSTICK_AXIS_RX,
		JOYSTICK_AXIS_RY,
		JOYSTICK_AXIS_RZ
	};
	for (int i = 0; i < ARRAY_SIZE(axes); ++i) {
		AxisInput axis;
		axis.deviceId = DEVICE_ID_PAD_0 + pDevNum;
		axis.axisId = axes[i];
		axis.value = 0.0f;
		NativeAxis(&axis, 1);
	}
}

void SendNativeAxis(InputDeviceID deviceId, int value, int &lastValue, InputAxis axisId) {
	if (value != lastValue) {
		AxisInput axis;
		axis.deviceId = deviceId;
		axis.axisId = axisId;
		axis.value = (float)value * (1.0f / 10000.0f); // Convert axis to normalised float
		NativeAxis(&axis, 1);
	}
	lastValue = value;
}

static LONG *ValueForAxisId(DIJOYSTATE2 &js, int axisId) {
	switch (axisId) {
	case JOYSTICK_AXIS_X: return &js.lX;
	case JOYSTICK_AXIS_Y: return &js.lY;
	case JOYSTICK_AXIS_Z: return &js.lZ;
	case JOYSTICK_AXIS_RX: return &js.lRx;
	case JOYSTICK_AXIS_RY: return &js.lRy;
	case JOYSTICK_AXIS_RZ: return &js.lRz;
	default: return nullptr;
	}
}

int DinputDevice::UpdateState() {
	if (!pJoystick) return -1;

	DIJOYSTATE2 js;

	if (FAILED(pJoystick->Poll())) {
		if(pJoystick->Acquire() == DIERR_INPUTLOST)
			return -1;
	}

	if(FAILED(pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js)))
		return -1;

	ApplyButtons(js);

	if (analog)	{
		// TODO: Use the batched interface.
		AxisInput axis;
		axis.deviceId = DEVICE_ID_PAD_0 + pDevNum;

		SendNativeAxis(DEVICE_ID_PAD_0 + pDevNum, js.lX, last_lX_, JOYSTICK_AXIS_X);
		SendNativeAxis(DEVICE_ID_PAD_0 + pDevNum, js.lY, last_lY_, JOYSTICK_AXIS_Y);
		SendNativeAxis(DEVICE_ID_PAD_0 + pDevNum, js.lZ, last_lZ_, JOYSTICK_AXIS_Z);
		SendNativeAxis(DEVICE_ID_PAD_0 + pDevNum, js.lRx, last_lRx_, JOYSTICK_AXIS_RX);
		SendNativeAxis(DEVICE_ID_PAD_0 + pDevNum, js.lRy, last_lRy_, JOYSTICK_AXIS_RY);
		SendNativeAxis(DEVICE_ID_PAD_0 + pDevNum, js.lRz, last_lRz_, JOYSTICK_AXIS_RZ);
	}

	//check if the values have changed from last time and skip polling the rest of the dinput devices if they did
	//this doesn't seem to quite work if only the axis have changed
	if ((memcmp(js.rgbButtons, pPrevState.rgbButtons, sizeof(BYTE) * 128) != 0)
		|| (memcmp(js.rgdwPOV, pPrevState.rgdwPOV, sizeof(DWORD) * 4) != 0)
		|| js.lVX != 0 || js.lVY != 0 || js.lVZ != 0 || js.lVRx != 0 || js.lVRy != 0 || js.lVRz != 0)
	{
		pPrevState = js;
		return InputDevice::UPDATESTATE_SKIP_PAD;
	}
	return -1;
}

void DinputDevice::ApplyButtons(DIJOYSTATE2 &state) {
	BYTE *buttons = state.rgbButtons;
	u32 downMask = 0x80;

	for (int i = 0; i < ARRAY_SIZE(dinput_buttons); ++i) {
		if (state.rgbButtons[i] == lastButtons_[i]) {
			continue;
		}

		bool down = (state.rgbButtons[i] & downMask) == downMask;
		KeyInput key;
		key.deviceId = DEVICE_ID_PAD_0 + pDevNum;
		key.flags = down ? KeyInputFlags::DOWN : KeyInputFlags::UP;
		key.keyCode = dinput_buttons[i];
		NativeKey(key);

		lastButtons_[i] = state.rgbButtons[i];
	}

	// Now the POV hat, which can technically go in any degree but usually does not.
	if (LOWORD(state.rgdwPOV[0]) != lastPOV_[0]) {
		KeyInput dpad[4]{};
		for (int i = 0; i < 4; ++i) {
			dpad[i].deviceId = DEVICE_ID_PAD_0 + pDevNum;
			dpad[i].flags = KeyInputFlags::UP;
		}
		dpad[0].keyCode = NKCODE_DPAD_UP;
		dpad[1].keyCode = NKCODE_DPAD_LEFT;
		dpad[2].keyCode = NKCODE_DPAD_DOWN;
		dpad[3].keyCode = NKCODE_DPAD_RIGHT;

		if (LOWORD(state.rgdwPOV[0]) != JOY_POVCENTERED) {
			// These are the edges, so we use or.
			if (state.rgdwPOV[0] >= JOY_POVLEFT_FORWARD || state.rgdwPOV[0] <= JOY_POVFORWARD_RIGHT) {
				dpad[0].flags = KeyInputFlags::DOWN;
			}
			if (state.rgdwPOV[0] >= JOY_POVBACKWARD_LEFT && state.rgdwPOV[0] <= JOY_POVLEFT_FORWARD) {
				dpad[1].flags = KeyInputFlags::DOWN;
			}
			if (state.rgdwPOV[0] >= JOY_POVRIGHT_BACKWARD && state.rgdwPOV[0] <= JOY_POVBACKWARD_LEFT) {
				dpad[2].flags = KeyInputFlags::DOWN;
			}
			if (state.rgdwPOV[0] >= JOY_POVFORWARD_RIGHT && state.rgdwPOV[0] <= JOY_POVRIGHT_BACKWARD) {
				dpad[3].flags = KeyInputFlags::DOWN;
			}
		}

		NativeKey(dpad[0]);
		NativeKey(dpad[1]);
		NativeKey(dpad[2]);
		NativeKey(dpad[3]);

		lastPOV_[0] = LOWORD(state.rgdwPOV[0]);
	}
}

size_t DinputDevice::getNumPads()
{
	getDevices(needsCheck_);
	needsCheck_ = false;
	return devices.size();
}

static std::set<u32> DetectXInputVIDPIDs() {
	std::set<u32> xinputVidPids;

	ComPtr<IWbemLocator> pIWbemLocator;
	if (FAILED(CoCreateInstance(__uuidof(WbemLocator), nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pIWbemLocator))))
		return xinputVidPids;

	ComPtr<IWbemServices> pIWbemServices;
	if (FAILED(pIWbemLocator->ConnectServer(_bstr_t(L"root\\cimv2"), nullptr, nullptr, nullptr, 0,
		nullptr, nullptr, &pIWbemServices)))
		return xinputVidPids;

	CoSetProxyBlanket(pIWbemServices.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
		RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

	ComPtr<IEnumWbemClassObject> pEnumDevices;
	if (FAILED(pIWbemServices->CreateInstanceEnum(_bstr_t(L"Win32_PNPEntity"), 0, nullptr, &pEnumDevices)))
		return xinputVidPids;

	IWbemClassObject* pDevices[32] = { 0 };
	ULONG uReturned = 0;

	while (SUCCEEDED(pEnumDevices->Next(10000, 32, pDevices, &uReturned)) && uReturned > 0) {
		for (ULONG i = 0; i < uReturned; i++) {
			VARIANT var{};
			if (SUCCEEDED(pDevices[i]->Get(L"DeviceID", 0, &var, nullptr, nullptr)))
			{
				if (wcsstr(var.bstrVal, L"IG_"))
				{
					DWORD vid = 0, pid = 0;
					const WCHAR *strVid = wcsstr(var.bstrVal, L"VID_");
					const WCHAR *strPid = wcsstr(var.bstrVal, L"PID_");

					if (strVid) swscanf_s(strVid, L"VID_%4x", &vid);
					if (strPid) swscanf_s(strPid, L"PID_%4x", &pid);

					const DWORD vidpid = MAKELONG(vid, pid);
					xinputVidPids.insert((u32)vidpid);
				}
				VariantClear(&var);
			}
			pDevices[i]->Release();
		}
	}

	return xinputVidPids;
}

DInputMetaDevice::DInputMetaDevice() {
	//find all connected DInput devices of class GamePad
	numDinputDevices_ = DinputDevice::getNumPads();
	for (size_t i = 0; i < numDinputDevices_; i++) {
		devices_.push_back(std::make_unique<DinputDevice>(static_cast<int>(i)));
	}
}

int DInputMetaDevice::UpdateState() {
	static const int CHECK_FREQUENCY = 71;  // Just an arbitrary prime to try to not collide with other periodic checks.
	if (checkCounter_++ > CHECK_FREQUENCY) {
		const size_t newCount = DinputDevice::getNumPads();
		if (newCount > numDinputDevices_) {
			INFO_LOG(Log::System, "New controller device detected");
			for (size_t i = numDinputDevices_; i < newCount; i++) {
				devices_.push_back(std::make_unique<DinputDevice>(static_cast<int>(i)));
			}
			numDinputDevices_ = newCount;
		}
		checkCounter_ = 0;
	}

	for (const auto &device : devices_) {
		if (device->UpdateState() == InputDevice::UPDATESTATE_SKIP_PAD)
			return InputDevice::UPDATESTATE_SKIP_PAD;
	}
	return 0;
}
