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
#include "Core/CoreTiming.h"
#include "ChunkFile.h"
#include "sceUmd.h"
#include "sceKernelThread.h"

const u64 MICRO_DELAY_ACTIVATE = 4000;

struct UmdWaitingThread
{
	SceUID threadID;
	u32 stat;

	inline static UmdWaitingThread Make(SceUID threadID, u32 stat)
	{
		UmdWaitingThread th;
		th.threadID = threadID;
		th.stat = stat;
		return th;
	}
};

static u8 umdActivated = 1;
static u32 umdStatus = 0;
static u32 umdErrorStat = 0;
static int driveCBId = -1;
static int umdStatTimeoutEvent = -1;
static int umdStatChangeEvent = -1;
static std::vector<UmdWaitingThread> umdWaitingThreads;

struct PspUmdInfo {
	u32 size;
	u32 type;
};

void __UmdStatTimeout(u64 userdata, int cyclesLate);
void __UmdStatChange(u64 userdata, int cyclesLate);

void __UmdInit()
{
	umdStatTimeoutEvent = CoreTiming::RegisterEvent("UmdTimeout", __UmdStatTimeout);
	umdStatChangeEvent = CoreTiming::RegisterEvent("UmdChange", __UmdStatChange);
	umdActivated = 1;
	umdStatus = 0;
	umdErrorStat = 0;
	driveCBId = -1;
}

void __UmdDoState(PointerWrap &p)
{
	p.Do(umdActivated);
	p.Do(umdStatus);
	p.Do(umdErrorStat);
	p.Do(driveCBId);
	p.Do(umdStatTimeoutEvent);
	CoreTiming::RestoreRegisterEvent(umdStatTimeoutEvent, "UmdTimeout", __UmdStatTimeout);
	p.Do(umdStatChangeEvent);
	CoreTiming::RestoreRegisterEvent(umdStatChangeEvent, "UmdChange", __UmdStatChange);
	p.Do(umdWaitingThreads);
	p.DoMarker("sceUmd");
}

u8 __KernelUmdGetState()
{
	u8 state = PSP_UMD_PRESENT;
	if (umdActivated) {
		state |= PSP_UMD_READY;
		state |= PSP_UMD_READABLE;
	}
	// TODO: My tests give PSP_UMD_READY but I suppose that's when it's been sitting in the drive?
	else
		state |= PSP_UMD_NOT_READY;
	return state;
}

void __UmdStatChange(u64 userdata, int cyclesLate)
{
	// TODO: Why not a bool anyway?
	umdActivated = userdata & 0xFF;

	// Wake anyone waiting on this.
	for (size_t i = 0; i < umdWaitingThreads.size(); ++i) {
		UmdWaitingThread &info = umdWaitingThreads[i];

		u32 error;
		SceUID waitID = __KernelGetWaitID(info.threadID, WAITTYPE_UMD, error);
		bool keep = false;
		if (waitID == 1) {
			if ((info.stat & __KernelUmdGetState()) != 0)
				__KernelResumeThreadFromWait(info.threadID, 0);
			// Only if they are still waiting do we keep them in the list.
			else
				keep = true;
		}

		if (!keep)
			umdWaitingThreads.erase(umdWaitingThreads.begin() + i--);
	}
}

void __KernelUmdActivate()
{
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READABLE;
	__KernelNotifyCallbackType(THREAD_CALLBACK_UMD, -1, notifyArg);

	// Don't activate immediately, take time to "spin up."
	CoreTiming::RemoveAllEvents(umdStatChangeEvent);
	CoreTiming::ScheduleEvent(usToCycles(MICRO_DELAY_ACTIVATE), umdStatChangeEvent, 1);
}

void __KernelUmdDeactivate()
{
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READY;
	__KernelNotifyCallbackType(THREAD_CALLBACK_UMD, -1, notifyArg);

	CoreTiming::RemoveAllEvents(umdStatChangeEvent);
	__UmdStatChange(0, 0);
}

int sceUmdCheckMedium()
{
	DEBUG_LOG(HLE, "1=sceUmdCheckMedium()");
	return 1; //non-zero: disc in drive
}
	
