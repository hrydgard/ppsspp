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

#include <vector>

#include "Common/System/System.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/Loaders.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "Core/MIPS/MIPS.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceUmd.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "Core/RetroAchievements.h"

#include "Core/FileSystems/BlockDevices.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"

static constexpr u64 MICRO_DELAY_ACTIVATE = 4000;
// Does not include PSP_UMD_CHANGED.
static constexpr uint32_t UMD_STAT_ALLOW_WAIT = PSP_UMD_NOT_PRESENT | PSP_UMD_PRESENT | PSP_UMD_NOT_READY | PSP_UMD_READY | PSP_UMD_READABLE;

static bool umdActivated = true;
static u32 umdStatus = 0;
static u32 umdErrorStat = 0;
static int driveCBId = 0;
static int umdStatTimeoutEvent = -1;
static int umdStatChangeEvent = -1;
static int umdInsertChangeEvent = -1;
static std::vector<SceUID> umdWaitingThreads;
static std::map<SceUID, u64> umdPausedWaits;

bool g_UMDReplacePermit = false;
bool UMDInserted = true;

struct PspUmdInfo {
	u32_le size;
	u32_le type;
};

static void __UmdStatTimeout(u64 userdata, int cyclesLate);
static void __UmdStatChange(u64 userdata, int cyclesLate);
static void __UmdInsertChange(u64 userdata, int cyclesLate);
static void __UmdBeginCallback(SceUID threadID, SceUID prevCallbackId);
static void __UmdEndCallback(SceUID threadID, SceUID prevCallbackId);

void __UmdInit()
{
	umdStatTimeoutEvent = CoreTiming::RegisterEvent("UmdTimeout", __UmdStatTimeout);
	umdStatChangeEvent = CoreTiming::RegisterEvent("UmdChange", __UmdStatChange);
	umdInsertChangeEvent = CoreTiming::RegisterEvent("UmdInsertChange", __UmdInsertChange);
	umdActivated = true;
	umdStatus = 0;
	umdErrorStat = 0;
	driveCBId = 0;
	umdWaitingThreads.clear();
	umdPausedWaits.clear();
	g_UMDReplacePermit = false;

	__KernelRegisterWaitTypeFuncs(WAITTYPE_UMD, __UmdBeginCallback, __UmdEndCallback);
}

void __UmdDoState(PointerWrap &p)
{
	auto s = p.Section("sceUmd", 1, 3);
	if (!s)
		return;

	u8 activatedByte = umdActivated ? 1 : 0;
	Do(p, umdActivated);
	umdActivated = activatedByte != 0;
	Do(p, umdStatus);
	Do(p, umdErrorStat);
	Do(p, driveCBId);
	Do(p, umdStatTimeoutEvent);
	CoreTiming::RestoreRegisterEvent(umdStatTimeoutEvent, "UmdTimeout", __UmdStatTimeout);
	Do(p, umdStatChangeEvent);
	CoreTiming::RestoreRegisterEvent(umdStatChangeEvent, "UmdChange", __UmdStatChange);
	Do(p, umdWaitingThreads);
	Do(p, umdPausedWaits);

	if (s > 1) {
		Do(p, g_UMDReplacePermit);
		if (g_UMDReplacePermit) {
			System_Notify(SystemNotification::UI);
		}
	}
	if (s > 2) {
		Do(p, umdInsertChangeEvent);
		Do(p, UMDInserted);
	} else {
		umdInsertChangeEvent = -1;
		UMDInserted = true;
	}
	CoreTiming::RestoreRegisterEvent(umdInsertChangeEvent, "UmdInsertChange", __UmdInsertChange);
}

static u8 __KernelUmdGetState() {
	if (!UMDInserted) {
		return PSP_UMD_NOT_PRESENT;
	}

	// Most games seem to expect the disc to be ready early on, active or not.
	// It seems like the PSP sets this state when the disc is "ready".
	u8 state = PSP_UMD_PRESENT | PSP_UMD_READY;
	if (umdActivated) {
		state |= PSP_UMD_READABLE;
	}
	return state;
}

