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

static const struct {int from, to;} dinput_ctrl_map[] = {
    { 11,             KEYCODE_BUTTON_THUMBR },
    { 10,             KEYCODE_BUTTON_THUMBL },
    { 1,              KEYCODE_BUTTON_B },
    { 2,              KEYCODE_BUTTON_A },
    { 0,              KEYCODE_BUTTON_Y  },
    { 3,              KEYCODE_BUTTON_X  },
    { 8,              KEYCODE_BUTTON_SELECT },
    { 9,              KEYCODE_BUTTON_START },
    { 4,              KEYCODE_BUTTON_L2 },
    { 5,              KEYCODE_BUTTON_R2 },
    { 6,              KEYCODE_BUTTON_L1 },
    { 7,              KEYCODE_BUTTON_R1 },
    { POV_CODE_UP,    KEYCODE_DPAD_UP },
    { POV_CODE_DOWN,  KEYCODE_DPAD_DOWN },
    { POV_CODE_LEFT,  KEYCODE_DPAD_LEFT },
    { POV_CODE_RIGHT, KEYCODE_DPAD_RIGHT },
};



struct Stick {
	float x;
	float y;
};

static Stick NormalizedDeadzoneFilter(short x, short y);

const unsigned int dinput_ctrl_map_size = sizeof(dinput_ctrl_map) / sizeof(dinput_ctrl_map[0]);

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

int DinputDevice::UpdateState(InputState &input_state) {
	if (g_Config.iForceInputDevice == 0) return -1;
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
		Stick left = NormalizedDeadzoneFilter(js.lX, js.lY);
		Stick right = NormalizedDeadzoneFilter(js.lZ, js.lRz);

		input_state.pad_lstick_x += left.x;
		input_state.pad_lstick_y += left.y;
		input_state.pad_rstick_x += right.x;
		input_state.pad_rstick_y += right.y;

		AxisInput axis;
		axis.deviceId = DEVICE_ID_PAD_0;

		axis.axisId = JOYSTICK_AXIS_X;
		axis.value = left.x;
		NativeAxis(axis);

		axis.axisId = JOYSTICK_AXIS_Y;
		axis.value = left.y;
		NativeAxis(axis);

		axis.axisId = JOYSTICK_AXIS_Z;
		axis.value = right.x;
		NativeAxis(axis);

		axis.axisId = JOYSTICK_AXIS_RZ;
		axis.value = right.y;
		NativeAxis(axis);
	}

	return UPDATESTATE_SKIP_PAD;
}

static Stick NormalizedDeadzoneFilter(short x, short y) {
		Stick s;
		s.x = (float)x / 10000.f;
		s.y = -((float)y / 10000.f);

		// Expand and clamp. Hack to let us reach the corners on most pads.
		s.x = std::min(1.0f, std::max(-1.0f, s.x * 1.2f));
		s.y = std::min(1.0f, std::max(-1.0f, s.y * 1.2f));

		return s;
}

