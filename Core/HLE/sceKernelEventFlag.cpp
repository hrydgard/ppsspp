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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/HLE/HLE.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelEventFlag.h"
#include "Core/HLE/KernelWaitHelpers.h"

void __KernelEventFlagTimeout(u64 userdata, int cycleslate);

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

class EventFlag : public KernelObject {
public:
	const char *GetName() override { return nef.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "EventFlag"; }
	void GetQuickInfo(char *ptr, int size) override {
		sprintf(ptr, "init=%08x cur=%08x numwait=%i",
			nef.initPattern,
			nef.currentPattern,
			nef.numWaitThreads);
	}

	static u32 GetMissingErrorCode() {
		return SCE_KERNEL_ERROR_UNKNOWN_EVFID;
	}
	static int GetStaticIDType() { return SCE_KERNEL_TMID_EventFlag; }
	int GetIDType() const override { return SCE_KERNEL_TMID_EventFlag; }

	void DoState(PointerWrap &p) override {
		auto s = p.Section("EventFlag", 1);
		if (!s)
			return;

		Do(p, nef);
		EventFlagTh eft = { 0 };
		Do(p, waitingThreads, eft);
		Do(p, pausedWaits);
	}

	NativeEventFlag nef;
	std::vector<EventFlagTh> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, EventFlagTh> pausedWaits;
};


/** Event flag creation attributes */
enum PspEventFlagAttributes {
	/** Allow the event flag to be waited upon by multiple threads */
	PSP_EVENT_WAITMULTIPLE = 0x200
};

/** Event flag wait types */
enum PspEventFlagWaitTypes {
	/** Wait for all bits in the pattern to be set */
	PSP_EVENT_WAITAND = 0x00,
	/** Wait for one or more bits in the pattern to be set */
	PSP_EVENT_WAITOR = 0x01,
	/** Clear the entire pattern when it matches. */
	PSP_EVENT_WAITCLEARALL = 0x10,
	/** Clear the wait pattern when it matches */
	PSP_EVENT_WAITCLEAR = 0x20,

	PSP_EVENT_WAITKNOWN = PSP_EVENT_WAITCLEAR | PSP_EVENT_WAITCLEARALL | PSP_EVENT_WAITOR,
};

static int eventFlagWaitTimer = -1;

void __KernelEventFlagBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelEventFlagEndCallback(SceUID threadID, SceUID prevCallbackId);

void __KernelEventFlagInit() {
	eventFlagWaitTimer = CoreTiming::RegisterEvent("EventFlagTimeout", __KernelEventFlagTimeout);
	__KernelRegisterWaitTypeFuncs(WAITTYPE_EVENTFLAG, __KernelEventFlagBeginCallback, __KernelEventFlagEndCallback);
}

void __KernelEventFlagDoState(PointerWrap &p) {
	auto s = p.Section("sceKernelEventFlag", 1);
	if (!s)
		return;

	Do(p, eventFlagWaitTimer);
	CoreTiming::RestoreRegisterEvent(eventFlagWaitTimer, "EventFlagTimeout", __KernelEventFlagTimeout);
}

KernelObject *__KernelEventFlagObject() {
	// Default object to load from state.
	return new EventFlag;
}

static bool __KernelCheckEventFlagMatches(u32 pattern, u32 bits, u8 wait) {
	// Is this in OR (any bit can match) or AND (all bits must match) mode?
	if (wait & PSP_EVENT_WAITOR) {
		return (bits & pattern) != 0;
	} else {
		return (bits & pattern) == bits;
	}
}

static bool __KernelApplyEventFlagMatch(u32_le *pattern, u32 bits, u8 wait, u32 outAddr) {
	if (__KernelCheckEventFlagMatches(*pattern, bits, wait)) {
		if (Memory::IsValidAddress(outAddr))
			Memory::Write_U32(*pattern, outAddr);

		if (wait & PSP_EVENT_WAITCLEAR)
			*pattern &= ~bits;
		if (wait & PSP_EVENT_WAITCLEARALL)
			*pattern = 0;
		return true;
	}
	return false;
}

