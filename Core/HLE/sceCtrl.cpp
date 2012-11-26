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

#define CTRL_MODE_DIGITAL   0
#define CTRL_MODE_ANALOG    1

const int PSP_CTRL_ERROR_INVALID_MODE = 0x80000107;

// Returned control data
struct _ctrl_data
{
	u32 frame;
	u32 buttons;
	u8  analog[2];
	u8  unused[6];
};

static u32 oldButtons;
struct CtrlLatch {
	u32 btnMake;
	u32 btnBreak;
	u32 btnPress;
	u32 btnRelease;
};


//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
static bool ctrlInited = false;
static bool analogEnabled = false;

static _ctrl_data ctrl;
static CtrlLatch latch;

static std::recursive_mutex ctrlMutex;

// STATE END
//////////////////////////////////////////////////////////////////////////


void sceCtrlInit();

void UpdateLatch()
{
	if (!ctrlInited)
		sceCtrlInit();

	u32 changed = ctrl.buttons ^ oldButtons;
	latch.btnMake = ctrl.buttons & changed;
	latch.btnBreak = oldButtons & changed;
	latch.btnPress = ctrl.buttons;
	latch.btnRelease = (oldButtons & ~ctrl.buttons) & changed;
		
	oldButtons = ctrl.buttons;
}


u32 __CtrlPeekButtons()
{
	if (!ctrlInited)
		sceCtrlInit();

	return ctrl.buttons;
}

// Functions so that the rest of the emulator can control what the sceCtrl interface should return
// to the game:

void __CtrlButtonDown(u32 buttonBit)
{
	if (!ctrlInited)
		sceCtrlInit();

	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
	ctrl.buttons |= buttonBit;
}

void __CtrlButtonUp(u32 buttonBit)
{
	if (!ctrlInited)
		sceCtrlInit();

	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
	ctrl.buttons &= ~buttonBit;
}

void __CtrlSetAnalog(float x, float y)
{
	if (!ctrlInited)
		sceCtrlInit();

	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
	// TODO: Circle!
	if (x > 1.0f) x = 1.0f;
	if (y > 1.0f) y = 1.0f;
	if (x < -1.0f) x = -1.0f;
	if (y < -1.0f) y = -1.0f;
	ctrl.analog[0] = (u8)(x * 127.f + 128.f);
	ctrl.analog[1] = (u8)(y * 127.f + 128.f);
}

void sceCtrlInit()
{
	ctrlInited = true;

	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.analog[0] = 128;
	ctrl.analog[1] = 128;
	// TODO: Make this increment in the correct way.
	ctrl.frame = 1;

	DEBUG_LOG(HLE,"sceCtrlInit");	
	RETURN(0);
}

u32 sceCtrlSetSamplingCycle(u32 cycle)
{
	ERROR_LOG(HLE, "UNIMPL sceCtrlSetSamplingCycle(%u)", cycle);
	return 0;
}

int sceCtrlGetSamplingCycle(u32 cyclePtr)
{
	ERROR_LOG(HLE, "UNIMPL sceCtrlSetSamplingCycle(%08x)", cyclePtr);
	return 0;
}

u32 sceCtrlSetSamplingMode(u32 mode)
{
	u32 retVal = 0;

	DEBUG_LOG(HLE, "sceCtrlSetSamplingMode(%i)", mode);
	if (mode > 1)
		return PSP_CTRL_ERROR_INVALID_MODE;

	retVal = analogEnabled == true ? CTRL_MODE_ANALOG : CTRL_MODE_DIGITAL;
	analogEnabled = mode == CTRL_MODE_ANALOG ? true : false;
	return retVal;
}

int sceCtrlGetSamplingMode(u32 modePtr)
{
	u32 retVal = analogEnabled == true ? CTRL_MODE_ANALOG : CTRL_MODE_DIGITAL;

	if (Memory::IsValidAddress(modePtr))
		Memory::Write_U32(retVal, modePtr);

	return 0;
}

void sceCtrlSetIdleCancelThreshold()
{
	DEBUG_LOG(HLE,"UNIMPL sceCtrlSetIdleCancelThreshold");
	RETURN(0);
}

