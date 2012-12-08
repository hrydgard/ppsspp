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
#include "../../Core/CoreTiming.h"
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
	char 		name[32];
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
	int GetIDType() const { return SCE_KERNEL_TMID_Semaphore; }

	NativeSemaphore ns;
	std::vector<SceUID> waitingThreads;
};

bool semaInitComplete = false;
int semaWaitTimer = 0;

void __KernelSemaInit()
{
	semaWaitTimer = CoreTiming::RegisterEvent("SemaphoreTimeout", &__KernelSemaTimeout);
	semaInitComplete = true;
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
		s->ns.numWaitThreads--;
	}

	if (timeoutPtr != 0 && semaWaitTimer != 0)
	{
		// Remove any event for this thread.
		u64 cyclesLeft = CoreTiming::UnscheduleEvent(semaWaitTimer, threadID);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(threadID, result);
	wokeThreads = true;
	return true;
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

std::vector<SceUID>::iterator __KernelSemaFindPriority(std::vector<SceUID> &waiting, std::vector<SceUID>::iterator begin)
{
	_dbg_assert_msg_(HLE, !waiting.empty(), "__KernelSemaFindPriority: Trying to find best of no threads.");

	std::vector<SceUID>::iterator iter, end, best = waiting.end();
	u32 best_prio = 0xFFFFFFFF;
	for (iter = begin, end = waiting.end(); iter != end; ++iter)
	{
		u32 iter_prio = __KernelGetThreadPrio(*iter);
		if (iter_prio < best_prio)
		{
			best = iter;
			best_prio = iter_prio;
		}
	}

	_dbg_assert_msg_(HLE, best != waiting.end(), "__KernelSemaFindPriority: Returning invalid best thread.");
	return best;
}

// int sceKernelCancelSema(SceUID id, int newCount, int *numWaitThreads);
// void because it changes threads.
void sceKernelCancelSema(SceUID id, int newCount, u32 numWaitThreadsPtr)
{
	DEBUG_LOG(HLE,"sceKernelCancelSema(%i)", id);

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (newCount > s->ns.maxCount)
		{
			RETURN(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
			return;
		}

		if (numWaitThreadsPtr)
		{
			Memory::Write_U32(s->ns.numWaitThreads, numWaitThreadsPtr);
		}

		if (newCount < 0)
			s->ns.currentCount = s->ns.initCount;
		else
			s->ns.currentCount = newCount;
		s->ns.numWaitThreads = 0;

		// We need to set the return value BEFORE rescheduling threads.
		RETURN(0);

		if (__KernelClearSemaThreads(s, SCE_KERNEL_ERROR_WAIT_CANCEL))
			__KernelReSchedule("semaphore canceled");
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelCancelSema : Trying to cancel invalid semaphore %i", id);
		RETURN(error);
	}
}

//SceUID sceKernelCreateSema(const char *name, SceUInt attr, int initVal, int maxVal, SceKernelSemaOptParam *option);
// void because it changes threads.
void sceKernelCreateSema(const char* name, u32 attr, int initVal, int maxVal, u32 optionPtr)
{
	if (!semaInitComplete)
		__KernelSemaInit();

	if (!name)
	{
		RETURN(SCE_KERNEL_ERROR_ERROR);
		return;
	}

	Semaphore *s = new Semaphore;
	SceUID id = kernelObjects.Create(s);

	s->ns.size = sizeof(NativeSemaphore);
	strncpy(s->ns.name, name, 31);
	s->ns.name[31] = 0;
	s->ns.attr = attr;
	s->ns.initCount = initVal;
	s->ns.currentCount = s->ns.initCount;
	s->ns.maxCount = maxVal;
	s->ns.numWaitThreads = 0;

	DEBUG_LOG(HLE,"%i=sceKernelCreateSema(%s, %08x, %i, %i, %08x)", id, s->ns.name, s->ns.attr, s->ns.initCount, s->ns.maxCount, optionPtr);

	if (optionPtr != 0)
		WARN_LOG(HLE,"sceKernelCreateSema(%s) unsupported options parameter.", name);

	RETURN(id);
}

//int sceKernelDeleteSema(SceUID semaid);
// void because it changes threads.
void sceKernelDeleteSema(SceUID id)
{
	DEBUG_LOG(HLE,"sceKernelDeleteSema(%i)", id);

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		bool wokeThreads = __KernelClearSemaThreads(s, SCE_KERNEL_ERROR_WAIT_DELETE);
		RETURN(kernelObjects.Destroy<Semaphore>(id));

		if (wokeThreads)
			__KernelReSchedule("semaphore deleted");
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelDeleteSema : Trying to delete invalid semaphore %i", id);
		RETURN(error);
	}
}