static bool __KernelUnlockEventFlagForThread(EventFlag *e, EventFlagTh &th, u32 &error, int result, bool &wokeThreads) {
	if (!HLEKernel::VerifyWait(th.threadID, WAITTYPE_EVENTFLAG, e->GetUID()))
		return true;

	// If result is an error code, we're just letting it go.
	if (result == 0) {
		if (!__KernelApplyEventFlagMatch(&e->nef.currentPattern, th.bits, th.wait, th.outAddr))
			return false;
	} else {
		// Otherwise, we set the current result since we're bailing.
		if (Memory::IsValidAddress(th.outAddr))
			Memory::Write_U32(e->nef.currentPattern, th.outAddr);
	}

	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(th.threadID, error);
	if (timeoutPtr != 0 && eventFlagWaitTimer != -1) {
		// Remove any event for this thread.
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(eventFlagWaitTimer, th.threadID);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(th.threadID, result);
	wokeThreads = true;
	return true;
}

static bool __KernelClearEventFlagThreads(EventFlag *e, int reason) {
	u32 error;
	bool wokeThreads = false;
	for (auto &event : e->waitingThreads)
		__KernelUnlockEventFlagForThread(e, event, error, reason, wokeThreads);
	e->waitingThreads.clear();

	return wokeThreads;
}

void __KernelEventFlagBeginCallback(SceUID threadID, SceUID prevCallbackId) {
	auto result = HLEKernel::WaitBeginCallback<EventFlag, WAITTYPE_EVENTFLAG, EventFlagTh>(threadID, prevCallbackId, eventFlagWaitTimer);
	if (result == HLEKernel::WAIT_CB_SUCCESS)
		DEBUG_LOG(Log::sceKernel, "sceKernelWaitEventFlagCB: Suspending lock wait for callback");
	else if (result == HLEKernel::WAIT_CB_BAD_WAIT_DATA)
		ERROR_LOG_REPORT(Log::sceKernel, "sceKernelWaitEventFlagCB: wait not found to pause for callback");
	else
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelWaitEventFlagCB: beginning callback with bad wait id?");
}

void __KernelEventFlagEndCallback(SceUID threadID, SceUID prevCallbackId) {
	auto result = HLEKernel::WaitEndCallback<EventFlag, WAITTYPE_EVENTFLAG, EventFlagTh>(threadID, prevCallbackId, eventFlagWaitTimer, __KernelUnlockEventFlagForThread);
	if (result == HLEKernel::WAIT_CB_RESUMED_WAIT)
		DEBUG_LOG(Log::sceKernel, "sceKernelWaitEventFlagCB: Resuming lock wait from callback");
}

//SceUID sceKernelCreateEventFlag(const char *name, int attr, int bits, SceKernelEventFlagOptParam *opt);
int sceKernelCreateEventFlag(const char *name, u32 flag_attr, u32 flag_initPattern, u32 optPtr) {
	if (!name) {
		return hleReportWarning(Log::sceKernel, SCE_KERNEL_ERROR_ERROR, "invalid name");
	}

	// These attributes aren't valid.
	if ((flag_attr & 0x100) != 0 || flag_attr >= 0x300) {
		return hleReportWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ATTR, "invalid attr parameter: %08x", flag_attr);
	}

	EventFlag *e = new EventFlag();
	SceUID id = kernelObjects.Create(e);

	e->nef.size = sizeof(NativeEventFlag);
	strncpy(e->nef.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	e->nef.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	e->nef.attr = flag_attr;
	e->nef.initPattern = flag_initPattern;
	e->nef.currentPattern = e->nef.initPattern;
	e->nef.numWaitThreads = 0;

	if (optPtr != 0) {
		u32 size = Memory::Read_U32(optPtr);
		if (size > 4)
			WARN_LOG_REPORT(Log::sceKernel, "sceKernelCreateEventFlag(%s) unsupported options parameter, size = %d", name, size);
	}
	if ((flag_attr & ~PSP_EVENT_WAITMULTIPLE) != 0)
		WARN_LOG_REPORT(Log::sceKernel, "sceKernelCreateEventFlag(%s) unsupported attr parameter: %08x", name, flag_attr);

	return hleLogSuccessI(Log::sceKernel, id);
}

u32 sceKernelCancelEventFlag(SceUID uid, u32 pattern, u32 numWaitThreadsPtr) {
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(uid, error);
	if (e) {
		e->nef.numWaitThreads = (int) e->waitingThreads.size();
		if (Memory::IsValidAddress(numWaitThreadsPtr))
			Memory::Write_U32(e->nef.numWaitThreads, numWaitThreadsPtr);

		e->nef.currentPattern = pattern;

		if (__KernelClearEventFlagThreads(e, SCE_KERNEL_ERROR_WAIT_CANCEL))
			hleReSchedule("event flag canceled");

		hleEatCycles(580);
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogDebug(Log::sceKernel, error, "invalid event flag");
	}
}

u32 sceKernelClearEventFlag(SceUID id, u32 bits) {
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e) {
		e->nef.currentPattern &= bits;
		// Note that it's not possible for threads to get woken up by this action.
		hleEatCycles(430);
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogDebug(Log::sceKernel, error, "invalid event flag");
	}
}

u32 sceKernelDeleteEventFlag(SceUID uid) {
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(uid, error);
	if (e) {
		bool wokeThreads = __KernelClearEventFlagThreads(e, SCE_KERNEL_ERROR_WAIT_DELETE);
		if (wokeThreads)
			hleReSchedule("event flag deleted");

		return hleLogSuccessI(Log::sceKernel, kernelObjects.Destroy<EventFlag>(uid));
	} else {
		return hleLogDebug(Log::sceKernel, error, "invalid event flag");
	}
}

