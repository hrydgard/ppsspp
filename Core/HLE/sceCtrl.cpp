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

#include <cmath>
#include <mutex>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Math/math_util.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HW/Display.h"
#include "Core/System.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Replay.h"
#include "Core/Util/AudioFormat.h"  // for clamp_u8

/* Index for the two analog directions */
#define CTRL_ANALOG_X   0
#define CTRL_ANALOG_Y   1
#define CTRL_ANALOG_CENTER 128

#define CTRL_MODE_DIGITAL   0
#define CTRL_MODE_ANALOG    1

const u32 NUM_CTRL_BUFFERS = 64;

enum {
	CTRL_WAIT_POSITIVE = 1,
	CTRL_WAIT_NEGATIVE = 2,
};

struct CtrlData {
	u32_le frame;
	u32_le buttons;
	// The PSP has only one stick, but has space for more info.
	// The second stick is populated for HD remasters and possibly in the PSP emulator on PS3/Vita.
	u8 analog[2][2];
	u8 unused[4];
};

struct CtrlLatch {
	u32_le btnMake;
	u32_le btnBreak;
	u32_le btnPress;
	u32_le btnRelease;
};


//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
static bool analogEnabled = false;
static int ctrlLatchBufs = 0;
static u32 ctrlOldButtons = 0;

static CtrlData ctrlBufs[NUM_CTRL_BUFFERS];
static CtrlData ctrlCurrent;
static u32 ctrlBuf = 0;
static u32 ctrlBufRead = 0;
static CtrlLatch latch;
static u32 dialogBtnMake = 0;

static int ctrlIdleReset = -1;
static int ctrlIdleBack = -1;

static int ctrlCycle = 0;

static std::vector<SceUID> waitingThreads;
static std::mutex ctrlMutex;

static int ctrlTimer = -1;

static u16 leftVibration = 0;
static u16 rightVibration = 0;
// The higher the dropout, the longer Vibration will run
static u8 vibrationLeftDropout = 160;
static u8 vibrationRightDropout = 160;

// STATE END
//////////////////////////////////////////////////////////////////////////

// Not savestated, this is emu state.
// Not related to sceCtrl*RapidFire(), although it may do the same thing.
static bool emuRapidFire = false;
static u32 emuRapidFireFrames = 0;
static bool emuRapidFireToggle = true;
static u32 emuRapidFireInterval = 5;

// These buttons are not affected by rapid fire (neither is analog.)
const u32 CTRL_EMU_RAPIDFIRE_MASK = CTRL_UP | CTRL_DOWN | CTRL_LEFT | CTRL_RIGHT;

static void __CtrlUpdateLatch()
{
	std::lock_guard<std::mutex> guard(ctrlMutex);
	u64 t = CoreTiming::GetGlobalTimeUs();

	u32 buttons = ctrlCurrent.buttons;
	if (emuRapidFire && emuRapidFireToggle)
		buttons &= CTRL_EMU_RAPIDFIRE_MASK;

	ReplayApplyCtrl(buttons, ctrlCurrent.analog, t);

	// Copy in the current data to the current buffer.
	ctrlBufs[ctrlBuf] = ctrlCurrent;

	if (PSP_CoreParameter().compat.flags().DaxterRotatedAnalogStick) {
		// For some reason, Daxter rotates the analog input. See #17015
		float angle = (15.0f / 360.0f) * (2.0f * M_PI);
		float cosAngle = cosf(angle);
		float sinAngle = sinf(angle);
		float x = ctrlBufs[ctrlBuf].analog[0][0] - 128;
		float y = ctrlBufs[ctrlBuf].analog[0][1] - 128;
		float rX = x * cosAngle - y * sinAngle;
		float rY = x * sinAngle + y * cosAngle;
		ctrlBufs[ctrlBuf].analog[0][0] = (u8)Clamp(rX + 128, 0.0f, 255.0f);
		ctrlBufs[ctrlBuf].analog[0][1] = (u8)Clamp(rY + 128, 0.0f, 255.0f);
	}

	ctrlBufs[ctrlBuf].buttons = buttons;

	u32 changed = buttons ^ ctrlOldButtons;
	latch.btnMake |= buttons & changed;
	latch.btnBreak |= ctrlOldButtons & changed;
	latch.btnPress |= buttons;
	latch.btnRelease |= ~buttons;
	dialogBtnMake |= buttons & changed;
	ctrlLatchBufs++;

	ctrlOldButtons = buttons;

	ctrlBufs[ctrlBuf].frame = (u32)t;
	if (!analogEnabled)
		memset(ctrlBufs[ctrlBuf].analog, CTRL_ANALOG_CENTER, sizeof(ctrlBufs[ctrlBuf].analog));

	ctrlBuf = (ctrlBuf + 1) % NUM_CTRL_BUFFERS;

	// If we wrapped around, push the read head forward.
	// TODO: Is this right?
	if (ctrlBufRead == ctrlBuf)
		ctrlBufRead = (ctrlBufRead + 1) % NUM_CTRL_BUFFERS;
}

