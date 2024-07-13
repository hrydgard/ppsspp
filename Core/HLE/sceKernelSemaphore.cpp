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

#include <algorithm>
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/HLE/HLE.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelSemaphore.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "Core/HLE/FunctionWrappers.h"

#define PSP_SEMA_ATTR_FIFO 0
#define PSP_SEMA_ATTR_PRIORITY 0x100

/** Current state of a semaphore.
* @see sceKernelReferSemaStatus.
*/

struct NativeSemaphore
{
	/** Size of the ::SceKernelSemaInfo structure. */
	SceSize_le size;
	/** NUL-terminated name of the semaphore. */
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	/** Attributes. */
	SceUInt_le attr;
	/** The initial count the semaphore was created with. */
	s32_le initCount;
	/** The current count. */
	s32_le currentCount;
	/** The maximum count. */
	s32_le maxCount;
	/** The number of threads waiting on the semaphore. */
	s32_le numWaitThreads;
};


struct PSPSemaphore : public KernelObject {
	const char *GetName() override { return ns.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "Semaphore"; }

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_SEMID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Semaphore; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Semaphore; }

	void DoState(PointerWrap &p) override
	{
		auto s = p.Section("Semaphore", 1);
		if (!s)
			return;

		Do(p, ns);
		SceUID dv = 0;
		Do(p, waitingThreads, dv);
		Do(p, pausedWaits);
	}

	NativeSemaphore ns;
	std::vector<SceUID> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, u64> pausedWaits;
};

static int semaWaitTimer = -1;

void __KernelSemaBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelSemaEndCallback(SceUID threadID, SceUID prevCallbackId);

void __KernelSemaInit()
{
	semaWaitTimer = CoreTiming::RegisterEvent("SemaphoreTimeout", __KernelSemaTimeout);
	__KernelRegisterWaitTypeFuncs(WAITTYPE_SEMA, __KernelSemaBeginCallback, __KernelSemaEndCallback);
}

void __KernelSemaDoState(PointerWrap &p)
{
	auto s = p.Section("sceKernelSema", 1);
	if (!s)
		return;

	Do(p, semaWaitTimer);
	CoreTiming::RestoreRegisterEvent(semaWaitTimer, "SemaphoreTimeout", __KernelSemaTimeout);
}

KernelObject *__KernelSemaphoreObject()
{
	return new PSPSemaphore;
}

// Returns whether the thread should be removed.
static bool __KernelUnlockSemaForThread(PSPSemaphore *s, SceUID threadID, u32 &error, int result, bool &wokeThreads) {
	if (!HLEKernel::VerifyWait(threadID, WAITTYPE_SEMA, s->GetUID()))
		return true;

	// If result is an error code, we're just letting it go.
	if (result == 0)
	{
		int wVal = (int) __KernelGetWaitValue(threadID, error);
		if (wVal > s->ns.currentCount)
			return false;

		s->ns.currentCount -= wVal;
	}

	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0 && semaWaitTimer != -1)
	{
		// Remove any event for this thread.
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(semaWaitTimer, threadID);
		if (cyclesLeft < 0)
			cyclesLeft = 0;
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(threadID, result);
	wokeThreads = true;
	return true;
}

void __KernelSemaBeginCallback(SceUID threadID, SceUID prevCallbackId)
{
	auto result = HLEKernel::WaitBeginCallback<PSPSemaphore, WAITTYPE_SEMA, SceUID>(threadID, prevCallbackId, semaWaitTimer);
	if (result == HLEKernel::WAIT_CB_SUCCESS)
		DEBUG_LOG(Log::sceKernel, "sceKernelWaitSemaCB: Suspending sema wait for callback");
	else
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelWaitSemaCB: beginning callback with bad wait id?");
}

void __KernelSemaEndCallback(SceUID threadID, SceUID prevCallbackId)
{
	auto result = HLEKernel::WaitEndCallback<PSPSemaphore, WAITTYPE_SEMA, SceUID>(threadID, prevCallbackId, semaWaitTimer, __KernelUnlockSemaForThread);
	if (result == HLEKernel::WAIT_CB_RESUMED_WAIT)
		DEBUG_LOG(Log::sceKernel, "sceKernelWaitSemaCB: Resuming sema wait for callback");
}

// Resume all waiting threads (for delete / cancel.)
// Returns true if it woke any threads.
static bool __KernelClearSemaThreads(PSPSemaphore *s, int reason) {
	u32 error;
	bool wokeThreads = false;
	std::vector<SceUID>::iterator iter, end;
	for (iter = s->waitingThreads.begin(), end = s->waitingThreads.end(); iter != end; ++iter)
		__KernelUnlockSemaForThread(s, *iter, error, reason, wokeThreads);
	s->waitingThreads.clear();

	return wokeThreads;
}