//int sceKernelDeleteSema(SceUID semaid, SceKernelSemaInfo *info);
// void because it changes threads.
void sceKernelReferSemaStatus(SceUID id, u32 infoPtr)
{
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		DEBUG_LOG(HLE,"sceKernelReferSemaStatus(%i, %08x)", id, infoPtr);
		Memory::WriteStruct(infoPtr, &s->ns);
		RETURN(0);
	}
	else
	{
		ERROR_LOG(HLE,"Error %08x", error);
		RETURN(error);
	}
}
	
//int sceKernelSignalSema(SceUID semaid, int signal);
// void because it changes threads.
void sceKernelSignalSema(SceUID id, int signal)
{
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (s->ns.currentCount + signal - s->ns.numWaitThreads > s->ns.maxCount)
		{
			RETURN(SCE_KERNEL_ERROR_SEMA_OVF);
			return;
		}

		int oldval = s->ns.currentCount;
		s->ns.currentCount += signal;
		DEBUG_LOG(HLE,"sceKernelSignalSema(%i, %i) (old: %i, new: %i)", id, signal, oldval, s->ns.currentCount);

		// We need to set the return value BEFORE processing other threads.
		RETURN(0);

		bool wokeThreads = false;
		std::vector<SceUID>::iterator iter, end, best;
retry:
		for (iter = s->waitingThreads.begin(), end = s->waitingThreads.end(); iter != end; ++iter)
		{
			if ((s->ns.attr & PSP_SEMA_ATTR_PRIORITY) != 0)
				best = __KernelSemaFindPriority(s->waitingThreads, iter);
			else
				best = iter;

			if (__KernelUnlockSemaForThread(s, *best, error, 0, wokeThreads))
			{
				s->waitingThreads.erase(best);
				goto retry;
			}
		}

		if (wokeThreads)
			__KernelReSchedule("semaphore signaled");
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelSignalSema : Trying to signal invalid semaphore %i", id);
		RETURN(error;)
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
		s->ns.numWaitThreads--;
	}

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
}

void __KernelSetSemaTimeout(Semaphore *s, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || semaWaitTimer == 0)
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

void __KernelWaitSema(SceUID id, int wantedCount, u32 timeoutPtr, const char *badSemaMessage, bool processCallbacks)
{
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (wantedCount > s->ns.maxCount || wantedCount <= 0)
		{
			RETURN(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
			return;
		}

		// We need to set the return value BEFORE processing callbacks / etc.
		RETURN(0);

		if (s->ns.currentCount >= wantedCount)
		{
			s->ns.currentCount -= wantedCount;
			if (processCallbacks)
			{
				bool callbacksProcessed = __KernelForceCallbacks();
				if (callbacksProcessed)
					__KernelExecutePendingMipsCalls();
			}
		}
		else
		{
			s->ns.numWaitThreads++;
			s->waitingThreads.push_back(__KernelGetCurThread());
			__KernelSetSemaTimeout(s, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_SEMA, id, wantedCount, timeoutPtr, processCallbacks);
			if (processCallbacks)
				__KernelCheckCallbacks();
		}
	}
	else
	{
		ERROR_LOG(HLE, badSemaMessage, id);
		RETURN(error);
	}
}

//int sceKernelWaitSema(SceUID semaid, int signal, SceUInt *timeout);
// void because it changes threads.
void sceKernelWaitSema(SceUID id, int wantedCount, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelWaitSema(%i, %i, %i)", id, wantedCount, timeoutPtr);

	__KernelWaitSema(id, wantedCount, timeoutPtr, "sceKernelWaitSema: Trying to wait for invalid semaphore %i", false);
} 

//int sceKernelWaitSemaCB(SceUID semaid, int signal, SceUInt *timeout);
// void because it changes threads.
void sceKernelWaitSemaCB(SceUID id, int wantedCount, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelWaitSemaCB(%i, %i, %i)", id, wantedCount, timeoutPtr);

	__KernelWaitSema(id, wantedCount, timeoutPtr, "sceKernelWaitSemaCB: Trying to wait for invalid semaphore %i", true);
}

// Should be same as WaitSema but without the wait, instead returning SCE_KERNEL_ERROR_SEMA_ZERO
void sceKernelPollSema(SceUID id, int wantedCount)
{
	DEBUG_LOG(HLE,"sceKernelPollSema(%i, %i)", id, wantedCount);

	if (wantedCount <= 0)
	{
		RETURN(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
		return;
	}

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (s->ns.currentCount >= wantedCount)
		{
			s->ns.currentCount -= wantedCount;
			RETURN(0);
		}
		else
			RETURN(SCE_KERNEL_ERROR_SEMA_ZERO);
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelPollSema: Trying to poll invalid semaphore %i", id);
		RETURN(error);
	}
}

