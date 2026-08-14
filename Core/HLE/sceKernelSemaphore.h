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

#pragma once

#include <map>
#include <vector>

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/ErrorCodes.h"

class PointerWrap;

struct NativeSemaphore {
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

// Exposed here (rather than kept private to sceKernelSemaphore.cpp) so the WebSocket debugger
// can read a live object's state directly via
// kernelObjects.Get<PSPSemaphore>()/Iterate<PSPSemaphore>() - see HLEKernelObjectSubscriber.cpp.
// That's a read-only use: nothing outside this file should call DoState() or otherwise mutate a
// PSPSemaphore - it's public here for this file's own use as before, not an invitation to write
// to it from elsewhere.
struct PSPSemaphore : public KernelObject {
	const char *GetName() override { return ns.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "Semaphore"; }

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_SEMID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Semaphore; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Semaphore; }

	void DoState(PointerWrap &p) override;

	NativeSemaphore ns;
	std::vector<SceUID> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, u64> pausedWaits;
};

int sceKernelCancelSema(SceUID id, int newCount, u32 numWaitThreadsPtr);
int sceKernelCreateSema(const char* name, u32 attr, int initVal, int maxVal, u32 optionPtr);
int sceKernelDeleteSema(SceUID id);
int sceKernelPollSema(SceUID id, int wantedCount);
int sceKernelReferSemaStatus(SceUID id, u32 infoPtr);
int sceKernelSignalSema(SceUID id, int signal);
int sceKernelWaitSema(SceUID semaid, int signal, u32 timeoutPtr);
int sceKernelWaitSemaCB(SceUID semaid, int signal, u32 timeoutPtr);

void __KernelSemaTimeout(u64 userdata, int cycleslate);

void __KernelSemaInit();
void __KernelSemaDoState(PointerWrap &p);
KernelObject *__KernelSemaphoreObject();

extern "C"
{
#include "ext/libkirk/kirk_engine.h"
}
