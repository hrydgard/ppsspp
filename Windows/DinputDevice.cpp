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
#include "Core/Reporting.h"
#include "Xinput.h"
#pragma comment(lib,"dinput8.lib")

#ifdef min
#undef min
#undef max
#endif

unsigned int dinput_ctrl_map[] = {
	11,     PAD_BUTTON_MENU,         // Open PauseScreen
	10,     PAD_BUTTON_BACK,         // Toggle PauseScreen & Back Setting Page
	1,		PAD_BUTTON_A,            // Cross    = XBOX-A
	2,		PAD_BUTTON_B,            // Circle   = XBOX-B 
	0,		PAD_BUTTON_X,            // Square   = XBOX-X
	3,		PAD_BUTTON_Y,            // Triangle = XBOX-Y
	8,		PAD_BUTTON_SELECT,
	9,		PAD_BUTTON_START,
	4,		PAD_BUTTON_LBUMPER,      // LTrigger = XBOX-LBumper
	5,		PAD_BUTTON_RBUMPER,      // RTrigger = XBOX-RBumper
	6,      PAD_BUTTON_LEFT_THUMB,   // Turbo
	7,      PAD_BUTTON_RIGHT_THUMB,  // Open PauseScreen
	POV_CODE_UP, PAD_BUTTON_UP,
	POV_CODE_DOWN, PAD_BUTTON_DOWN,
	POV_CODE_LEFT, PAD_BUTTON_LEFT,
	POV_CODE_RIGHT, PAD_BUTTON_RIGHT,
};

const unsigned int dinput_ctrl_map_size = sizeof(dinput_ctrl_map);

#define DIFF  (JOY_POVRIGHT - JOY_POVFORWARD) / 2
#define JOY_POVFORWARD_RIGHT	JOY_POVFORWARD + DIFF
#define JOY_POVRIGHT_BACKWARD	JOY_POVRIGHT + DIFF
#define JOY_POVBACKWARD_LEFT	JOY_POVBACKWARD + DIFF
#define JOY_POVLEFT_FORWARD		JOY_POVLEFT + DIFF

struct XINPUT_DEVICE_NODE
{
    DWORD dwVidPid;
    XINPUT_DEVICE_NODE* pNext;
};
XINPUT_DEVICE_NODE*     g_pXInputDeviceList = NULL;

bool IsXInputDevice( const GUID* pGuidProductFromDirectInput )
{
    XINPUT_DEVICE_NODE* pNode = g_pXInputDeviceList;
    while( pNode )
    {
        if( pNode->dwVidPid == pGuidProductFromDirectInput->Data1 )
            return true;
        pNode = pNode->pNext;
    }

    return false;
}

