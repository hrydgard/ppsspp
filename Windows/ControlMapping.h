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

#include "InputDevice.h"

struct RawInputState
{
	UINT button;
	UINT prevButton;
	RawInputState() {
		button = 0;
		prevButton = 0;
	}
};

#define POV_CODE_UP 0x0100
#define POV_CODE_DOWN 0x0200
#define POV_CODE_LEFT 0x0400
#define POV_CODE_RIGHT 0x0800
#define XBOX_CODE_LEFTTRIGGER  0x00010000
#define XBOX_CODE_RIGHTTRIGGER 0x00020000

#define CONTROLS_KEYBOARD_INDEX        0
#define CONTROLS_DIRECT_INPUT_INDEX    1
#define CONTROLS_XINPUT_INDEX          2
#define CONTROLS_KEYBOARD_ANALOG_INDEX 3
#define CONTROLS_DEVICE_NUM            4