u32 sceCtrlReadBufferPositive(u32 ctrlDataPtr, u32 nBufs)
{
	DEBUG_LOG(HLE,"sceCtrlReadBufferPositive(%08x, %i)", ctrlDataPtr, nBufs);
	_assert_msg_(HLE, nBufs > 0, "sceCtrlReadBufferPositive: trying to read nothing?");

	if (!ctrlInited)
		sceCtrlInit();

	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);

	if (Memory::IsValidAddress(ctrlDataPtr))
	{
		_ctrl_data *ctrlData = (_ctrl_data*) Memory::GetPointer(ctrlDataPtr);
		memcpy(ctrlData, &ctrl, sizeof(_ctrl_data));
		if (!analogEnabled)
		{
			ctrlData->analog[0] = 128;
			ctrlData->analog[1] = 128;
		}
	}

	return 1;
}

u32 sceCtrlReadBufferNegative(u32 ctrlDataPtr, u32 nBufs)
{
	DEBUG_LOG(HLE,"sceCtrlReadBufferNegative(%08x, %i)", ctrlDataPtr, nBufs);
	_assert_msg_(HLE, nBufs > 0, "sceCtrlReadBufferNegative: trying to read nothing?");

	if (!ctrlInited)
		sceCtrlInit();

	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);

	if (Memory::IsValidAddress(ctrlDataPtr))
	{
		_ctrl_data *ctrlData = (_ctrl_data*) Memory::GetPointer(ctrlDataPtr);
		memcpy(ctrlData, &ctrl, sizeof(_ctrl_data));
		ctrlData->buttons = ~ctrlData->buttons;
		if (!analogEnabled)
		{
			ctrlData->analog[0] = 128;
			ctrlData->analog[1] = 128;
		}
	}

	return 1;
}

u32 sceCtrlPeekLatch(u32 latchDataPtr)
{
	ERROR_LOG(HLE,"FAKE sceCtrlPeekLatch(%08x)", latchDataPtr);

	if (Memory::IsValidAddress(latchDataPtr))
		Memory::WriteStruct(latchDataPtr, &latch);
	return 1;
}

u32 sceCtrlReadLatch(u32 latchDataPtr)
{
	ERROR_LOG(HLE,"FAKE sceCtrlReadLatch(%08x)", latchDataPtr);

	// Hackery to do it here.
	UpdateLatch();

	if (Memory::IsValidAddress(latchDataPtr))
		Memory::WriteStruct(latchDataPtr, &latch);

	return 1;
}

static const HLEFunction sceCtrl[] = 
{
	{0x3E65A0EA, WrapV_V<sceCtrlInit>, "sceCtrlInit"}, //(int unknown), init with 0
	{0x1f4011e6, WrapU_U<sceCtrlSetSamplingMode>, "sceCtrlSetSamplingMode"}, //(int on);
	{0x6A2774F3, WrapU_U<sceCtrlSetSamplingCycle>, "sceCtrlSetSamplingCycle"},
	{0x02BAAD91, WrapI_U<sceCtrlGetSamplingCycle>,"sceCtrlGetSamplingCycle"},
	{0xDA6B76A1, WrapI_U<sceCtrlGetSamplingMode>, "sceCtrlGetSamplingMode"},
	{0x1f803938, WrapU_UU<sceCtrlReadBufferPositive>, "sceCtrlReadBufferPositive"}, //(ctrl_data_t* paddata, int unknown) // unknown should be 1
	{0x3A622550, WrapU_UU<sceCtrlReadBufferPositive>, "sceCtrlPeekBufferPositive"},
	{0xC152080A, WrapU_UU<sceCtrlReadBufferNegative>, "sceCtrlPeekBufferNegative"},
	{0x60B81F86, WrapU_UU<sceCtrlReadBufferNegative>, "sceCtrlReadBufferNegative"},
	{0xB1D0E5CD, WrapU_U<sceCtrlPeekLatch>, "sceCtrlPeekLatch"},
	{0x0B588501, WrapU_U<sceCtrlReadLatch>, "sceCtrlReadLatch"},
	{0x348D99D4, 0, "sceCtrl_348D99D4"},
	{0xAF5960F3, 0, "sceCtrl_AF5960F3"},
	{0xA68FD260, 0, "sceCtrlClearRapidFire"},
	{0x6841BE1A, 0, "sceCtrlSetRapidFire"},
	{0xa7144800, WrapV_V<sceCtrlSetIdleCancelThreshold>, "sceCtrlSetIdleCancelThreshold"},
	{0x687660fa, 0, "sceCtrlGetIdleCancelThreshold"},
};	

void Register_sceCtrl()
{
	RegisterModule("sceCtrl", ARRAY_SIZE(sceCtrl), sceCtrl);
}
