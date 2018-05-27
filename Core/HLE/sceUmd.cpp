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

#include "file/file_util.h"

#include "Common/ChunkFile.h"
#include "Core/Loaders.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Host.h"
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

#include "Core/FileSystems/BlockDevices.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"

const u64 MICRO_DELAY_ACTIVATE = 4000;

static u8 umdActivated = 1;
static u32 umdStatus = 0;
static u32 umdErrorStat = 0;
static int driveCBId = 0;
static int umdStatTimeoutEvent = -1;
static int umdStatChangeEvent = -1;
static int umdInsertChangeEvent = -1;
static std::vector<SceUID> umdWaitingThreads;
static std::map<SceUID, u64> umdPausedWaits;

bool UMDReplacePermit = false;
bool UMDInserted = true;

struct PspUmdInfo {
	u32_le size;
	u32_le type;
};

void __UmdStatTimeout(u64 userdata, int cyclesLate);
void __UmdStatChange(u64 userdata, int cyclesLate);
void __UmdInsertChange(u64 userdata, int cyclesLate);
void __UmdBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __UmdEndCallback(SceUID threadID, SceUID prevCallbackId);

void __UmdInit()
{
	umdStatTimeoutEvent = CoreTiming::RegisterEvent("UmdTimeout", __UmdStatTimeout);
	umdStatChangeEvent = CoreTiming::RegisterEvent("UmdChange", __UmdStatChange);
	umdInsertChangeEvent = CoreTiming::RegisterEvent("UmdInsertChange", __UmdInsertChange);
	umdActivated = 1;
	umdStatus = 0;
	umdErrorStat = 0;
	driveCBId = 0;
	umdWaitingThreads.clear();
	umdPausedWaits.clear();

	__KernelRegisterWaitTypeFuncs(WAITTYPE_UMD, __UmdBeginCallback, __UmdEndCallback);
}

void __UmdDoState(PointerWrap &p)
{
	auto s = p.Section("sceUmd", 1, 3);
	if (!s)
		return;

	p.Do(umdActivated);
	p.Do(umdStatus);
	p.Do(umdErrorStat);
	p.Do(driveCBId);
	p.Do(umdStatTimeoutEvent);
	CoreTiming::RestoreRegisterEvent(umdStatTimeoutEvent, "UmdTimeout", __UmdStatTimeout);
	p.Do(umdStatChangeEvent);
	CoreTiming::RestoreRegisterEvent(umdStatChangeEvent, "UmdChange", __UmdStatChange);
	p.Do(umdWaitingThreads);
	p.Do(umdPausedWaits);

	if (s > 1) {
		p.Do(UMDReplacePermit);
		if (UMDReplacePermit)
			host->UpdateUI();
	}
	if (s > 2) {
		p.Do(umdInsertChangeEvent);
		CoreTiming::RestoreRegisterEvent(umdInsertChangeEvent, "UmdInsertChange", __UmdInsertChange);
		p.Do(UMDInserted);
	}
	else
		UMDInserted = true;
}

static u8 __KernelUmdGetState()
{
	// Most games seem to expect the disc to be ready early on, active or not.
	// It seems like the PSP sets this state when the disc is "ready".
	u8 state = PSP_UMD_PRESENT | PSP_UMD_READY;
	if (umdActivated) {
		state |= PSP_UMD_READABLE;
	}
	return state;
}

void __UmdInsertChange(u64 userdata, int cyclesLate)
{
	UMDInserted = true;
}

void __UmdStatChange(u64 userdata, int cyclesLate)
{
	// TODO: Why not a bool anyway?
	umdActivated = userdata & 0xFF;

	// Wake anyone waiting on this.
	for (size_t i = 0; i < umdWaitingThreads.size(); ++i) {
		const SceUID threadID = umdWaitingThreads[i];

		u32 error;
		u32 stat = __KernelGetWaitValue(threadID, error);
		bool keep = false;
		if (HLEKernel::VerifyWait(threadID, WAITTYPE_UMD, 1)) {
			if ((stat & __KernelUmdGetState()) != 0)
				__KernelResumeThreadFromWait(threadID, 0);
			// Only if they are still waiting do we keep them in the list.
			else
				keep = true;
		}

		if (!keep)
			umdWaitingThreads.erase(umdWaitingThreads.begin() + i--);
	}
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
	CoreTiming::RemoveAllEvents(umdStatChangeEvent);
	CoreTiming::ScheduleEvent(usToCycles(MICRO_DELAY_ACTIVATE), umdStatChangeEvent, 1);
}

static void __KernelUmdDeactivate()
{
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READY;
	if (driveCBId != 0)
		__KernelNotifyCallback(driveCBId, notifyArg);

	CoreTiming::RemoveAllEvents(umdStatChangeEvent);
	__UmdStatChange(0, 0);
}

