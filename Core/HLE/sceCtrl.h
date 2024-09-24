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

#include "Common/CommonTypes.h"

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

// System-ish buttons.  Not generally used by games.
#define CTRL_HOME         0x00010000
#define CTRL_HOLD         0x00020000
#define CTRL_WLAN         0x00040000
#define CTRL_REMOTE_HOLD  0x00080000
#define CTRL_VOL_UP       0x00100000
#define CTRL_VOL_DOWN     0x00200000
#define CTRL_SCREEN       0x00400000
#define CTRL_NOTE         0x00800000
#define CTRL_DISC         0x01000000
#define CTRL_MEMSTICK     0x02000000
#define CTRL_FORWARD      0x10000000
#define CTRL_BACK         0x20000000
#define CTRL_PLAYPAUSE    0x40000000

// Obscure extra keys that were never mapped to hardware, but can be used to bring up the debug menu in SOTN, see issue #17464
#define CTRL_L3 0x0002
#define CTRL_R3 0x0004
#define CTRL_L2 0x0400
#define CTRL_R2 0x0800

#define CTRL_MASK_DPAD    (CTRL_UP | CTRL_DOWN | CTRL_LEFT | CTRL_RIGHT)
#define CTRL_MASK_ACTION  (CTRL_SQUARE | CTRL_TRIANGLE | CTRL_CIRCLE | CTRL_CROSS)
#define CTRL_MASK_TRIGGER (CTRL_LTRIGGER | CTRL_RTRIGGER)
#define CTRL_MASK_USER    (CTRL_MASK_DPAD | CTRL_MASK_ACTION | CTRL_START | CTRL_SELECT | CTRL_MASK_TRIGGER | CTRL_HOME | CTRL_HOLD | CTRL_WLAN | CTRL_REMOTE_HOLD | CTRL_VOL_UP | CTRL_VOL_DOWN | CTRL_SCREEN | CTRL_NOTE | CTRL_L2 | CTRL_L3 | CTRL_R2 | CTRL_R3)

void __CtrlInit();
void __CtrlDoState(PointerWrap &p);
void __CtrlShutdown();

// Clears and sets selected buttons. NOTE: Clearing happens first.
void __CtrlUpdateButtons(u32 bitsToSet, u32 bitsToClear);

// Call this to set the position of an analog stick, ideally when it changes.
// X and Y values should be from -1 to 1, inclusive, in a square (no need to force to a circle.)
// No deadzone filtering is done (but note that this applies to the actual PSP as well.)
void __CtrlSetAnalogXY(int stick, float x, float y);
void __CtrlSetAnalogX(int stick, float x);
void __CtrlSetAnalogY(int stick, float y);

// Call this to enable rapid-fire.  This will cause buttons other than arrows to alternate.
void __CtrlSetRapidFire(bool state, int interval);
bool __CtrlGetRapidFire();

// For use by internal UI like MsgDialog
u32 __CtrlPeekButtons();
u32 __CtrlPeekButtonsVisual();  // also incorporates rapid-fire
void __CtrlPeekAnalog(int stick, float *x, float *y);
u32 __CtrlReadLatch();

void Register_sceCtrl_driver();

u16 sceCtrlGetRightVibration();
u16 sceCtrlGetLeftVibration();

namespace SceCtrl {
	void SetLeftVibration(u16 lVibration);
	void SetRightVibration(u16 rVibration);
	void SetVibrationLeftDropout(u8 vibrationLDropout);
	void SetVibrationRightDropout(u8 vibrationRDropout);
};