int sceKernelCancelSema(SceUID id, int newCount, u32 numWaitThreadsPtr)
{
	u32 error;
	PSPSemaphore *s = kernelObjects.Get<PSPSemaphore>(id, error);
	if (s)
	{
		if (newCount > s->ns.maxCount)
		{
			DEBUG_LOG(Log::sceKernel, "sceKernelCancelSema(%i, %i, %08x): invalid count", id, newCount, numWaitThreadsPtr);
			return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
		}

		DEBUG_LOG(Log::sceKernel, "sceKernelCancelSema(%i, %i, %08x)", id, newCount, numWaitThreadsPtr);

		s->ns.numWaitThreads = (int) s->waitingThreads.size();
		if (Memory::IsValidAddress(numWaitThreadsPtr))
			Memory::Write_U32(s->ns.numWaitThreads, numWaitThreadsPtr);

		if (newCount < 0)
			s->ns.currentCount = s->ns.initCount;
		else
			s->ns.currentCount = newCount;

		if (__KernelClearSemaThreads(s, SCE_KERNEL_ERROR_WAIT_CANCEL))
			hleReSchedule("semaphore canceled");

		return 0;
	}
	else
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelCancelSema(%i, %i, %08x): invalid semaphore", id, newCount, numWaitThreadsPtr);
		return error;
	}
}

int sceKernelCreateSema(const char* name, u32 attr, int initVal, int maxVal, u32 optionPtr) {
	if (!name)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ERROR, "invalid name");
	if (attr >= 0x200)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ATTR, "invalid attr parameter %08x", attr);

	PSPSemaphore *s = new PSPSemaphore();
	SceUID id = kernelObjects.Create(s);

	s->ns.size = sizeof(NativeSemaphore);
	strncpy(s->ns.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	s->ns.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	s->ns.attr = attr;
	s->ns.initCount = initVal;
	s->ns.currentCount = s->ns.initCount;
	s->ns.maxCount = maxVal;
	s->ns.numWaitThreads = 0;

	// Many games pass garbage into optionPtr, it doesn't have any options.
	if (optionPtr != 0) {
		if (!Memory::IsValidRange(optionPtr, 4))
			hleLogWarning(Log::sceKernel, id, "invalid options parameter");
		else if (Memory::Read_U32(optionPtr) > 4)
			hleLogDebug(Log::sceKernel, id, "invalid options parameter size");
	}
	if ((attr & ~PSP_SEMA_ATTR_PRIORITY) != 0)
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelCreateSema(%s) unsupported attr parameter: %08x", name, attr);

	return hleLogSuccessX(Log::sceKernel, id);
}

int sceKernelDeleteSema(SceUID id)
{
	u32 error;
	PSPSemaphore *s = kernelObjects.Get<PSPSemaphore>(id, error);
	if (s)
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelDeleteSema(%i)", id);

		bool wokeThreads = __KernelClearSemaThreads(s, SCE_KERNEL_ERROR_WAIT_DELETE);
		if (wokeThreads)
			hleReSchedule("semaphore deleted");

		return kernelObjects.Destroy<PSPSemaphore>(id);
	}
	else
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelDeleteSema(%i): invalid semaphore", id);
		return error;
	}
}

int sceKernelReferSemaStatus(SceUID id, u32 infoPtr) {
	u32 error;
	PSPSemaphore *s = kernelObjects.Get<PSPSemaphore>(id, error);
	if (s) {
		auto info = PSPPointer<NativeSemaphore>::Create(infoPtr);
		if (!info.IsValid())
			return hleLogWarning(Log::sceKernel, -1, "invalid pointer");

		HLEKernel::CleanupWaitingThreads(WAITTYPE_SEMA, id, s->waitingThreads);

		s->ns.numWaitThreads = (int) s->waitingThreads.size();
		if (info->size != 0) {
			*info = s->ns;
			info.NotifyWrite("SemaStatus");
		}
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogError(Log::sceKernel, error);
	}
}

