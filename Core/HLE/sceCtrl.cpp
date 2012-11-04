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

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "StdMutex.h"
#include "sceCtrl.h"

/* Index for the two analog directions */
#define CTRL_ANALOG_X   0
#define CTRL_ANALOG_Y   1


// Returned control data
struct _ctrl_data
{
	u32 frame;
	u32 buttons;
	u8  analog[2];
	u8  unused[6];
};


//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
static bool ctrlInited = false;
static bool analogEnabled = false;
static _ctrl_data ctrl;
static std::recursive_mutex ctrlMutex;
// STATE END
//////////////////////////////////////////////////////////////////////////


// Functions so that the rest of the emulator can control what the sceCtrl interface should return
// to the game:

void __CtrlButtonDown(u32 buttonBit)
{
  std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
  ctrl.buttons |= buttonBit;
}

void __CtrlButtonUp(u32 buttonBit)
{
  std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
  ctrl.buttons &= ~buttonBit;
}

void __CtrlSetAnalog(float x, float y)
{
  std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
  // TODO: Circle!
  if (x > 1.0f) x = 1.0f;
  if (y > 1.0f) y = 1.0f;
  if (x < -1.0f) x = -1.0f;
  if (y > -1.0f) y = -1.0f;
  ctrl.analog[0] = (u8)(x * 127.f + 128.f);
  ctrl.analog[1] = (u8)(y * 127.f + 128.f);
}

void sceCtrlInit()
{
	ctrlInited = true;
  memset(&ctrl, 0, sizeof(ctrl));
	DEBUG_LOG(HLE,"sceCtrlInit");
}

void sceCtrlSetSamplingMode()
{
	DEBUG_LOG(HLE,"sceCtrlSetSamplingMode");
	if (ctrlInited)
	{
    // Looks odd
		analogEnabled = true;
	}
}

void sceCtrlSetIdleCancelThreshold()
{
  DEBUG_LOG(HLE,"UNIMPL sceCtrlSetIdleCancelThreshold");
}

void sceCtrlReadBufferPositive()
{
  std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
	if (ctrlInited)
	{
		static int frame = 0;
		_ctrl_data &data = ctrl;
		data.frame=frame;
    frame++;
#ifdef _WIN32
    // TODO: Move this outta here!
    data.buttons = 0;
    int analogX = 128;
    int analogY = 128;
		if (GetAsyncKeyState(VK_SPACE))
			data.buttons|=CTRL_START;
		if (GetAsyncKeyState('V'))
			data.buttons|=CTRL_SELECT;
		if (GetAsyncKeyState('A'))
			data.buttons|=CTRL_SQUARE;
		if (GetAsyncKeyState('S'))
			data.buttons|=CTRL_TRIANGLE;
		if (GetAsyncKeyState('X'))
			data.buttons|=CTRL_CIRCLE;
		if (GetAsyncKeyState('Z'))
			data.buttons|=CTRL_CROSS;
		if (GetAsyncKeyState('Q'))
			data.buttons|=CTRL_LTRIGGER;
		if (GetAsyncKeyState('W'))
			data.buttons|=CTRL_RTRIGGER;
		if (GetAsyncKeyState(VK_UP)) {
			data.buttons|=CTRL_UP;
			analogY -= 100;
		}
		if (GetAsyncKeyState(VK_DOWN)) {
			data.buttons|=CTRL_DOWN;
			analogY += 100;
		}
		if (GetAsyncKeyState(VK_LEFT)) {
			data.buttons|=CTRL_LEFT;
			analogX -= 100;
		}
		if (GetAsyncKeyState(VK_RIGHT))
		{
			data.buttons|=CTRL_RIGHT;
			analogX += 100;
		}

    data.analog[0]=analogX;
    data.analog[1]=analogY;
#endif

    memcpy(Memory::GetPointer(PARAM(0)), &data, sizeof(_ctrl_data));
	}
}

static const HLEFunction sceCtrl[] = 
{
  {0x6a2774f3, sceCtrlInit,          "sceCtrlInit"}, //(int unknown), init with 0
  {0x1f4011e6, sceCtrlSetSamplingMode, "sceCtrlSetSamplingMode"}, //(int on);
  {0x1f803938, sceCtrlReadBufferPositive,          "sceCtrlReadBufferPositive"}, //(ctrl_data_t* paddata, int unknown) // unknown should be 1
  {0x6A2774F3, 0, "sceCtrlSetSamplingCycle"}, //?
  {0x6A2774F3,sceCtrlInit,"sceCtrlSetSamplingCycle"},
  {0x02BAAD91,0,"sceCtrlGetSamplingCycle"},
  {0xDA6B76A1,0,"sceCtrlGetSamplingMode"},
  {0x3A622550,sceCtrlReadBufferPositive,"sceCtrlPeekBufferPositive"},
  {0xC152080A,0,"sceCtrlPeekBufferNegative"},
  {0x60B81F86,0,"sceCtrlReadBufferNegative"},
  {0xB1D0E5CD,0,"sceCtrlPeekLatch"},
  {0x0B588501,0,"sceCtrlReadLatch"},
  {0x348D99D4,0,"sceCtrl_348D99D4"},
  {0xAF5960F3,0,"sceCtrl_AF5960F3"},
  {0xA68FD260,0,"sceCtrlClearRapidFire"},
  {0x6841BE1A,0,"sceCtrlSetRapidFire"},
  {0xa7144800,sceCtrlSetIdleCancelThreshold,"sceCtrlSetIdleCancelThreshold"},
  {0x687660fa,0,"sceCtrlGetIdleCancelThreshold"},
};	

void Register_sceCtrl() {
  RegisterModule("sceCtrl",ARRAY_SIZE(sceCtrl),sceCtrl);
}
