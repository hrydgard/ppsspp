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
#include "../CoreTiming.h"
#include "StdMutex.h"
#include "sceCtrl.h"
#include "sceDisplay.h"
#include "sceKernel.h"
#include "sceKernelThread.h"

/* Index for the two analog directions */
#define CTRL_ANALOG_X   0
#define CTRL_ANALOG_Y   1

#define CTRL_MODE_DIGITAL   0
#define CTRL_MODE_ANALOG    1

const int PSP_CTRL_ERROR_INVALID_MODE = 0x80000107;
const int PSP_CTRL_ERROR_INVALID_NUM_BUFFERS = 0x80000104;

enum
{
	CTRL_WAIT_POSITIVE = 1,
	CTRL_WAIT_NEGATIVE = 2,
};

// Returned control data
struct _ctrl_data
{
	u32 frame;
	u32 buttons;
	u8  analog[2];
	u8  unused[6];
};

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
static int ctrlLatchBufs = 0;
static u32 ctrlOldButtons = 0;

static _ctrl_data ctrlBufs[64];
static _ctrl_data ctrlCurrent;
static int ctrlBuf = 0;
static int ctrlBufRead = 0;
static CtrlLatch latch;

static std::vector<SceUID> waitingThreads;
static std::recursive_mutex ctrlMutex;

// STATE END
//////////////////////////////////////////////////////////////////////////


void __CtrlUpdateLatch()
{
	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);

	u32 changed = ctrlCurrent.buttons ^ ctrlOldButtons;
	latch.btnMake |= ctrlCurrent.buttons & changed;
	latch.btnBreak |= ctrlOldButtons & changed;
	latch.btnPress |= ctrlCurrent.buttons;
	latch.btnRelease |= (ctrlOldButtons & ~ctrlCurrent.buttons) & changed;
	ctrlLatchBufs++;
		
	ctrlOldButtons = ctrlCurrent.buttons;

	// Copy in the current data to the current buffer.
	memcpy(&ctrlBufs[ctrlBuf], &ctrlCurrent, sizeof(_ctrl_data));

	ctrlBufs[ctrlBuf].frame = (u32) (CoreTiming::GetTicks() / CoreTiming::GetClockFrequencyMHz());
	if (!analogEnabled)
	{
		ctrlBufs[ctrlBuf].analog[0] = 128;
		ctrlBufs[ctrlBuf].analog[1] = 128;
	}

	ctrlBuf = (ctrlBuf + 1) % 64;

	// If we wrapped around, push the read head forward.
	// TODO: Is this right?
	if (ctrlBufRead == ctrlBuf)
		ctrlBufRead = (ctrlBufRead + 1) % 64;
}

int __CtrlResetLatch()
{
	int oldBufs = ctrlLatchBufs;
	memset(&latch, 0, sizeof(CtrlLatch));
	ctrlLatchBufs = 0;
	return oldBufs;
}

u32 __CtrlPeekButtons()
{
	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);

	return ctrlCurrent.buttons;
}

// Functions so that the rest of the emulator can control what the sceCtrl interface should return
// to the game:

void __CtrlButtonDown(u32 buttonBit)
{
	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
	ctrlCurrent.buttons |= buttonBit;
}

void __CtrlButtonUp(u32 buttonBit)
{
	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
	ctrlCurrent.buttons &= ~buttonBit;
}

void __CtrlSetAnalog(float x, float y)
{
	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
	// TODO: Circle!
	if (x > 1.0f) x = 1.0f;
	if (y > 1.0f) y = 1.0f;
	if (x < -1.0f) x = -1.0f;
	if (y < -1.0f) y = -1.0f;
	ctrlCurrent.analog[0] = (u8)(x * 127.f + 128.f);
	ctrlCurrent.analog[1] = (u8)(y * 127.f + 128.f);
}

int __CtrlReadSingleBuffer(u32 ctrlDataPtr, bool negative)
{
	_ctrl_data data;
	if (Memory::IsValidAddress(ctrlDataPtr))
	{
		memcpy(&data, &ctrlBufs[ctrlBufRead], sizeof(_ctrl_data));
		ctrlBufRead = (ctrlBufRead + 1) % 64;

		if (negative)
			data.buttons = ~data.buttons;

		Memory::WriteStruct(ctrlDataPtr, &data);
		return 1;
	}

	return 0;
}

int __CtrlReadBuffer(u32 ctrlDataPtr, u32 nBufs, bool negative, bool peek)
{
	if (nBufs > 64)
		return PSP_CTRL_ERROR_INVALID_NUM_BUFFERS;

	int resetRead = ctrlBufRead;

	int done = 0;
	for (u32 i = 0; i < nBufs; ++i)
	{
		// Ran out of buffers.
		if (ctrlBuf == ctrlBufRead)
			break;

		done += __CtrlReadSingleBuffer(ctrlDataPtr, negative);
		ctrlDataPtr += sizeof(_ctrl_data);
	}

	if (peek)
		ctrlBufRead = resetRead;

	return done;
}

