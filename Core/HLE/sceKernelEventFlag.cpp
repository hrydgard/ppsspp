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

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelEventFlag.h"

#include <queue>


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

//SceUID sceKernelCreateEventFlag(const char *name, int attr, int bits, SceKernelEventFlagOptParam *opt);
void sceKernelCreateEventFlag()
{
	const char *name = Memory::GetCharPointer(PARAM(0));

	EventFlag *e = new EventFlag();
	SceUID id = kernelObjects.Create(e);

	e->nef.size = sizeof(NativeEventFlag);
	strncpy(e->nef.name, name, 32);
	e->nef.attr = PARAM(1);
	e->nef.initPattern = PARAM(2);
	e->nef.currentPattern = e->nef.initPattern;
	e->nef.numWaitThreads = 0;

	DEBUG_LOG(HLE,"%i=sceKernelCreateEventFlag(\"%s\", %08x, %08x, %08x)", id, e->nef.name, e->nef.attr, e->nef.currentPattern, PARAM(3));
	RETURN(id);
}

//int sceKernelClearEventFlag(SceUID evid, u32 bits);
void sceKernelClearEventFlag()
{
	SceUID id = PARAM(0);
	u32 bits = PARAM(1);

	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		DEBUG_LOG(HLE,"sceKernelClearEventFlag(%i, %08x)", id, bits);
		e->nef.currentPattern &= bits;
		// Note that it's not possible for threads to get woken up by this action.
		RETURN(0);
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelClearEventFlag(%i, %08x) - error", id, bits);
		RETURN(error);
	}
}

//int sceKernelDeleteEventFlag(int evid);
void sceKernelDeleteEventFlag()
{
	SceUID uid = PARAM(0);
	DEBUG_LOG(HLE,"sceKernelDeleteEventFlag(%i)", uid);
	RETURN(kernelObjects.Destroy<EventFlag>(uid));
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

//int sceKernelSetEventFlag(SceUID evid, u32 bits);
void sceKernelSetEventFlag()
{
	SceUID id = PARAM(0);
	u32 bitsToSet = PARAM(1);
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
		RETURN(0);
	}
	else
	{
		RETURN(error);
	}
}

//int sceKernelWaitEventFlag(SceUID evid, u32 bits, u32 wait, u32 *outBits, SceUInt *timeout);
void sceKernelWaitEventFlag()
{
	SceUID id = PARAM(0);
	u32 bits = PARAM(1);
	u32 wait = PARAM(2);
	u32 outBitsPtr = PARAM(3);
	u32 timeoutPtr = PARAM(4);

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

			__KernelWaitCurThread(WAITTYPE_EVENTFLAG, id, 0, 0, false);
			// MUST NOT return a value after __KernelWaitCurThread as we may have been rescheduled!
		}
	}
	else
	{
		RETURN(error);
	}
}

//int sceKernelWaitEventFlagCB(SceUID evid, u32 bits, u32 wait, u32 *outBits, SceUInt *timeout);
void sceKernelWaitEventFlagCB()
{
	SceUID id = PARAM(0);
	u32 bits = PARAM(1);
	u32 wait = PARAM(2);
	u32 outBitsPtr = PARAM(3);
	u32 timeoutPtr = PARAM(4);

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

			__KernelWaitCurThread(WAITTYPE_EVENTFLAG, id, 0, 0, true);
			__KernelCheckCallbacks();
		}
	}
	else
	{
		RETURN(error);
	}
}

//int sceKernelPollEventFlag(int evid, u32 bits, u32 wait, u32 *outBits);
void sceKernelPollEventFlag()
{
	SceUID id = PARAM(0);
	u32 bits = PARAM(1);
	u32 wait = PARAM(2);
	u32 outBitsPtr = PARAM(3);
	u32 timeoutPtr = PARAM(4);

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
			RETURN(SCE_KERNEL_ERROR_EVF_COND);
		}
		else
		{
			RETURN(0);
		}
	}
	else
	{
		RETURN(error);
	}
}

//int sceKernelReferEventFlagStatus(SceUID event, SceKernelEventFlagInfo *status);
void sceKernelReferEventFlagStatus()
{
	SceUID id = PARAM(0);
	u32 statusAddr = PARAM(1);

	DEBUG_LOG(HLE,"sceKernelReferEventFlagStatus(%i, %08x)", id, statusAddr);
	u32 error;
	EventFlag *e = kernelObjects.Get<EventFlag>(id, error);
	if (e)
	{
		Memory::WriteStruct(statusAddr, &e->nef);
		RETURN(0);
	}
	else
	{
		RETURN(error);
	}
}

// never seen this one
void sceKernelCancelEventFlag()
{
	ERROR_LOG(HLE,"UNIMPL: sceKernelCancelEventFlag()");
	RETURN(0);
}
