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

#include "../Common/CommonTypes.h"

class PointerWrap;

void Register_sceCtrl();

const int CTRL_STICK_LEFT = 0;
// The actual PSP only has one, but HD remasters expose this, maybe also the emulator on the PSP/Vita.
const int CTRL_STICK_RIGHT = 1;

#define CTRL_SQUARE     0x8000
#define CTRL_TRIANGLE   0x1000
#define CTRL_CIRCLE     0x2000
#define CTRL_CROSS      0x4000
#define CTRL_UP         0x0010
#define CTRL_DOWN       0x0040
#define CTRL_LEFT       0x0080
#define CTRL_RIGHT      0x0020
#define CTRL_START      0x0008
#define CTRL_SELECT     0x0001
#define CTRL_LTRIGGER   0x0100
#define CTRL_RTRIGGER   0x0200

#define CTRL_ALL_BUTTONS 0xF3F9

void __CtrlInit();
void __CtrlDoState(PointerWrap &p);
void __CtrlShutdown();

// Call this whenever a button is pressed, using the above CTRL_ constants.
// Multiple buttons may be sent in one call OR'd together.
// Resending a currently pressed button is fine but not required.
void __CtrlButtonDown(u32 buttonBit);
// Call this whenever a button is released.  Similar to __CtrlButtonDown().
void __CtrlButtonUp(u32 buttonBit);

// Call this to set the position of an analog stick, ideally when it changes.
// Position value should be from -1 to 1, inclusive, in a square (no need to force to a circle.)
// No deadzone filtering is done (but note that this applies to the actual PSP as well.)
void __CtrlSetAnalogX(float value, int stick = CTRL_STICK_LEFT);
void __CtrlSetAnalogY(float value, int stick = CTRL_STICK_LEFT);

// Call this to enable rapid-fire.  This will cause buttons other than arrows to alternate.
void __CtrlSetRapidFire(bool state);

// For use by internal UI like MsgDialog
u32 __CtrlPeekButtons();
void __CtrlPeekAnalog(int stick, float *x, float *y);
u32 __CtrlReadLatch();

void Register_sceCtrl_driver();