void __CtrlVblank()
{
	// When in vblank sampling mode, this samples the ctrl data into the buffers and updates the latch.
	__CtrlUpdateLatch();

	// Wake up a single thread that was waiting for the buffer.
retry:
	if (!waitingThreads.empty() && ctrlBuf != ctrlBufRead)
	{
		SceUID threadID = waitingThreads[0];
		waitingThreads.erase(waitingThreads.begin());

		u32 error;
		SceUID wVal = __KernelGetWaitID(threadID, WAITTYPE_CTRL, error);
		// Make sure it didn't get woken or something.
		if (wVal == 0)
			goto retry;

		u32 ctrlDataPtr = __KernelGetWaitValue(threadID, error);
		int retVal = __CtrlReadSingleBuffer(ctrlDataPtr, wVal == CTRL_WAIT_NEGATIVE);
		__KernelResumeThreadFromWait(threadID, retVal);
	}
}

void __CtrlInit()
{
	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);

	if (!ctrlInited)
	{
		__DisplayListenVblank(__CtrlVblank);
		ctrlInited = true;
	}

	ctrlBuf = 0;
	ctrlBufRead = 0;
	ctrlOldButtons = 0;
	ctrlLatchBufs = 0;

	memset(&latch, 0, sizeof(latch));
	// Start with everything released.
	latch.btnRelease = 0xffffffff;

	memset(&ctrlCurrent, 0, sizeof(ctrlCurrent));
	memset(&ctrlBufs, 0, sizeof(ctrlBufs));
	ctrlCurrent.analog[0] = 128;
	ctrlCurrent.analog[1] = 128;
}

void sceCtrlInit()
{
	__CtrlInit();

	DEBUG_LOG(HLE,"sceCtrlInit");	
	RETURN(0);
}

u32 sceCtrlSetSamplingCycle(u32 cycle)
{
	if (cycle == 0)
	{
		// TODO: Change to vblank when we support something else.
		DEBUG_LOG(HLE, "sceCtrlSetSamplingCycle(%u)", cycle);
	}
	else
	{
		ERROR_LOG(HLE, "UNIMPL sceCtrlSetSamplingCycle(%u)", cycle);
	}
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

void sceCtrlReadBufferPositive(u32 ctrlDataPtr, u32 nBufs)
{
	// TODO: Wait for vblank if there are 0 buffers (resched.)
	DEBUG_LOG(HLE,"sceCtrlReadBufferPositive(%08x, %i)", ctrlDataPtr, nBufs);

	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, false, false);
	if (done != 0)
		RETURN(done);
	else
	{
		waitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_CTRL, CTRL_WAIT_POSITIVE, ctrlDataPtr, 0, false);
	}
}

void sceCtrlReadBufferNegative(u32 ctrlDataPtr, u32 nBufs)
{
	// TODO: Wait for vblank if there are 0 buffers (resched.)
	DEBUG_LOG(HLE,"sceCtrlReadBufferNegative(%08x, %i)", ctrlDataPtr, nBufs);

	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, true, false);
	if (done != 0)
		RETURN(done);
	else
	{
		waitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_CTRL, CTRL_WAIT_NEGATIVE, ctrlDataPtr, 0, false);
	}
}

int sceCtrlPeekBufferPositive(u32 ctrlDataPtr, u32 nBufs)
{
	DEBUG_LOG(HLE,"sceCtrlPeekBufferPositive(%08x, %i)", ctrlDataPtr, nBufs);
	return __CtrlReadBuffer(ctrlDataPtr, nBufs, false, true);
}

int sceCtrlPeekBufferNegative(u32 ctrlDataPtr, u32 nBufs)
{
	DEBUG_LOG(HLE,"sceCtrlPeekBufferNegative(%08x, %i)", ctrlDataPtr, nBufs);
	return __CtrlReadBuffer(ctrlDataPtr, nBufs, true, true);
}

u32 sceCtrlPeekLatch(u32 latchDataPtr)
{
	DEBUG_LOG(HLE, "sceCtrlPeekLatch(%08x)", latchDataPtr);

	if (Memory::IsValidAddress(latchDataPtr))
		Memory::WriteStruct(latchDataPtr, &latch);

	return ctrlLatchBufs;
}

u32 sceCtrlReadLatch(u32 latchDataPtr)
{
	DEBUG_LOG(HLE, "sceCtrlReadLatch(%08x)", latchDataPtr);

	if (Memory::IsValidAddress(latchDataPtr))
		Memory::WriteStruct(latchDataPtr, &latch);

	return __CtrlResetLatch();
}

static const HLEFunction sceCtrl[] = 
{
	{0x3E65A0EA, WrapV_V<sceCtrlInit>, "sceCtrlInit"}, //(int unknown), init with 0
	{0x1f4011e6, WrapU_U<sceCtrlSetSamplingMode>, "sceCtrlSetSamplingMode"}, //(int on);
	{0x6A2774F3, WrapU_U<sceCtrlSetSamplingCycle>, "sceCtrlSetSamplingCycle"},
	{0x02BAAD91, WrapI_U<sceCtrlGetSamplingCycle>,"sceCtrlGetSamplingCycle"},
	{0xDA6B76A1, WrapI_U<sceCtrlGetSamplingMode>, "sceCtrlGetSamplingMode"},
	{0x1f803938, WrapV_UU<sceCtrlReadBufferPositive>, "sceCtrlReadBufferPositive"}, //(ctrl_data_t* paddata, int unknown) // unknown should be 1
	{0x3A622550, WrapI_UU<sceCtrlPeekBufferPositive>, "sceCtrlPeekBufferPositive"},
	{0xC152080A, WrapI_UU<sceCtrlPeekBufferNegative>, "sceCtrlPeekBufferNegative"},
	{0x60B81F86, WrapV_UU<sceCtrlReadBufferNegative>, "sceCtrlReadBufferNegative"},
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