u32 sceKernelSetEventFlag(SceUID id, u32 bitsToSet) {
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e) {
		bool wokeThreads = false;

		e->nef.currentPattern |= bitsToSet;

		for (size_t i = 0; i < e->waitingThreads.size(); ++i) {
			EventFlagTh *t = &e->waitingThreads[i];
			if (__KernelUnlockEventFlagForThread(e, *t, error, 0, wokeThreads)) {
				e->waitingThreads.erase(e->waitingThreads.begin() + i);
				// Try the one that used to be in this place next.
				--i;
			}
		}

		if (wokeThreads)
			hleReSchedule("event flag set");

		hleEatCycles(430);
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogDebug(Log::sceKernel, error, "invalid event flag");
	}
}

void __KernelEventFlagTimeout(u64 userdata, int cycleslate) {
	SceUID threadID = (SceUID)userdata;

	// This still needs to set the result pointer from the wait.
	u32 error;
	SceUID flagID = __KernelGetWaitID(threadID, WAITTYPE_EVENTFLAG, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	EventFlag *e = kernelObjects.Get<EventFlag>(flagID, error);
	if (e) {
		if (timeoutPtr != 0)
			Memory::Write_U32(0, timeoutPtr);

		for (size_t i = 0; i < e->waitingThreads.size(); i++) {
			EventFlagTh *t = &e->waitingThreads[i];
			if (t->threadID == threadID) {
				bool wokeThreads;

				// This thread isn't waiting anymore, but we'll remove it from waitingThreads later.
				// The reason is, if it times out, but what it was waiting on is DELETED prior to it
				// actually running, it will get a DELETE result instead of a TIMEOUT.
				// So, we need to remember it or we won't be able to mark it DELETE instead later.
				__KernelUnlockEventFlagForThread(e, *t, error, SCE_KERNEL_ERROR_WAIT_TIMEOUT, wokeThreads);
				break;
			}
		}
	}
}

static void __KernelSetEventFlagTimeout(EventFlag *e, u32 timeoutPtr) {
	if (timeoutPtr == 0 || eventFlagWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This seems like the actual timing of timeouts on hardware.
	if (micro <= 1)
		micro = 25;
	else if (micro <= 209)
		micro = 240;

	// This should call __KernelEventFlagTimeout() later, unless we cancel it.
	CoreTiming::ScheduleEvent(usToCycles(micro), eventFlagWaitTimer, __KernelGetCurThread());
}

int sceKernelWaitEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr) {
	if ((wait & ~PSP_EVENT_WAITKNOWN) != 0) {
		return hleReportWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_MODE, "invalid mode parameter: %08x", wait);
	}
	// Can't wait on 0, that's guaranteed to wait forever.
	if (bits == 0) {
		return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_EVF_ILPAT, "bad pattern");
	}

	if (!__KernelIsDispatchEnabled()) {
		return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
	}

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e) {
		EventFlagTh th;
		if (!__KernelApplyEventFlagMatch(&e->nef.currentPattern, bits, wait, outBitsPtr)) {
			// If this thread was left in waitingThreads after a timeout, remove it.
			// Otherwise we might write the outBitsPtr in the wrong place.
			HLEKernel::RemoveWaitingThread(e->waitingThreads, __KernelGetCurThread());

			u32 timeout = 0xFFFFFFFF;
			if (Memory::IsValidAddress(timeoutPtr))
				timeout = Memory::Read_U32(timeoutPtr);

			// Do we allow more than one thread to wait?
			if (e->waitingThreads.size() > 0 && (e->nef.attr & PSP_EVENT_WAITMULTIPLE) == 0) {
				return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_EVF_MULTI);
			}

			(void)hleLogSuccessI(Log::sceKernel, 0, "waiting");

			// No match - must wait.
			th.threadID = __KernelGetCurThread();
			th.bits = bits;
			th.wait = wait;
			// If < 5ms, sometimes hardware doesn't write this, but it's unpredictable.
			th.outAddr = timeout == 0 ? 0 : outBitsPtr;
			e->waitingThreads.push_back(th);

			__KernelSetEventFlagTimeout(e, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_EVENTFLAG, id, 0, timeoutPtr, false, "event flag waited");
		} else {
			(void)hleLogSuccessI(Log::sceKernel, 0);
		}

		hleEatCycles(500);
		return 0;
	} else {
		return hleLogDebug(Log::sceKernel, error, "invalid event flag");
	}
}

