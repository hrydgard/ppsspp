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

#include <math.h>
#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../CoreTiming.h"
#include "ChunkFile.h"
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

const int PSP_CTRL_ERROR_INVALID_IDLE_PTR = 0x80000023;

const u32 NUM_CTRL_BUFFERS = 64;

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
	u8  analogRight[2];  // Only present in the PSP emu on the PS3 and maybe Vita
	u8  unused[4];
};

struct CtrlLatch {
	u32 btnMake;
	u32 btnBreak;
	u32 btnPress;
	u32 btnRelease;
};


//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
static bool analogEnabled = false;
static int ctrlLatchBufs = 0;
static u32 ctrlOldButtons = 0;

static _ctrl_data ctrlBufs[NUM_CTRL_BUFFERS];
static _ctrl_data ctrlCurrent;
static u32 ctrlBuf = 0;
static u32 ctrlBufRead = 0;
static CtrlLatch latch;

static int ctrlIdleReset = -1;
static int ctrlIdleBack = -1;

static int ctrlCycle = 0;

static std::vector<SceUID> waitingThreads;
static std::recursive_mutex ctrlMutex;

static int ctrlTimer = -1;

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
	ctrlBufs[ctrlBuf].analogRight[0] = 128;
	ctrlBufs[ctrlBuf].analogRight[1] = 128;

	ctrlBuf = (ctrlBuf + 1) % NUM_CTRL_BUFFERS;

	// If we wrapped around, push the read head forward.
	// TODO: Is this right?
	if (ctrlBufRead == ctrlBuf)
		ctrlBufRead = (ctrlBufRead + 1) % NUM_CTRL_BUFFERS;
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

u32 __CtrlReadLatch()
{
	u32 ret = latch.btnMake;
	__CtrlResetLatch();
	return ret;
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

void __CtrlSetAnalog(float x, float y, int stick)
{
	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);
	if (stick == 0) {
		ctrlCurrent.analog[0] = (u8)ceilf(x * 127.5f + 127.5f);
		ctrlCurrent.analog[1] = (u8)ceilf(-y * 127.5f + 127.5f);
	} else {
		ctrlCurrent.analogRight[0] = (u8)ceilf(x * 127.5f + 127.5f);
		ctrlCurrent.analogRight[1] = (u8)ceilf(-y * 127.5f + 127.5f);
	}
}

int __CtrlReadSingleBuffer(u32 ctrlDataPtr, bool negative)
{
	_ctrl_data data;
	if (Memory::IsValidAddress(ctrlDataPtr))
	{
		memcpy(&data, &ctrlBufs[ctrlBufRead], sizeof(_ctrl_data));
		ctrlBufRead = (ctrlBufRead + 1) % NUM_CTRL_BUFFERS;

		if (negative)
			data.buttons = ~data.buttons;

		Memory::WriteStruct(ctrlDataPtr, &data);
		return 1;
	}

	return 0;
}

int __CtrlReadBuffer(u32 ctrlDataPtr, u32 nBufs, bool negative, bool peek)
{
	if (nBufs > NUM_CTRL_BUFFERS)
		return SCE_KERNEL_ERROR_INVALID_SIZE;

	u32 resetRead = ctrlBufRead;

	u32 availBufs;
	// Peeks always work, they just go go from now X buffers.
	if (peek)
		availBufs = nBufs;
	else
	{
		availBufs = (ctrlBuf - ctrlBufRead + NUM_CTRL_BUFFERS) % NUM_CTRL_BUFFERS;
		if (availBufs > nBufs)
			availBufs = nBufs;
	}
	ctrlBufRead = (ctrlBuf - availBufs + NUM_CTRL_BUFFERS) % NUM_CTRL_BUFFERS;

	int done = 0;
	for (u32 i = 0; i < availBufs; ++i)
	{
		done += __CtrlReadSingleBuffer(ctrlDataPtr, negative);
		ctrlDataPtr += sizeof(_ctrl_data);
	}

	if (peek)
		ctrlBufRead = resetRead;

	return done;
}

