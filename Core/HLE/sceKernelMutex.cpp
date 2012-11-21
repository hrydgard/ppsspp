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

#include <algorithm>
#include <map>
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

#define PSP_LWMUTEX_ERROR_NO_SUCH_LWMUTEX 0x800201CA
// Note: used only for _600.
#define PSP_LWMUTEX_ERROR_TRYLOCK_FAILED 0x800201CB
#define PSP_LWMUTEX_ERROR_NOT_LOCKED 0x800201CC
#define PSP_LWMUTEX_ERROR_LOCK_OVERFLOW 0x800201CD
#define PSP_LWMUTEX_ERROR_UNLOCK_UNDERFLOW 0x800201CE
#define PSP_LWMUTEX_ERROR_ALREADY_LOCKED 0x800201CF

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
	static u32 GetMissingErrorCode() { return PSP_MUTEX_ERROR_NO_SUCH_MUTEX; }
	int GetIDType() const { return SCE_KERNEL_TMID_Mutex; }
	NativeMutex nm;
	std::vector<SceUID> waitingThreads;
};

// Guesswork - not exposed anyway
struct NativeLwMutex
{
	SceSize size;
	char name[32];
	SceUInt attr;
	SceUInt workareaPtr;
};

struct NativeLwMutexWorkarea
{
	int lockLevel;
	SceUID lockThread;
	int attr;
	int numWaitThreads;
	SceUID uid;
	int pad[3];

	void init()
	{
		memset(this, 0, sizeof(NativeLwMutexWorkarea));
	}

	void clear()
	{
		lockLevel = 0;
		lockThread = -1;
		uid = -1;
	}
};

struct LwMutex : public KernelObject
{
	const char *GetName() {return nm.name;}
	const char *GetTypeName() {return "LwMutex";}
	static u32 GetMissingErrorCode() { return PSP_LWMUTEX_ERROR_NO_SUCH_LWMUTEX; }
	int GetIDType() const { return SCE_KERNEL_TMID_LwMutex; }
	NativeLwMutex nm;
	std::vector<SceUID> waitingThreads;
};

bool mutexInitComplete = false;
int mutexWaitTimer = 0;
int lwMutexWaitTimer = 0;
// Thread -> Mutex locks for thread end.
std::map<SceUID, SceUID> mutexHeldLocks;

void __KernelMutexInit()
{
	mutexWaitTimer = CoreTiming::RegisterEvent("MutexTimeout", &__KernelMutexTimeout);
	lwMutexWaitTimer = CoreTiming::RegisterEvent("LwMutexTimeout", &__KernelLwMutexTimeout);

	// TODO: Install on first mutex (if it's slow?)
	__KernelListenThreadEnd(&__KernelMutexThreadEnd);

	mutexInitComplete = true;
}

void __KernelMutexAcquireLock(Mutex *mutex, int count, SceUID thread)
{
	_dbg_assert_msg_(HLE, mutexHeldLocks.find(threadID) == mutexHeldLocks.end(), "Thread %d wasn't removed from mutexHeldLocks properly.");
	mutexHeldLocks.insert(std::make_pair(thread, mutex->GetUID()));

	mutex->nm.lockLevel = count;
	mutex->nm.lockThread = thread;
}

void __KernelMutexAcquireLock(Mutex *mutex, int count)
{
	__KernelMutexAcquireLock(mutex, count, __KernelGetCurThread());
}

void __KernelMutexEraseLock(Mutex *mutex)
{
	if (mutex->nm.lockThread != -1)
		mutexHeldLocks.erase(mutex->nm.lockThread);
	mutex->nm.lockThread = -1;
}

