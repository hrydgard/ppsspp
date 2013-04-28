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

void __CtrlInit();
void __CtrlDoState(PointerWrap &p);
void __CtrlShutdown();

void __CtrlButtonDown(u32 buttonBit);
void __CtrlButtonUp(u32 buttonBit);
// -1 to 1, try to keep it in the circle
void __CtrlSetAnalog(float x, float y, int stick = 0);

// For use by internal UI like MsgDialog
u32 __CtrlPeekButtons();
u32 __CtrlReadLatch();