static int __CtrlResetLatch()
{
	int oldBufs = ctrlLatchBufs;
	memset(&latch, 0, sizeof(CtrlLatch));
	ctrlLatchBufs = 0;
	return oldBufs;
}

u32 __CtrlPeekButtons()
{
	std::lock_guard<std::mutex> guard(ctrlMutex);

	return ctrlCurrent.buttons;
}

u32 __CtrlPeekButtonsVisual()
{
	u32 buttons;
	{
		std::lock_guard<std::mutex> guard(ctrlMutex);
		buttons = ctrlCurrent.buttons;
	}

	if (emuRapidFire && emuRapidFireToggle)
		buttons &= CTRL_EMU_RAPIDFIRE_MASK;
	return buttons;
}

void __CtrlPeekAnalog(int stick, float *x, float *y)
{
	std::lock_guard<std::mutex> guard(ctrlMutex);

	*x = (ctrlCurrent.analog[stick][CTRL_ANALOG_X] - 127.5f) / 127.5f;
	*y = -(ctrlCurrent.analog[stick][CTRL_ANALOG_Y] - 127.5f) / 127.5f;
}


u32 __CtrlReadLatch()
{
	u32 ret = dialogBtnMake;
	dialogBtnMake = 0;
	return ret;
}

void __CtrlUpdateButtons(u32 bitsToSet, u32 bitsToClear)
{
	bitsToClear &= CTRL_MASK_USER;
	bitsToSet &= CTRL_MASK_USER;

	std::lock_guard<std::mutex> guard(ctrlMutex);
	// There's no atomic operation for this, so mutex it is.
	ctrlCurrent.buttons = (ctrlCurrent.buttons & ~bitsToClear) | bitsToSet;
}

void __CtrlSetAnalogXY(int stick, float x, float y)
{
	u8 scaledX = clamp_u8((int)ceilf(x * 127.5f + 127.5f));
	// TODO: We might have too many negations of Y...
	u8 scaledY = clamp_u8((int)ceilf(-y * 127.5f + 127.5f));

	std::lock_guard<std::mutex> guard(ctrlMutex);
	ctrlCurrent.analog[stick][CTRL_ANALOG_X] = scaledX;
	ctrlCurrent.analog[stick][CTRL_ANALOG_Y] = scaledY;
}

// not making XY to use these due to mutex guard usage
void __CtrlSetAnalogX(int stick, float x) {
	u8 scaledX = clamp_u8((int)ceilf(x * 127.5f + 127.5f));
	std::lock_guard<std::mutex> guard(ctrlMutex);
	ctrlCurrent.analog[stick][CTRL_ANALOG_X] = scaledX;
}

void __CtrlSetAnalogY(int stick, float y) {
	u8 scaledY = clamp_u8((int)ceilf(-y * 127.5f + 127.5f));
	std::lock_guard<std::mutex> guard(ctrlMutex);
	ctrlCurrent.analog[stick][CTRL_ANALOG_Y] = scaledY;
}

void __CtrlSetRapidFire(bool state, int interval)
{
	emuRapidFire = state;
	emuRapidFireToggle = true;
	emuRapidFireInterval = interval;
}

bool __CtrlGetRapidFire()
{
	return emuRapidFire;
}

static int __CtrlReadSingleBuffer(PSPPointer<CtrlData> data, bool negative)
{
	if (data.IsValid())
	{
		*data = ctrlBufs[ctrlBufRead];
		ctrlBufRead = (ctrlBufRead + 1) % NUM_CTRL_BUFFERS;

		// Mask out buttons games aren't allowed to see.
		data->buttons &= CTRL_MASK_USER;
		if (negative)
			data->buttons = ~data->buttons;

		return 1;
	}

	return 0;
}

static int __CtrlReadBuffer(u32 ctrlDataPtr, u32 nBufs, bool negative, bool peek)
{
	if (nBufs > NUM_CTRL_BUFFERS)
		return SCE_KERNEL_ERROR_INVALID_SIZE;

	if (!peek && !__KernelIsDispatchEnabled())
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	if (!peek && __IsInInterrupt())
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;

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
	auto data = PSPPointer<CtrlData>::Create(ctrlDataPtr);
	for (u32 i = 0; i < availBufs; ++i)
		done += __CtrlReadSingleBuffer(data++, negative);

	if (peek)
		ctrlBufRead = resetRead;

	return done;
}