void sceKernelCreateMutex(const char *name, u32 attr, int initialCount, u32 optionsPtr)
{
	if (!mutexInitComplete)
		__KernelMutexInit();

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
	if (initialCount == 0)
	{
		mutex->nm.lockLevel = 0;
		mutex->nm.lockThread = -1;
	}
	else
		__KernelMutexAcquireLock(mutex, initialCount);

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
			if (timeoutPtr != 0 && mutexWaitTimer != 0)
			{
				// Remove any event for this thread.
				int cyclesLeft = CoreTiming::UnscheduleEvent(mutexWaitTimer, threadID);
				Memory::Write_U32(cyclesToUs(cyclesLeft), timeoutPtr);
			}

			__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		}
		if (mutex->nm.lockThread != -1)
			__KernelMutexEraseLock(mutex);
		mutex->waitingThreads.empty();

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
		// Two positive ints will always overflow to negative.
		else if (count + mutex->nm.lockLevel < 0)
			error = PSP_MUTEX_ERROR_LOCK_OVERFLOW;
	}

	if (error)
		return false;

	if (mutex->nm.lockLevel == 0)
	{
		__KernelMutexAcquireLock(mutex, count);
		// Nobody had it locked - no need to block
		return true;
	}

	if (mutex->nm.lockThread == __KernelGetCurThread())
	{
		// Recursive mutex, let's just increase the lock count and keep going
		if (mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE)
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

bool __KernelUnlockMutex(Mutex *mutex, u32 &error)
{
	__KernelMutexEraseLock(mutex);

	// TODO: PSP_MUTEX_ATTR_PRIORITY
	bool wokeThreads = false;
	std::vector<SceUID>::iterator iter, end;
	for (iter = mutex->waitingThreads.begin(), end = mutex->waitingThreads.end(); iter != end; ++iter)
	{
		SceUID threadID = *iter;

		int wVal = (int)__KernelGetWaitValue(threadID, error);
		u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);

		__KernelMutexAcquireLock(mutex, wVal, threadID);

		if (timeoutPtr != 0 && mutexWaitTimer != 0)
		{
			// Remove any event for this thread.
			int cyclesLeft = CoreTiming::UnscheduleEvent(mutexWaitTimer, threadID);
			Memory::Write_U32(cyclesToUs(cyclesLeft), timeoutPtr);
		}

		__KernelResumeThreadFromWait(threadID, 0);
		wokeThreads = true;
		mutex->waitingThreads.erase(iter);
		break;
	}

	if (!wokeThreads)
		mutex->nm.lockThread = -1;

	return wokeThreads;
}

void __KernelMutexTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	SceUID mutexID = __KernelGetWaitID(threadID, WAITTYPE_MUTEX, error);
	Mutex *mutex = kernelObjects.Get<Mutex>(mutexID, error);
	if (mutex)
	{
		// This thread isn't waiting anymore.
		mutex->waitingThreads.erase(std::remove(mutex->waitingThreads.begin(), mutex->waitingThreads.end(), threadID), mutex->waitingThreads.end());
	}

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
}

void __KernelMutexThreadEnd(SceUID threadID)
{
	u32 error;

	// If it was waiting on the mutex, it should finish now.
	SceUID mutexID = __KernelGetWaitID(threadID, WAITTYPE_MUTEX, error);
	if (mutexID)
	{
		Mutex *mutex = kernelObjects.Get<Mutex>(mutexID, error);
		if (mutex)
			mutex->waitingThreads.erase(std::remove(mutex->waitingThreads.begin(), mutex->waitingThreads.end(), threadID), mutex->waitingThreads.end());
	}

	std::map<SceUID, SceUID>::iterator iter = mutexHeldLocks.find(threadID);
	if (iter != mutexHeldLocks.end())
	{
		SceUID mutexID = (*iter).second;
		Mutex *mutex = kernelObjects.Get<Mutex>(mutexID, error);

		if (mutex)
			__KernelUnlockMutex(mutex, error);
	}
}

void __KernelWaitMutex(Mutex *mutex, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || mutexWaitTimer == 0)
		return;

	// This should call __KernelMutexTimeout() later, unless we cancel it.
	int micro = (int) Memory::Read_U32(timeoutPtr);
	CoreTiming::ScheduleEvent(usToCycles(micro), mutexWaitTimer, __KernelGetCurThread());
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
		__KernelWaitMutex(mutex, timeoutPtr);
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
		__KernelWaitMutex(mutex, timeoutPtr);
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
		else if (mutex->nm.lockLevel == 0 || mutex->nm.lockThread != __KernelGetCurThread())
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
		__KernelUnlockMutex(mutex, error);
		__KernelReSchedule("mutex unlocked");
	}
}

void sceKernelCreateLwMutex(u32 workareaPtr, const char *name, u32 attr, int initialCount, u32 optionsPtr)
{
	if (!mutexInitComplete)
		__KernelMutexInit();

	DEBUG_LOG(HLE,"sceKernelCreateLwMutex(%08x, %s, %08x, %d, %08x)", workareaPtr, name, attr, initialCount, optionsPtr);

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

	LwMutex *mutex = new LwMutex();
	SceUID id = kernelObjects.Create(mutex);
	mutex->nm.size = sizeof(mutex);
	strncpy(mutex->nm.name, name, 31);
	mutex->nm.name[31] = 0;
	mutex->nm.attr = attr;
	mutex->nm.workareaPtr = workareaPtr;

	NativeLwMutexWorkarea workarea;
	workarea.init();
	workarea.lockLevel = initialCount;
	if (initialCount == 0)
		workarea.lockThread = 0;
	else
		workarea.lockThread = __KernelGetCurThread();
	workarea.attr = attr;
	workarea.uid = id;

	Memory::WriteStruct(workareaPtr, &workarea);

	if (optionsPtr != 0)
		WARN_LOG(HLE,"sceKernelCreateLwMutex(%s) unsupported options parameter.", name);

	RETURN(0);

	__KernelReSchedule("lwmutex created");
}

