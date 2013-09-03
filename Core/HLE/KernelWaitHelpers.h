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

#include <vector>
#include <map>
#include <algorithm>
#include "Core/HLE/sceKernelThread.h"

namespace HLEKernel
{

template <typename WaitInfoType, typename PauseType>
inline bool WaitPauseHelperUpdate(SceUID pauseKey, SceUID threadID, std::vector<WaitInfoType> &waitingThreads, std::map<SceUID, PauseType> &pausedWaits, u64 pauseTimeout) {
	WaitInfoType waitData = {0};
	for (size_t i = 0; i < waitingThreads.size(); i++) {
		WaitInfoType *t = &waitingThreads[i];
		if (t->threadID == threadID)
		{
			waitData = *t;
			// TODO: Hmm, what about priority/fifo order?  Does it lose its place in line?
			waitingThreads.erase(waitingThreads.begin() + i);
			break;
		}
	}

	if (waitData.threadID != threadID)
		return false;

	waitData.pausedTimeout = pauseTimeout;
	pausedWaits[pauseKey] = waitData;
	return true;
}

template <>
inline bool WaitPauseHelperUpdate<SceUID, u64>(SceUID pauseKey, SceUID threadID, std::vector<SceUID> &waitingThreads, std::map<SceUID, u64> &pausedWaits, u64 pauseTimeout) {
	// TODO: Hmm, what about priority/fifo order?  Does it lose its place in line?
	waitingThreads.erase(std::remove(waitingThreads.begin(), waitingThreads.end(), threadID), waitingThreads.end());
	pausedWaits[pauseKey] = pauseTimeout;
	return true;
}

template <typename WaitInfoType, typename PauseType>
inline u64 WaitPauseHelperGet(SceUID pauseKey, SceUID threadID, std::map<SceUID, PauseType> &pausedWaits, WaitInfoType &waitData) {
	waitData = pausedWaits[pauseKey];
	u64 waitDeadline = waitData.pausedTimeout;
	pausedWaits.erase(pauseKey);
	return waitDeadline;
}

template <>
inline u64 WaitPauseHelperGet<SceUID, u64>(SceUID pauseKey, SceUID threadID, std::map<SceUID, u64> &pausedWaits, SceUID &waitData) {
	waitData = threadID;
	u64 waitDeadline = pausedWaits[pauseKey];
	pausedWaits.erase(pauseKey);
	return waitDeadline;
}

enum WaitBeginEndCallbackResult {
	WAIT_CB_BAD_WAIT_DATA = -2,
	WAIT_CB_BAD_WAIT_ID = -1,
	WAIT_CB_SUCCESS = 0,
	WAIT_CB_RESUMED_WAIT = 1,
};

template <typename WaitInfoType, typename PauseType>
WaitBeginEndCallbackResult WaitBeginCallback(SceUID threadID, SceUID prevCallbackId, int waitTimer, std::vector<WaitInfoType> &waitingThreads, std::map<SceUID, PauseType> &pausedWaits, bool doTimeout = true) {
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	// This means two callbacks in a row.  PSP crashes if the same callback runs inside itself.
	// TODO: Handle this better?
	if (pausedWaits.find(pauseKey) != pausedWaits.end()) {
		return WAIT_CB_SUCCESS;
	}

	u64 pausedTimeout = 0;
	if (doTimeout && waitTimer != -1) {
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(waitTimer, threadID);
		pausedTimeout = CoreTiming::GetTicks() + cyclesLeft;
	}

	if (!WaitPauseHelperUpdate(pauseKey, threadID, waitingThreads, pausedWaits, pausedTimeout)) {
		return WAIT_CB_BAD_WAIT_DATA;
	}

	return WAIT_CB_SUCCESS;
}

template <typename KO, WaitType waitType, typename WaitInfoType>
WaitBeginEndCallbackResult WaitBeginCallback(SceUID threadID, SceUID prevCallbackId, int waitTimer) {
	u32 error;
	SceUID uid = __KernelGetWaitID(threadID, waitType, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	KO *ko = uid == 0 ? NULL : kernelObjects.Get<KO>(uid, error);
	if (ko) {
		return WaitBeginCallback(threadID, prevCallbackId, waitTimer, ko->waitingThreads, ko->pausedWaits, timeoutPtr != 0);
	} else {
		return WAIT_CB_BAD_WAIT_ID;
	}
}

template <typename KO, WaitType waitType, typename WaitInfoType, typename PauseType, class TryUnlockFunc>
WaitBeginEndCallbackResult WaitEndCallback(SceUID threadID, SceUID prevCallbackId, int waitTimer, TryUnlockFunc TryUnlock, std::vector<WaitInfoType> &waitingThreads, std::map<SceUID, PauseType> &pausedWaits) {
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	// Note: Cancel does not affect suspended semaphore waits, probably same for others.

	u32 error;
	SceUID uid = __KernelGetWaitID(threadID, waitType, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	KO *ko = uid == 0 ? NULL : kernelObjects.Get<KO>(uid, error);
	if (!ko || pausedWaits.find(pauseKey) == pausedWaits.end()) {
		// TODO: Since it was deleted, we don't know how long was actually left.
		// For now, we just say the full time was taken.
		if (timeoutPtr != 0 && waitTimer != -1) {
			Memory::Write_U32(0, timeoutPtr);
		}

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		return WAIT_CB_SUCCESS;
	}

	WaitInfoType waitData;
	u64 waitDeadline = WaitPauseHelperGet(pauseKey, threadID, pausedWaits, waitData);

	// TODO: Don't wake up if __KernelCurHasReadyCallbacks()?

	bool wokeThreads;
	// Attempt to unlock.
	if (TryUnlock(ko, waitData, error, 0, wokeThreads)) {
		return WAIT_CB_SUCCESS;
	}

	// We only check if it timed out if it couldn't unlock.
	s64 cyclesLeft = waitDeadline - CoreTiming::GetTicks();
	if (cyclesLeft < 0 && waitDeadline != 0) {
		if (timeoutPtr != 0 && waitTimer != -1)
			Memory::Write_U32(0, timeoutPtr);

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
		return WAIT_CB_SUCCESS;
	} else {
		if (timeoutPtr != 0 && waitTimer != -1) {
			CoreTiming::ScheduleEvent(cyclesLeft, waitTimer, __KernelGetCurThread());
		}

		// TODO: Should this not go at the end?
		waitingThreads.push_back(waitData);
		return WAIT_CB_RESUMED_WAIT;
	}
}

template <typename KO, WaitType waitType, typename WaitInfoType, class TryUnlockFunc>
WaitBeginEndCallbackResult WaitEndCallback(SceUID threadID, SceUID prevCallbackId, int waitTimer, TryUnlockFunc TryUnlock) {
	u32 error;
	SceUID uid = __KernelGetWaitID(threadID, waitType, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	KO *ko = uid == 0 ? NULL : kernelObjects.Get<KO>(uid, error);
	if (!ko) {
		// TODO: Since it was deleted, we don't know how long was actually left.
		// For now, we just say the full time was taken.
		if (timeoutPtr != 0 && waitTimer != -1) {
			Memory::Write_U32(0, timeoutPtr);
		}

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		return WAIT_CB_SUCCESS;
	}

	return WaitEndCallback<KO, waitType>(threadID, prevCallbackId, waitTimer, TryUnlock, ko->waitingThreads, ko->pausedWaits);
}

};
