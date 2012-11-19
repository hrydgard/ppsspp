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

// UNFINISHED

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../../Core/CoreTiming.h"
#include "sceKernel.h"
#include "sceKernelMutex.h"
#include "sceKernelThread.h"

#define PSP_MUTEX_ATTR_FIFO 0
#define PSP_MUTEX_ATTR_PRIORITY 0x100
#define PSP_MUTEX_ATTR_ALLOW_RECURSIVE 0x200

// Not sure about the names of these
#define PSP_MUTEX_ERROR_NO_SUCH_MUTEX 0x800201C3
#define PSP_MUTEX_ERROR_TRYLOCK_FAILED 0x800201C4
#define PSP_MUTEX_ERROR_NOT_LOCKED 0x800201C5
#define PSP_MUTEX_ERROR_LOCK_OVERFLOW 0x800201C6
#define PSP_MUTEX_ERROR_UNLOCK_UNDERFLOW 0x800201C7
#define PSP_MUTEX_ERROR_ALREADY_LOCKED 0x800201C8


// Guesswork - not exposed anyway
struct NativeMutex
{
	SceSize size;
	char name[32];
	SceUInt attr;

	int lockLevel;
	int lockThread;	// The thread holding the lock
};

struct Mutex : public KernelObject
{
	const char *GetName() {return nm.name;}
	const char *GetTypeName() {return "Mutex";}
	static u32 GetMissingErrorCode() { return PSP_MUTEX_ERROR_NO_SUCH_MUTEX; }	// Not sure?
	int GetIDType() const { return SCE_KERNEL_TMID_Mutex; }
	NativeMutex nm;
	std::vector<SceUID> waitingThreads;
	int waitTimer;
};

struct LWMutex : public KernelObject
{
	const char *GetName() {return nm.name;}
	const char *GetTypeName() {return "LWMutex";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_SEMID; }	// Not sure?
	int GetIDType() const { return SCE_KERNEL_TMID_LwMutex; }
	NativeMutex nm;
	std::vector<SceUID> waitingThreads;
};

void sceKernelCreateMutex(const char *name, u32 attr, int initialCount, u32 optionsPtr)
{
	u32 error = 0;
	if (!name)
		error = SCE_KERNEL_ERROR_ERROR;
	else if (initialCount < 0)
		error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	else if ((attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) == 0 && initialCount > 1)
		error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;

	if (error)
	{
		RETURN(error);
		return;
	}

	DEBUG_LOG(HLE,"sceKernelCreateMutex(%s, %08x, %d, %08x)", name, attr, initialCount, optionsPtr);

	Mutex *mutex = new Mutex();
	SceUID id = kernelObjects.Create(mutex);

	mutex->nm.size = sizeof(mutex);
	strncpy(mutex->nm.name, name, 31);
	mutex->nm.name[31] = 0;
	mutex->nm.attr = attr;
	mutex->nm.lockLevel = initialCount;
	if (mutex->nm.lockLevel == 0)
		mutex->nm.lockThread = -1;
	else
		mutex->nm.lockThread = __KernelGetCurThread();
	mutex->waitTimer = 0;

	if (optionsPtr != 0)
		WARN_LOG(HLE,"sceKernelCreateMutex(%s) unsupported options parameter.", name);

	RETURN(id);

	__KernelReSchedule("mutex created");
}

void sceKernelDeleteMutex(SceUID id)
{
	DEBUG_LOG(HLE,"sceKernelDeleteMutex(%i)", id);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (mutex)
	{
		std::vector<SceUID>::iterator iter, end;
		for (iter = mutex->waitingThreads.begin(), end = mutex->waitingThreads.end(); iter != end; ++iter)
		{
			SceUID threadID = *iter;

			u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
			if (timeoutPtr != 0 && mutex->waitTimer != 0)
			{
				// Remove any event for this thread.
				int cyclesLeft = CoreTiming::UnscheduleEvent(mutex->waitTimer, threadID);
				Memory::Write_U32(cyclesToUs(cyclesLeft), timeoutPtr);
			}

			__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		}
		mutex->waitingThreads.empty();

		// TODO: Any way to erase the CoreTiming event type?  We leak.

		RETURN(kernelObjects.Destroy<Mutex>(id));
		__KernelReSchedule("mutex deleted");
	}
	else
		RETURN(error);
}

bool __KernelLockMutex(Mutex *mutex, int count, u32 &error)
{
	if (!error)
	{
		if (count <= 0)
			error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
		else if (count > 1 && !(mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE))
			error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
		// Two positive ints will always sum to negative.
		else if (count + mutex->nm.lockLevel < 0)
			error = PSP_MUTEX_ERROR_LOCK_OVERFLOW;
	}

	if (error)
		return false;

	if (mutex->nm.lockLevel == 0)
	{
		mutex->nm.lockLevel += count;
		mutex->nm.lockThread = __KernelGetCurThread();
		// Nobody had it locked - no need to block
		return true;
	}

	if (mutex->nm.lockThread == __KernelGetCurThread())
	{
		// Recursive mutex, let's just increase the lock count and keep going
		if ((mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE))
		{
			mutex->nm.lockLevel += count;
			return true;
		}
		else
		{
			error = PSP_MUTEX_ERROR_ALREADY_LOCKED;
			return false;
		}
	}

	return false;
}

void __KernelMutexTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
}

