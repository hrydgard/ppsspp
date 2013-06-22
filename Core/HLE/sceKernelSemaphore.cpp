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
#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "ChunkFile.h"
#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelSemaphore.h"

#define PSP_SEMA_ATTR_FIFO 0
#define PSP_SEMA_ATTR_PRIORITY 0x100

/** Current state of a semaphore.
* @see sceKernelReferSemaStatus.
*/

struct NativeSemaphore
{
	/** Size of the ::SceKernelSemaInfo structure. */
	SceSize 	size;
	/** NUL-terminated name of the semaphore. */
	char 		name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	/** Attributes. */
	SceUInt 	attr;
	/** The initial count the semaphore was created with. */
	int 		initCount;
	/** The current count. */
	int 		currentCount;
	/** The maximum count. */
	int 		maxCount;
	/** The number of threads waiting on the semaphore. */
	int 		numWaitThreads;
};


struct Semaphore : public KernelObject 
{
	const char *GetName() {return ns.name;}
	const char *GetTypeName() {return "Semaphore";}

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_SEMID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Semaphore; }
	int GetIDType() const { return SCE_KERNEL_TMID_Semaphore; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(ns);
		SceUID dv = 0;
		p.Do(waitingThreads, dv);
		p.Do(pausedWaitTimeouts);
		p.DoMarker("Semaphore");
	}

	NativeSemaphore ns;
	std::vector<SceUID> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, u64> pausedWaitTimeouts;
};

static int semaWaitTimer = -1;

void __KernelSemaBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelSemaEndCallback(SceUID threadID, SceUID prevCallbackId, u32 &returnValue);

void __KernelSemaInit()
{
	semaWaitTimer = CoreTiming::RegisterEvent("SemaphoreTimeout", __KernelSemaTimeout);
	__KernelRegisterWaitTypeFuncs(WAITTYPE_SEMA, __KernelSemaBeginCallback, __KernelSemaEndCallback);
}

void __KernelSemaDoState(PointerWrap &p)
{
	p.Do(semaWaitTimer);
	CoreTiming::RestoreRegisterEvent(semaWaitTimer, "SemaphoreTimeout", __KernelSemaTimeout);
	p.DoMarker("sceKernelSema");
}

KernelObject *__KernelSemaphoreObject()
{
	return new Semaphore;
}

// Returns whether the thread should be removed.
bool __KernelUnlockSemaForThread(Semaphore *s, SceUID threadID, u32 &error, int result, bool &wokeThreads)
{
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_SEMA, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);

	// The waitID may be different after a timeout.
	if (waitID != s->GetUID())
		return true;

	// If result is an error code, we're just letting it go.
	if (result == 0)
	{
		int wVal = (int) __KernelGetWaitValue(threadID, error);
		if (wVal > s->ns.currentCount)
			return false;

		s->ns.currentCount -= wVal;
	}

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
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	u32 error;
	SceUID semaID = __KernelGetWaitID(threadID, WAITTYPE_SEMA, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	Semaphore *s = semaID == 0 ? NULL : kernelObjects.Get<Semaphore>(semaID, error);
	if (s)
	{
		// This means two callbacks in a row.  PSP crashes if the same callback runs inside itself.
		// TODO: Handle this better?
		if (s->pausedWaitTimeouts.find(pauseKey) != s->pausedWaitTimeouts.end())
			return;

		if (timeoutPtr != 0 && semaWaitTimer != -1)
		{
			s64 cyclesLeft = CoreTiming::UnscheduleEvent(semaWaitTimer, threadID);
			s->pausedWaitTimeouts[pauseKey] = CoreTiming::GetTicks() + cyclesLeft;
		}
		else
			s->pausedWaitTimeouts[pauseKey] = 0;

		// TODO: Hmm, what about priority/fifo order?  Does it lose its place in line?
		s->waitingThreads.erase(std::remove(s->waitingThreads.begin(), s->waitingThreads.end(), threadID), s->waitingThreads.end());

		DEBUG_LOG(HLE, "sceKernelWaitSemaCB: Suspending sema wait for callback");
	}
	else
		WARN_LOG_REPORT(HLE, "sceKernelWaitSemaCB: beginning callback with bad wait id?");
}

