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
#define PSP_MUTEX_ATTR_KNOWN (PSP_MUTEX_ATTR_PRIORITY | PSP_MUTEX_ATTR_ALLOW_RECURSIVE)

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
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	SceUInt attr;
	int initialCount;
	int lockLevel;
	int numWaitThreads;
};

struct Mutex : public KernelObject
{
	const char *GetName() {return nm.name;}
	const char *GetTypeName() {return "Mutex";}
	static u32 GetMissingErrorCode() { return PSP_MUTEX_ERROR_NO_SUCH_MUTEX; }
	int GetIDType() const { return SCE_KERNEL_TMID_Mutex; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nm);
		SceUID dv = 0;
		p.Do(waitingThreads, dv);
		p.Do(lockThread);
		p.DoMarker("Mutex");
	}

	NativeMutex nm;
	std::vector<SceUID> waitingThreads;
	int lockThread;	// The thread holding the lock
};

// Guesswork - not exposed anyway
struct NativeLwMutex
{
	SceSize size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
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

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nm);
		SceUID dv = 0;
		p.Do(waitingThreads, dv);
		p.DoMarker("LwMutex");
	}

	NativeLwMutex nm;
	std::vector<SceUID> waitingThreads;
};

static int mutexWaitTimer = -1;
static int lwMutexWaitTimer = -1;
// Thread -> Mutex locks for thread end.
typedef std::multimap<SceUID, SceUID> MutexMap;
static MutexMap mutexHeldLocks;

void __KernelMutexInit()
{
	mutexWaitTimer = CoreTiming::RegisterEvent("MutexTimeout", __KernelMutexTimeout);
	lwMutexWaitTimer = CoreTiming::RegisterEvent("LwMutexTimeout", __KernelLwMutexTimeout);

	__KernelListenThreadEnd(&__KernelMutexThreadEnd);
}

void __KernelMutexDoState(PointerWrap &p)
{
	p.Do(mutexWaitTimer);
	CoreTiming::RestoreRegisterEvent(mutexWaitTimer, "MutexTimeout", __KernelMutexTimeout);
	p.Do(lwMutexWaitTimer);
	CoreTiming::RestoreRegisterEvent(lwMutexWaitTimer, "LwMutexTimeout", __KernelLwMutexTimeout);
	p.Do(mutexHeldLocks);
	p.DoMarker("sceKernelMutex");
}

KernelObject *__KernelMutexObject()
{
	return new Mutex;
}

KernelObject *__KernelLwMutexObject()
{
	return new LwMutex;
}

void __KernelMutexShutdown()
{
	mutexHeldLocks.clear();
}

void __KernelMutexAcquireLock(Mutex *mutex, int count, SceUID thread)
{
#if defined(_DEBUG)
	std::pair<MutexMap::iterator, MutexMap::iterator> locked = mutexHeldLocks.equal_range(thread);
	for (MutexMap::iterator iter = locked.first; iter != locked.second; ++iter)
		_dbg_assert_msg_(HLE, (*iter).second != mutex->GetUID(), "Thread %d / mutex %d wasn't removed from mutexHeldLocks properly.", thread, mutex->GetUID());
#endif

	mutexHeldLocks.insert(std::make_pair(thread, mutex->GetUID()));

	mutex->nm.lockLevel = count;
	mutex->lockThread = thread;
}

void __KernelMutexAcquireLock(Mutex *mutex, int count)
{
	__KernelMutexAcquireLock(mutex, count, __KernelGetCurThread());
}

void __KernelMutexEraseLock(Mutex *mutex)
{
	if (mutex->lockThread != -1)
	{
		SceUID id = mutex->GetUID();
		std::pair<MutexMap::iterator, MutexMap::iterator> locked = mutexHeldLocks.equal_range(mutex->lockThread);
		for (MutexMap::iterator iter = locked.first; iter != locked.second; ++iter)
		{
			if ((*iter).second == id)
			{
				mutexHeldLocks.erase(iter);
				break;
			}
		}
	}
	mutex->lockThread = -1;
}

std::vector<SceUID>::iterator __KernelMutexFindPriority(std::vector<SceUID> &waiting)
{
	_dbg_assert_msg_(HLE, !waiting.empty(), "__KernelMutexFindPriority: Trying to find best of no threads.");

	std::vector<SceUID>::iterator iter, end, best = waiting.end();
	u32 best_prio = 0xFFFFFFFF;
	for (iter = waiting.begin(), end = waiting.end(); iter != end; ++iter)
	{
		u32 iter_prio = __KernelGetThreadPrio(*iter);
		if (iter_prio < best_prio)
		{
			best = iter;
			best_prio = iter_prio;
		}
	}

	_dbg_assert_msg_(HLE, best != waiting.end(), "__KernelMutexFindPriority: Returning invalid best thread.");
	return best;
}

