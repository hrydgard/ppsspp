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

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelSemaphore.h"

/** Current state of a semaphore.
* @see sceKernelReferSemaStatus.
*/

struct NativeSemaphore
{
	/** Size of the ::SceKernelSemaInfo structure. */
	SceSize 	size;
	/** NUL-terminated name of the semaphore. */
	char 		name[32];
	/** Attributes. */
	SceUInt 	attr;
	/** The initial count the semaphore was created with. */
	int 		initCount;
	/** The current count. */
	int 		currentCount;
	/** The maximum count. */
	int 		maxCount;
	/** The number of threads waiting on the semaphore. */
	int 		numWaitThreads;
};


struct Semaphore : public KernelObject 
{
	const char *GetName() {return ns.name;}
	const char *GetTypeName() {return "Semaphore";}

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_SEMID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Semaphore; }

	NativeSemaphore ns;
	std::vector<SceUID> waitingThreads;
};

void sceKernelCancelSema()
{
	SceUID id = PARAM(0);
	ERROR_LOG(HLE,"UNIMPL sceKernelCancelSema(%i)", id);
	RETURN(0);
}

//SceUID sceKernelCreateSema(const char *name, SceUInt attr, int initVal, int maxVal, SceKernelSemaOptParam *option);
void sceKernelCreateSema()
{
	const char *name = Memory::GetCharPointer(PARAM(0));

	Semaphore *s = new Semaphore;
	SceUID id = kernelObjects.Create(s);

	strncpy(s->ns.name, name, 32);
	s->ns.attr = PARAM(1);
	s->ns.initCount = PARAM(2);
	s->ns.currentCount = s->ns.initCount;
	s->ns.maxCount = PARAM(3);
	s->ns.numWaitThreads = 0;

	DEBUG_LOG(HLE,"%i=sceKernelCreateSema(%s, %08x, %i, %i, %08x)", id, s->ns.name, s->ns.attr, s->ns.initCount, s->ns.maxCount, PARAM(4));

	RETURN(id);
}

//int sceKernelDeleteSema(SceUID semaid);
void sceKernelDeleteSema()
{
	SceUID id = PARAM(0);
	DEBUG_LOG(HLE,"sceKernelDeleteSema(%i)", id);
	RETURN(kernelObjects.Destroy<Semaphore>(id));
}

void sceKernelReferSemaStatus()
{
	SceUID id = PARAM(0);
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		DEBUG_LOG(HLE,"sceKernelReferSemaStatus(%i, %08x)", id, PARAM(1));
		NativeSemaphore *outptr = (NativeSemaphore*)Memory::GetPointer(PARAM(1));
		int szToCopy = outptr->size - 4;
		memcpy((char*)outptr + 4, (char*)s + 4, szToCopy);
		RETURN(0);
	}
	else
	{
		ERROR_LOG(HLE,"Error %08x", error);
		RETURN(error);
	}
}
	
//int sceKernelSignalSema(SceUID semaid, int signal);
void sceKernelSignalSema()
{
	//TODO: check that this thing really works :)
	SceUID id = PARAM(0);
	u32 signal = PARAM(1);
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		int oldval = s->ns.currentCount;

		s->ns.currentCount += signal;
		DEBUG_LOG(HLE,"sceKernelSignalSema(%i, %i) (old: %i, new: %i)", id, signal, oldval, s->ns.currentCount);
			
		bool wokeThreads = false;
retry:
		//TODO: check for threads to wake up - wake them
		std::vector<SceUID>::iterator iter;
		for (iter = s->waitingThreads.begin(); iter!=s->waitingThreads.end(); s++)
		{
			SceUID id = *iter;
			int wVal = (int)__KernelGetWaitValue(id, error);
			if (wVal <= s->ns.currentCount)
			{
				__KernelResumeThreadFromWait(id);
				s->ns.currentCount -= wVal;
				wokeThreads = true;
				s->waitingThreads.erase(iter);
				goto retry;
			}
			else
			{
				break;
			}
		}
		//pop the thread that were released from waiting

		// I don't think we should reschedule here
		//if (wokeThreads)
		//	__KernelReSchedule("semaphore signalled");

		RETURN(0);
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelSignalSema : Trying to signal invalid semaphore %i", id);
		RETURN(error);
	}
}

//int sceKernelWaitSema(SceUID semaid, int signal, SceUInt *timeout);
void sceKernelWaitSema()
{
	SceUID id = PARAM(0);
	int wantedCount = PARAM(1);
	u32 timeoutPtr = PARAM(2);
		
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		DEBUG_LOG(HLE,"sceKernelWaitSema(%i, %i, %i)", id, wantedCount, timeoutPtr);
		if (s->ns.currentCount >= wantedCount) //TODO fix
		{
			s->ns.currentCount -= wantedCount;
			RETURN(0);
		}
		else
		{
			s->waitingThreads.push_back(__KernelGetCurThread());
			__KernelWaitCurThread(WAITTYPE_SEMA, id, wantedCount, 0, false);
			return;
		}
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelWaitSema : Trying to wait for invalid semaphore %i", id);
		RETURN(error);
	}
} 

//int sceKernelWaitSemaCB(SceUID semaid, int signal, SceUInt *timeout);
void sceKernelWaitSemaCB()
{
	SceUID id = PARAM(0);
	int wantedCount = PARAM(1);
	int timeout = PARAM(2);
	DEBUG_LOG(HLE,"sceKernelWaitSemaCB(%i, %i, %i)", id, wantedCount, timeout);
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		DEBUG_LOG(HLE,"CurrentCount: %i, Signal: %i", s->ns.currentCount, wantedCount);
		if (s->ns.currentCount >= wantedCount) //TODO fix
		{
			DEBUG_LOG(HLE,"Subtracting");
			s->ns.currentCount -= wantedCount;
		}
		else
		{
			s->waitingThreads.push_back(__KernelGetCurThread());
			__KernelWaitCurThread(WAITTYPE_SEMA, id, wantedCount, 0, true);
			__KernelCheckCallbacks();
			return;
		}
		DEBUG_LOG(HLE,"After: CurrentCount: %i, Signal: %i", s->ns.currentCount, wantedCount);
		RETURN(0);
	}
	else
	{
		ERROR_LOG(HLE,"sceKernelWaitSemaCB - Bad semaphore");
		RETURN(error);
	}
}

// Should be same as WaitSema but without the wait, instead returning SCE_KERNEL_ERROR_SEMA_ZERO
void sceKernelPollSema()
{
	SceUID id = PARAM(0);
	int wantedCount = PARAM(1);

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		DEBUG_LOG(HLE,"sceKernelPollSema(%i, %i)", id, wantedCount);
		if (s->ns.currentCount >= wantedCount) //TODO fix
		{
			s->ns.currentCount -= wantedCount;
			RETURN(0);
		}
		else
		{
			RETURN(SCE_KERNEL_ERROR_SEMA_ZERO);
		}
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelPollSema : Trying to poll invalid semaphore %i", id);
		RETURN(error);
	}
}