void __KernelSemaEndCallback(SceUID threadID, SceUID prevCallbackId, u32 &returnValue)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	// Note: Cancel does not affect suspended semaphore waits.

	u32 error;
	SceUID semaID = __KernelGetWaitID(threadID, WAITTYPE_SEMA, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	Semaphore *s = semaID == 0 ? NULL : kernelObjects.Get<Semaphore>(semaID, error);
	if (!s || s->pausedWaitTimeouts.find(pauseKey) == s->pausedWaitTimeouts.end())
	{
		// TODO: Since it was deleted, we don't know how long was actually left.
		// For now, we just say the full time was taken.
		if (timeoutPtr != 0 && semaWaitTimer != -1)
			Memory::Write_U32(0, timeoutPtr);

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		return;
	}

	u64 waitDeadline = s->pausedWaitTimeouts[pauseKey];
	s->pausedWaitTimeouts.erase(pauseKey);

	// TODO: Don't wake up if __KernelCurHasReadyCallbacks()?

	bool wokeThreads;
	// Attempt to unlock.
	if (__KernelUnlockSemaForThread(s, threadID, error, 0, wokeThreads))
		return;

	// We only check if it timed out if it couldn't unlock.
	s64 cyclesLeft = waitDeadline - CoreTiming::GetTicks();
	if (cyclesLeft < 0 && waitDeadline != 0)
	{
		if (timeoutPtr != 0 && semaWaitTimer != -1)
			Memory::Write_U32(0, timeoutPtr);

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	}
	else
	{
		if (timeoutPtr != 0 && semaWaitTimer != -1)
			CoreTiming::ScheduleEvent(cyclesLeft, semaWaitTimer, __KernelGetCurThread());

		// TODO: Should this not go at the end?
		s->waitingThreads.push_back(threadID);

		DEBUG_LOG(HLE, "sceKernelWaitSemaCB: Resuming sema wait for callback");
	}
}

// Resume all waiting threads (for delete / cancel.)
// Returns true if it woke any threads.
bool __KernelClearSemaThreads(Semaphore *s, int reason)
{
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
	DEBUG_LOG(HLE, "sceKernelCancelSema(%i, %i, %08x)", id, newCount, numWaitThreadsPtr);

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (newCount > s->ns.maxCount)
			return SCE_KERNEL_ERROR_ILLEGAL_COUNT;

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
		ERROR_LOG(HLE, "sceKernelCancelSema : Trying to cancel invalid semaphore %i", id);
		return error;
	}
}

int sceKernelCreateSema(const char* name, u32 attr, int initVal, int maxVal, u32 optionPtr)
{
	if (!name)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateSema(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}
	if (attr >= 0x200)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateSema(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}

	Semaphore *s = new Semaphore;
	SceUID id = kernelObjects.Create(s);

	s->ns.size = sizeof(NativeSemaphore);
	strncpy(s->ns.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	s->ns.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	s->ns.attr = attr;
	s->ns.initCount = initVal;
	s->ns.currentCount = s->ns.initCount;
	s->ns.maxCount = maxVal;
	s->ns.numWaitThreads = 0;

	DEBUG_LOG(HLE, "%i=sceKernelCreateSema(%s, %08x, %i, %i, %08x)", id, s->ns.name, s->ns.attr, s->ns.initCount, s->ns.maxCount, optionPtr);

	if (optionPtr != 0)
	{
		u32 size = Memory::Read_U32(optionPtr);
		if (size != 0)
			WARN_LOG_REPORT(HLE, "sceKernelCreateSema(%s) unsupported options parameter, size = %d", name, size);
	}
	if ((attr & ~PSP_SEMA_ATTR_PRIORITY) != 0)
		WARN_LOG_REPORT(HLE, "sceKernelCreateSema(%s) unsupported attr parameter: %08x", name, attr);

	return id;
}

int sceKernelDeleteSema(SceUID id)
{
	DEBUG_LOG(HLE, "sceKernelDeleteSema(%i)", id);

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		bool wokeThreads = __KernelClearSemaThreads(s, SCE_KERNEL_ERROR_WAIT_DELETE);
		if (wokeThreads)
			hleReSchedule("semaphore deleted");

		return kernelObjects.Destroy<Semaphore>(id);
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelDeleteSema : Trying to delete invalid semaphore %i", id);
		return error;
	}
}