void sceKernelDeleteLwMutex(u32 workareaPtr)
{
	DEBUG_LOG(HLE,"sceKernelDeleteLwMutex(%08x)", workareaPtr);

	if (!workareaPtr || !Memory::IsValidAddress(workareaPtr))
	{
		RETURN(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
		return;
	}

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error;
	LwMutex *mutex = kernelObjects.Get<LwMutex>(workarea.uid, error);
	if (mutex)
	{
		std::vector<SceUID>::iterator iter, end;
		for (iter = mutex->waitingThreads.begin(), end = mutex->waitingThreads.end(); iter != end; ++iter)
		{
			SceUID threadID = *iter;

			u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
			if (timeoutPtr != 0 && lwMutexWaitTimer != 0)
			{
				// Remove any event for this thread.
				int cyclesLeft = CoreTiming::UnscheduleEvent(lwMutexWaitTimer, threadID);
				Memory::Write_U32(cyclesToUs(cyclesLeft), timeoutPtr);
			}

			__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		}
		mutex->waitingThreads.empty();

		RETURN(kernelObjects.Destroy<LwMutex>(workarea.uid));
		workarea.clear();
		Memory::WriteStruct(workareaPtr, &workarea);

		__KernelReSchedule("mutex deleted");
	}
	else
		RETURN(error);
}

bool __KernelLockLwMutex(NativeLwMutexWorkarea &workarea, int count, u32 &error)
{
	if (!error)
	{
		if (count <= 0)
			error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
		else if (count > 1 && !(workarea.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE))
			error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
		// Two positive ints will always overflow to negative.
		else if (count + workarea.lockLevel < 0)
			error = PSP_LWMUTEX_ERROR_LOCK_OVERFLOW;
		else if (workarea.uid == -1)
			error = PSP_LWMUTEX_ERROR_NO_SUCH_LWMUTEX;
	}

	if (error)
		return false;

	if (workarea.lockLevel == 0)
	{
		if (workarea.lockThread != 0)
		{
			// Validate that it actually exists so we can return an error if not.
			kernelObjects.Get<LwMutex>(workarea.uid, error);
			if (error)
				return false;
		}

		workarea.lockLevel = count;
		workarea.lockThread = __KernelGetCurThread();
		return true;
	}

	if (workarea.lockThread == __KernelGetCurThread())
	{
		// Recursive mutex, let's just increase the lock count and keep going
		if (workarea.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE)
		{
			workarea.lockLevel += count;
			return true;
		}
		else
		{
			error = PSP_LWMUTEX_ERROR_ALREADY_LOCKED;
			return false;
		}
	}

	return false;
}

bool __KernelUnlockLwMutex(NativeLwMutexWorkarea &workarea, u32 &error)
{
	LwMutex *mutex = kernelObjects.Get<LwMutex>(workarea.uid, error);
	if (error)
	{
		workarea.lockThread = 0;
		return false;
	}

	// TODO: PSP_MUTEX_ATTR_PRIORITY
	bool wokeThreads = false;
	std::vector<SceUID>::iterator iter, end;
	for (iter = mutex->waitingThreads.begin(), end = mutex->waitingThreads.end(); iter != end; ++iter)
	{
		SceUID threadID = *iter;

		int wVal = (int)__KernelGetWaitValue(threadID, error);
		u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);

		workarea.lockLevel = wVal;
		workarea.lockThread = threadID;

		if (timeoutPtr != 0 && lwMutexWaitTimer != 0)
		{
			// Remove any event for this thread.
			int cyclesLeft = CoreTiming::UnscheduleEvent(lwMutexWaitTimer, threadID);
			Memory::Write_U32(cyclesToUs(cyclesLeft), timeoutPtr);
		}

		__KernelResumeThreadFromWait(threadID, 0);
		wokeThreads = true;
		mutex->waitingThreads.erase(iter);
		break;
	}

	if (!wokeThreads)
		workarea.lockThread = 0;

	return wokeThreads;
}