static void __CtrlDoSample()
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

		PSPPointer<CtrlData> ctrlDataPtr;
		ctrlDataPtr = __KernelGetWaitValue(threadID, error);
		int retVal = __CtrlReadSingleBuffer(ctrlDataPtr, wVal == CTRL_WAIT_NEGATIVE);
		__KernelResumeThreadFromWait(threadID, retVal);
		__KernelReSchedule("ctrl buffers updated");
	}
}

static void __CtrlVblank()
{
	emuRapidFireFrames++;
	if (emuRapidFireFrames >= emuRapidFireInterval) {
		emuRapidFireFrames = 0;
		emuRapidFireToggle = !emuRapidFireToggle;
	}

	// Reduce gamepad Vibration by set % each frame
	leftVibration *= (float)vibrationLeftDropout / 256.0f;
	rightVibration *= (float)vibrationRightDropout / 256.0f;

	// This always runs, so make sure we're in vblank mode.
	if (ctrlCycle == 0)
		__CtrlDoSample();
}

static void __CtrlTimerUpdate(u64 userdata, int cyclesLate)
{
	// This only runs in timer mode (ctrlCycle > 0.)
	_dbg_assert_msg_(ctrlCycle > 0, "Ctrl: sampling cycle should be > 0");

	CoreTiming::ScheduleEvent(usToCycles(ctrlCycle) - cyclesLate, ctrlTimer, 0);

	__CtrlDoSample();
}

void __CtrlInit()
{
	ctrlTimer = CoreTiming::RegisterEvent("CtrlSampleTimer", __CtrlTimerUpdate);
	__DisplayListenVblank(__CtrlVblank);

	ctrlIdleReset = -1;
	ctrlIdleBack = -1;
	ctrlCycle = 0;

	std::lock_guard<std::mutex> guard(ctrlMutex);

	ctrlBuf = 1;
	ctrlBufRead = 0;
	ctrlOldButtons = 0;
	ctrlLatchBufs = 0;
	dialogBtnMake = 0;

	memset(&latch, 0, sizeof(latch));
	// Start with everything released.
	latch.btnRelease = 0xffffffff;

	memset(&ctrlCurrent, 0, sizeof(ctrlCurrent));
	memset(ctrlCurrent.analog, CTRL_ANALOG_CENTER, sizeof(ctrlCurrent.analog));
	analogEnabled = false;

	for (u32 i = 0; i < NUM_CTRL_BUFFERS; i++)
		memcpy(&ctrlBufs[i], &ctrlCurrent, sizeof(CtrlData));
}

void __CtrlDoState(PointerWrap &p)
{
	std::lock_guard<std::mutex> guard(ctrlMutex);

	auto s = p.Section("sceCtrl", 1, 3);
	if (!s)
		return;

	Do(p, analogEnabled);
	Do(p, ctrlLatchBufs);
	Do(p, ctrlOldButtons);

	p.DoVoid(ctrlBufs, sizeof(ctrlBufs));
	if (s <= 2) {
		CtrlData dummy = {0};
		Do(p, dummy);
	}
	Do(p, ctrlBuf);
	Do(p, ctrlBufRead);
	Do(p, latch);
	if (s == 1) {
		dialogBtnMake = 0;
	} else {
		Do(p, dialogBtnMake);
	}

	Do(p, ctrlIdleReset);
	Do(p, ctrlIdleBack);

	Do(p, ctrlCycle);

	SceUID dv = 0;
	Do(p, waitingThreads, dv);

	Do(p, ctrlTimer);
	CoreTiming::RestoreRegisterEvent(ctrlTimer, "CtrlSampleTimer", __CtrlTimerUpdate);
}

void __CtrlShutdown()
{
	waitingThreads.clear();
}