DinputDevice::DinputDevice()
{
	pJoystick = NULL;
	pDI = NULL;

	if(FAILED(DirectInput8Create(GetModuleHandle(NULL),DIRECTINPUT_VERSION,IID_IDirectInput8,(void**)&pDI,NULL)))
		return;

	if(FAILED(pDI->CreateDevice(GUID_Joystick, &pJoystick, NULL )))
	{
		pDI->Release();
		pDI = NULL;
		return;
	}

	if(FAILED(pJoystick->SetDataFormat(&c_dfDIJoystick2)))
	{
		pJoystick->Release();
		pJoystick = NULL;
		return;
	}

	// ignore if device suppert XInput
	DIDEVICEINSTANCE dinfo = {0};
	pJoystick->GetDeviceInfo(&dinfo);
	if (IsXInputDevice(&dinfo.guidProduct))
	{
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

	analog = FAILED(pJoystick->SetProperty(DIPROP_RANGE, &diprg.diph))?false:true;

	// Other devices suffer If do not set the dead zone. 
	// TODO: the dead zone will make configurable in the Control dialog.
	DIPROPDWORD dipw;
	dipw.diph.dwSize       = sizeof(DIPROPDWORD);
	dipw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipw.diph.dwHow        = DIPH_DEVICE;
	dipw.diph.dwObj        = 0;
	// dwData 1000 is deadzone(0% - 10%)
	dipw.dwData            = 1000;

	analog |= FAILED(pJoystick->SetProperty(DIPROP_DEADZONE, &dipw.diph))?false:true;

}

DinputDevice::~DinputDevice()
{
	if (pJoystick)
	{
		pJoystick->Release();
		pJoystick= NULL;
	}

	if (pDI)
	{
		pDI->Release();
		pDI= NULL;
	}
}

static inline int getPadCodeFromVirtualPovCode(unsigned int povCode)
{
	int mergedCode = 0;
	for (int i = 0; i < dinput_ctrl_map_size / sizeof(dinput_ctrl_map[0]); i += 2) {
		if (dinput_ctrl_map[i] != 0xFFFFFFFF && dinput_ctrl_map[i] > 0xFF && dinput_ctrl_map[i] & povCode)
			mergedCode |= dinput_ctrl_map[i + 1];
	}
	return mergedCode;
}

int DinputDevice::UpdateState(InputState &input_state)
{
	if (g_Config.iForceInputDevice == 0) return -1;
	if (!pJoystick) return -1;

	DIJOYSTATE2 js;

	if (FAILED(pJoystick->Poll()))
	{
		if(pJoystick->Acquire() == DIERR_INPUTLOST)
			return -1;
	}

	if(FAILED(pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js)))
    return -1;
	switch (js.rgdwPOV[0])
	{
		case JOY_POVFORWARD:		input_state.pad_buttons |= getPadCodeFromVirtualPovCode(POV_CODE_UP); break;
		case JOY_POVBACKWARD:		input_state.pad_buttons |= getPadCodeFromVirtualPovCode(POV_CODE_DOWN); break;
		case JOY_POVLEFT:			input_state.pad_buttons |= getPadCodeFromVirtualPovCode(POV_CODE_LEFT); break;
		case JOY_POVRIGHT:			input_state.pad_buttons |= getPadCodeFromVirtualPovCode(POV_CODE_RIGHT); break;
		case JOY_POVFORWARD_RIGHT:	input_state.pad_buttons |= getPadCodeFromVirtualPovCode(POV_CODE_UP | POV_CODE_RIGHT); break;
		case JOY_POVRIGHT_BACKWARD:	input_state.pad_buttons |= getPadCodeFromVirtualPovCode(POV_CODE_RIGHT | POV_CODE_DOWN); break;
		case JOY_POVBACKWARD_LEFT:	input_state.pad_buttons |= getPadCodeFromVirtualPovCode(POV_CODE_DOWN | POV_CODE_LEFT); break;
		case JOY_POVLEFT_FORWARD:	input_state.pad_buttons |= getPadCodeFromVirtualPovCode(POV_CODE_LEFT | POV_CODE_UP); break;
	}

	if (analog)
	{
		float x = (float)js.lX / 10000.f;
		float y = -((float)js.lY / 10000.f);

		// Expand and clamp. Hack to let us reach the corners on most pads.
		x = std::min(1.0f, std::max(-1.0f, x * 1.2f));
		y = std::min(1.0f, std::max(-1.0f, y * 1.2f));

		input_state.pad_lstick_x += x;
		input_state.pad_lstick_y += y;
	}

	for (u8 i = 0; i < sizeof(dinput_ctrl_map)/sizeof(dinput_ctrl_map[0]); i += 2)
	{
		// DIJOYSTATE2 supported 128 buttons. for exclude the Virtual POV_CODE bit fields.
		if (dinput_ctrl_map[i] < DIRECTINPUT_RGBBUTTONS_MAX && js.rgbButtons[dinput_ctrl_map[i]] & 0x80)
		{
			input_state.pad_buttons |= dinput_ctrl_map[i+1];
		}
	}

	const LONG rthreshold = 8000;

	switch (g_Config.iRightStickBind) {
	case 0:
		break;
	case 1:
		if      (js.lRz >  rthreshold) input_state.pad_buttons |= PAD_BUTTON_RIGHT;
		else if (js.lRz < -rthreshold) input_state.pad_buttons |= PAD_BUTTON_LEFT;
		if      (js.lZ >  rthreshold) input_state.pad_buttons |= PAD_BUTTON_UP;
		else if (js.lZ < -rthreshold) input_state.pad_buttons |= PAD_BUTTON_DOWN;
		break;
	case 2:
		if      (js.lRz >  rthreshold) input_state.pad_buttons |= PAD_BUTTON_B;
		else if (js.lRz < -rthreshold) input_state.pad_buttons |= PAD_BUTTON_X;
		if      (js.lZ >  rthreshold) input_state.pad_buttons |= PAD_BUTTON_Y;
		else if (js.lZ < -rthreshold) input_state.pad_buttons |= PAD_BUTTON_A;
		break;
	case 3:
		if      (js.lRz >  rthreshold) input_state.pad_buttons |= PAD_BUTTON_RBUMPER;
		else if (js.lRz < -rthreshold) input_state.pad_buttons |= PAD_BUTTON_LBUMPER;
		break;
	case 4:
		if      (js.lRz >  rthreshold) input_state.pad_buttons |= PAD_BUTTON_RBUMPER;
		else if (js.lRz < -rthreshold) input_state.pad_buttons |= PAD_BUTTON_LBUMPER;
		if      (js.lZ >  rthreshold) input_state.pad_buttons |= PAD_BUTTON_Y;
		else if (js.lZ < -rthreshold) input_state.pad_buttons |= PAD_BUTTON_A;
		break;
	}

	return UPDATESTATE_SKIP_PAD;
}

int DinputDevice::UpdateRawStateSingle(RawInputState &rawState)
{
	if (g_Config.iForceInputDevice == 0) return FALSE;
	if (!pJoystick) return FALSE;

	DIJOYSTATE2 js;

	if (FAILED(pJoystick->Poll()))
	{
		if(pJoystick->Acquire() == DIERR_INPUTLOST)
			return FALSE;
	}

	if(FAILED(pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js)))
    return -1;
	switch (js.rgdwPOV[0])
	{
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