void __CtrlDoSample()
{
	// This samples the ctrl data into the buffers and updates the latch.
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

void __CtrlVblank()
{
	// This always runs, so make sure we're in vblank mode.
	if (ctrlCycle == 0)
		__CtrlDoSample();
}

void __CtrlTimerUpdate(u64 userdata, int cyclesLate)
{
	// This only runs in timer mode (ctrlCycle > 0.)
	_dbg_assert_msg_(HLE, ctrlCycle > 0, "Ctrl: sampling cycle should be > 0");

	__CtrlDoSample();
	CoreTiming::ScheduleEvent(usToCycles(ctrlCycle), ctrlTimer, 0);
}

void __CtrlInit()
{
	ctrlTimer = CoreTiming::RegisterEvent("CtrlSampleTimer", __CtrlTimerUpdate);
	__DisplayListenVblank(__CtrlVblank);

	ctrlIdleReset = -1;
	ctrlIdleBack = -1;
	ctrlCycle = 0;

	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);

	ctrlBuf = 1;
	ctrlBufRead = 0;
	ctrlOldButtons = 0;
	ctrlLatchBufs = 0;

	memset(&latch, 0, sizeof(latch));
	// Start with everything released.
	latch.btnRelease = 0xffffffff;

	memset(&ctrlCurrent, 0, sizeof(ctrlCurrent));
	ctrlCurrent.analog[0] = 128;
	ctrlCurrent.analog[1] = 128;
	ctrlCurrent.analogRight[0] = 128;
	ctrlCurrent.analogRight[1] = 128;

	for (u32 i = 0; i < NUM_CTRL_BUFFERS; i++)
		memcpy(&ctrlBufs[i], &ctrlCurrent, sizeof(_ctrl_data));
}

void __CtrlDoState(PointerWrap &p)
{
	std::lock_guard<std::recursive_mutex> guard(ctrlMutex);

	p.Do(analogEnabled);
	p.Do(ctrlLatchBufs);
	p.Do(ctrlOldButtons);

	p.DoVoid(ctrlBufs, sizeof(ctrlBufs));
	p.Do(ctrlCurrent);
	p.Do(ctrlBuf);
	p.Do(ctrlBufRead);
	p.Do(latch);

	p.Do(ctrlIdleReset);
	p.Do(ctrlIdleBack);

	p.Do(ctrlCycle);

	SceUID dv = 0;
	p.Do(waitingThreads, dv);

	p.Do(ctrlTimer);
	CoreTiming::RestoreRegisterEvent(ctrlTimer, "CtrlSampleTimer", __CtrlTimerUpdate);
	p.DoMarker("sceCtrl");
}

void __CtrlShutdown()
{
	waitingThreads.clear();
}

u32 sceCtrlSetSamplingCycle(u32 cycle)
{
	DEBUG_LOG(HLE, "sceCtrlSetSamplingCycle(%u)", cycle);

	if ((cycle > 0 && cycle < 5555) || cycle > 20000)
	{
		WARN_LOG(HLE, "SCE_KERNEL_ERROR_INVALID_VALUE=sceCtrlSetSamplingCycle(%u)", cycle);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}

	u32 prev = ctrlCycle;
	ctrlCycle = cycle;

	if (prev > 0)
		CoreTiming::UnscheduleEvent(ctrlTimer, 0);
	if (cycle > 0)
		CoreTiming::ScheduleEvent(usToCycles(ctrlCycle), ctrlTimer, 0);

	return prev;
}

int sceCtrlGetSamplingCycle(u32 cyclePtr)
{
	DEBUG_LOG(HLE, "sceCtrlSetSamplingCycle(%08x)", cyclePtr);
	if (Memory::IsValidAddress(cyclePtr))
		Memory::Write_U32(ctrlCycle, cyclePtr);
	return 0;
}