void DinputDevice::ApplyButtons(DIJOYSTATE2 &state, InputState &input_state) {
	BYTE *buttons = state.rgbButtons;
	u32 downMask = 0x80;

	for(u8 i = 0; i < dinput_ctrl_map_size; i++) {
		if (dinput_ctrl_map[i].from < DIRECTINPUT_RGBBUTTONS_MAX && (state.rgbButtons[dinput_ctrl_map[i].from] & downMask)) {
			KeyInput key;
			key.deviceId = DEVICE_ID_PAD_0;
			key.flags = KEY_DOWN;
			key.keyCode = dinput_ctrl_map[i].to;
			NativeKey(key);

			// Hack needed to let the special buttons work..
			// TODO: Is there no better way to handle this with DirectInput?
			switch(dinput_ctrl_map[i].to) {
			case KEYCODE_BUTTON_THUMBL:
				input_state.pad_buttons |= PAD_BUTTON_LEFT_THUMB;
				break;
			case KEYCODE_BUTTON_THUMBR:
				input_state.pad_buttons |= PAD_BUTTON_BACK; 
				break;
			case KEYCODE_BUTTON_L2:
				input_state.pad_buttons |= PAD_BUTTON_LEFT_TRIGGER;
				break;
			case KEYCODE_BUTTON_R2:
				input_state.pad_buttons |= PAD_BUTTON_RIGHT_TRIGGER;
				break;
			case KEYCODE_BACK:
				input_state.pad_buttons |= PAD_BUTTON_BACK;
				break;

			}
		}

		if (dinput_ctrl_map[i].from < DIRECTINPUT_RGBBUTTONS_MAX && (state.rgbButtons[dinput_ctrl_map[i].from] == 0)) {
			KeyInput key;
			key.deviceId = DEVICE_ID_PAD_0;
			key.flags = KEY_UP;
			key.keyCode = dinput_ctrl_map[i].to;
			NativeKey(key);
		}

		// TODO: Is there really no better way to handle the POV buttons?
		if(dinput_ctrl_map[i].from < DIRECTINPUT_RGBBUTTONS_MAX) {
			KeyInput key;
			key.deviceId = DEVICE_ID_PAD_0;
			switch(state.rgdwPOV[0]) {
			case JOY_POVFORWARD:
				key.keyCode =  KEYCODE_DPAD_UP;
				key.flags = KEY_DOWN;
				NativeKey(key);
				break;

			case JOY_POVLEFT_FORWARD:
				key.keyCode =  KEYCODE_DPAD_UP;
				key.flags = KEY_DOWN;
				NativeKey(key);
				key.keyCode =  KEYCODE_DPAD_LEFT;
				key.flags = KEY_DOWN;
				NativeKey(key);
				break;

			case JOY_POVFORWARD_RIGHT:
				key.keyCode =  KEYCODE_DPAD_UP;
				key.flags = KEY_DOWN;
				NativeKey(key);
				key.keyCode =  KEYCODE_DPAD_RIGHT;
				key.flags = KEY_DOWN;
				NativeKey(key);
				break;

			case JOY_POVBACKWARD:
				key.keyCode = KEYCODE_DPAD_DOWN;
				key.flags = KEY_DOWN;
				NativeKey(key);
				break;

			case JOY_POVBACKWARD_LEFT:
				key.keyCode =  KEYCODE_DPAD_DOWN;
				key.flags = KEY_DOWN;
				NativeKey(key);
				key.keyCode =  KEYCODE_DPAD_LEFT;
				key.flags = KEY_DOWN;
				NativeKey(key);
				break;

			case JOY_POVRIGHT_BACKWARD:
				key.keyCode =  KEYCODE_DPAD_DOWN;
				key.flags = KEY_DOWN;
				NativeKey(key);
				key.keyCode =  KEYCODE_DPAD_LEFT;
				key.flags = KEY_DOWN;
				NativeKey(key);
				break;

			case JOY_POVLEFT:
				key.keyCode = KEYCODE_DPAD_LEFT;
				key.flags = KEY_DOWN;
				NativeKey(key);
				break;

			case JOY_POVRIGHT:	
				key.keyCode = KEYCODE_DPAD_RIGHT;
				key.flags = KEY_DOWN;
				NativeKey(key);
				break;

			default:
				key.keyCode = KEYCODE_DPAD_UP;
				key.flags = KEY_UP;
				NativeKey(key);
				key.keyCode = KEYCODE_DPAD_DOWN;
				key.flags = KEY_UP;
				NativeKey(key);
				key.keyCode = KEYCODE_DPAD_LEFT;
				key.flags = KEY_UP;
				NativeKey(key);
				key.keyCode = KEYCODE_DPAD_RIGHT;
				key.flags = KEY_UP;
				NativeKey(key);
				break;
			}
		}
	}

	// TODO: Remove this once proper analog stick
	// binding is implemented.
	const LONG rthreshold = 8000;
	
	KeyInput RAS;
	RAS.deviceId = DEVICE_ID_PAD_0;
	switch (g_Config.iRightStickBind) {
	case 0:
		break;
	case 1:
		if(!g_Config.iSwapRightAxes) {
			if (state.lRz > rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_RIGHT;
				NativeKey(RAS);
			}
			else if (state.lRz < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_LEFT;
				NativeKey(RAS);
			}
			if (state.lZ > rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_DOWN;
				NativeKey(RAS);
			}
			else if (state.lZ < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_UP;
				NativeKey(RAS);
			}
		}
		else {
			if (state.lX >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_RIGHT;
				NativeKey(RAS);
			}
			else if (state.lZ < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_LEFT;
				NativeKey(RAS);
			}
			if (state.lRz >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_UP;
				NativeKey(RAS);
			}
			else if (state.lRz < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_DOWN;
				NativeKey(RAS);
			}
		}
		break;
	case 2:
		if(!g_Config.iSwapRightAxes) {
			if (state.lRz > rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_B;
				NativeKey(RAS);
			}
			else if (state.lRz < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_X;
				NativeKey(RAS);
			}
			if (state.lZ > rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_Y;
				NativeKey(RAS);
			}
			else if (state.lZ < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_A;
				NativeKey(RAS);
			}
		}
		else {
			if (state.lZ >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_B;
				NativeKey(RAS);
			}
			else if (state.lZ < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_X;
				NativeKey(RAS);
			}
			if (state.lRz >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_Y;
				NativeKey(RAS);
			}
			else if (state.lRz < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = RAS.keyCode = KEYCODE_BUTTON_A;
				NativeKey(RAS);
			}
		}
		break;
	case 3:
		if(!g_Config.iSwapRightAxes) {
			if (state.lRz >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_R1;
				NativeKey(RAS);
			}
			else if (state.lRz < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_L1;
				NativeKey(RAS);
			}
		}
		else {
			if (state.lZ >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_R1;
				NativeKey(RAS);
			}
			else if (state.lZ < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_L1;
				NativeKey(RAS);
			}
		}
		break;
	case 4:
		if(!g_Config.iSwapRightAxes) {
			if (state.lRz > rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_R1;
				NativeKey(RAS);
			}
			else if (state.lRz < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_L1;
				NativeKey(RAS);
			}
			if (state.lZ > rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_Y;
				NativeKey(RAS);
			}
			else if (state.lZ < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_A;
				NativeKey(RAS);
			}
		}
		else {
			if (state.lZ >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_R1;
				NativeKey(RAS);
			}
			else if (state.lZ < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_L1;
				NativeKey(RAS);
			}
			if (state.lRz >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_BUTTON_Y;
				NativeKey(RAS);
			}
			else if (state.lRz < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = RAS.keyCode = KEYCODE_BUTTON_A;
				NativeKey(RAS);
			}
		}
		break;
	case 5:
		if(!g_Config.iSwapRightAxes) {
			if (state.lRz >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_RIGHT;
				NativeKey(RAS);
			}
			else if (state.lRz < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_LEFT;
				NativeKey(RAS);
			}
		}
		else {
			if (state.lZ >  rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_LEFT;
				NativeKey(RAS);
			}
			else if (state.lZ < -rthreshold) {
				RAS.flags = KEY_DOWN;
				RAS.keyCode = KEYCODE_DPAD_RIGHT;
				NativeKey(RAS);
			}
		}
		break;
	}
}

int DinputDevice::UpdateRawStateSingle(RawInputState &rawState) {
	if (g_Config.iForceInputDevice == 0) return FALSE;
	if (!pJoystick) return FALSE;

	DIJOYSTATE2 js;

	if (FAILED(pJoystick->Poll())) {
		if(pJoystick->Acquire() == DIERR_INPUTLOST)
			return FALSE;
	}

	if(FAILED(pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js)))
    return -1;
	switch (js.rgdwPOV[0]) {
		case JOY_POVFORWARD:		rawState.button = POV_CODE_UP; return TRUE;
		case JOY_POVBACKWARD:		rawState.button = POV_CODE_DOWN; return TRUE;
		case JOY_POVLEFT:			rawState.button = POV_CODE_LEFT; return TRUE;
		case JOY_POVRIGHT:			rawState.button = POV_CODE_RIGHT; return TRUE;
	}

	for (int i = 0; i < DIRECTINPUT_RGBBUTTONS_MAX; i++) {
		if (js.rgbButtons[i] & 0x80) {
			rawState.button = i;
			return TRUE;
		}
	}
	return FALSE;
}
