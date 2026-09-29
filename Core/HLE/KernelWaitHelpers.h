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
#include <algorithm> // std::erase/remove

#include "Common/CommonTypes.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/ErrorCodes.h"

namespace HLEKernel
{

// Cancels the pending timeout event for a wait that's being satisfied, and writes the time
// remaining back to the game. The event goes off WAIT_TIMEOUT_LATENCY_US after the deadline.
// Note the clamp: UnscheduleEvent returns the scheduled time minus the current time, which goes
// negative when the event is already overdue but hasn't been processed yet. Without the clamp we'd
// write a huge bogus timeout back into the game's variable.
inline void WriteRemainingTimeout(int waitTimer, SceUID threadID, u32 timeoutPtr) {
	if (timeoutPtr == 0 || waitTimer == -1)
		return;

	s64 cyclesLeft = CoreTiming::UnscheduleEvent(waitTimer, threadID) - usToCycles(WAIT_TIMEOUT_LATENCY_US);
	if (cyclesLeft < 0)
		cyclesLeft = 0;
	Memory::WriteOrException_U32((u32)cyclesToUs(cyclesLeft), timeoutPtr);
}

// For kernel object waits, which all time out on the same event (see __KernelWaitCurThreadWithTimeout).
inline void WriteRemainingTimeout(SceUID threadID, u32 timeoutPtr) {
	WriteRemainingTimeout(__KernelWaitTimeoutEvent(), threadID, timeoutPtr);
}

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
			Memory::WriteOrException_U32(0, timeoutPtr);

		// This thread isn't waiting anymore, but we'll remove it from waitingThreads later.
		// The reason is, if it times out, but what it was waiting on is DELETED prior to it
		// actually running, it will get a DELETE result instead of a TIMEOUT.
		// So, we need to remember it or we won't be able to mark it DELETE instead later.
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
		__KernelReSchedule("wait timed out");
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

	// Shouldn't happen: each nesting level pauses under its own key, and on hardware a callback can
	// nest only one level (a CB wait that would go deeper never returns.)
	if (pausedWaits.find(pauseKey) != pausedWaits.end()) {
		return WAIT_CB_SUCCESS;
	}

	u64 pausedTimeout = 0;
	if (doTimeout && waitTimer != -1) {
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(waitTimer, threadID);
		pausedTimeout = CoreTiming::GetTicks(currentMIPS) + cyclesLeft;
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

// The same, for a kernel object wait timing out on the shared event.
template <typename KO, WaitType waitType, typename WaitInfoType>
WaitBeginEndCallbackResult WaitBeginCallback(SceUID threadID, SceUID prevCallbackId) {
	return WaitBeginCallback<KO, waitType, WaitInfoType>(threadID, prevCallbackId, __KernelWaitTimeoutEvent());
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
			Memory::WriteOrException_U32(0, timeoutPtr);
		}

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		return WAIT_CB_SUCCESS;
	}

	u64 waitDeadline = WaitPauseHelperGet(pauseKey, threadID, pausedWaits, waitData);

	// TODO: Don't wake up if __KernelCurHasReadyCallbacks()?

	// The timeout kept running during the callback. Put the timer back first, so that an unlock
	// reports the time that's left.
	s64 cyclesLeft = waitDeadline - CoreTiming::GetTicks(currentMIPS);
	const bool hasTimer = timeoutPtr != 0 && waitTimer != -1 && waitDeadline != 0;
	if (hasTimer) {
		CoreTiming::ScheduleEvent(cyclesLeft < 0 ? 0 : cyclesLeft, waitTimer, threadID);
	}

	bool wokeThreads;
	// Attempt to unlock.
	if (TryUnlock(ko, waitData, error, 0, wokeThreads)) {
		return WAIT_CB_SUCCESS;
	}

	// We only check if it timed out if it couldn't unlock.
	if (cyclesLeft < 0 && waitDeadline != 0) {
		if (hasTimer) {
			CoreTiming::UnscheduleEvent(waitTimer, threadID);
		}
		if (timeoutPtr != 0 && waitTimer != -1) {
			Memory::WriteOrException_U32(0, timeoutPtr);
		}

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
		return WAIT_CB_TIMED_OUT;
	}
	return WAIT_CB_RESUMED_WAIT;
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
			Memory::WriteOrException_U32(0, timeoutPtr);
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

// The same, for a kernel object wait timing out on the shared event.
template <typename KO, WaitType waitType, typename WaitInfoType, class TryUnlockFunc>
WaitBeginEndCallbackResult WaitEndCallback(SceUID threadID, SceUID prevCallbackId, TryUnlockFunc TryUnlock) {
	return WaitEndCallback<KO, waitType, WaitInfoType>(threadID, prevCallbackId, __KernelWaitTimeoutEvent(), TryUnlock);
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

// Ends every wait on an object with the given result (for cancel and delete), through the object's
// function for releasing one waiter:
// bool Unlock(KO *ko, WaitInfoType &waitingThreadInfo, u32 &error, int result, bool &wokeThreads)
template <typename KO, class UnlockFunc>
inline bool ClearWaitingThreads(KO *ko, int result, UnlockFunc Unlock) {
	u32 error;
	bool wokeThreads = false;
	for (auto &waiting : ko->waitingThreads) {
		Unlock(ko, waiting, error, result, wokeThreads);
	}
	ko->waitingThreads.clear();
	return wokeThreads;
}

// A waiting list holds either thread ids or structs with a threadID.
inline SceUID WaitingThreadID(const SceUID &threadID) {
	return threadID;
}
template <typename T>
inline SceUID WaitingThreadID(const T &waitInfo) {
	return waitInfo.threadID;
}

// For objects created with the priority attribute: best priority first, and among equals the order
// they started waiting in.
template <typename T>
inline void SortWaitingThreadsByPriority(std::vector<T> &waitingThreads) {
	std::stable_sort(waitingThreads.begin(), waitingThreads.end(), [](const T &a, const T &b) {
		return __KernelThreadSortPriority(WaitingThreadID(a), WaitingThreadID(b));
	});
}

// The first waiter with the best priority, without reordering the list.
template <typename T>
inline typename std::vector<T>::iterator FindBestPriorityWaiter(std::vector<T> &waitingThreads) {
	_dbg_assert_msg_(!waitingThreads.empty(), "FindBestPriorityWaiter: no threads");
	auto best = waitingThreads.end();
	u32 bestPriority = 0xFFFFFFFF;
	for (auto iter = waitingThreads.begin(); iter != waitingThreads.end(); ++iter) {
		const u32 priority = __KernelGetThreadPrio(WaitingThreadID(*iter));
		if (priority < bestPriority) {
			best = iter;
			bestPriority = priority;
		}
	}
	return best;
}

template <typename T>
inline void RemoveWaitingThread(std::vector<T> &waitingThreads, const SceUID threadID) {
	waitingThreads.erase(std::remove(waitingThreads.begin(), waitingThreads.end(), threadID), waitingThreads.end());
}

};