int sceKernelWaitEventFlagCB(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr) {
	if ((wait & ~PSP_EVENT_WAITKNOWN) != 0) {
		return hleReportWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_MODE, "invalid mode parameter: %08x", wait);
	}
	// Can't wait on 0, that's guaranteed to wait forever.
	if (bits == 0) {
		return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_EVF_ILPAT, "bad pattern");
	}

	if (!__KernelIsDispatchEnabled()) {
		return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
	}

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e) {
		EventFlagTh th;
		// We only check, not apply here.  This way the CLEAR/etc. options don't apply yet.
		// If we run callbacks, we will check again after the callbacks complete.
		bool doWait = !__KernelCheckEventFlagMatches(e->nef.currentPattern, bits, wait);
		bool doCallbackWait = false;
		if (__KernelCurHasReadyCallbacks()) {
			doWait = true;
			doCallbackWait = true;
		}

		if (doWait) {
			// If this thread was left in waitingThreads after a timeout, remove it.
			// Otherwise we might write the outBitsPtr in the wrong place.
			HLEKernel::RemoveWaitingThread(e->waitingThreads, __KernelGetCurThread());

			u32 timeout = 0xFFFFFFFF;
			if (Memory::IsValidAddress(timeoutPtr))
				timeout = Memory::Read_U32(timeoutPtr);

			// Do we allow more than one thread to wait?
			if (e->waitingThreads.size() > 0 && (e->nef.attr & PSP_EVENT_WAITMULTIPLE) == 0) {
				return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_EVF_MULTI);
			}

			(void)hleLogSuccessI(Log::sceKernel, 0, "waiting");

			// No match - must wait.
			th.threadID = __KernelGetCurThread();
			th.bits = bits;
			th.wait = wait;
			// If < 5ms, sometimes hardware doesn't write this, but it's unpredictable.
			th.outAddr = timeout == 0 ? 0 : outBitsPtr;
			e->waitingThreads.push_back(th);

			__KernelSetEventFlagTimeout(e, timeoutPtr);
			if (doCallbackWait)
				__KernelWaitCallbacksCurThread(WAITTYPE_EVENTFLAG, id, 0, timeoutPtr);
			else
				__KernelWaitCurThread(WAITTYPE_EVENTFLAG, id, 0, timeoutPtr, true, "event flag waited");
		} else {
			(void)hleLogSuccessI(Log::sceKernel, 0);
			__KernelApplyEventFlagMatch(&e->nef.currentPattern, bits, wait, outBitsPtr);
			hleCheckCurrentCallbacks();
		}

		hleEatCycles(500);
		return 0;
	} else {
		return hleLogDebug(Log::sceKernel, error, "invalid event flag");
	}
}

int sceKernelPollEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr) {
	if ((wait & ~PSP_EVENT_WAITKNOWN) != 0) {
		return hleReportWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_MODE, "invalid mode parameter: %08x", wait);
	}
	// Poll seems to also fail when CLEAR and CLEARALL are used together, but not wait.
	if ((wait & PSP_EVENT_WAITCLEAR) != 0 && (wait & PSP_EVENT_WAITCLEARALL) != 0) {
		return hleReportWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_MODE, "invalid mode parameter: %08x", wait);
	}
	// Can't wait on 0, it never matches.
	if (bits == 0) {
		return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_EVF_ILPAT, "bad pattern");
	}

	hleEatCycles(360);

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e) {
		if (!__KernelApplyEventFlagMatch(&e->nef.currentPattern, bits, wait, outBitsPtr)) {
			if (Memory::IsValidAddress(outBitsPtr))
				Memory::Write_U32(e->nef.currentPattern, outBitsPtr);

			if (e->waitingThreads.size() > 0 && (e->nef.attr & PSP_EVENT_WAITMULTIPLE) == 0) {
				return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_EVF_MULTI);
			}

			// No match - return that, this is polling, not waiting.
			return hleLogDebug(Log::sceKernel, SCE_KERNEL_ERROR_EVF_COND);
		} else {
			return hleLogSuccessI(Log::sceKernel, 0);
		}
	} else {
		return hleLogDebug(Log::sceKernel, error, "invalid event flag");
	}
}

//int sceKernelReferEventFlagStatus(SceUID event, SceKernelEventFlagInfo *status);
u32 sceKernelReferEventFlagStatus(SceUID id, u32 statusPtr) {
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e) {
		auto status = PSPPointer<NativeEventFlag>::Create(statusPtr);
		if (!status.IsValid())
			return hleLogWarning(Log::sceKernel, -1, "invalid ptr");

		HLEKernel::CleanupWaitingThreads(WAITTYPE_EVENTFLAG, id, e->waitingThreads);

		e->nef.numWaitThreads = (int) e->waitingThreads.size();
		if (status->size != 0) {
			*status = e->nef;
			status.NotifyWrite("EventFlagStatus");
		}
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogDebug(Log::sceKernel, error, "invalid event flag");
	}
}