int sceKernelReferSemaStatus(SceUID id, u32 infoPtr)
{
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		DEBUG_LOG(HLE, "sceKernelReferSemaStatus(%i, %08x)", id, infoPtr);

		if (!Memory::IsValidAddress(infoPtr))
			return -1;

		u32 error;
		for (auto iter = s->waitingThreads.begin(); iter != s->waitingThreads.end(); ++iter)
		{
			SceUID waitID = __KernelGetWaitID(*iter, WAITTYPE_SEMA, error);
			// The thread is no longer waiting for this, clean it up.
			if (waitID != id)
				s->waitingThreads.erase(iter--);
		}

		s->ns.numWaitThreads = (int) s->waitingThreads.size();
		if (Memory::Read_U32(infoPtr) != 0)
			Memory::WriteStruct(infoPtr, &s->ns);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelReferSemaStatus: error %08x", error);
		return error;
	}
}

int sceKernelSignalSema(SceUID id, int signal)
{
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (s->ns.currentCount + signal - (int) s->waitingThreads.size() > s->ns.maxCount)
			return SCE_KERNEL_ERROR_SEMA_OVF;

		int oldval = s->ns.currentCount;
		s->ns.currentCount += signal;
		DEBUG_LOG(HLE, "sceKernelSignalSema(%i, %i) (old: %i, new: %i)", id, signal, oldval, s->ns.currentCount);

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

		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelSignalSema : Trying to signal invalid semaphore %i", id);
		return error;
	}
}

void __KernelSemaTimeout(u64 userdata, int cycleslate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	SceUID semaID = __KernelGetWaitID(threadID, WAITTYPE_SEMA, error);
	Semaphore *s = kernelObjects.Get<Semaphore>(semaID, error);
	if (s)
	{
		// This thread isn't waiting anymore, but we'll remove it from waitingThreads later.
		// The reason is, if it times out, but what it was waiting on is DELETED prior to it
		// actually running, it will get a DELETE result instead of a TIMEOUT.
		// So, we need to remember it or we won't be able to mark it DELETE instead later.
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	}
}

void __KernelSetSemaTimeout(Semaphore *s, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || semaWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This happens to be how the hardware seems to time things.
	if (micro <= 3)
		micro = 15;
	else if (micro <= 249)
		micro = 250;

	// This should call __KernelSemaTimeout() later, unless we cancel it.
	CoreTiming::ScheduleEvent(usToCycles(micro), semaWaitTimer, __KernelGetCurThread());
}

int __KernelWaitSema(SceUID id, int wantedCount, u32 timeoutPtr, const char *badSemaMessage, bool processCallbacks)
{
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (wantedCount > s->ns.maxCount || wantedCount <= 0)
			return SCE_KERNEL_ERROR_ILLEGAL_COUNT;

		// If there are any callbacks, we always wait, and wake after the callbacks.
		bool hasCallbacks = processCallbacks && __KernelCurHasReadyCallbacks();
		if (s->ns.currentCount >= wantedCount && s->waitingThreads.size() == 0 && !hasCallbacks)
		{
			if (hasCallbacks)
			{
				// Might actually end up having to wait, so set the timeout.
				__KernelSetSemaTimeout(s, timeoutPtr);
				__KernelWaitCallbacksCurThread(WAITTYPE_SEMA, id, wantedCount, timeoutPtr);
			}
			else
				s->ns.currentCount -= wantedCount;
		}
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
	{
		ERROR_LOG(HLE, badSemaMessage, id);
		return error;
	}
}

int sceKernelWaitSema(SceUID id, int wantedCount, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelWaitSema(%i, %i, %i)", id, wantedCount, timeoutPtr);

	return __KernelWaitSema(id, wantedCount, timeoutPtr, "sceKernelWaitSema: Trying to wait for invalid semaphore %i", false);
}

int sceKernelWaitSemaCB(SceUID id, int wantedCount, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelWaitSemaCB(%i, %i, %i)", id, wantedCount, timeoutPtr);

	return __KernelWaitSema(id, wantedCount, timeoutPtr, "sceKernelWaitSemaCB: Trying to wait for invalid semaphore %i", true);
}

// Should be same as WaitSema but without the wait, instead returning SCE_KERNEL_ERROR_SEMA_ZERO
int sceKernelPollSema(SceUID id, int wantedCount)
{
	DEBUG_LOG(HLE, "sceKernelPollSema(%i, %i)", id, wantedCount);

	if (wantedCount <= 0)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (s->ns.currentCount >= wantedCount && s->waitingThreads.size() == 0)
		{
			s->ns.currentCount -= wantedCount;
			return 0;
		}
		else
			return SCE_KERNEL_ERROR_SEMA_ZERO;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelPollSema: Trying to poll invalid semaphore %i", id);
		return error;
	}
}