u32 sceUmdGetDiscInfo(u32 infoAddr)
{
	DEBUG_LOG(HLE, "sceUmdGetDiscInfo(%08x)", infoAddr);

	if (Memory::IsValidAddress(infoAddr)) {
		PspUmdInfo info;
		Memory::ReadStruct(infoAddr, &info);
		if (info.size != 8)
			return PSP_ERROR_UMD_INVALID_PARAM;

		info.type = PSP_UMD_TYPE_GAME;
		Memory::WriteStruct(infoAddr, &info);
		return 0;
	} else
		return PSP_ERROR_UMD_INVALID_PARAM;
}

int sceUmdActivate(u32 mode, const char *name)
{
	if (mode < 1 || mode > 2)
		return PSP_ERROR_UMD_INVALID_PARAM;

	__KernelUmdActivate();

	if (mode == 1) {
		DEBUG_LOG(HLE, "0=sceUmdActivate(%d, %s)", mode, name);
	} else {
		ERROR_LOG(HLE, "UNTESTED 0=sceUmdActivate(%d, %s)", mode, name);
	}

	return 0;
}

int sceUmdDeactivate(u32 mode, const char *name)
{
	// Why 18?  No idea.
	if (mode > 18)
		return PSP_ERROR_UMD_INVALID_PARAM;

	__KernelUmdDeactivate();

	if (mode == 1) {
		DEBUG_LOG(HLE, "0=sceUmdDeactivate(%d, %s)", mode, name);
	} else {
		ERROR_LOG(HLE, "UNTESTED 0=sceUmdDeactivate(%d, %s)", mode, name);
	}

	return 0;
}

u32 sceUmdRegisterUMDCallBack(u32 cbId)
{
	int retVal;

	// TODO: If the callback is invalid, return PSP_ERROR_UMD_INVALID_PARAM.
	if (cbId == 0)
		retVal = PSP_ERROR_UMD_INVALID_PARAM;
	else {
		// Remove the old one, we're replacing.
		if (driveCBId != -1)
			__KernelUnregisterCallback(THREAD_CALLBACK_UMD, driveCBId);

		retVal = __KernelRegisterCallback(THREAD_CALLBACK_UMD, cbId);
		driveCBId = cbId;
	}

	DEBUG_LOG(HLE, "%d=sceUmdRegisterUMDCallback(id=%08x)", retVal, cbId);
	return retVal;
}

int sceUmdUnRegisterUMDCallBack(int cbId)
{
	int retVal;

	if (cbId != driveCBId)
		retVal = PSP_ERROR_UMD_INVALID_PARAM;
	else {
		retVal = cbId;
		driveCBId = -1;
		__KernelUnregisterCallback(THREAD_CALLBACK_UMD, cbId);
	}

	DEBUG_LOG(HLE, "%08x=sceUmdUnRegisterUMDCallBack(id=%08x)", retVal, cbId);
	return retVal;
}

u32 sceUmdGetDriveStat()
{
	//u32 retVal = PSP_UMD_INITED | PSP_UMD_READY | PSP_UMD_PRESENT;
	u32 retVal = __KernelUmdGetState();
	DEBUG_LOG(HLE,"0x%02x=sceUmdGetDriveStat()", retVal);
	return retVal;
}

void __UmdStatTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_UMD, error);
	// Assuming it's still waiting.
	if (waitID == 1)
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);

	for (size_t i = 0; i < umdWaitingThreads.size(); ++i) {
		if (umdWaitingThreads[i].threadID == threadID)
			umdWaitingThreads.erase(umdWaitingThreads.begin() + i--);
	}
}

void __UmdWaitStat(u32 timeout)
{
	// This happens to be how the hardware seems to time things.
	if (timeout <= 4)
		timeout = 15;
	else if (timeout <= 215)
		timeout = 250;

	CoreTiming::ScheduleEvent(usToCycles((int) timeout), umdStatTimeoutEvent, __KernelGetCurThread());
}

/** 
* Wait for a drive to reach a certain state
*
* @param stat - The drive stat to wait for.
* @return < 0 on error
*
*/
int sceUmdWaitDriveStat(u32 stat)
{
	DEBUG_LOG(HLE,"0=sceUmdWaitDriveStat(stat = %08x)", stat);

	if ((stat & __KernelUmdGetState()) == 0) {
		umdWaitingThreads.push_back(UmdWaitingThread::Make(__KernelGetCurThread(), stat));
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, 0, "umd stat waited");
	}

	return 0;
}