static void UmdWakeThreads() {
	// Wake anyone waiting on this.
	for (size_t i = 0; i < umdWaitingThreads.size(); ++i) {
		const SceUID threadID = umdWaitingThreads[i];

		u32 error;
		u32 stat = __KernelGetWaitValue(threadID, error);
		bool keep = false;
		if (HLEKernel::VerifyWait(threadID, WAITTYPE_UMD, 1)) {
			// Only if they are still waiting do we keep them in the list.
			keep = (stat & __KernelUmdGetState()) == 0;
			if (!keep) {
				__KernelResumeThreadFromWait(threadID, 0);
			}
		}

		if (!keep) {
			umdWaitingThreads.erase(umdWaitingThreads.begin() + i--);
		}
	}
}

static void __UmdStatChange(u64 userdata, int cyclesLate) {
	umdActivated = userdata != 0;

	UmdWakeThreads();
}

static void __UmdInsertChange(u64 userdata, int cyclesLate) {
	UMDInserted = true;

	UmdWakeThreads();
}

static void __KernelUmdActivate()
{
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READABLE;
	// PSP_UMD_READY will be returned when sceKernelGetCompiledSdkVersion() != 0
	if (sceKernelGetCompiledSdkVersion() != 0) {
		notifyArg |= PSP_UMD_READY;
	}
	if (driveCBId != 0)
		__KernelNotifyCallback(driveCBId, notifyArg);

	// Don't activate immediately, take time to "spin up."
	CoreTiming::RemoveEvent(umdStatChangeEvent);
	CoreTiming::ScheduleEvent(usToCycles(MICRO_DELAY_ACTIVATE), umdStatChangeEvent, 1);
}

static void __KernelUmdDeactivate()
{
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READY;
	if (driveCBId != 0)
		__KernelNotifyCallback(driveCBId, notifyArg);

	CoreTiming::RemoveEvent(umdStatChangeEvent);
	__UmdStatChange(0, 0);
}

static void __UmdBeginCallback(SceUID threadID, SceUID prevCallbackId)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	if (HLEKernel::VerifyWait(threadID, WAITTYPE_UMD, 1))
	{
		// This means two callbacks in a row.  PSP crashes if the same callback runs inside itself.
		// TODO: Handle this better?
		if (umdPausedWaits.find(pauseKey) != umdPausedWaits.end())
			return;

		_dbg_assert_msg_(umdStatTimeoutEvent != -1, "Must have a umd timer");
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(umdStatTimeoutEvent, threadID);
		if (cyclesLeft != 0)
			umdPausedWaits[pauseKey] = CoreTiming::GetTicks() + cyclesLeft;
		else
			umdPausedWaits[pauseKey] = 0;

		HLEKernel::RemoveWaitingThread(umdWaitingThreads, threadID);

		DEBUG_LOG(Log::sceIo, "sceUmdWaitDriveStatCB: Suspending lock wait for callback");
	}
	else
		WARN_LOG_REPORT(Log::sceIo, "sceUmdWaitDriveStatCB: beginning callback with bad wait id?");
}

