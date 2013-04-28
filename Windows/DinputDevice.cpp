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
#include "DinputDevice.h"
#include "Core/Config.h"
#include "input/input_state.h"
#include "Core/Reporting.h"
#include "Xinput.h"
#pragma comment(lib,"dinput8.lib")

static const unsigned int dinput_ctrl_map[] = {
	9,		PAD_BUTTON_START,
	8,		PAD_BUTTON_SELECT,
	4,		PAD_BUTTON_LBUMPER,
	5,		PAD_BUTTON_RBUMPER,
	1,		PAD_BUTTON_A,
	2,		PAD_BUTTON_B,
	0,		PAD_BUTTON_X,
	3,		PAD_BUTTON_Y,
};

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
		case JOY_POVFORWARD:	input_state.pad_buttons |= PAD_BUTTON_UP; break;
		case JOY_POVBACKWARD:	input_state.pad_buttons |= PAD_BUTTON_DOWN; break;
		case JOY_POVLEFT:		input_state.pad_buttons |= PAD_BUTTON_LEFT; break;
		case JOY_POVRIGHT:		input_state.pad_buttons |= PAD_BUTTON_RIGHT; break;
	}

	if (analog)
	{
		input_state.pad_lstick_x = (float)js.lX / 10000.f;
		input_state.pad_lstick_y = -((float)js.lY / 10000.f);
	}

	for (u8 i = 0; i < sizeof(dinput_ctrl_map)/sizeof(dinput_ctrl_map[0]); i += 2)
	{
		if (js.rgbButtons[dinput_ctrl_map[i]] & 0x80)
		{
			input_state.pad_buttons |= dinput_ctrl_map[i+1];
		}
	}

	return UPDATESTATE_SKIP_PAD;
}