int sceUmdWaitDriveStatWithTimer(u32 stat, u32 timeout)
{
	DEBUG_LOG(HLE,"0=sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d)", stat, timeout);

	if ((stat & __KernelUmdGetState()) == 0) {
		__UmdWaitStat(timeout);
		umdWaitingThreads.push_back(UmdWaitingThread::Make(__KernelGetCurThread(), stat));
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, 0, "umd stat waited with timer");
	} else
		hleReSchedule("umd stat waited with timer");

	return 0;
}

int sceUmdWaitDriveStatCB(u32 stat, u32 timeout)
{
	DEBUG_LOG(HLE,"0=sceUmdWaitDriveStatCB(stat = %08x, timeout = %d)", stat, timeout);

	hleCheckCurrentCallbacks();
	if ((stat & __KernelUmdGetState()) == 0) {
		if (timeout == 0)
			timeout = 8000;

		__UmdWaitStat(timeout);
		umdWaitingThreads.push_back(UmdWaitingThread::Make(__KernelGetCurThread(), stat));
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, true, "umd stat waited");
	} else
		hleReSchedule("umd stat waited");

	return 0;
}

u32 sceUmdCancelWaitDriveStat()
{
	DEBUG_LOG(HLE,"0=sceUmdCancelWaitDriveStat()");

	__KernelTriggerWait(WAITTYPE_UMD, 1, SCE_KERNEL_ERROR_WAIT_CANCEL, "umd stat ready", true);
	// TODO: We should call UnscheduleEvent() event here?
	// But it's not often used anyway, and worst-case it will just do nothing unless it waits again.
	return 0;
}

u32 sceUmdGetErrorStat()
{
	DEBUG_LOG(HLE,"%i=sceUmdGetErrorStat()", umdErrorStat);
	return umdErrorStat;
}

u32 sceUmdReplaceProhibit()
{
	DEBUG_LOG(HLE,"sceUmdReplaceProhibit()");
	return 0;
}

u32 sceUmdReplacePermit()
{
	DEBUG_LOG(HLE,"sceUmdReplacePermit()");
	return 0;
}

const HLEFunction sceUmdUser[] = 
{
	{0xC6183D47,WrapI_UC<sceUmdActivate>,"sceUmdActivate"},
	{0x6B4A146C,&WrapU_V<sceUmdGetDriveStat>,"sceUmdGetDriveStat"},
	{0x46EBB729,WrapI_V<sceUmdCheckMedium>,"sceUmdCheckMedium"},
	{0xE83742BA,WrapI_UC<sceUmdDeactivate>,"sceUmdDeactivate"},
	{0x8EF08FCE,WrapI_U<sceUmdWaitDriveStat>,"sceUmdWaitDriveStat"},
	{0x56202973,WrapI_UU<sceUmdWaitDriveStatWithTimer>,"sceUmdWaitDriveStatWithTimer"},
	{0x4A9E5E29,WrapI_UU<sceUmdWaitDriveStatCB>,"sceUmdWaitDriveStatCB"},
	{0x6af9b50a,WrapU_V<sceUmdCancelWaitDriveStat>,"sceUmdCancelWaitDriveStat"},
	{0x6B4A146C,&WrapU_V<sceUmdGetDriveStat>,"sceUmdGetDriveStat"},
	{0x20628E6F,&WrapU_V<sceUmdGetErrorStat>,"sceUmdGetErrorStat"},
	{0x340B7686,WrapU_U<sceUmdGetDiscInfo>,"sceUmdGetDiscInfo"},
	{0xAEE7404D,&WrapU_U<sceUmdRegisterUMDCallBack>,"sceUmdRegisterUMDCallBack"},
	{0xBD2BDE07,&WrapI_I<sceUmdUnRegisterUMDCallBack>,"sceUmdUnRegisterUMDCallBack"},
	{0x87533940,WrapU_V<sceUmdReplaceProhibit>,"sceUmdReplaceProhibit"},
	{0xCBE9F02A,WrapU_V<sceUmdReplacePermit>,"sceUmdReplacePermit"},
};

void Register_sceUmdUser()
{
	RegisterModule("sceUmdUser", ARRAY_SIZE(sceUmdUser), sceUmdUser);
}