void __kernelWaitMutex(Mutex *mutex, u32 timeoutPtr)
{
	if (timeoutPtr == 0)
		return;

	if (mutex->waitTimer == 0)
		mutex->waitTimer = CoreTiming::RegisterEvent("ScheduledTimeout", &__KernelMutexTimeout);

	// This should call __KernelMutexTimeout() later, unless we cancel it.
	int micro = (int) Memory::Read_U32(timeoutPtr);
	CoreTiming::ScheduleEvent(usToCycles(micro), mutex->waitTimer, __KernelGetCurThread());
}

// int sceKernelLockMutex(SceUID id, int count, int *timeout)
// void because it changes threads.
void sceKernelLockMutex(SceUID id, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelLockMutex(%i, %i, %08x)", id, count, timeoutPtr);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);

	if (__KernelLockMutex(mutex, count, error))
	{
		RETURN(0);
		__KernelReSchedule("mutex locked");
	}
	else if (error)
		RETURN(error);
	else
	{
		mutex->waitingThreads.push_back(__KernelGetCurThread());
		__kernelWaitMutex(mutex, timeoutPtr);
		__KernelWaitCurThread(WAITTYPE_MUTEX, id, count, timeoutPtr, false);
	}
}

// int sceKernelLockMutexCB(SceUID id, int count, int *timeout)
// void because it changes threads.
void sceKernelLockMutexCB(SceUID id, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelLockMutexCB(%i, %i, %08x)", id, count, timeoutPtr);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);

	if (__KernelLockMutex(mutex, count, error))
	{
		RETURN(0);
		__KernelReSchedule("mutex locked");
	}
	else if (error)
		RETURN(error);
	else
	{
		mutex->waitingThreads.push_back(__KernelGetCurThread());
		__kernelWaitMutex(mutex, timeoutPtr);
		__KernelWaitCurThread(WAITTYPE_MUTEX, id, count, timeoutPtr, true);
		__KernelCheckCallbacks();
	}

	__KernelReSchedule("mutex locked");
}

// int sceKernelTryLockMutex(SceUID id, int count)
// void because it changes threads.
void sceKernelTryLockMutex(SceUID id, int count)
{
	DEBUG_LOG(HLE,"sceKernelTryLockMutex(%i, %i)", id, count);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);

	if (__KernelLockMutex(mutex, count, error))
	{
		RETURN(0);
		__KernelReSchedule("mutex trylocked");
	}
	else if (error)
		RETURN(error);
	else
		RETURN(PSP_MUTEX_ERROR_TRYLOCK_FAILED);
}

// int sceKernelUnlockMutex(SceUID id, int count)
// void because it changes threads.
void sceKernelUnlockMutex(SceUID id, int count)
{
	DEBUG_LOG(HLE,"sceKernelUnlockMutex(%i, %i)", id, count);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);

	if (!error)
	{
		if (count <= 0)
			error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
		else if ((mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) == 0 && count > 1)
			error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
		else if (mutex->nm.lockLevel == 0)
			error = PSP_MUTEX_ERROR_NOT_LOCKED;
		else if (mutex->nm.lockLevel < count)
			error = PSP_MUTEX_ERROR_UNLOCK_UNDERFLOW;
	}

	if (error)
	{
		RETURN(error);
		return;
	}

	mutex->nm.lockLevel -= count;
	RETURN(0);

	if (mutex->nm.lockLevel == 0)
	{
		mutex->nm.lockThread = -1;

		// TODO: PSP_MUTEX_ATTR_PRIORITY
		bool wokeThreads = false;
		std::vector<SceUID>::iterator iter, end;
		for (iter = mutex->waitingThreads.begin(), end = mutex->waitingThreads.end(); iter != end; ++iter)
		{
			SceUID threadID = *iter;

			int wVal = (int)__KernelGetWaitValue(threadID, error);
			u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);

			mutex->nm.lockThread = threadID;
			mutex->nm.lockLevel = wVal;

			if (timeoutPtr != 0 && mutex->waitTimer != 0)
			{
				// Remove any event for this thread.
				int cyclesLeft = CoreTiming::UnscheduleEvent(mutex->waitTimer, threadID);
				Memory::Write_U32(cyclesToUs(cyclesLeft), timeoutPtr);
			}

			__KernelResumeThreadFromWait(threadID, 0);
			wokeThreads = true;
			mutex->waitingThreads.erase(iter);
			break;
		}

		__KernelReSchedule("mutex unlocked");
	}
}

struct NativeLwMutex
{
	SceSize size;
	char name[32];
	SceUInt attr;
	SceUID mutexUid;
	SceUInt opaqueWorkAreaAddr;
	int numWaitThreads;
	int locked;
	int threadid;  // thread holding the lock
};

void sceKernelCreateLwMutex()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelCreateLwMutex()");
	RETURN(0);
}

void sceKernelDeleteLwMutex()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelDeleteLwMutex()");
	RETURN(0);
}

void sceKernelTryLockLwMutex()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelTryLockLwMutex()");
	RETURN(0);
}

void sceKernelLockLwMutex()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelLockLwMutex()");
	RETURN(0);
}

void sceKernelLockLwMutexCB()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelLockLwMutexCB()");
	RETURN(0);
}

void sceKernelUnlockLwMutex()
{
	ERROR_LOG(HLE,"UNIMPL void sceKernelUnlockLwMutex()");
	RETURN(0);
}