void __UmdBeginCallback(SceUID threadID, SceUID prevCallbackId)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	if (HLEKernel::VerifyWait(threadID, WAITTYPE_UMD, 1))
	{
		// This means two callbacks in a row.  PSP crashes if the same callback runs inside itself.
		// TODO: Handle this better?
		if (umdPausedWaits.find(pauseKey) != umdPausedWaits.end())
			return;

		_dbg_assert_msg_(SCEIO, umdStatTimeoutEvent != -1, "Must have a umd timer");
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(umdStatTimeoutEvent, threadID);
		if (cyclesLeft != 0)
			umdPausedWaits[pauseKey] = CoreTiming::GetTicks() + cyclesLeft;
		else
			umdPausedWaits[pauseKey] = 0;

		HLEKernel::RemoveWaitingThread(umdWaitingThreads, threadID);

		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatCB: Suspending lock wait for callback");
	}
	else
		WARN_LOG_REPORT(SCEIO, "sceUmdWaitDriveStatCB: beginning callback with bad wait id?");
}

void __UmdEndCallback(SceUID threadID, SceUID prevCallbackId)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	u32 error;
	u32 stat = __KernelGetWaitValue(threadID, error);
	if (umdPausedWaits.find(pauseKey) == umdPausedWaits.end())
	{
		WARN_LOG_REPORT(SCEIO, "__UmdEndCallback(): UMD paused wait missing");

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
		_dbg_assert_msg_(SCEIO, umdStatTimeoutEvent != -1, "Must have a umd timer");
		CoreTiming::ScheduleEvent(cyclesLeft, umdStatTimeoutEvent, __KernelGetCurThread());

		umdWaitingThreads.push_back(threadID);

		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatCB: Resuming lock wait for callback");
	}
}

static int sceUmdCheckMedium()
{
	if (UMDInserted) {
		DEBUG_LOG(SCEIO, "1=sceUmdCheckMedium()");
		return 1; //non-zero: disc in drive
	}
	DEBUG_LOG(SCEIO, "0=sceUmdCheckMedium()");
	return 0;
}
	
static u32 sceUmdGetDiscInfo(u32 infoAddr)
{
	DEBUG_LOG(SCEIO, "sceUmdGetDiscInfo(%08x)", infoAddr);

	if (Memory::IsValidAddress(infoAddr)) {
		auto info = PSPPointer<PspUmdInfo>::Create(infoAddr);
		if (info->size != 8)
			return PSP_ERROR_UMD_INVALID_PARAM;

		info->type = PSP_UMD_TYPE_GAME;
		return 0;
	} else
		return PSP_ERROR_UMD_INVALID_PARAM;
}

static int sceUmdActivate(u32 mode, const char *name)
{
	if (mode < 1 || mode > 2)
		return PSP_ERROR_UMD_INVALID_PARAM;

	__KernelUmdActivate();

	if (mode == 1) {
		DEBUG_LOG(SCEIO, "0=sceUmdActivate(%d, %s)", mode, name);
	} else {
		ERROR_LOG(SCEIO, "UNTESTED 0=sceUmdActivate(%d, %s)", mode, name);
	}

	return 0;
}

static int sceUmdDeactivate(u32 mode, const char *name)
{
	// Why 18?  No idea.
	if (mode > 18)
		return PSP_ERROR_UMD_INVALID_PARAM;

	__KernelUmdDeactivate();

	if (mode == 1) {
		DEBUG_LOG(SCEIO, "0=sceUmdDeactivate(%d, %s)", mode, name);
	} else {
		ERROR_LOG(SCEIO, "UNTESTED 0=sceUmdDeactivate(%d, %s)", mode, name);
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
	DEBUG_LOG(SCEIO, "%d=sceUmdRegisterUMDCallback(id=%08x)", retVal, cbId);
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
	DEBUG_LOG(SCEIO, "%08x=sceUmdUnRegisterUMDCallBack(id=%08x)", retVal, cbId);
	return retVal;
}

static u32 sceUmdGetDriveStat()
{
	if (!UMDInserted) {
		WARN_LOG(SCEIO, "sceUmdGetDriveStat: UMD is taking out for switch UMD");
		return PSP_UMD_NOT_PRESENT;
	}
	//u32 retVal = PSP_UMD_INITED | PSP_UMD_READY | PSP_UMD_PRESENT;
	u32 retVal = __KernelUmdGetState();
	// This one can be very spammy.
	VERBOSE_LOG(SCEIO,"0x%02x=sceUmdGetDriveStat()", retVal);
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
static int sceUmdWaitDriveStat(u32 stat)
{
	if (stat == 0) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStat(stat = %08x): bad status", stat);
		return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
	}

	if (!__KernelIsDispatchEnabled()) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStat(stat = %08x): dispatch disabled", stat);
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStat(stat = %08x): inside interrupt", stat);
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStat(stat = %08x): waiting", stat);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, 0, "umd stat waited");
		return 0;
	}

	DEBUG_LOG(SCEIO, "0=sceUmdWaitDriveStat(stat = %08x)", stat);
	return 0;
}