int sceKernelSignalSema(SceUID id, int signal)
{
	u32 error;
	PSPSemaphore *s = kernelObjects.Get<PSPSemaphore>(id, error);
	if (s)
	{
		if (s->ns.currentCount + signal - (int) s->waitingThreads.size() > s->ns.maxCount)
		{
			VERBOSE_LOG(Log::sceKernel, "sceKernelSignalSema(%i, %i): overflow (at %i)", id, signal, s->ns.currentCount);
			return SCE_KERNEL_ERROR_SEMA_OVF;
		}

		int oldval = s->ns.currentCount;
		s->ns.currentCount += signal;
		DEBUG_LOG(Log::sceKernel, "sceKernelSignalSema(%i, %i) (count: %i -> %i)", id, signal, oldval, s->ns.currentCount);

		if ((s->ns.attr & PSP_SEMA_ATTR_PRIORITY) != 0)
			std::stable_sort(s->waitingThreads.begin(), s->waitingThreads.end(), __KernelThreadSortPriority);

		bool wokeThreads = false;
retry:
		for (auto iter = s->waitingThreads.begin(), end = s->waitingThreads.end(); iter != end; ++iter)
		{
			if (__KernelUnlockSemaForThread(s, *iter, error, 0, wokeThreads))
			{
				s->waitingThreads.erase(iter);
				goto retry;
			}
		}

		if (wokeThreads)
			hleReSchedule("semaphore signaled");

		hleEatCycles(900);
		return 0;
	}
	else
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelSignalSema(%i, %i): invalid semaphore", id, signal);
		return error;
	}
}

void __KernelSemaTimeout(u64 userdata, int cycleslate)
{
	SceUID threadID = (SceUID)userdata;
	u32 error;
	SceUID uid = __KernelGetWaitID(threadID, WAITTYPE_SEMA, error);

	HLEKernel::WaitExecTimeout<PSPSemaphore, WAITTYPE_SEMA>(threadID);

	// If in FIFO mode, that may have cleared another thread to wake up.
	PSPSemaphore *s = kernelObjects.Get<PSPSemaphore>(uid, error);
	if (s && (s->ns.attr & PSP_SEMA_ATTR_PRIORITY) == PSP_SEMA_ATTR_FIFO) {
		bool wokeThreads;
		std::vector<SceUID>::iterator iter = s->waitingThreads.begin();
		// Unlock every waiting thread until the first that must still wait.
		while (iter != s->waitingThreads.end() && __KernelUnlockSemaForThread(s, *iter, error, 0, wokeThreads)) {
			s->waitingThreads.erase(iter);
			iter = s->waitingThreads.begin();
		}
	}
}

