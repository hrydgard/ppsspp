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
#include "sceKernel.h"
#include "sceKernelMutex.h"
#include "sceKernelThread.h"

#define PSP_MUTEX_ATTR_FIFO 0
#define PSP_MUTEX_ATTR_PRIORITY 0x100
#define PSP_MUTEX_ATTR_ALLOW_RECURSIVE 0x200

// Not sure about the names of these
#define PSP_MUTEX_ERROR_NOT_LOCKED 0x800201C5
#define PSP_MUTEX_ERROR_NO_SUCH_MUTEX 0x800201C3
#define PSP_MUTEX_ERROR_UNLOCK_UNDERFLOW 0x800201C7

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
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_SEMID; }	// Not sure?
	int GetIDType() const { return SCE_KERNEL_TMID_Mutex; }
	NativeMutex nm;
	std::vector<SceUID> waitingThreads;
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

SceUID sceKernelCreateMutex(const char *name, u32 attr, int initialCount, u32 optionsPtr)
{
	DEBUG_LOG(HLE,"sceKernelCreateMutex(%s, %08x, %d, %08x)", name, attr, initialCount, optionsPtr);

	Mutex *mutex = new Mutex();
	SceUID id = kernelObjects.Create(mutex);

	mutex->nm.size = sizeof(mutex);
	mutex->nm.attr = attr;
	mutex->nm.lockLevel = initialCount;
	// TODO: Does initial_count > 0 mean lock automatically by the current thread?  Would make sense.
	mutex->nm.lockThread = -1;

	strncpy(mutex->nm.name, name, 32);
	return id;
}

void sceKernelDeleteMutex(SceUID id)
{
	DEBUG_LOG(HLE,"sceKernelDeleteMutex(%i)", id);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (mutex)
	{
		RETURN(0);

		kernelObjects.Destroy<Mutex>(id);
		// TODO: Almost certainly need to reschedule (sometimes?)
	}
	else
		RETURN(PSP_MUTEX_ERROR_NO_SUCH_MUTEX);
}

// int sceKernelLockMutex(SceUID id, int count, int *timeout)
// void because it changes threads.
void sceKernelLockMutex(SceUID id, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelLockMutex(%i, %i, %08x)", id, count, timeoutPtr);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (!mutex)
	{
		RETURN(PSP_MUTEX_ERROR_NO_SUCH_MUTEX);
		return;
	}
	if (count <= 0)
	{
		RETURN(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
		return;
	}
	if (count > 1 && !(mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE))
	{
		RETURN(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
		return;
	}

	RETURN(0);

	if (mutex->nm.lockLevel == 0)
	{
		mutex->nm.lockLevel += count;
		mutex->nm.lockThread = __KernelGetCurThread();
		// Nobody had it locked - no need to block
	}
	else if ((mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) && mutex->nm.lockThread == __KernelGetCurThread())
	{
		// Recursive mutex, let's just increase the lock count and keep going
		mutex->nm.lockLevel += count;
	}
	else
	{
		mutex->waitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_MUTEX, id, count, 0, false);
	}
}

// int sceKernelLockMutexCB(SceUID id, int count, int *timeout)
// void because it changes threads.
void sceKernelLockMutexCB(SceUID id, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelLockMutexCB(%i, %i, %08x)", id, count, timeoutPtr);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (!mutex)
	{
		RETURN(PSP_MUTEX_ERROR_NO_SUCH_MUTEX);
		return;
	}
	if (count <= 0)
	{
		RETURN(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
		return;
	}
	if (count > 1 && !(mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE))
	{
		RETURN(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
		return;
	}

	RETURN(0);

	if (mutex->nm.lockLevel == 0)
	{
		mutex->nm.lockLevel += count;
		mutex->nm.lockThread = __KernelGetCurThread();
		// Nobody had it locked - no need to block
	}
	else if ((mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) && mutex->nm.lockThread == __KernelGetCurThread())
	{
		// Recursive mutex, let's just increase the lock count and keep going
		mutex->nm.lockLevel += count;
	}
	else
	{
		mutex->waitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_MUTEX, id, count, 0, true);
		__KernelCheckCallbacks();
	}

	__KernelReSchedule("mutex locked");
}

// int sceKernelUnlockMutex(SceUID id, int count)
// void because it changes threads.
void sceKernelUnlockMutex(SceUID id, int count)
{
	DEBUG_LOG(HLE,"sceKernelUnlockMutex(%i, %i)", id, count);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (!mutex)
	{
		RETURN(PSP_MUTEX_ERROR_NO_SUCH_MUTEX);
		return;
	}
	if (mutex->nm.lockLevel == 0)
	{
		RETURN(PSP_MUTEX_ERROR_NOT_LOCKED);
		return;
	}
	if (mutex->nm.lockLevel < count)
	{
		RETURN(PSP_MUTEX_ERROR_UNLOCK_UNDERFLOW);
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

			mutex->nm.lockThread = threadID;
			mutex->nm.lockLevel = wVal;

			__KernelResumeThreadFromWait(threadID);
			wokeThreads = true;
			mutex->waitingThreads.erase(iter);
			break;
		}

		// Not sure if this should actually resched, need to test.
		if (wokeThreads)
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