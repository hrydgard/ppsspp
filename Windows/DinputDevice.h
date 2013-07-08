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

#pragma once
#include <InitGuid.h>
#define DIRECTINPUT_VERSION 0x0800
#define DIRECTINPUT_RGBBUTTONS_MAX 128
#include "InputDevice.h"
#include "dinput.h"

class DinputDevice :
	public InputDevice
{
public:
	DinputDevice();
	~DinputDevice();
	virtual int UpdateState(InputState &input_state);
	virtual bool IsPad() { return true; }
private:
	void ApplyButtons(DIJOYSTATE2 &state, InputState &input_state);
	LPDIRECTINPUT8			pDI;
	LPDIRECTINPUTDEVICE8    pJoystick;
	bool					analog;
	BYTE                    lastButtons_[128];
	WORD                    lastPOV_[4];
	short                   last_lX_;
	short                   last_lY_;
	short                   last_lZ_;
	short                   last_lRx_;
	short                   last_lRy_;
	short                   last_lRz_;
};