static void __UmdEndCallback(SceUID threadID, SceUID prevCallbackId)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	u32 error;
	u32 stat = __KernelGetWaitValue(threadID, error);
	if (umdPausedWaits.find(pauseKey) == umdPausedWaits.end())
	{
		WARN_LOG_REPORT(Log::sceIo, "__UmdEndCallback(): UMD paused wait missing");

		__KernelResumeThreadFromWait(threadID, 0);
		return;
	}

	u64 waitDeadline = umdPausedWaits[pauseKey];
	umdPausedWaits.erase(pauseKey);

	// TODO: Don't wake up if __KernelCurHasReadyCallbacks()?

	if ((stat & __KernelUmdGetState()) != 0)
	{
		__KernelResumeThreadFromWait(threadID, 0);
		return;
	}

	s64 cyclesLeft = waitDeadline - CoreTiming::GetTicks();
	if (cyclesLeft < 0 && waitDeadline != 0)
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	else
	{
		_dbg_assert_msg_(umdStatTimeoutEvent != -1, "Must have a umd timer");
		CoreTiming::ScheduleEvent(cyclesLeft, umdStatTimeoutEvent, __KernelGetCurThread());

		umdWaitingThreads.push_back(threadID);

		DEBUG_LOG(Log::sceIo, "sceUmdWaitDriveStatCB: Resuming lock wait for callback");
	}
}

static int sceUmdCheckMedium()
{
	if (UMDInserted) {
		DEBUG_LOG(Log::sceIo, "1=sceUmdCheckMedium()");
		return 1; //non-zero: disc in drive
	}
	DEBUG_LOG(Log::sceIo, "0=sceUmdCheckMedium()");
	return 0;
}
	
static u32 sceUmdGetDiscInfo(u32 infoAddr)
{
	DEBUG_LOG(Log::sceIo, "sceUmdGetDiscInfo(%08x)", infoAddr);

	if (Memory::IsValidAddress(infoAddr)) {
		auto info = PSPPointer<PspUmdInfo>::Create(infoAddr);
		if (info->size != 8)
			return PSP_ERROR_UMD_INVALID_PARAM;

		info->type = PSP_UMD_TYPE_GAME;
		return 0;
	} else
		return PSP_ERROR_UMD_INVALID_PARAM;
}

static int sceUmdActivate(u32 mode, const char *name) {
	if (mode < 1 || mode > 2)
		return hleLogWarning(Log::sceIo, PSP_ERROR_UMD_INVALID_PARAM);

	__KernelUmdActivate();

	if (mode != 1) {
		return hleLogError(Log::sceIo, 0, "UNTESTED");
	}
	return hleLogSuccessI(Log::sceIo, 0);
}

static int sceUmdDeactivate(u32 mode, const char *name)
{
	// Why 18?  No idea.
	if (mode > 18)
		return PSP_ERROR_UMD_INVALID_PARAM;

	__KernelUmdDeactivate();

	if (mode == 1) {
		DEBUG_LOG(Log::sceIo, "0=sceUmdDeactivate(%d, %s)", mode, name);
	} else {
		ERROR_LOG(Log::sceIo, "UNTESTED 0=sceUmdDeactivate(%d, %s)", mode, name);
	}

	return 0;
}

static u32 sceUmdRegisterUMDCallBack(u32 cbId)
{
	int retVal = 0;

	// TODO: If the callback is invalid, return PSP_ERROR_UMD_INVALID_PARAM.
	if (!kernelObjects.IsValid(cbId)) {
		retVal = PSP_ERROR_UMD_INVALID_PARAM;
	} else {
		// There's only ever one.
		driveCBId = cbId;
	}
	DEBUG_LOG(Log::sceIo, "%d=sceUmdRegisterUMDCallback(id=%08x)", retVal, cbId);
	return retVal;
}

static int sceUmdUnRegisterUMDCallBack(int cbId)
{
	int retVal;

	if (cbId != driveCBId) {
		retVal = PSP_ERROR_UMD_INVALID_PARAM;
	} else {
		if (sceKernelGetCompiledSdkVersion() > 0x3000000) {
			retVal = 0;
		} else {
			retVal = cbId;
		}
		driveCBId = 0;
	}
	DEBUG_LOG(Log::sceIo, "%08x=sceUmdUnRegisterUMDCallBack(id=%08x)", retVal, cbId);
	return retVal;
}

