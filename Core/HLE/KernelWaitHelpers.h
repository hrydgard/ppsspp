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

// Should be called from the CoreTiming handler for the wait func.
template <typename KO, WaitType waitType>
inline void WaitExecTimeout(SceUID threadID) {
	u32 error;
	SceUID uid = __KernelGetWaitID(threadID, waitType, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	KO *ko = uid == 0 ? NULL : kernelObjects.Get<KO>(uid, error);
	if (ko)
	{
		if (timeoutPtr != 0)
			Memory::Write_U32(0, timeoutPtr);

		// This thread isn't waiting anymore, but we'll remove it from waitingThreads later.
		// The reason is, if it times out, but what it was waiting on is DELETED prior to it
		// actually running, it will get a DELETE result instead of a TIMEOUT.
		// So, we need to remember it or we won't be able to mark it DELETE instead later.
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	}
}

// Move a thread from the waiting thread list to the paused thread list.
// This version is for vectors which contain structs, which must have SceUID threadID and u64 pausedTimeout.
// Should not be called directly.
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

// Move a thread from the waiting thread list to the paused thread list.
// This version is for a simpler list of SceUIDs.  The paused list is a std::map<SceUID, u64>.
// Should not be called directly.
template <>
inline bool WaitPauseHelperUpdate<SceUID, u64>(SceUID pauseKey, SceUID threadID, std::vector<SceUID> &waitingThreads, std::map<SceUID, u64> &pausedWaits, u64 pauseTimeout) {
	// TODO: Hmm, what about priority/fifo order?  Does it lose its place in line?
	waitingThreads.erase(std::remove(waitingThreads.begin(), waitingThreads.end(), threadID), waitingThreads.end());
	pausedWaits[pauseKey] = pauseTimeout;
	return true;
}

// Retrieve the paused wait info from the list, and pop it.
// Returns the pausedTimeout value.
// Should not be called directly.
template <typename WaitInfoType, typename PauseType>
inline u64 WaitPauseHelperGet(SceUID pauseKey, SceUID threadID, std::map<SceUID, PauseType> &pausedWaits, WaitInfoType &waitData) {
	waitData = pausedWaits[pauseKey];
	u64 waitDeadline = waitData.pausedTimeout;
	pausedWaits.erase(pauseKey);
	return waitDeadline;
}

// Retrieve the paused wait info from the list, and pop it.
// This version is for a simple std::map paused list.
// Should not be called directly.
template <>
inline u64 WaitPauseHelperGet<SceUID, u64>(SceUID pauseKey, SceUID threadID, std::map<SceUID, u64> &pausedWaits, SceUID &waitData) {
	waitData = threadID;
	u64 waitDeadline = pausedWaits[pauseKey];
	pausedWaits.erase(pauseKey);
	return waitDeadline;
}

enum WaitBeginEndCallbackResult {
	// Returned when the thread cannot be found in the waiting threads list.
	// Only returned for struct types, which have other data than the threadID.
	WAIT_CB_BAD_WAIT_DATA = -2,
	// Returned when the wait ID of the thread no longer matches the kernel object.
	WAIT_CB_BAD_WAIT_ID = -1,
	// Success, whether that means the wait was paused, deleted, etc.
	WAIT_CB_SUCCESS = 0,
	// Success, and resumed waiting.  Useful for logging.
	WAIT_CB_RESUMED_WAIT = 1,
	// Success, but the wait timed out.  Useful for logging.
	WAIT_CB_TIMED_OUT = 2,
};

// Meant to be called in a registered begin callback function for a wait type.
//
// The goal of this function is to pause the wait.  While inside a callback, waits are released.
// Once the callback returns, the wait should be resumed (see WaitEndCallback.)
//
// This assumes the object has been validated already.  The primary purpose is if you need
// to use a specific pausedWaits list (for example, sceMsgPipe has two types of waiting per object.)
//
// In most cases, use the other, simpler version of WaitBeginCallback().
template <typename WaitInfoType, typename PauseType>
WaitBeginEndCallbackResult WaitBeginCallback(SceUID threadID, SceUID prevCallbackId, int waitTimer, std::vector<WaitInfoType> &waitingThreads, std::map<SceUID, PauseType> &pausedWaits, bool doTimeout = true) {
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	// This means two callbacks in a row.  PSP crashes if the same callback waits inside itself (may need more testing.)
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

// Meant to be called in a registered begin callback function for a wait type.
//
// The goal of this function is to pause the wait.  While inside a callback, waits are released.
// Once the callback returns, the wait should be resumed (see WaitEndCallback.)
//
// In the majority of cases, calling this function is sufficient for the BeginCallback handler.
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

// Meant to be called in a registered end callback function for a wait type.
//
// The goal of this function is to resume the wait, or to complete it if a wait is no longer needed.
//
// This version allows you to specify the pausedWaits and waitingThreads vectors, primarily for
// MsgPipes which have two waiting thread lists.  Unlike the matching WaitBeginCallback() function,
// this still validates the wait (since it needs other data from the object.)
//
// In most cases, use the other, simpler version of WaitEndCallback().
template <typename KO, WaitType waitType, typename WaitInfoType, typename PauseType, class TryUnlockFunc>
WaitBeginEndCallbackResult WaitEndCallback(SceUID threadID, SceUID prevCallbackId, int waitTimer, TryUnlockFunc TryUnlock, WaitInfoType &waitData, std::vector<WaitInfoType> &waitingThreads, std::map<SceUID, PauseType> &pausedWaits) {
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
		if (timeoutPtr != 0 && waitTimer != -1) {
			Memory::Write_U32(0, timeoutPtr);
		}

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
		return WAIT_CB_TIMED_OUT;
	} else {
		if (timeoutPtr != 0 && waitTimer != -1) {
			CoreTiming::ScheduleEvent(cyclesLeft, waitTimer, __KernelGetCurThread());
		}
		return WAIT_CB_RESUMED_WAIT;
	}
}

// Meant to be called in a registered end callback function for a wait type.
//
// The goal of this function is to resume the wait, or to complete it if a wait is no longer needed.
//
// The TryUnlockFunc signature should be (choosen due to similarity to existing funcitons):
// bool TryUnlock(KO *ko, WaitInfoType waitingThreadInfo, u32 &error, int result, bool &wokeThreads)
template <typename KO, WaitType waitType, typename WaitInfoType, class TryUnlockFunc>
WaitBeginEndCallbackResult WaitEndCallback(SceUID threadID, SceUID prevCallbackId, int waitTimer, TryUnlockFunc TryUnlock) {
	u32 error;
	SceUID uid = __KernelGetWaitID(threadID, waitType, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	KO *ko = uid == 0 ? NULL : kernelObjects.Get<KO>(uid, error);
	// We need the ko for the vectors, but to avoid a null check we validate it here too.
	if (!ko) {
		// TODO: Since it was deleted, we don't know how long was actually left.
		// For now, we just say the full time was taken.
		if (timeoutPtr != 0 && waitTimer != -1) {
			Memory::Write_U32(0, timeoutPtr);
		}

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		return WAIT_CB_SUCCESS;
	}

	WaitInfoType waitData;
	auto result = WaitEndCallback<KO, waitType>(threadID, prevCallbackId, waitTimer, TryUnlock, waitData, ko->waitingThreads, ko->pausedWaits);
	if (result == WAIT_CB_RESUMED_WAIT) {
		// TODO: Should this not go at the end?
		ko->waitingThreads.push_back(waitData);
	}
	return result;
}

// Verify that a thread has not been released from waiting, e.g. by sceKernelReleaseWaitThread().
// For a waiting thread info struct.
template <typename T>
inline bool VerifyWait(const T &waitInfo, WaitType waitType, SceUID uid) {
	u32 error;
	SceUID waitID = __KernelGetWaitID(waitInfo.threadID, waitType, error);
	return waitID == uid && error == 0;
}

// Verify that a thread has not been released from waiting, e.g. by sceKernelReleaseWaitThread().
template <>
inline bool VerifyWait(const SceUID &threadID, WaitType waitType, SceUID uid) {
	u32 error;
	SceUID waitID = __KernelGetWaitID(threadID, waitType, error);
	return waitID == uid && error == 0;
}

// Resume a thread from waiting for a particular object.
template <typename T>
inline bool ResumeFromWait(SceUID threadID, WaitType waitType, SceUID uid, T result) {
	if (VerifyWait(threadID, waitType, uid)) {
		__KernelResumeThreadFromWait(threadID, result);
		return true;
	}
	return false;
}

// Removes threads that are not waiting anymore from a waitingThreads list.
template <typename T>
inline void CleanupWaitingThreads(WaitType waitType, SceUID uid, std::vector<T> &waitingThreads) {
	size_t size = waitingThreads.size();
	for (size_t i = 0; i < size; ++i) {
		if (!VerifyWait(waitingThreads[i], waitType, uid)) {
			// Decrement size and swap what was there with i.
			if (--size != i) {
				std::swap(waitingThreads[i], waitingThreads[size]);
			}
			// Now we haven't checked the new i, so go back and do i again.
			--i;
		}
	}
	waitingThreads.resize(size);
}

template <typename T>
inline void RemoveWaitingThread(std::vector<T> &waitingThreads, const SceUID threadID) {
	waitingThreads.erase(std::remove(waitingThreads.begin(), waitingThreads.end(), threadID), waitingThreads.end());
}

};