static u32 sceCtrlSetSamplingCycle(u32 cycle)
{
	DEBUG_LOG(Log::sceCtrl, "sceCtrlSetSamplingCycle(%u)", cycle);

	if ((cycle > 0 && cycle < 5555) || cycle > 20000)
	{
		WARN_LOG(Log::sceCtrl, "SCE_KERNEL_ERROR_INVALID_VALUE=sceCtrlSetSamplingCycle(%u)", cycle);
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

static int sceCtrlGetSamplingCycle(u32 cyclePtr)
{
	DEBUG_LOG(Log::sceCtrl, "sceCtrlGetSamplingCycle(%08x)", cyclePtr);
	if (Memory::IsValidAddress(cyclePtr))
		Memory::Write_U32(ctrlCycle, cyclePtr);
	return 0;
}

static u32 sceCtrlSetSamplingMode(u32 mode)
{
	u32 retVal = 0;

	DEBUG_LOG(Log::sceCtrl, "sceCtrlSetSamplingMode(%i)", mode);
	if (mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	retVal = analogEnabled == true ? CTRL_MODE_ANALOG : CTRL_MODE_DIGITAL;
	analogEnabled = mode == CTRL_MODE_ANALOG ? true : false;
	return retVal;
}

static int sceCtrlGetSamplingMode(u32 modePtr)
{
	u32 retVal = analogEnabled == true ? CTRL_MODE_ANALOG : CTRL_MODE_DIGITAL;
	DEBUG_LOG(Log::sceCtrl, "%d=sceCtrlGetSamplingMode(%08x)", retVal, modePtr);

	if (Memory::IsValidAddress(modePtr))
		Memory::Write_U32(retVal, modePtr);

	return 0;
}

static int sceCtrlSetIdleCancelThreshold(int idleReset, int idleBack)
{
	DEBUG_LOG(Log::sceCtrl, "FAKE sceCtrlSetIdleCancelThreshold(%d, %d)", idleReset, idleBack);

	if (idleReset < -1 || idleBack < -1 || idleReset > 128 || idleBack > 128)
		return SCE_KERNEL_ERROR_INVALID_VALUE;

	ctrlIdleReset = idleReset;
	ctrlIdleBack = idleBack;
	return 0;
}

static int sceCtrlGetIdleCancelThreshold(u32 idleResetPtr, u32 idleBackPtr)
{
	DEBUG_LOG(Log::sceCtrl, "sceCtrlSetIdleCancelThreshold(%08x, %08x)", idleResetPtr, idleBackPtr);

	if (idleResetPtr && !Memory::IsValidAddress(idleResetPtr))
		return SCE_KERNEL_ERROR_PRIV_REQUIRED;
	if (idleBackPtr && !Memory::IsValidAddress(idleBackPtr))
		return SCE_KERNEL_ERROR_PRIV_REQUIRED;

	if (idleResetPtr)
		Memory::Write_U32(ctrlIdleReset, idleResetPtr);
	if (idleBackPtr)
		Memory::Write_U32(ctrlIdleBack, idleBackPtr);

	return 0;
}

static int sceCtrlReadBufferPositive(u32 ctrlDataPtr, u32 nBufs)
{
	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, false, false);
	hleEatCycles(330);
	if (done != 0)
	{
		DEBUG_LOG(Log::sceCtrl, "%d=sceCtrlReadBufferPositive(%08x, %i)", done, ctrlDataPtr, nBufs);
	}
	else
	{
		waitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_CTRL, CTRL_WAIT_POSITIVE, ctrlDataPtr, 0, false, "ctrl buffer waited");
		DEBUG_LOG(Log::sceCtrl, "sceCtrlReadBufferPositive(%08x, %i) - waiting", ctrlDataPtr, nBufs);
	}
	return done;
}

static int sceCtrlReadBufferNegative(u32 ctrlDataPtr, u32 nBufs)
{
	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, true, false);
	hleEatCycles(330);
	if (done != 0)
	{
		DEBUG_LOG(Log::sceCtrl, "%d=sceCtrlReadBufferNegative(%08x, %i)", done, ctrlDataPtr, nBufs);
	}
	else
	{
		waitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_CTRL, CTRL_WAIT_NEGATIVE, ctrlDataPtr, 0, false, "ctrl buffer waited");
		DEBUG_LOG(Log::sceCtrl, "sceCtrlReadBufferNegative(%08x, %i) - waiting", ctrlDataPtr, nBufs);
	}
	return done;
}

static int sceCtrlPeekBufferPositive(u32 ctrlDataPtr, u32 nBufs)
{
	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, false, true);
	// Some homebrew call this in a tight loop - so VERBOSE it is.
	VERBOSE_LOG(Log::sceCtrl, "%d=sceCtrlPeekBufferPositive(%08x, %i)", done, ctrlDataPtr, nBufs);
	hleEatCycles(330);
	return done;
}

static int sceCtrlPeekBufferNegative(u32 ctrlDataPtr, u32 nBufs)
{
	int done = __CtrlReadBuffer(ctrlDataPtr, nBufs, true, true);
	// Some homebrew call this in a tight loop - so VERBOSE it is.
	VERBOSE_LOG(Log::sceCtrl, "%d=sceCtrlPeekBufferNegative(%08x, %i)", done, ctrlDataPtr, nBufs);
	hleEatCycles(330);
	return done;
}