static u32 sceUmdGetDriveStat()
{
	if (!UMDInserted) {
		WARN_LOG(Log::sceIo, "sceUmdGetDriveStat: UMD is taking out for switch UMD");
		return PSP_UMD_NOT_PRESENT;
	}
	//u32 retVal = PSP_UMD_INITED | PSP_UMD_READY | PSP_UMD_PRESENT;
	u32 retVal = __KernelUmdGetState();
	// This one can be very spammy.
	VERBOSE_LOG(Log::sceIo,"0x%02x=sceUmdGetDriveStat()", retVal);
	return retVal;
}

static void __UmdStatTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_UMD, error);
	// Assuming it's still waiting.
	if (waitID == 1)
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);

	HLEKernel::RemoveWaitingThread(umdWaitingThreads, threadID);
}

static void __UmdWaitStat(u32 timeout)
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
static int sceUmdWaitDriveStat(u32 stat) {
	if ((stat & UMD_STAT_ALLOW_WAIT) == 0) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT, "bad status");
	}
	if (!__KernelIsDispatchEnabled()) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
	}
	if (__IsInInterrupt()) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "inside interrupt");
	}

	hleEatCycles(520);
	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(Log::sceIo, "sceUmdWaitDriveStat(stat = %08x): waiting", stat);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, 0, "umd stat waited");
		return 0;
	}

	return hleLogSuccessI(Log::sceIo, 0);
}

static int sceUmdWaitDriveStatWithTimer(u32 stat, u32 timeout) {
	if ((stat & UMD_STAT_ALLOW_WAIT) == 0) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT, "bad status");
	}
	if (!__KernelIsDispatchEnabled()) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
	}
	if (__IsInInterrupt()) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "inside interrupt");
	}

	hleEatCycles(520);
	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(Log::sceIo, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): waiting", stat, timeout);
		__UmdWaitStat(timeout);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, false, "umd stat waited with timer");
		return 0;
	} else {
		hleReSchedule("umd stat checked");
	}

	return hleLogSuccessI(Log::sceIo, 0);
}

static int sceUmdWaitDriveStatCB(u32 stat, u32 timeout) {
	if ((stat & UMD_STAT_ALLOW_WAIT) == 0) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT, "bad status");
	}
	if (!__KernelIsDispatchEnabled()) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
	}
	if (__IsInInterrupt()) {
		return hleLogDebug(Log::sceIo, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "inside interrupt");
	}

	hleEatCycles(520);
	hleCheckCurrentCallbacks();
	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(Log::sceIo, "sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): waiting", stat, timeout);
		if (timeout == 0) {
			timeout = 8000;
		}

		__UmdWaitStat(timeout);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, true, "umd stat waited");
	} else {
		hleReSchedule("umd stat waited");
	}

	return hleLogSuccessI(Log::sceIo, 0);
}

static u32 sceUmdCancelWaitDriveStat()
{
	DEBUG_LOG(Log::sceIo, "0=sceUmdCancelWaitDriveStat()");

	for (size_t i = 0; i < umdWaitingThreads.size(); ++i) {
		const SceUID threadID = umdWaitingThreads[i];
		CoreTiming::UnscheduleEvent(umdStatTimeoutEvent, threadID);
		HLEKernel::ResumeFromWait(threadID, WAITTYPE_UMD, 1, (int)SCE_KERNEL_ERROR_WAIT_CANCEL);
	}
	umdWaitingThreads.clear();

	return 0;
}

static u32 sceUmdGetErrorStat()
{
	DEBUG_LOG(Log::sceIo,"%i=sceUmdGetErrorStat()", umdErrorStat);
	return umdErrorStat;
}

void __UmdReplace(const Path &filepath) {
	std::string error = "";
	FileLoader *fileLoader;
	if (!UmdReplace(filepath, &fileLoader, error)) {
		ERROR_LOG(Log::sceIo, "UMD Replace failed: %s", error.c_str());
		return;
	}

	Achievements::ChangeUMD(filepath, fileLoader);

	UMDInserted = false;
	// Wake any threads waiting for the disc to be removed.
	UmdWakeThreads();

	CoreTiming::ScheduleEvent(usToCycles(200*1000), umdInsertChangeEvent, 0); // Wait sceUmdCheckMedium call
	// TODO Is this always correct if UMD was not activated?
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READABLE | PSP_UMD_CHANGED;
	if (driveCBId != 0)
		__KernelNotifyCallback(driveCBId, notifyArg);
}