u32 sceCtrlSetSamplingMode(u32 mode)
{
	u32 retVal = 0;

	DEBUG_LOG(HLE, "sceCtrlSetSamplingMode(%i)", mode);
	if (mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	retVal = analogEnabled == true ? CTRL_MODE_ANALOG : CTRL_MODE_DIGITAL;
	analogEnabled = mode == CTRL_MODE_ANALOG ? true : false;
	return retVal;
}

int sceCtrlGetSamplingMode(u32 modePtr)
{
	u32 retVal = analogEnabled == true ? CTRL_MODE_ANALOG : CTRL_MODE_DIGITAL;
	DEBUG_LOG(HLE, "%d=sceCtrlGetSamplingMode(%08x)", retVal, modePtr);

	if (Memory::IsValidAddress(modePtr))
		Memory::Write_U32(retVal, modePtr);

	return 0;
}

int sceCtrlSetIdleCancelThreshold(int idleReset, int idleBack)
{
	DEBUG_LOG(HLE, "FAKE sceCtrlSetIdleCancelThreshold(%d, %d)", idleReset, idleBack);

	if (idleReset < -1 || idleBack < -1 || idleReset > 128 || idleBack > 128)
		return SCE_KERNEL_ERROR_INVALID_VALUE;

	ctrlIdleReset = idleReset;
	ctrlIdleBack = idleBack;
	return 0;
}

int sceCtrlGetIdleCancelThreshold(u32 idleResetPtr, u32 idleBackPtr)
{
	DEBUG_LOG(HLE, "sceCtrlSetIdleCancelThreshold(%08x, %08x)", idleResetPtr, idleBackPtr);

	if (idleResetPtr && !Memory::IsValidAddress(idleResetPtr))
		return PSP_CTRL_ERROR_INVALID_IDLE_PTR;
	if (idleBackPtr && !Memory::IsValidAddress(idleBackPtr))
		return PSP_CTRL_ERROR_INVALID_IDLE_PTR;

	if (idleResetPtr)
		Memory::Write_U32(ctrlIdleReset, idleResetPtr);
	if (idleBackPtr)
		Memory::Write_U32(ctrlIdleBack, idleBackPtr);

	return 0;
}

void sceCtrlReadBufferPositive(u32 ctrlDataPtr, u32 nBufs)
{
	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, false, false);
	if (done != 0)
	{
		RETURN(done);
		DEBUG_LOG(HLE, "%d=sceCtrlReadBufferPositive(%08x, %i)", done, ctrlDataPtr, nBufs);
	}
	else
	{
		waitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_CTRL, CTRL_WAIT_POSITIVE, ctrlDataPtr, 0, false, "ctrl buffer waited");
		DEBUG_LOG(HLE, "sceCtrlReadBufferPositive(%08x, %i) - waiting", ctrlDataPtr, nBufs);
	}
}

void sceCtrlReadBufferNegative(u32 ctrlDataPtr, u32 nBufs)
{
	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, true, false);
	if (done != 0)
	{
		RETURN(done);
		DEBUG_LOG(HLE, "%d=sceCtrlReadBufferNegative(%08x, %i)", done, ctrlDataPtr, nBufs);
	}
	else
	{
		waitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_CTRL, CTRL_WAIT_NEGATIVE, ctrlDataPtr, 0, false, "ctrl buffer waited");
		DEBUG_LOG(HLE, "sceCtrlReadBufferNegative(%08x, %i) - waiting", ctrlDataPtr, nBufs);
	}
}

int sceCtrlPeekBufferPositive(u32 ctrlDataPtr, u32 nBufs)
{
	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, false, true);
	DEBUG_LOG(HLE, "%d=sceCtrlPeekBufferPositive(%08x, %i)", done, ctrlDataPtr, nBufs);
	return done;
}

int sceCtrlPeekBufferNegative(u32 ctrlDataPtr, u32 nBufs)
{
	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, true, true);
	DEBUG_LOG(HLE, "%d=sceCtrlPeekBufferNegative(%08x, %i)", done, ctrlDataPtr, nBufs);
	return done;
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
	{0x3E65A0EA, 0, "sceCtrlInit"}, //(int unknown), init with 0
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
	{0x348D99D4, 0, "sceCtrlSetSuspendingExtraSamples"},
	{0xAF5960F3, 0, "sceCtrlGetSuspendingExtraSamples"},
	{0xA68FD260, 0, "sceCtrlClearRapidFire"},
	{0x6841BE1A, 0, "sceCtrlSetRapidFire"},
	{0xa7144800, WrapI_II<sceCtrlSetIdleCancelThreshold>, "sceCtrlSetIdleCancelThreshold"},
	{0x687660fa, WrapI_UU<sceCtrlGetIdleCancelThreshold>, "sceCtrlGetIdleCancelThreshold"},
};	

void Register_sceCtrl()
{
	RegisterModule("sceCtrl", ARRAY_SIZE(sceCtrl), sceCtrl);
}