static int sceUmdWaitDriveStatWithTimer(u32 stat, u32 timeout)
{
	if (stat == 0) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): bad status", stat, timeout);
		return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
	}

	if (!__KernelIsDispatchEnabled()) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): dispatch disabled", stat, timeout);
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): inside interrupt", stat, timeout);
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): waiting", stat, timeout);
		__UmdWaitStat(timeout);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, 0, "umd stat waited with timer");
		return 0;
	} else {
		hleReSchedule("umd stat checked");
	}

	DEBUG_LOG(SCEIO, "0=sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d)", stat, timeout);
	return 0;
}

static int sceUmdWaitDriveStatCB(u32 stat, u32 timeout)
{
	if (!UMDInserted) {
		WARN_LOG(SCEIO, "sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): UMD is taking out for switch UMD", stat, timeout);
		return PSP_UMD_NOT_PRESENT;
	}

	if (stat == 0) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): bad status", stat, timeout);
		return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
	}

	if (!__KernelIsDispatchEnabled()) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): dispatch disabled", stat, timeout);
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		DEBUG_LOG(SCEIO, "sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): inside interrupt", stat, timeout);
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	hleCheckCurrentCallbacks();
	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(SCEIO, "0=sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): waiting", stat, timeout);
		if (timeout == 0) {
			timeout = 8000;
		}

		__UmdWaitStat(timeout);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, true, "umd stat waited");
	} else {
		hleReSchedule("umd stat waited");
	}

	DEBUG_LOG(SCEIO, "0=sceUmdWaitDriveStatCB(stat = %08x, timeout = %d)", stat, timeout);
	return 0;
}

static u32 sceUmdCancelWaitDriveStat()
{
	DEBUG_LOG(SCEIO, "0=sceUmdCancelWaitDriveStat()");

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
	DEBUG_LOG(SCEIO,"%i=sceUmdGetErrorStat()", umdErrorStat);
	return umdErrorStat;
}

void __UmdReplace(std::string filepath) {
	// TODO: This should really go through Loaders, no?  What if it's an invalid file?

	// Only get system from disc0 seems have been enough.
	IFileSystem* currentUMD = pspFileSystem.GetSystem("disc0:");
	IFileSystem* currentISOBlock = pspFileSystem.GetSystem("umd0:");
	if (!currentUMD)
		return;

	FileLoader *loadedFile = ConstructFileLoader(filepath);

	IFileSystem* umd2;
	if (!loadedFile->Exists()) {
		delete loadedFile;
		return;
	}
	UpdateLoadedFile(loadedFile);

	if (loadedFile->IsDirectory()) {
		umd2 = new VirtualDiscFileSystem(&pspFileSystem, filepath);
	} else {
		auto bd = constructBlockDevice(loadedFile);
		if (!bd)
			return;
		umd2 = new ISOFileSystem(&pspFileSystem, bd);
		pspFileSystem.Remount(currentUMD, umd2);

		if (currentUMD != currentISOBlock) {
			// We mounted an ISO block system separately.
			IFileSystem *iso = new ISOBlockSystem(static_cast<ISOFileSystem *>(umd2));
			pspFileSystem.Remount(currentISOBlock, iso);
			delete currentISOBlock;
		}
	}
	delete currentUMD;
	UMDInserted = false;
	CoreTiming::ScheduleEvent(usToCycles(200*1000), umdInsertChangeEvent, 0); // Wait sceUmdCheckMedium call
	// TODO Is this always correct if UMD was not activated?
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READABLE | PSP_UMD_CHANGED;
	if (driveCBId != -1)
		__KernelNotifyCallback(driveCBId, notifyArg);
}

bool getUMDReplacePermit() {
	return UMDReplacePermit;
}

static u32 sceUmdReplaceProhibit()
{
	UMDReplacePermit = false;
	DEBUG_LOG(SCEIO,"sceUmdReplaceProhibit()");
	host->UpdateUI();
	return 0;
}

static u32 sceUmdReplacePermit()
{
	UMDReplacePermit = true;
	DEBUG_LOG(SCEIO,"sceUmdReplacePermit()");
	host->UpdateUI();
	return 0;
}

const HLEFunction sceUmdUser[] = 
{
	{0XC6183D47, &WrapI_UC<sceUmdActivate>,               "sceUmdActivate",               'i', "xs"},
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