bool getUMDReplacePermit() {
	return g_UMDReplacePermit;
}

static u32 sceUmdReplaceProhibit()
{
	DEBUG_LOG(Log::sceIo, "sceUmdReplaceProhibit()");
	if (g_UMDReplacePermit) {
		INFO_LOG(Log::sceIo, "sceUmdReplaceProhibit() - prohibited");
		g_UMDReplacePermit = false;
		System_Notify(SystemNotification::SWITCH_UMD_UPDATED);
	}
	return 0;
}

static u32 sceUmdReplacePermit()
{
	DEBUG_LOG(Log::sceIo, "sceUmdReplacePermit()");
	if (!g_UMDReplacePermit) {
		INFO_LOG(Log::sceIo, "sceUmdReplacePermit() - permitted");
		g_UMDReplacePermit = true;
		System_Notify(SystemNotification::SWITCH_UMD_UPDATED);
	}
	return 0;
}

const HLEFunction sceUmdUser[] = 
{
	{0XC6183D47, &WrapI_UC<sceUmdActivate>,               "sceUmdActivate",               'i', "is"},
	{0X6B4A146C, &WrapU_V<sceUmdGetDriveStat>,            "sceUmdGetDriveStat",           'x', ""  },
	{0X46EBB729, &WrapI_V<sceUmdCheckMedium>,             "sceUmdCheckMedium",            'i', ""  },
	{0XE83742BA, &WrapI_UC<sceUmdDeactivate>,             "sceUmdDeactivate",             'i', "xs"},
	{0X8EF08FCE, &WrapI_U<sceUmdWaitDriveStat>,           "sceUmdWaitDriveStat",          'i', "x" },
	{0X56202973, &WrapI_UU<sceUmdWaitDriveStatWithTimer>, "sceUmdWaitDriveStatWithTimer", 'i', "xx"},
	{0X4A9E5E29, &WrapI_UU<sceUmdWaitDriveStatCB>,        "sceUmdWaitDriveStatCB",        'i', "xx"},
	{0X6AF9B50A, &WrapU_V<sceUmdCancelWaitDriveStat>,     "sceUmdCancelWaitDriveStat",    'x', ""  },
	{0X20628E6F, &WrapU_V<sceUmdGetErrorStat>,            "sceUmdGetErrorStat",           'x', ""  },
	{0X340B7686, &WrapU_U<sceUmdGetDiscInfo>,             "sceUmdGetDiscInfo",            'x', "x" },
	{0XAEE7404D, &WrapU_U<sceUmdRegisterUMDCallBack>,     "sceUmdRegisterUMDCallBack",    'x', "x" },
	{0XBD2BDE07, &WrapI_I<sceUmdUnRegisterUMDCallBack>,   "sceUmdUnRegisterUMDCallBack",  'i', "i" },
	{0X87533940, &WrapU_V<sceUmdReplaceProhibit>,         "sceUmdReplaceProhibit",        'x', ""  },
	{0XCBE9F02A, &WrapU_V<sceUmdReplacePermit>,           "sceUmdReplacePermit",          'x', ""  },
	{0X14C6C45C, nullptr,                                 "sceUmdUnuseUMDInMsUsbWlan",    '?', ""  },
	{0XB103FA38, nullptr,                                 "sceUmdUseUMDInMsUsbWlan",      '?', ""  },
};

void Register_sceUmdUser()
{
	RegisterModule("sceUmdUser", ARRAY_SIZE(sceUmdUser), sceUmdUser);
}
