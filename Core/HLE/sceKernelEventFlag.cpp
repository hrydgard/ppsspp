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

//http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/kernel/managers/EventFlagManager.java?r=1263

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "ChunkFile.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelEventFlag.h"

void __KernelEventFlagTimeout(u64 userdata, int cycleslate);

struct NativeEventFlag
{
	u32 size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	u32 attr;
	u32 initPattern;
	u32 currentPattern;
	int numWaitThreads;
};

struct EventFlagTh
{
	SceUID tid;
	u32 bits;
	u32 wait;
	u32 outAddr;
	u64 pausedTimeout;
};

class EventFlag : public KernelObject
{
public:
	const char *GetName() {return nef.name;}
	const char *GetTypeName() {return "EventFlag";}
	void GetQuickInfo(char *ptr, int size)
	{
		sprintf(ptr, "init=%08x cur=%08x numwait=%i",
			nef.initPattern,
			nef.currentPattern,
			nef.numWaitThreads);
	}

	static u32 GetMissingErrorCode() {
		return SCE_KERNEL_ERROR_UNKNOWN_EVFID;
	}
	static int GetStaticIDType() { return SCE_KERNEL_TMID_EventFlag; }
	int GetIDType() const { return SCE_KERNEL_TMID_EventFlag; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nef);
		EventFlagTh eft = {0};
		p.Do(waitingThreads, eft);
		p.Do(pausedWaits);
		p.DoMarker("EventFlag");
	}

	NativeEventFlag nef;
	std::vector<EventFlagTh> waitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, EventFlagTh> pausedWaits;
};


/** Event flag creation attributes */
enum PspEventFlagAttributes
{
	/** Allow the event flag to be waited upon by multiple threads */
	PSP_EVENT_WAITMULTIPLE = 0x200
};

