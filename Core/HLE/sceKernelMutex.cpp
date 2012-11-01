// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

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
#define PSP_MUTEX_ERROR_NOT_LOCKED 0x800201C7
#define PSP_MUTEX_ERROR_NO_SUCH_MUTEX 0x800201C3

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

u32 sceKernelCreateMutex(const char *name, u32 attr, u32 options)
{
	DEBUG_LOG(HLE,"sceKernelCreateMutex(%s, %08x, %08x)", name, attr, options);

	Mutex *mutex = new Mutex();
	SceUID id = kernelObjects.Create(mutex);

	mutex->nm.size = sizeof(mutex);
	mutex->nm.attr = attr;
	mutex->nm.lockLevel = 0;
	mutex->nm.lockThread = -1;

	strncpy(mutex->nm.name, name, 32);
	return id;
}

u32 sceKernelDeleteMutex(u32 id)
{
	DEBUG_LOG(HLE,"sceKernelDeleteMutex(%i)", id);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (!mutex)
		return PSP_MUTEX_ERROR_NO_SUCH_MUTEX;
	kernelObjects.Destroy<Mutex>(id);
	return 0;
}

u32 sceKernelLockMutex(u32 id, u32 count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelLockMutex(%i, %i, %08x)", id, count, timeoutPtr);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (!mutex)
		return PSP_MUTEX_ERROR_NO_SUCH_MUTEX;
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
		// Yeah, we need to block. Somehow.
	}
	return 0;
}

u32 sceKernelLockMutexCB(u32 id, u32 count, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"UNIMPL sceKernelLockMutexCB(%i, %i, %08x)", id, count, timeoutPtr);
	return 0;
}

u32 sceKernelUnlockMutex(u32 id, u32 count)
{
	DEBUG_LOG(HLE,"UNFINISHED sceKernelUnlockMutex(%i, %i)", id, count);
	u32 error;
	Mutex *mutex = kernelObjects.Get<Mutex>(id, error);
	if (!mutex)
		return PSP_MUTEX_ERROR_NO_SUCH_MUTEX;
	if (mutex->nm.lockLevel == 0)
		return PSP_MUTEX_ERROR_NOT_LOCKED;
	mutex->nm.lockLevel -= count;
	// TODO....
	return 0;
}