int sceKernelCreateMutex(const char *name, u32 attr, int initialCount, u32 optionsPtr)
{
	if (!name)
	{
		WARN_LOG(HLE, "%08x=sceKernelCreateMutex(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}
	if (attr >= 0xC00)
	{
		WARN_LOG(HLE, "%08x=sceKernelCreateMutex(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}

	if (initialCount < 0)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	if ((attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) == 0 && initialCount > 1)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;

	Mutex *mutex = new Mutex();
	SceUID id = kernelObjects.Create(mutex);

	mutex->nm.size = sizeof(mutex);
	strncpy(mutex->nm.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	mutex->nm.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	mutex->nm.attr = attr;
	mutex->nm.initialCount = initialCount;
	if (initialCount == 0)
	{
		mutex->nm.lockLevel = 0;
		mutex->lockThread = -1;
	}
	else
		__KernelMutexAcquireLock(mutex, initialCount);

	DEBUG_LOG(HLE, "%i=sceKernelCreateMutex(%s, %08x, %d, %08x)", id, name, attr, initialCount, optionsPtr);

	if (optionsPtr != 0)
		WARN_LOG(HLE, "sceKernelCreateMutex(%s) unsupported options parameter: %08x", name, optionsPtr);
	if ((attr & ~PSP_MUTEX_ATTR_KNOWN) != 0)
		WARN_LOG(HLE, "sceKernelCreateMutex(%s) unsupported attr parameter: %08x", name, attr);

	return id;
}

bool __KernelUnlockMutexForThread(Mutex *mutex, SceUID threadID, u32 &error, int result)
{
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_MUTEX, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);

	// The waitID may be different after a timeout.
	if (waitID != mutex->GetUID())
		return false;

	// If result is an error code, we're just letting it go.
	if (result == 0)
	{
		int wVal = (int)__KernelGetWaitValue(threadID, error);
		__KernelMutexAcquireLock(mutex, wVal, threadID);
	}

	if (timeoutPtr != 0 && mutexWaitTimer != -1)
	{
		// Remove any event for this thread.
		u64 cyclesLeft = CoreTiming::UnscheduleEvent(mutexWaitTimer, threadID);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(threadID, result);
	return true;
}

int sceKernelDeleteMutex(SceUID id)
{
	DEBUG_LOG(HLE,"sceKernelDeleteMutex(%i)", id);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (mutex)
	{
		bool wokeThreads = false;
		std::vector<SceUID>::iterator iter, end;
		for (iter = mutex->waitingThreads.begin(), end = mutex->waitingThreads.end(); iter != end; ++iter)
			wokeThreads |= __KernelUnlockMutexForThread(mutex, *iter, error, SCE_KERNEL_ERROR_WAIT_DELETE);

		if (mutex->lockThread != -1)
			__KernelMutexEraseLock(mutex);
		mutex->waitingThreads.clear();

		if (wokeThreads)
			hleReSchedule("mutex deleted");

		return kernelObjects.Destroy<Mutex>(id);
	}
	else
		return error;
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

	if (mutex->lockThread == __KernelGetCurThread())
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

	bool wokeThreads = false;
	std::vector<SceUID>::iterator iter;
	while (!wokeThreads && !mutex->waitingThreads.empty())
	{
		if ((mutex->nm.attr & PSP_MUTEX_ATTR_PRIORITY) != 0)
			iter = __KernelMutexFindPriority(mutex->waitingThreads);
		else
			iter = mutex->waitingThreads.begin();

		wokeThreads |= __KernelUnlockMutexForThread(mutex, *iter, error, 0);
		mutex->waitingThreads.erase(iter);
	}

	if (!wokeThreads)
		mutex->lockThread = -1;

	return wokeThreads;
}

void __KernelMutexTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);

	// We intentionally don't remove from waitingThreads here yet.
	// The reason is, if it times out, but what it was waiting on is DELETED prior to it
	// actually running, it will get a DELETE result instead of a TIMEOUT.
	// So, we need to remember it or we won't be able to mark it DELETE instead later.
}

void __KernelMutexThreadEnd(SceUID threadID)
{
	u32 error;

	// If it was waiting on the mutex, it should finish now.
	SceUID waitingMutexID = __KernelGetWaitID(threadID, WAITTYPE_MUTEX, error);
	if (waitingMutexID)
	{
		Mutex *mutex = kernelObjects.Get<Mutex>(waitingMutexID, error);
		if (mutex)
			mutex->waitingThreads.erase(std::remove(mutex->waitingThreads.begin(), mutex->waitingThreads.end(), threadID), mutex->waitingThreads.end());
	}

	// Unlock all mutexes the thread had locked.
	std::pair<MutexMap::iterator, MutexMap::iterator> locked = mutexHeldLocks.equal_range(threadID);
	for (MutexMap::iterator iter = locked.first; iter != locked.second; )
	{
		// Need to increment early so erase() doesn't invalidate.
		SceUID mutexID = (*iter++).second;
		Mutex *mutex = kernelObjects.Get<Mutex>(mutexID, error);

		if (mutex)
		{
			mutex->nm.lockLevel = 0;
			__KernelUnlockMutex(mutex, error);
		}
	}
}

void __KernelWaitMutex(Mutex *mutex, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || mutexWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This happens to be how the hardware seems to time things.
	if (micro <= 3)
		micro = 15;
	else if (micro <= 249)
		micro = 250;

	// This should call __KernelMutexTimeout() later, unless we cancel it.
	CoreTiming::ScheduleEvent(usToCycles(micro), mutexWaitTimer, __KernelGetCurThread());
}

// int sceKernelLockMutex(SceUID id, int count, int *timeout)
int sceKernelLockMutex(SceUID id, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelLockMutex(%i, %i, %08x)", id, count, timeoutPtr);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);

	if (__KernelLockMutex(mutex, count, error))
		return 0;
	else if (error)
		return error;
	else
	{
		SceUID threadID = __KernelGetCurThread();
		// May be in a tight loop timing out (where we don't remove from waitingThreads yet), don't want to add duplicates.
		if (std::find(mutex->waitingThreads.begin(), mutex->waitingThreads.end(), threadID) == mutex->waitingThreads.end())
			mutex->waitingThreads.push_back(threadID);
		__KernelWaitMutex(mutex, timeoutPtr);
		__KernelWaitCurThread(WAITTYPE_MUTEX, id, count, timeoutPtr, false);

		// Return value will be overwritten by wait.
		return 0;
	}
}

// int sceKernelLockMutexCB(SceUID id, int count, int *timeout)
int sceKernelLockMutexCB(SceUID id, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelLockMutexCB(%i, %i, %08x)", id, count, timeoutPtr);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);

	if (__KernelLockMutex(mutex, count, error))
	{
		hleCheckCurrentCallbacks();
		return 0;
	}
	else if (error)
		return error;
	else
	{
		SceUID threadID = __KernelGetCurThread();
		// May be in a tight loop timing out (where we don't remove from waitingThreads yet), don't want to add duplicates.
		if (std::find(mutex->waitingThreads.begin(), mutex->waitingThreads.end(), threadID) == mutex->waitingThreads.end())
			mutex->waitingThreads.push_back(threadID);
		__KernelWaitMutex(mutex, timeoutPtr);
		__KernelWaitCurThread(WAITTYPE_MUTEX, id, count, timeoutPtr, true);

		// Return value will be overwritten by wait.
		return 0;
	}
}

// int sceKernelTryLockMutex(SceUID id, int count)
int sceKernelTryLockMutex(SceUID id, int count)
{
	DEBUG_LOG(HLE, "sceKernelTryLockMutex(%i, %i)", id, count);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);

	if (__KernelLockMutex(mutex, count, error))
		return 0;
	else if (error)
		return error;
	else
		return PSP_MUTEX_ERROR_TRYLOCK_FAILED;
}

// int sceKernelUnlockMutex(SceUID id, int count)
int sceKernelUnlockMutex(SceUID id, int count)
{
	DEBUG_LOG(HLE, "sceKernelUnlockMutex(%i, %i)", id, count);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);

	if (error)
		return error;
	if (count <= 0)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	if ((mutex->nm.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) == 0 && count > 1)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	if (mutex->nm.lockLevel == 0 || mutex->lockThread != __KernelGetCurThread())
		return PSP_MUTEX_ERROR_NOT_LOCKED;
	if (mutex->nm.lockLevel < count)
		return PSP_MUTEX_ERROR_UNLOCK_UNDERFLOW;

	mutex->nm.lockLevel -= count;

	if (mutex->nm.lockLevel == 0)
	{
		if (__KernelUnlockMutex(mutex, error))
			hleReSchedule("mutex unlocked");
	}

	return 0;
}

int sceKernelCreateLwMutex(u32 workareaPtr, const char *name, u32 attr, int initialCount, u32 optionsPtr)
{
	if (!name)
	{
		WARN_LOG(HLE, "%08x=sceKernelCreateLwMutex(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}
	if (attr >= 0x400)
	{
		WARN_LOG(HLE, "%08x=sceKernelCreateLwMutex(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}

	if (initialCount < 0)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	if ((attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) == 0 && initialCount > 1)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;

	LwMutex *mutex = new LwMutex();
	SceUID id = kernelObjects.Create(mutex);
	mutex->nm.size = sizeof(mutex);
	strncpy(mutex->nm.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	mutex->nm.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
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

	DEBUG_LOG(HLE, "sceKernelCreateLwMutex(%08x, %s, %08x, %d, %08x)", workareaPtr, name, attr, initialCount, optionsPtr);

	if (optionsPtr != 0)
		WARN_LOG(HLE, "sceKernelCreateLwMutex(%s) unsupported options parameter: %08x", name, optionsPtr);
	if ((attr & ~PSP_MUTEX_ATTR_KNOWN) != 0)
		WARN_LOG(HLE, "sceKernelCreateLwMutex(%s) unsupported attr parameter: %08x", name, attr);

	return 0;
}

int sceKernelReferMutexStatus(SceUID id, u32 infoAddr)
{
	u32 error;
	Mutex *m = kernelObjects.Get<Mutex>(id, error);
	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelReferMutexStatus(%i, %08x): invalid mbx id", id, infoAddr);
		return error;
	}

	// Should we crash the thread somehow?
	if (!Memory::IsValidAddress(infoAddr))
		return -1;

	// Refresh and write
	m->nm.numWaitThreads = m->waitingThreads.size();
	Memory::WriteStruct(infoAddr, &m->nm);
	return 0;
}

bool __KernelUnlockLwMutexForThread(LwMutex *mutex, NativeLwMutexWorkarea &workarea, SceUID threadID, u32 &error, int result)
{
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_LWMUTEX, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);

	// The waitID may be different after a timeout.
	if (waitID != mutex->GetUID())
		return false;

	// If result is an error code, we're just letting it go.
	if (result == 0)
	{
		workarea.lockLevel = (int) __KernelGetWaitValue(threadID, error);
		workarea.lockThread = threadID;
	}

	if (timeoutPtr != 0 && lwMutexWaitTimer != -1)
	{
		// Remove any event for this thread.
		u64 cyclesLeft = CoreTiming::UnscheduleEvent(lwMutexWaitTimer, threadID);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(threadID, result);
	return true;
}

int sceKernelDeleteLwMutex(u32 workareaPtr)
{
	DEBUG_LOG(HLE, "sceKernelDeleteLwMutex(%08x)", workareaPtr);

	if (!workareaPtr || !Memory::IsValidAddress(workareaPtr))
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error;
	LwMutex *mutex = kernelObjects.Get<LwMutex>(workarea.uid, error);
	if (mutex)
	{
		bool wokeThreads = false;
		std::vector<SceUID>::iterator iter, end;
		for (iter = mutex->waitingThreads.begin(), end = mutex->waitingThreads.end(); iter != end; ++iter)
			wokeThreads |= __KernelUnlockLwMutexForThread(mutex, workarea, *iter, error, SCE_KERNEL_ERROR_WAIT_DELETE);
		mutex->waitingThreads.clear();

		workarea.clear();
		Memory::WriteStruct(workareaPtr, &workarea);

		if (wokeThreads)
			hleReSchedule("lwmutex deleted");

		return kernelObjects.Destroy<LwMutex>(mutex->GetUID());
	}
	else
		return error;
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

	bool wokeThreads = false;
	std::vector<SceUID>::iterator iter;
	while (!wokeThreads && !mutex->waitingThreads.empty())
	{
		if ((mutex->nm.attr & PSP_MUTEX_ATTR_PRIORITY) != 0)
			iter = __KernelMutexFindPriority(mutex->waitingThreads);
		else
			iter = mutex->waitingThreads.begin();

		wokeThreads |= __KernelUnlockLwMutexForThread(mutex, workarea, *iter, error, 0);
		mutex->waitingThreads.erase(iter);
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

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);

	// We intentionally don't remove from waitingThreads here yet.
	// The reason is, if it times out, but what it was waiting on is DELETED prior to it
	// actually running, it will get a DELETE result instead of a TIMEOUT.
	// So, we need to remember it or we won't be able to mark it DELETE instead later.
}

void __KernelWaitLwMutex(LwMutex *mutex, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || lwMutexWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This happens to be how the hardware seems to time things.
	if (micro <= 3)
		micro = 15;
	else if (micro <= 249)
		micro = 250;

	// This should call __KernelLwMutexTimeout() later, unless we cancel it.
	CoreTiming::ScheduleEvent(usToCycles(micro), lwMutexWaitTimer, __KernelGetCurThread());
}

int sceKernelTryLockLwMutex(u32 workareaPtr, int count)
{
	DEBUG_LOG(HLE, "sceKernelTryLockLwMutex(%08x, %i)", workareaPtr, count);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (__KernelLockLwMutex(workarea, count, error))
	{
		Memory::WriteStruct(workareaPtr, &workarea);
		return 0;
	}
	// Unlike sceKernelTryLockLwMutex_600, this always returns the same error.
	else if (error)
		return PSP_MUTEX_ERROR_TRYLOCK_FAILED;
	else
		return PSP_MUTEX_ERROR_TRYLOCK_FAILED;
}

int sceKernelTryLockLwMutex_600(u32 workareaPtr, int count)
{
	DEBUG_LOG(HLE, "sceKernelTryLockLwMutex_600(%08x, %i)", workareaPtr, count);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (__KernelLockLwMutex(workarea, count, error))
	{
		Memory::WriteStruct(workareaPtr, &workarea);
		return 0;
	}
	else if (error)
		return error;
	else
		return PSP_LWMUTEX_ERROR_TRYLOCK_FAILED;
}

int sceKernelLockLwMutex(u32 workareaPtr, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelLockLwMutex(%08x, %i, %08x)", workareaPtr, count, timeoutPtr);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (__KernelLockLwMutex(workarea, count, error))
	{
		Memory::WriteStruct(workareaPtr, &workarea);
		return 0;
	}
	else if (error)
		return error;
	else
	{
		LwMutex *mutex = kernelObjects.Get<LwMutex>(workarea.uid, error);
		if (mutex)
		{
			SceUID threadID = __KernelGetCurThread();
			// May be in a tight loop timing out (where we don't remove from waitingThreads yet), don't want to add duplicates.
			if (std::find(mutex->waitingThreads.begin(), mutex->waitingThreads.end(), threadID) == mutex->waitingThreads.end())
				mutex->waitingThreads.push_back(threadID);
			__KernelWaitLwMutex(mutex, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_LWMUTEX, workarea.uid, count, timeoutPtr, false);

			// Return value will be overwritten by wait.
			return 0;
		}
		else
			return error;
	}
}

int sceKernelLockLwMutexCB(u32 workareaPtr, int count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelLockLwMutexCB(%08x, %i, %08x)", workareaPtr, count, timeoutPtr);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	u32 error = 0;
	if (__KernelLockLwMutex(workarea, count, error))
	{
		Memory::WriteStruct(workareaPtr, &workarea);
		hleCheckCurrentCallbacks();
		return 0;
	}
	else if (error)
		return error;
	else
	{
		LwMutex *mutex = kernelObjects.Get<LwMutex>(workarea.uid, error);
		if (mutex)
		{
			SceUID threadID = __KernelGetCurThread();
			// May be in a tight loop timing out (where we don't remove from waitingThreads yet), don't want to add duplicates.
			if (std::find(mutex->waitingThreads.begin(), mutex->waitingThreads.end(), threadID) == mutex->waitingThreads.end())
				mutex->waitingThreads.push_back(threadID);
			__KernelWaitLwMutex(mutex, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_LWMUTEX, workarea.uid, count, timeoutPtr, true);

			// Return value will be overwritten by wait.
			return 0;
		}
		else
			return error;
	}
}

int sceKernelUnlockLwMutex(u32 workareaPtr, int count)
{
	DEBUG_LOG(HLE, "sceKernelUnlockLwMutex(%08x, %i)", workareaPtr, count);

	NativeLwMutexWorkarea workarea;
	Memory::ReadStruct(workareaPtr, &workarea);

	if (workarea.uid == -1)
		return PSP_LWMUTEX_ERROR_NO_SUCH_LWMUTEX;
	else if (count <= 0)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	else if ((workarea.attr & PSP_MUTEX_ATTR_ALLOW_RECURSIVE) == 0 && count > 1)
		return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
	else if (workarea.lockLevel == 0 || workarea.lockThread != __KernelGetCurThread())
		return PSP_LWMUTEX_ERROR_NOT_LOCKED;
	else if (workarea.lockLevel < count)
		return PSP_LWMUTEX_ERROR_UNLOCK_UNDERFLOW;

	workarea.lockLevel -= count;

	if (workarea.lockLevel == 0)
	{
		u32 error;
		if (__KernelUnlockLwMutex(workarea, error))
			hleReSchedule("lwmutex unlocked");
		Memory::WriteStruct(workareaPtr, &workarea);
	}
	else
		Memory::WriteStruct(workareaPtr, &workarea);

	return 0;
}
