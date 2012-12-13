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
#include "../../Core/CoreTiming.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelEventFlag.h"

#include <queue>

void __KernelEventFlagTimeout(u64 userdata, int cycleslate);

struct NativeEventFlag
{
	u32 size;
	char name[32];
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
	int GetIDType() const { return SCE_KERNEL_TMID_EventFlag; }

	NativeEventFlag nef;
	std::vector<EventFlagTh> waitingThreads;
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
	PSP_EVENT_WAITAND = 0,
	/** Wait for one or more bits in the pattern to be set */
	PSP_EVENT_WAITOR	= 1,
	/** Clear the wait pattern when it matches */
	PSP_EVENT_WAITCLEAR = 0x20
};

bool eventFlagInitComplete = false;
int eventFlagWaitTimer = 0;

void __KernelEventFlagInit()
{
	eventFlagWaitTimer = CoreTiming::RegisterEvent("EventFlagTimeout", &__KernelEventFlagTimeout);
	eventFlagInitComplete = true;
}

//SceUID sceKernelCreateEventFlag(const char *name, int attr, int bits, SceKernelEventFlagOptParam *opt);
int sceKernelCreateEventFlag(const char *name, u32 flag_attr, u32 flag_initPattern, u32 optPtr)
{
	if (!eventFlagInitComplete)
		__KernelEventFlagInit();

	EventFlag *e = new EventFlag();
	SceUID id = kernelObjects.Create(e);

	e->nef.size = sizeof(NativeEventFlag);
	strncpy(e->nef.name, name, 32);
	e->nef.attr = flag_attr;
	e->nef.initPattern = flag_initPattern;
	e->nef.currentPattern = e->nef.initPattern;
	e->nef.numWaitThreads = 0;

	DEBUG_LOG(HLE,"%i=sceKernelCreateEventFlag(\"%s\", %08x, %08x, %08x)", id, e->nef.name, e->nef.attr, e->nef.currentPattern, optPtr);
	return id;
}

u32 sceKernelClearEventFlag(SceUID id, u32 bits)
{
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		DEBUG_LOG(HLE,"sceKernelClearEventFlag(%i, %08x)", id, bits);
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
	DEBUG_LOG(HLE,"sceKernelDeleteEventFlag(%i)", uid);
	return kernelObjects.Destroy<EventFlag>(uid);
}

u8 __KernelEventFlagMatches(u32 *pattern, u32 bits, u8 wait, u32 outAddr)
{
	if ((wait & PSP_EVENT_WAITOR)
		? (bits & *pattern) /* one or more bits of the mask */
		: ((bits & *pattern) == bits)) /* all the bits of the mask */
	{
		if (Memory::IsValidAddress(outAddr))
			Memory::Write_U32(*pattern, outAddr);

		if (wait & PSP_EVENT_WAITCLEAR)
			*pattern &= ~bits;
		return 1;
	}
	return 0;
}			 

u32 sceKernelSetEventFlag(SceUID id, u32 bitsToSet)
{
	u32 error;
	DEBUG_LOG(HLE,"sceKernelSetEventFlag(%i, %08x)", id, bitsToSet);
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		bool wokeThreads = false;

		e->nef.currentPattern |= bitsToSet;

retry:
		for (size_t i = 0; i < e->waitingThreads.size(); i++)
		{
			EventFlagTh *t = &e->waitingThreads[i];
			if (__KernelEventFlagMatches(&e->nef.currentPattern, t->bits, t->wait, t->outAddr))
			{
				__KernelResumeThreadFromWait(t->tid);
				wokeThreads = true;
				e->nef.numWaitThreads--;
				e->waitingThreads.erase(e->waitingThreads.begin() + i);
				goto retry;
			}
		}
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
				if (Memory::IsValidAddress(t->outAddr))
					Memory::Write_U32(e->nef.currentPattern, t->outAddr);
				e->nef.numWaitThreads--;
				e->waitingThreads.erase(e->waitingThreads.begin() + i);
				break;
			}
		}
	}

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
}

void __KernelSetEventFlagTimeout(EventFlag *e, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || eventFlagWaitTimer == 0)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// TODO: Test actual timing.
	if (micro <= 3)
		micro = 15;
	else if (micro <= 249)
		micro = 250;

	// This should call __KernelEventFlagTimeout() later, unless we cancel it.
	CoreTiming::ScheduleEvent(usToCycles(micro), eventFlagWaitTimer, __KernelGetCurThread());
}

int sceKernelWaitEventFlag(SceUID id, u32 bits, u32 wait, u32 outBitsPtr, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelWaitEventFlag(%i, %08x, %i, %08x, %08x)", id, bits, wait, outBitsPtr, timeoutPtr);

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		EventFlagTh th;
		if (!__KernelEventFlagMatches(&e->nef.currentPattern, bits, wait, outBitsPtr))
		{
			// No match - must wait.
			e->nef.numWaitThreads++;
			th.tid = __KernelGetCurThread();
			th.bits = bits;
			th.wait = wait;
			th.outAddr = outBitsPtr;
			e->waitingThreads.push_back(th);
			u32 timeout;
			if (Memory::IsValidAddress(timeoutPtr))
				timeout = Memory::Read_U32(timeoutPtr);

			__KernelSetEventFlagTimeout(e, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_EVENTFLAG, id, 0, timeoutPtr, false);
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
	DEBUG_LOG(HLE,"sceKernelWaitEventFlagCB(%i, %08x, %i, %08x, %08x)", id, bits, wait, outBitsPtr, timeoutPtr);

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		EventFlagTh th;
		if (!__KernelEventFlagMatches(&e->nef.currentPattern, bits, wait, outBitsPtr))
		{
			// No match - must wait.
			e->nef.numWaitThreads++;
			th.tid = __KernelGetCurThread();
			th.bits = bits;
			th.wait = wait;
			th.outAddr = outBitsPtr;
			e->waitingThreads.push_back(th);
			u32 timeout;
			if (Memory::IsValidAddress(timeoutPtr))
				timeout = Memory::Read_U32(timeoutPtr);

			__KernelSetEventFlagTimeout(e, timeoutPtr);
			__KernelWaitCurThread(WAITTYPE_EVENTFLAG, id, 0, timeoutPtr, true);
		}
		// TODO: Verify.
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
	DEBUG_LOG(HLE,"sceKernelPollEventFlag(%i, %08x, %i, %08x, %08x)", id, bits, wait, outBitsPtr, timeoutPtr);

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		if (!__KernelEventFlagMatches(&e->nef.currentPattern, bits, wait, outBitsPtr))
		{
			if (Memory::IsValidAddress(outBitsPtr))
				Memory::Write_U32(e->nef.currentPattern, outBitsPtr);
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
	DEBUG_LOG(HLE,"sceKernelReferEventFlagStatus(%i, %08x)", id, statusPtr);
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		Memory::WriteStruct(statusPtr, &e->nef);
		return 0;
	}
	else
	{
		return error;
	}
}

// never seen this one
u32 sceKernelCancelEventFlag()
{
	ERROR_LOG(HLE,"UNIMPL: sceKernelCancelEventFlag()");
	return 0;
}