/** Event flag wait types */
enum PspEventFlagWaitTypes
{
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

int eventFlagWaitTimer = -1;

void __KernelEventFlagBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __KernelEventFlagEndCallback(SceUID threadID, SceUID prevCallbackId, u32 &returnValue);

void __KernelEventFlagInit()
{
	eventFlagWaitTimer = CoreTiming::RegisterEvent("EventFlagTimeout", __KernelEventFlagTimeout);
	__KernelRegisterWaitTypeFuncs(WAITTYPE_EVENTFLAG, __KernelEventFlagBeginCallback, __KernelEventFlagEndCallback);
}

void __KernelEventFlagDoState(PointerWrap &p)
{
	p.Do(eventFlagWaitTimer);
	CoreTiming::RestoreRegisterEvent(eventFlagWaitTimer, "EventFlagTimeout", __KernelEventFlagTimeout);
	p.DoMarker("sceKernelEventFlag");
}

KernelObject *__KernelEventFlagObject()
{
	// Default object to load from state.
	return new EventFlag;
}

bool __KernelEventFlagMatches(u32 *pattern, u32 bits, u8 wait, u32 outAddr)
{
	if ((wait & PSP_EVENT_WAITOR)
		? (bits & *pattern) /* one or more bits of the mask */
		: ((bits & *pattern) == bits)) /* all the bits of the mask */
	{
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

bool __KernelUnlockEventFlagForThread(EventFlag *e, EventFlagTh &th, u32 &error, int result, bool &wokeThreads)
{
	SceUID waitID = __KernelGetWaitID(th.tid, WAITTYPE_EVENTFLAG, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(th.tid, error);

	// The waitID may be different after a timeout.
	if (waitID != e->GetUID())
		return true;

	// If result is an error code, we're just letting it go.
	if (result == 0)
	{
		if (!__KernelEventFlagMatches(&e->nef.currentPattern, th.bits, th.wait, th.outAddr))
			return false;
	}
	else
	{
		// Otherwise, we set the current result since we're bailing.
		if (Memory::IsValidAddress(th.outAddr))
			Memory::Write_U32(e->nef.currentPattern, th.outAddr);
	}

	if (timeoutPtr != 0 && eventFlagWaitTimer != -1)
	{
		// Remove any event for this thread.
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(eventFlagWaitTimer, th.tid);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(th.tid, result);
	wokeThreads = true;
	return true;
}

bool __KernelClearEventFlagThreads(EventFlag *e, int reason)
{
	u32 error;
	bool wokeThreads = false;
	std::vector<EventFlagTh>::iterator iter, end;
	for (iter = e->waitingThreads.begin(), end = e->waitingThreads.end(); iter != end; ++iter)
		__KernelUnlockEventFlagForThread(e, *iter, error, reason, wokeThreads);
	e->waitingThreads.clear();

	return wokeThreads;
}

void __KernelEventFlagBeginCallback(SceUID threadID, SceUID prevCallbackId)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	u32 error;
	SceUID flagID = __KernelGetWaitID(threadID, WAITTYPE_EVENTFLAG, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	EventFlag *flag = flagID == 0 ? NULL : kernelObjects.Get<EventFlag>(flagID, error);
	if (flag)
	{

		// This means two callbacks in a row.  PSP crashes if the same callback runs inside itself.
		// TODO: Handle this better?
		if (flag->pausedWaits.find(pauseKey) != flag->pausedWaits.end())
			return;

		EventFlagTh waitData = {0};
		for (size_t i = 0; i < flag->waitingThreads.size(); i++)
		{
			EventFlagTh *t = &flag->waitingThreads[i];
			if (t->tid == threadID)
			{
				waitData = *t;
				// TODO: Hmm, what about priority/fifo order?  Does it lose its place in line?
				flag->waitingThreads.erase(flag->waitingThreads.begin() + i);
				break;
			}
		}

		if (waitData.tid != threadID)
		{
			ERROR_LOG_REPORT(HLE, "sceKernelWaitEventFlagCB: wait not found to pause for callback");
			return;
		}

		if (timeoutPtr != 0 && eventFlagWaitTimer != -1)
		{
			s64 cyclesLeft = CoreTiming::UnscheduleEvent(eventFlagWaitTimer, threadID);
			waitData.pausedTimeout = CoreTiming::GetTicks() + cyclesLeft;
		}
		else
			waitData.pausedTimeout = 0;

		flag->pausedWaits[pauseKey] = waitData;
		DEBUG_LOG(HLE, "sceKernelWaitEventFlagCB: Suspending lock wait for callback");
	}
	else
		WARN_LOG_REPORT(HLE, "sceKernelWaitEventFlagCB: beginning callback with bad wait id?");
}

void __KernelEventFlagEndCallback(SceUID threadID, SceUID prevCallbackId, u32 &returnValue)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	u32 error;
	SceUID flagID = __KernelGetWaitID(threadID, WAITTYPE_EVENTFLAG, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	EventFlag *flag = flagID == 0 ? NULL : kernelObjects.Get<EventFlag>(flagID, error);
	if (!flag || flag->pausedWaits.find(pauseKey) == flag->pausedWaits.end())
	{
		// TODO: Since it was deleted, we don't know how long was actually left.
		// For now, we just say the full time was taken.
		if (timeoutPtr != 0 && eventFlagWaitTimer != -1)
			Memory::Write_U32(0, timeoutPtr);

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_DELETE);
		return;
	}

	EventFlagTh waitData = flag->pausedWaits[pauseKey];
	u64 waitDeadline = waitData.pausedTimeout;
	flag->pausedWaits.erase(pauseKey);

	// TODO: Don't wake up if __KernelCurHasReadyCallbacks()?

	bool wokeThreads;
	// Attempt to unlock.
	if (__KernelUnlockEventFlagForThread(flag, waitData, error, 0, wokeThreads))
		return;

	// We only check if it timed out if it couldn't unlock.
	s64 cyclesLeft = waitDeadline - CoreTiming::GetTicks();
	if (cyclesLeft < 0 && waitDeadline != 0)
	{
		if (timeoutPtr != 0 && eventFlagWaitTimer != -1)
			Memory::Write_U32(0, timeoutPtr);

		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	}
	else
	{
		if (timeoutPtr != 0 && eventFlagWaitTimer != -1)
			CoreTiming::ScheduleEvent(cyclesLeft, eventFlagWaitTimer, __KernelGetCurThread());

		// TODO: Should this not go at the end?
		flag->waitingThreads.push_back(waitData);

		DEBUG_LOG(HLE, "sceKernelWaitEventFlagCB: Resuming lock wait for callback");
	}
}

//SceUID sceKernelCreateEventFlag(const char *name, int attr, int bits, SceKernelEventFlagOptParam *opt);
int sceKernelCreateEventFlag(const char *name, u32 flag_attr, u32 flag_initPattern, u32 optPtr)
{
	if (!name)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateEventFlag(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}

	// These attributes aren't valid.
	if ((flag_attr & 0x100) != 0 || flag_attr >= 0x300)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateEventFlag(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, flag_attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
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

	DEBUG_LOG(HLE, "%i=sceKernelCreateEventFlag(\"%s\", %08x, %08x, %08x)", id, e->nef.name, e->nef.attr, e->nef.currentPattern, optPtr);

	if (optPtr != 0)
		WARN_LOG_REPORT(HLE, "sceKernelCreateEventFlag(%s) unsupported options parameter: %08x", name, optPtr);
	if ((flag_attr & ~PSP_EVENT_WAITMULTIPLE) != 0)
		WARN_LOG_REPORT(HLE, "sceKernelCreateEventFlag(%s) unsupported attr parameter: %08x", name, flag_attr);

	return id;
}

u32 sceKernelCancelEventFlag(SceUID uid, u32 pattern, u32 numWaitThreadsPtr)
{
	DEBUG_LOG(HLE, "sceKernelCancelEventFlag(%i, %08X, %08X)", uid, pattern, numWaitThreadsPtr);

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(uid, error);
	if (e)
	{
		e->nef.numWaitThreads = (int) e->waitingThreads.size();
		if (Memory::IsValidAddress(numWaitThreadsPtr))
			Memory::Write_U32(e->nef.numWaitThreads, numWaitThreadsPtr);

		e->nef.currentPattern = pattern;

		if (__KernelClearEventFlagThreads(e, SCE_KERNEL_ERROR_WAIT_CANCEL))
			hleReSchedule("event flag canceled");

		return 0;
	}
	else
		return error;
}

u32 sceKernelClearEventFlag(SceUID id, u32 bits)
{
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		DEBUG_LOG(HLE, "sceKernelClearEventFlag(%i, %08x)", id, bits);
		e->nef.currentPattern &= bits;
		// Note that it's not possible for threads to get woken up by this action.
		return 0;
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelClearEventFlag(%i, %08x) - error", id, bits);
		return error;
	}
}

u32 sceKernelDeleteEventFlag(SceUID uid)
{
	DEBUG_LOG(HLE, "sceKernelDeleteEventFlag(%i)", uid);

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(uid, error);
	if (e)
	{
		bool wokeThreads = __KernelClearEventFlagThreads(e, SCE_KERNEL_ERROR_WAIT_DELETE);
		if (wokeThreads)
			hleReSchedule("event flag deleted");

		return kernelObjects.Destroy<EventFlag>(uid);
	}
	else
		return error;
}

u32 sceKernelSetEventFlag(SceUID id, u32 bitsToSet)
{
	u32 error;
	DEBUG_LOG(HLE, "sceKernelSetEventFlag(%i, %08x)", id, bitsToSet);
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		bool wokeThreads = false;

		e->nef.currentPattern |= bitsToSet;

		for (size_t i = 0; i < e->waitingThreads.size(); ++i)
		{
			EventFlagTh *t = &e->waitingThreads[i];
			if (__KernelUnlockEventFlagForThread(e, *t, error, 0, wokeThreads))
			{
				e->waitingThreads.erase(e->waitingThreads.begin() + i);
				// Try the one that used to be in this place next.
				--i;
			}
		}

		if (wokeThreads)
			hleReSchedule("event flag set");

		return 0;
	}
	else
	{
		return error;
	}
}

void __KernelEventFlagTimeout(u64 userdata, int cycleslate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	SceUID flagID = __KernelGetWaitID(threadID, WAITTYPE_EVENTFLAG, error);
	EventFlag *e = kernelObjects.Get<EventFlag>(flagID, error);
	if (e)
	{
		for (size_t i = 0; i < e->waitingThreads.size(); i++)
		{
			EventFlagTh *t = &e->waitingThreads[i];
			if (t->tid == threadID)
			{
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

void __KernelSetEventFlagTimeout(EventFlag *e, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || eventFlagWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This seems like the actual timing of timeouts on hardware.
	if (micro <= 1)
		micro = 5;
	else if (micro <= 209)
		micro = 240;

	// This should call __KernelEventFlagTimeout() later, unless we cancel it.
	CoreTiming::ScheduleEvent(usToCycles(micro), eventFlagWaitTimer, __KernelGetCurThread());
}

void __KernelEventFlagRemoveThread(EventFlag *e, SceUID threadID)
{
	for (size_t i = 0; i < e->waitingThreads.size(); i++)
	{
		EventFlagTh *t = &e->waitingThreads[i];
		if (t->tid == threadID)
		{
			e->waitingThreads.erase(e->waitingThreads.begin() + i);
			break;
		}
	}
}

int sceKernelWaitEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelWaitEventFlag(%i, %08x, %i, %08x, %08x)", id, bits, wait, outBitsPtr, timeoutPtr);

	if ((wait & ~PSP_EVENT_WAITKNOWN) != 0)
	{
		WARN_LOG_REPORT(HLE, "sceKernelWaitEventFlag(%i) invalid mode parameter: %08x", id, wait);
		return SCE_KERNEL_ERROR_ILLEGAL_MODE;
	}
	// Can't wait on 0, that's guaranteed to wait forever.
	if (bits == 0)
		return SCE_KERNEL_ERROR_EVF_ILPAT;

	if (!__KernelIsDispatchEnabled())
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		EventFlagTh th;
		if (!__KernelEventFlagMatches(&e->nef.currentPattern, bits, wait, outBitsPtr))
		{
			// If this thread was left in waitingThreads after a timeout, remove it.
			// Otherwise we might write the outBitsPtr in the wrong place.
			__KernelEventFlagRemoveThread(e, __KernelGetCurThread());

			u32 timeout = 0xFFFFFFFF;
			if (Memory::IsValidAddress(timeoutPtr))
				timeout = Memory::Read_U32(timeoutPtr);

			// Do we allow more than one thread to wait?
			if (e->waitingThreads.size() > 0 && (e->nef.attr & PSP_EVENT_WAITMULTIPLE) == 0)
				return SCE_KERNEL_ERROR_EVF_MULTI;

			// No match - must wait.
			th.tid = __KernelGetCurThread();
			th.bits = bits;
			th.wait = wait;
			// If < 5ms, sometimes hardware doesn't write this, but it's unpredictable.
			th.outAddr = timeout == 0 ? 0 : outBitsPtr;
			e->waitingThreads.push_back(th);

			__KernelSetEventFlagTimeout(e, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_EVENTFLAG, id, 0, timeoutPtr, false, "event flag waited");
		}

		return 0;
	}
	else
	{
		return error;
	}
}

int sceKernelWaitEventFlagCB(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelWaitEventFlagCB(%i, %08x, %i, %08x, %08x)", id, bits, wait, outBitsPtr, timeoutPtr);

	if ((wait & ~PSP_EVENT_WAITKNOWN) != 0)
	{
		WARN_LOG_REPORT(HLE, "sceKernelWaitEventFlagCB(%i) invalid mode parameter: %08x", id, wait);
		return SCE_KERNEL_ERROR_ILLEGAL_MODE;
	}
	// Can't wait on 0, that's guaranteed to wait forever.
	if (bits == 0)
		return SCE_KERNEL_ERROR_EVF_ILPAT;

	if (!__KernelIsDispatchEnabled())
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		EventFlagTh th;
		bool doWait = !__KernelEventFlagMatches(&e->nef.currentPattern, bits, wait, outBitsPtr);
		bool doCallbackWait = false;
		if (__KernelCurHasReadyCallbacks())
		{
			doWait = true;
			doCallbackWait = true;
		}

		if (doWait)
		{
			// If this thread was left in waitingThreads after a timeout, remove it.
			// Otherwise we might write the outBitsPtr in the wrong place.
			__KernelEventFlagRemoveThread(e, __KernelGetCurThread());

			u32 timeout = 0xFFFFFFFF;
			if (Memory::IsValidAddress(timeoutPtr))
				timeout = Memory::Read_U32(timeoutPtr);

			// Do we allow more than one thread to wait?
			if (e->waitingThreads.size() > 0 && (e->nef.attr & PSP_EVENT_WAITMULTIPLE) == 0)
				return SCE_KERNEL_ERROR_EVF_MULTI;

			// No match - must wait.
			th.tid = __KernelGetCurThread();
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
		}
		else
			hleCheckCurrentCallbacks();

		return 0;
	}
	else
	{
		return error;
	}
}

int sceKernelPollEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr)
{
	DEBUG_LOG(HLE, "sceKernelPollEventFlag(%i, %08x, %i, %08x, %08x)", id, bits, wait, outBitsPtr, timeoutPtr);

	if ((wait & ~PSP_EVENT_WAITKNOWN) != 0)
	{
		WARN_LOG_REPORT(HLE, "sceKernelPollEventFlag(%i) invalid mode parameter: %08x", id, wait);
		return SCE_KERNEL_ERROR_ILLEGAL_MODE;
	}
	// Poll seems to also fail when CLEAR and CLEARALL are used together, but not wait.
	if ((wait & PSP_EVENT_WAITCLEAR) != 0 && (wait & PSP_EVENT_WAITCLEARALL) != 0)
	{
		WARN_LOG_REPORT(HLE, "sceKernelPollEventFlag(%i) invalid mode parameter: %08x", id, wait);
		return SCE_KERNEL_ERROR_ILLEGAL_MODE;
	}
	// Can't wait on 0, it never matches.
	if (bits == 0)
		return SCE_KERNEL_ERROR_EVF_ILPAT;

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		if (!__KernelEventFlagMatches(&e->nef.currentPattern, bits, wait, outBitsPtr))
		{
			if (Memory::IsValidAddress(outBitsPtr))
				Memory::Write_U32(e->nef.currentPattern, outBitsPtr);

			if (e->waitingThreads.size() > 0 && (e->nef.attr & PSP_EVENT_WAITMULTIPLE) == 0)
				return SCE_KERNEL_ERROR_EVF_MULTI;

			// No match - return that, this is polling, not waiting.
			return SCE_KERNEL_ERROR_EVF_COND;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		return error;
	}
}

//int sceKernelReferEventFlagStatus(SceUID event, SceKernelEventFlagInfo *status);
u32 sceKernelReferEventFlagStatus(SceUID id, u32 statusPtr)
{
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		DEBUG_LOG(HLE, "sceKernelReferEventFlagStatus(%i, %08x)", id, statusPtr);

		if (!Memory::IsValidAddress(statusPtr))
			return -1;

		u32 error;
		for (auto iter = e->waitingThreads.begin(); iter != e->waitingThreads.end(); ++iter)
		{
			SceUID waitID = __KernelGetWaitID(iter->tid, WAITTYPE_EVENTFLAG, error);
			// The thread is no longer waiting for this, clean it up.
			if (waitID != id)
				e->waitingThreads.erase(iter--);
		}

		e->nef.numWaitThreads = (int) e->waitingThreads.size();
		if (Memory::Read_U32(statusPtr) != 0)
			Memory::WriteStruct(statusPtr, &e->nef);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelReferEventFlagStatus(%i, %08x): invalid event flag", id, statusPtr);
		return error;
	}
}