static void __CtrlWriteUserLatch(CtrlLatch *userLatch, int bufs) {
	*userLatch = latch;
	userLatch->btnBreak &= CTRL_MASK_USER;
	userLatch->btnMake &= CTRL_MASK_USER;
	userLatch->btnPress &= CTRL_MASK_USER;
	if (bufs > 0) {
		userLatch->btnRelease |= ~CTRL_MASK_USER;
	}
}

static u32 sceCtrlPeekLatch(u32 latchDataPtr) {
	auto userLatch = PSPPointer<CtrlLatch>::Create(latchDataPtr);
	if (userLatch.IsValid()) {
		__CtrlWriteUserLatch(userLatch, ctrlLatchBufs);
	}
	return hleLogSuccessI(Log::sceCtrl, ctrlLatchBufs);
}

static u32 sceCtrlReadLatch(u32 latchDataPtr) {
	auto userLatch = PSPPointer<CtrlLatch>::Create(latchDataPtr);
	if (userLatch.IsValid()) {
		__CtrlWriteUserLatch(userLatch, ctrlLatchBufs);
	}
	return hleLogSuccessI(Log::sceCtrl, __CtrlResetLatch());
}

static const HLEFunction sceCtrl[] =
{
	{0X3E65A0EA, nullptr,                                  "sceCtrlInit",                      '?', ""  }, //(int unknown), init with 0
	{0X1F4011E6, &WrapU_U<sceCtrlSetSamplingMode>,         "sceCtrlSetSamplingMode",           'x', "x" },
	{0X6A2774F3, &WrapU_U<sceCtrlSetSamplingCycle>,        "sceCtrlSetSamplingCycle",          'x', "x" },
	{0X02BAAD91, &WrapI_U<sceCtrlGetSamplingCycle>,        "sceCtrlGetSamplingCycle",          'i', "x" },
	{0XDA6B76A1, &WrapI_U<sceCtrlGetSamplingMode>,         "sceCtrlGetSamplingMode",           'i', "x" },
	{0X1F803938, &WrapI_UU<sceCtrlReadBufferPositive>,     "sceCtrlReadBufferPositive",        'i', "xx"},
	{0X3A622550, &WrapI_UU<sceCtrlPeekBufferPositive>,     "sceCtrlPeekBufferPositive",        'i', "xx"},
	{0XC152080A, &WrapI_UU<sceCtrlPeekBufferNegative>,     "sceCtrlPeekBufferNegative",        'i', "xx"},
	{0X60B81F86, &WrapI_UU<sceCtrlReadBufferNegative>,     "sceCtrlReadBufferNegative",        'i', "xx"},
	{0XB1D0E5CD, &WrapU_U<sceCtrlPeekLatch>,               "sceCtrlPeekLatch",                 'i', "x" },
	{0X0B588501, &WrapU_U<sceCtrlReadLatch>,               "sceCtrlReadLatch",                 'i', "x" },
	{0X348D99D4, nullptr,                                  "sceCtrlSetSuspendingExtraSamples", '?', ""  },
	{0XAF5960F3, nullptr,                                  "sceCtrlGetSuspendingExtraSamples", '?', ""  },
	{0XA68FD260, nullptr,                                  "sceCtrlClearRapidFire",            '?', ""  },
	{0X6841BE1A, nullptr,                                  "sceCtrlSetRapidFire",              '?', ""  },
	{0XA7144800, &WrapI_II<sceCtrlSetIdleCancelThreshold>, "sceCtrlSetIdleCancelThreshold",    'i', "ii"},
	{0X687660FA, &WrapI_UU<sceCtrlGetIdleCancelThreshold>, "sceCtrlGetIdleCancelThreshold",    'i', "xx"},
};

void Register_sceCtrl()
{
	RegisterModule("sceCtrl", ARRAY_SIZE(sceCtrl), sceCtrl);
}

void Register_sceCtrl_driver()
{
	RegisterModule("sceCtrl_driver", ARRAY_SIZE(sceCtrl), sceCtrl);
}

u16 sceCtrlGetRightVibration() {
	return rightVibration;
}

u16 sceCtrlGetLeftVibration() {
	return leftVibration;
}

namespace SceCtrl {
	void SetRightVibration(u16 rVibration) {
		rightVibration = rVibration;
	}
	void SetLeftVibration(u16 lVibration) {
		leftVibration = lVibration;
	}
	void SetVibrationRightDropout(u8 vibrationRDropout) {
		vibrationRightDropout = vibrationRDropout;
	}
	void SetVibrationLeftDropout(u8 vibrationLDropout) {
		vibrationLeftDropout = vibrationLDropout;
	}
}