static void __KernelSetSemaTimeout(PSPSemaphore *s, u32 timeoutPtr) {
	if (timeoutPtr == 0 || semaWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This happens to be how the hardware seems to time things.
	if (micro <= 3)
		micro = 24;
	else if (micro <= 249)
		micro = 245;

	// This should call __KernelSemaTimeout() later, unless we cancel it.
	CoreTiming::ScheduleEvent(usToCycles(micro), semaWaitTimer, __KernelGetCurThread());
}

static int __KernelWaitSema(SceUID id, int wantedCount, u32 timeoutPtr, bool processCallbacks)
{
	hleEatCycles(900);

	if (wantedCount <= 0)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;

	hleEatCycles(500);

	u32 error;
	PSPSemaphore *s = kernelObjects.Get<PSPSemaphore>(id, error);
	if (s)
	{
		if (wantedCount > s->ns.maxCount)
			return SCE_KERNEL_ERROR_ILLEGAL_COUNT;

		// If there are any callbacks, we always wait, and wake after the callbacks.
		bool hasCallbacks = processCallbacks && __KernelCurHasReadyCallbacks();
		if (s->ns.currentCount >= wantedCount && s->waitingThreads.size() == 0 && !hasCallbacks)
			s->ns.currentCount -= wantedCount;
		else
		{
			SceUID threadID = __KernelGetCurThread();
			// May be in a tight loop timing out (where we don't remove from waitingThreads yet), don't want to add duplicates.
			if (std::find(s->waitingThreads.begin(), s->waitingThreads.end(), threadID) == s->waitingThreads.end())
				s->waitingThreads.push_back(threadID);
			__KernelSetSemaTimeout(s, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_SEMA, id, wantedCount, timeoutPtr, processCallbacks, "sema waited");
		}

		return 0;
	}
	else
		return error;
}

int sceKernelWaitSema(SceUID id, int wantedCount, u32 timeoutPtr)
{
	int result = __KernelWaitSema(id, wantedCount, timeoutPtr, false);
	if (result == (int)SCE_KERNEL_ERROR_ILLEGAL_COUNT)
		DEBUG_LOG(Log::sceKernel, "SCE_KERNEL_ERROR_ILLEGAL_COUNT=sceKernelWaitSema(%i, %i, %i)", id, wantedCount, timeoutPtr);
	else if (result == 0)
		DEBUG_LOG(Log::sceKernel, "0=sceKernelWaitSema(%i, %i, %i)", id, wantedCount, timeoutPtr);
	else
		DEBUG_LOG(Log::sceKernel, "%08x=sceKernelWaitSema(%i, %i, %i)", result, id, wantedCount, timeoutPtr);
	return result;
}

int sceKernelWaitSemaCB(SceUID id, int wantedCount, u32 timeoutPtr)
{
	int result = __KernelWaitSema(id, wantedCount, timeoutPtr, true);
	if (result == (int)SCE_KERNEL_ERROR_ILLEGAL_COUNT)
		DEBUG_LOG(Log::sceKernel, "SCE_KERNEL_ERROR_ILLEGAL_COUNT=sceKernelWaitSemaCB(%i, %i, %i)", id, wantedCount, timeoutPtr);
	else if (result == 0)
		DEBUG_LOG(Log::sceKernel, "0=sceKernelWaitSemaCB(%i, %i, %i)", id, wantedCount, timeoutPtr);
	else
		DEBUG_LOG(Log::sceKernel, "%08x=sceKernelWaitSemaCB(%i, %i, %i)", result, id, wantedCount, timeoutPtr);
	return result;
}

// Should be same as WaitSema but without the wait, instead returning SCE_KERNEL_ERROR_SEMA_ZERO
int sceKernelPollSema(SceUID id, int wantedCount)
{
	if (wantedCount <= 0)
	{
		DEBUG_LOG(Log::sceKernel, "SCE_KERNEL_ERROR_ILLEGAL_COUNT=sceKernelPollSema(%i, %i)", id, wantedCount);
		return (int)SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	}

	u32 error;
	PSPSemaphore *s = kernelObjects.Get<PSPSemaphore>(id, error);
	if (s)
	{
		if (s->ns.currentCount >= wantedCount && s->waitingThreads.size() == 0)
		{
			DEBUG_LOG(Log::sceKernel, "0=sceKernelPollSema(%i, %i)", id, wantedCount);
			s->ns.currentCount -= wantedCount;
			return 0;
		}
		else
		{
			DEBUG_LOG(Log::sceKernel, "SCE_KERNEL_ERROR_SEMA_ZERO=sceKernelPollSema(%i, %i)", id, wantedCount);
			return SCE_KERNEL_ERROR_SEMA_ZERO;
		}
	}
	else
	{
		DEBUG_LOG(Log::sceKernel, "sceKernelPollSema(%i, %i): invalid semaphore", id, wantedCount);
		return error;
	}
}

// The below functions don't really belong to sceKernelSemaphore. They are the core crypto functionality,
// exposed through the confusingly named "sceUtilsBufferCopyWithRange" name, which Sony placed in the
// not-at-all-suspicious "semaphore" library, which has nothing to do with semaphores.

static u32 sceUtilsBufferCopyWithRange(u32 outAddr, int outSize, u32 inAddr, int inSize, int cmd)
{
	u8 *outAddress = Memory::IsValidRange(outAddr, outSize) ? Memory::GetPointerWriteUnchecked(outAddr) : nullptr;
	const u8 *inAddress = Memory::IsValidRange(inAddr, inSize) ? Memory::GetPointerUnchecked(inAddr) : nullptr;
	int temp = kirk_sceUtilsBufferCopyWithRange(outAddress, outSize, inAddress, inSize, cmd);
	if (temp != 0) {
		ERROR_LOG(Log::sceKernel, "hleUtilsBufferCopyWithRange: Failed with %d", temp);
	}
	return 0;
}

// Note sure what difference there is between this and sceUtilsBufferCopyWithRange.
static int sceUtilsBufferCopyByPollingWithRange(u32 outAddr, int outSize, u32 inAddr, int inSize, int cmd)
{
	u8 *outAddress = Memory::IsValidRange(outAddr, outSize) ? Memory::GetPointerWriteUnchecked(outAddr) : nullptr;
	const u8 *inAddress = Memory::IsValidRange(inAddr, inSize) ? Memory::GetPointerUnchecked(inAddr) : nullptr;
	return kirk_sceUtilsBufferCopyWithRange(outAddress, outSize, inAddress, inSize, cmd);
}

const HLEFunction semaphore[] = {
	{0x4C537C72, &WrapU_UIUII<sceUtilsBufferCopyWithRange>,          "sceUtilsBufferCopyWithRange",                   'x', "xixii" },
	{0x77E97079, &WrapI_UIUII<sceUtilsBufferCopyByPollingWithRange>, "sceUtilsBufferCopyByPollingWithRange",          'i', "xixii"  },
};

void Register_semaphore() {
	RegisterModule("semaphore", ARRAY_SIZE(semaphore), semaphore);
}