void __KernelLwMutexTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	SceUID mutexID = __KernelGetWaitID(threadID, WAITTYPE_LWMUTEX, error);
	LwMutex *mutex = kernelObjects.Get<LwMutex>(mutexID, error);
	if (mutex)
	{
		// This thread isn't waiting anymore.
		mutex->waitingThreads.erase(std::remove(mutex->waitingThreads.begin(), mutex->waitingThreads.end(), threadID), mutex->waitingThreads.end());
	}

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
}

void __KernelWaitLwMutex(LwMutex *mutex, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || lwMutexWaitTimer == 0)
		return;

	// This should call __KernelMutexTimeout() later, unless we cancel it.
	int micro = (int) Memory::Read_U32(timeoutPtr);
	CoreTiming::ScheduleEvent(usToCycles(micro), lwMutexWaitTimer, __KernelGetCurThread());
}

void sceKernelTryLockLwMutex(u32 workareaPtr, int count)
{
	DEBUG_LOG(HLE,"sceKernelTryLockLwMutex(%08x, %i)", workareaPtr, count);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (__KernelLockLwMutex(workarea, count, error))
	{
		Memory::WriteStruct(workareaPtr, &workarea);
		RETURN(0);
		__KernelReSchedule("lwmutex trylocked");
	}
	else if (error)
		RETURN(PSP_MUTEX_ERROR_TRYLOCK_FAILED);
	else
		RETURN(PSP_MUTEX_ERROR_TRYLOCK_FAILED);
}

void sceKernelTryLockLwMutex_600(u32 workareaPtr, int count)
{
	DEBUG_LOG(HLE,"sceKernelTryLockLwMutex_600(%08x, %i)", workareaPtr, count);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (__KernelLockLwMutex(workarea, count, error))
	{
		Memory::WriteStruct(workareaPtr, &workarea);
		RETURN(0);
		__KernelReSchedule("lwmutex trylocked");
	}
	else if (error)
		RETURN(error);
	else
		RETURN(PSP_LWMUTEX_ERROR_TRYLOCK_FAILED);
}

void sceKernelLockLwMutex(u32 workareaPtr, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelLockLwMutex(%08x, %i, %08x)", workareaPtr, count, timeoutPtr);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (__KernelLockLwMutex(workarea, count, error))
	{
		Memory::WriteStruct(workareaPtr, &workarea);
		RETURN(0);
		__KernelReSchedule("lwmutex locked");
	}
	else if (error)
		RETURN(error);
	else
	{
		LwMutex *mutex = kernelObjects.Get<LwMutex>(workarea.uid, error);
		if (mutex)
		{
			mutex->waitingThreads.push_back(__KernelGetCurThread());
			__KernelWaitLwMutex(mutex, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_LWMUTEX, workarea.uid, count, timeoutPtr, false);
		}
		else
			RETURN(error);
	}
}

void sceKernelLockLwMutexCB(u32 workareaPtr, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelLockLwMutexCB(%08x, %i, %08x)", workareaPtr, count, timeoutPtr);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (__KernelLockLwMutex(workarea, count, error))
	{
		Memory::WriteStruct(workareaPtr, &workarea);
		RETURN(0);
		__KernelReSchedule("lwmutex locked");
	}
	else if (error)
		RETURN(error);
	else
	{
		LwMutex *mutex = kernelObjects.Get<LwMutex>(workarea.uid, error);
		if (mutex)
		{
			mutex->waitingThreads.push_back(__KernelGetCurThread());
			__KernelWaitLwMutex(mutex, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_LWMUTEX, workarea.uid, count, timeoutPtr, true);
			__KernelCheckCallbacks();
		}
		else
			RETURN(error);
	}
}

void sceKernelUnlockLwMutex(u32 workareaPtr, int count)
{
	DEBUG_LOG(HLE,"sceKernelUnlockLwMutex(%08x, %i)", workareaPtr, count);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (workarea.uid == -1)
		error = PSP_LWMUTEX_ERROR_NO_SUCH_LWMUTEX;
	else if (count <= 0)
		error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	else if ((workarea.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) == 0 && count > 1)
		error = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	else if (workarea.lockLevel == 0 || workarea.lockThread != __KernelGetCurThread())
		error = PSP_LWMUTEX_ERROR_NOT_LOCKED;
	else if (workarea.lockLevel < count)
		error = PSP_LWMUTEX_ERROR_UNLOCK_UNDERFLOW;

	if (error)
	{
		RETURN(error);
		return;
	}

	workarea.lockLevel -= count;
	RETURN(0);

	if (workarea.lockLevel == 0)
	{
		__KernelUnlockLwMutex(workarea, error);
		Memory::WriteStruct(workareaPtr, &workarea);
		__KernelReSchedule("mutex unlocked");
	}
	else
		Memory::WriteStruct(workareaPtr, &workarea);

}