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

#include <limits.h>
#include <algorithm>

#include "Core/HLE/sceCtrl.h"
#include "DinputDevice.h"
#include "ControlMapping.h"
#include "Core/Config.h"
#include "input/input_state.h"
#include "base/NativeApp.h"
#include"input/keycodes.h"
#include "Core/Reporting.h"
#include "Xinput.h"
#pragma comment(lib,"dinput8.lib")

#ifdef min
#undef min
#undef max
#endif

// In order from 0.  There can be 128, but most controllers do not have that many.
static const int dinput_buttons[] = {
	KEYCODE_BUTTON_1,
	KEYCODE_BUTTON_2,
	KEYCODE_BUTTON_3,
	KEYCODE_BUTTON_4,
	KEYCODE_BUTTON_5,
	KEYCODE_BUTTON_6,
	KEYCODE_BUTTON_7,
	KEYCODE_BUTTON_8,
	KEYCODE_BUTTON_9,
	KEYCODE_BUTTON_10,
	KEYCODE_BUTTON_11,
	KEYCODE_BUTTON_12,
	KEYCODE_BUTTON_13,
	KEYCODE_BUTTON_14,
	KEYCODE_BUTTON_15,
	KEYCODE_BUTTON_16,
};

static float NormalizedDeadzoneFilter(short value);

#define DIFF  (JOY_POVRIGHT - JOY_POVFORWARD) / 2
#define JOY_POVFORWARD_RIGHT	JOY_POVFORWARD + DIFF
#define JOY_POVRIGHT_BACKWARD	JOY_POVRIGHT + DIFF
#define JOY_POVBACKWARD_LEFT	JOY_POVBACKWARD + DIFF
#define JOY_POVLEFT_FORWARD		JOY_POVLEFT + DIFF

struct XINPUT_DEVICE_NODE {
    DWORD dwVidPid;
    XINPUT_DEVICE_NODE* pNext;
};
XINPUT_DEVICE_NODE*     g_pXInputDeviceList = NULL;

bool IsXInputDevice( const GUID* pGuidProductFromDirectInput ) {
    XINPUT_DEVICE_NODE* pNode = g_pXInputDeviceList;
    while( pNode )
    {
        if( pNode->dwVidPid == pGuidProductFromDirectInput->Data1 )
            return true;
        pNode = pNode->pNext;
    }

    return false;
}

DinputDevice::DinputDevice() {
	pJoystick = NULL;
	pDI = NULL;
	memset(lastButtons_, 0, sizeof(lastButtons_));
	memset(lastPOV_, 0, sizeof(lastPOV_));
	last_lX_ = 0;
	last_lY_ = 0;
	last_lZ_ = 0;
	last_lRx_ = 0;
	last_lRy_ = 0;
	last_lRz_ = 0;

	if(FAILED(DirectInput8Create(GetModuleHandle(NULL),DIRECTINPUT_VERSION,IID_IDirectInput8,(void**)&pDI,NULL)))
		return;

	if(FAILED(pDI->CreateDevice(GUID_Joystick, &pJoystick, NULL ))) {
		pDI->Release();
		pDI = NULL;
		return;
	}

	if(FAILED(pJoystick->SetDataFormat(&c_dfDIJoystick2))) {
		pJoystick->Release();
		pJoystick = NULL;
		return;
	}

	// Ignore if device supports XInput
	DIDEVICEINSTANCE dinfo = {0};
	pJoystick->GetDeviceInfo(&dinfo);
	if (IsXInputDevice(&dinfo.guidProduct))	{
		pDI->Release();
		pDI = NULL;
		pJoystick->Release();
		pJoystick = NULL;
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
	// dwData 1000 is deadzone(0% - 10%)
	dipw.dwData            = 1000;

	analog |= FAILED(pJoystick->SetProperty(DIPROP_DEADZONE, &dipw.diph)) ? false : true;
}

DinputDevice::~DinputDevice() {
	if (pJoystick) {
		pJoystick->Release();
		pJoystick = NULL;
	}

	if (pDI) {
		pDI->Release();
		pDI = NULL;
	}
}

void SendNativeAxis(int deviceId, short value, short &lastValue, int axisId) {
	if (value == lastValue)
		return;

	AxisInput axis;
	axis.deviceId = deviceId;
	axis.axisId = axisId;
	axis.value = NormalizedDeadzoneFilter(value);
	NativeAxis(axis);

	lastValue = value;
}

int DinputDevice::UpdateState(InputState &input_state) {
	if (!pJoystick) return -1;

	DIJOYSTATE2 js;

	if (FAILED(pJoystick->Poll())) {
		if(pJoystick->Acquire() == DIERR_INPUTLOST)
			return -1;
	}

	if(FAILED(pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js)))
		return -1;

	ApplyButtons(js, input_state);

	if (analog)	{
		AxisInput axis;
		axis.deviceId = DEVICE_ID_PAD_0;

		SendNativeAxis(DEVICE_ID_PAD_0, js.lX, last_lX_, JOYSTICK_AXIS_X);
		SendNativeAxis(DEVICE_ID_PAD_0, js.lY, last_lY_, JOYSTICK_AXIS_Y);
		SendNativeAxis(DEVICE_ID_PAD_0, js.lZ, last_lZ_, JOYSTICK_AXIS_Z);
		SendNativeAxis(DEVICE_ID_PAD_0, js.lRx, last_lRx_, JOYSTICK_AXIS_RX);
		SendNativeAxis(DEVICE_ID_PAD_0, js.lRy, last_lRy_, JOYSTICK_AXIS_RY);
		SendNativeAxis(DEVICE_ID_PAD_0, js.lRz, last_lRz_, JOYSTICK_AXIS_RZ);
	}

	return UPDATESTATE_SKIP_PAD;
}

