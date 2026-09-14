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

#include <cstdio>
#include <map>
#include <vector>

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/ErrorCodes.h"

class PointerWrap;

struct NativeEventFlag {
	u32_le size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	u32_le attr;
	u32_le initPattern;
	u32_le currentPattern;
	s32_le numWaitThreads;
};

struct EventFlagTh {
	SceUID threadID;
	u32 bits;
	u32 wait;
	u32 outAddr;
	u64 pausedTimeout;

	bool operator ==(const SceUID &otherThreadID) const {
		return threadID == otherThreadID;
	}
};

// Exposed here (rather than kept private to sceKernelEventFlag.cpp) so the WebSocket debugger
// can read a live object's state directly via kernelObjects.Get<EventFlag>()/Iterate<EventFlag>()
// - see HLEKernelObjectSubscriber.cpp. That's a read-only use: nothing outside this file should
// call DoState() or otherwise mutate an EventFlag - it's public here for this file's own use as
// before, not an invitation to write to it from elsewhere.
class EventFlag : public KernelObject {
public:
	const char *GetName() override { return nef.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "EventFlag"; }
	void GetQuickInfo(char *ptr, int size) override {
		snprintf(ptr, size, "init=%08x cur=%08x numwait=%i",
			nef.initPattern,
			nef.currentPattern,
			nef.numWaitThreads);
	}

	static u32 GetMissingErrorCode() {
		return SCE_KERNEL_ERROR_UNKNOWN_EVFID;
	}
	static int GetStaticIDType() { return SCE_KERNEL_TMID_EventFlag; }
	int GetIDType() const override { return SCE_KERNEL_TMID_EventFlag; }

	void DoState(PointerWrap &p) override;

	NativeEventFlag nef;
	std::vector<EventFlagTh> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, EventFlagTh> pausedWaits;
};

int sceKernelCreateEventFlag(const char *name, u32 flag_attr, u32 flag_initPattern, u32 optPtr);
u32 sceKernelClearEventFlag(SceUID id, u32 bits);
u32 sceKernelDeleteEventFlag(SceUID uid);
u32 sceKernelSetEventFlag(SceUID id, u32 bitsToSet);
int sceKernelWaitEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr);
int sceKernelWaitEventFlagCB(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr);
int sceKernelPollEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr);
u32 sceKernelReferEventFlagStatus(SceUID id, u32 statusPtr);
u32 sceKernelCancelEventFlag(SceUID uid, u32 pattern, u32 numWaitThreadsPtr);

void __KernelEventFlagInit();
void __KernelEventFlagDoState(PointerWrap &p);
KernelObject *__KernelEventFlagObject();