static float NormalizedDeadzoneFilter(short value) {
	float result = (float)value / 10000.0f;

	// Expand and clamp. Hack to let us reach the corners on most pads.
	result = std::min(1.0f, std::max(result * 1.2f, -1.0f));

	return result;
}

void DinputDevice::ApplyButtons(DIJOYSTATE2 &state, InputState &input_state) {
	BYTE *buttons = state.rgbButtons;
	u32 downMask = 0x80;

	for (int i = 0; i < ARRAY_SIZE(dinput_buttons); ++i) {
		if (state.rgbButtons[i] == lastButtons_[i]) {
			continue;
		}

		bool down = (state.rgbButtons[i] & downMask) == downMask;
		KeyInput key;
		key.deviceId = DEVICE_ID_PAD_0;
		key.flags = down ? KEY_DOWN : KEY_UP;
		key.keyCode = dinput_buttons[i];
		NativeKey(key);

		lastButtons_[i] = state.rgbButtons[i];
	}

	// Now the POV hat, which can technically go in any degree but usually does not.
	if (LOWORD(state.rgdwPOV[0]) != lastPOV_[0]) {
		KeyInput dpad[4];
		for (int i = 0; i < 4; ++i) {
			dpad[i].deviceId = DEVICE_ID_PAD_0;
			dpad[i].flags = KEY_UP;
		}
		dpad[0].keyCode = KEYCODE_DPAD_UP;
		dpad[1].keyCode = KEYCODE_DPAD_LEFT;
		dpad[2].keyCode = KEYCODE_DPAD_DOWN;
		dpad[3].keyCode = KEYCODE_DPAD_RIGHT;

		if (LOWORD(state.rgdwPOV[0]) != JOY_POVCENTERED) {
			// These are the edges, so we use or.
			if (state.rgdwPOV[0] >= JOY_POVLEFT_FORWARD || state.rgdwPOV[0] <= JOY_POVFORWARD_RIGHT) {
				dpad[0].flags = KEY_DOWN;
			}
			if (state.rgdwPOV[0] >= JOY_POVBACKWARD_LEFT && state.rgdwPOV[0] <= JOY_POVLEFT_FORWARD) {
				dpad[1].flags = KEY_DOWN;
			}
			if (state.rgdwPOV[0] >= JOY_POVRIGHT_BACKWARD && state.rgdwPOV[0] <= JOY_POVBACKWARD_LEFT) {
				dpad[2].flags = KEY_DOWN;
			}
			if (state.rgdwPOV[0] >= JOY_POVFORWARD_RIGHT && state.rgdwPOV[0] <= JOY_POVRIGHT_BACKWARD) {
				dpad[3].flags = KEY_DOWN;
			}
		}

		NativeKey(dpad[0]);
		NativeKey(dpad[1]);
		NativeKey(dpad[2]);
		NativeKey(dpad[3]);

		lastPOV_[0] = LOWORD(state.rgdwPOV[0]);
	}
}

