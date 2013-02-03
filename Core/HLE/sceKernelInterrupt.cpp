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

#include <string>
#include <list>
#include <map>

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelInterrupt.h"
#include "sceKernelMutex.h"

void __DisableInterrupts();
void __EnableInterrupts();
bool __InterruptsEnabled();

// InterruptsManager
//////////////////////////////////////////////////////////////////////////
// INTERRUPT MANAGEMENT
//////////////////////////////////////////////////////////////////////////
void sceKernelCpuSuspendIntr()
{
	//LOG(HLE,"sceKernelCpuSuspendIntr");	// very spammy
	int returnValue;
	if (__InterruptsEnabled())
	{
		returnValue = 1;
		__DisableInterrupts();
	}
	else
	{
		returnValue = 0;
	}
	RETURN(returnValue);
}

void sceKernelCpuResumeIntr(u32 enable)
{
	//LOG(HLE,"sceKernelCpuResumeIntr(%i)", enable);	// very spammy
	if (enable)
	{
		__EnableInterrupts();
		hleRunInterrupts();
	}
	else
	{
		__DisableInterrupts();
	}
}

void sceKernelIsCpuIntrEnable()
{
	u32 retVal = __InterruptsEnabled(); 
	DEBUG_LOG(HLE, "%i=sceKernelIsCpuIntrEnable()", retVal);
	RETURN(retVal);
}

void sceKernelIsCpuIntrSuspended()
{
	u32 retVal = !__InterruptsEnabled(); 
	DEBUG_LOG(HLE, "%i=sceKernelIsCpuIntrSuspended()", retVal);
	RETURN(retVal);
}

void sceKernelCpuResumeIntrWithSync(u32 enable)
{
	sceKernelCpuResumeIntr(enable);
}

class IntrHandler {
public:
	IntrHandler()
		: subIntrCreator(NULL)
	{
	}

	SubIntrHandler *add(int subIntrNum)
	{
		SubIntrHandler *handler;
		if (subIntrCreator != NULL)
			handler = subIntrCreator();
		else
			handler = new SubIntrHandler();

		subIntrHandlers[subIntrNum] = handler;
		return handler;
	}
	void remove(int subIntrNum)
	{
		if (has(subIntrNum))
		{
			delete subIntrHandlers[subIntrNum];
			subIntrHandlers.erase(subIntrNum);
		}
	}
	bool has(int subIntrNum) const
	{
		return subIntrHandlers.find(subIntrNum) != subIntrHandlers.end();
	}
	SubIntrHandler *get(int subIntrNum)
	{
		if (has(subIntrNum))
			return subIntrHandlers[subIntrNum];
		else
			return NULL;
	}
	void clear()
	{
		std::map<int, SubIntrHandler *>::iterator it, end;
		for (it = subIntrHandlers.begin(), end = subIntrHandlers.end(); it != end; ++it)
			delete it->second;
		subIntrHandlers.clear();
	}

	void queueUp(int subintr)
	{
		// Just call execute on all the subintr handlers for this interrupt.
		// They will get queued up.
		for (std::map<int, SubIntrHandler *>::iterator iter = subIntrHandlers.begin(); iter != subIntrHandlers.end(); ++iter)
		{
			if (subintr == -1 || iter->first == subintr)
				iter->second->queueUp();
		}
	}

	void queueUpWithArg(int subintr, int arg)
	{
		// Just call execute on all the subintr handlers for this interrupt.
		// They will get queued up.
		for (std::map<int, SubIntrHandler *>::iterator iter = subIntrHandlers.begin(); iter != subIntrHandlers.end(); ++iter)
		{
			if (subintr == -1 || iter->first == subintr)
				iter->second->queueUpWithArg(arg);
		}
	}

	void setCreator(SubIntrCreator creator)
	{
		subIntrCreator = creator;
	}

	void DoState(PointerWrap &p)
	{
		// We assume that the same creator has already been registered.
		bool hasCreator = subIntrCreator != NULL;
		p.Do(hasCreator);
		if (hasCreator != (subIntrCreator != NULL))
		{
			ERROR_LOG(HLE, "Savestate failure: incompatible sub interrupt handler.");
			return;
		}

		int n = (int) subIntrHandlers.size();
		p.Do(n);

		if (p.mode == p.MODE_READ)
		{
			clear();
			for (int i = 0; i < n; ++i)
			{
				int subIntrNum;
				p.Do(subIntrNum);
				SubIntrHandler *handler = add(subIntrNum);
				handler->DoState(p);
			}
		}
		else
		{
			std::map<int, SubIntrHandler *>::iterator it, end;
			for (it = subIntrHandlers.begin(), end = subIntrHandlers.end(); it != end; ++it)
			{
				p.Do(it->first);
				it->second->DoState(p);
			}
		}
	}

private:
	SubIntrCreator subIntrCreator;
	std::map<int, SubIntrHandler *> subIntrHandlers;
};

class InterruptState
{
public:
	void save();
	void restore();
	void clear();

	void DoState(PointerWrap &p)
	{
		p.Do(insideInterrupt);
		p.Do(savedCpu);
		p.DoMarker("InterruptState");
	}

	bool insideInterrupt;
	ThreadContext savedCpu;
//	Action afterInterruptAction;
//	Action afterHandlerAction;
};

// STATE

InterruptState intState;
IntrHandler intrHandlers[PSP_NUMBER_INTERRUPTS];
std::list<PendingInterrupt> pendingInterrupts;

// Yeah, this bit is a bit silly.
static int interruptsEnabled = 1;
static bool inInterrupt;


void __InterruptsInit()
{
	interruptsEnabled = 1;
	inInterrupt = false;
	intState.clear();
}

void __InterruptsDoState(PointerWrap &p)
{
	int numInterrupts = PSP_NUMBER_INTERRUPTS;
	p.Do(numInterrupts);
	if (numInterrupts != PSP_NUMBER_INTERRUPTS)
	{
		ERROR_LOG(HLE, "Savestate failure: wrong number of interrupts, can't load.");
		return;
	}

	intState.DoState(p);
	PendingInterrupt pi(0, 0);
	p.Do(pendingInterrupts, pi);
	p.Do(interruptsEnabled);
	p.Do(inInterrupt);
	p.DoMarker("sceKernelInterrupt");
}

void __InterruptsDoStateLate(PointerWrap &p)
{
	// We do these later to ensure the handlers have been registered.
	for (int i = 0; i < PSP_NUMBER_INTERRUPTS; ++i)
		intrHandlers[i].DoState(p);
	p.DoMarker("sceKernelInterrupt Late");
}

void __InterruptsShutdown()
{
	for (int i = 0; i < PSP_NUMBER_INTERRUPTS; ++i)
		intrHandlers[i].clear();
	pendingInterrupts.clear();
}

void __DisableInterrupts()
{
	interruptsEnabled = 0;
}

void __EnableInterrupts()
{
	interruptsEnabled = 1;
}

bool __InterruptsEnabled()
{
	return interruptsEnabled != 0;
}

bool __IsInInterrupt()
{
	return inInterrupt;
}

bool __CanExecuteInterrupt()
{
	return !inInterrupt;
}

void InterruptState::save()
{
	insideInterrupt = __IsInInterrupt();
	__KernelSaveContext(&savedCpu);
}

void InterruptState::restore()
{
	::inInterrupt = insideInterrupt;
	__KernelLoadContext(&savedCpu);
}

void InterruptState::clear()
{
	insideInterrupt = false;
}

// http://forums.ps2dev.org/viewtopic.php?t=5687

// http://www.google.se/url?sa=t&rct=j&q=&esrc=s&source=web&cd=7&ved=0CFYQFjAG&url=http%3A%2F%2Fdev.psnpt.com%2Fredmine%2Fprojects%2Fuofw%2Frepository%2Frevisions%2F65%2Fraw%2Ftrunk%2Finclude%2Finterruptman.h&ei=J4pCUKvyK4nl4QSu-YC4Cg&usg=AFQjCNFxJcgzQnv6dK7aiQlht_BM9grfQQ&sig2=GGk5QUEWI6qouYDoyE07YQ

void SubIntrHandler::queueUp()
{
	if (!enabled)
		return;

	pendingInterrupts.push_back(PendingInterrupt(intrNumber, number));
};

void SubIntrHandler::queueUpWithArg(int arg)
{
	if (!enabled)
		return;

	pendingInterrupts.push_back(PendingInterrupt(intrNumber, number, arg));
}

void SubIntrHandler::copyArgsToCPU(const PendingInterrupt &pend)
{
	DEBUG_LOG(CPU, "Entering interrupt handler %08x", handlerAddress);
	currentMIPS->pc = handlerAddress;
	currentMIPS->r[MIPS_REG_A0] = pend.hasArg ? pend.arg : number;
	currentMIPS->r[MIPS_REG_A1] = handlerArg;
	// RA is already taken care of
}


// Returns true if anything was executed.
bool __RunOnePendingInterrupt()
{
	if (inInterrupt || !interruptsEnabled)
	{
		// Already in an interrupt! We'll keep going when it's done.
		return false;
	}
	// Can easily prioritize between different kinds of interrupts if necessary.
retry:
	if (pendingInterrupts.size())
	{
		// If we came from CoreTiming::Advance(), we might've come from a waiting thread's callback.
		// To avoid "injecting" return values into our saved state, we context switch here.
		__KernelSwitchOffThread("interrupt");

		PendingInterrupt pend = pendingInterrupts.front();
		SubIntrHandler *handler = intrHandlers[pend.intr].get(pend.subintr);
		if (handler == NULL)
		{
			WARN_LOG(HLE, "Ignoring interrupt, already been released.");
			pendingInterrupts.pop_front();
			goto retry;
		}

		intState.save();
		handler->copyArgsToCPU(pend);

		currentMIPS->r[MIPS_REG_RA] = __KernelInterruptReturnAddress();
		inInterrupt = true;
		return true;
	}
	else
	{
		// DEBUG_LOG(HLE, "No more interrupts!");
		return false;
	}
}

void __TriggerRunInterrupts(int type)
{
	// If interrupts aren't enabled, we run them later.
	if (interruptsEnabled && !inInterrupt)
	{
		if ((type & PSP_INTR_HLE) != 0)
			hleRunInterrupts();
		else if ((type & PSP_INTR_ALWAYS_RESCHED) != 0)
		{
			if (!__RunOnePendingInterrupt())
				__KernelSwitchOffThread("interrupt");
		}
		else
			__RunOnePendingInterrupt();
	}
}

void __TriggerInterrupt(int type, PSPInterrupt intno, int subintr)
{
	if (interruptsEnabled || (type & PSP_INTR_ONLY_IF_ENABLED) == 0)
	{
		intrHandlers[intno].queueUp(subintr);
		DEBUG_LOG(HLE, "Triggering subinterrupts for interrupt %i sub %i (%i in queue)", intno, subintr, (u32)pendingInterrupts.size());
		__TriggerRunInterrupts(type);
	}
}

void __TriggerInterruptWithArg(int type, PSPInterrupt intno, int subintr, int arg)
{
	if (interruptsEnabled || (type & PSP_INTR_ONLY_IF_ENABLED) == 0)
	{
		intrHandlers[intno].queueUpWithArg(subintr, arg);
		DEBUG_LOG(HLE, "Triggering subinterrupts for interrupt %i sub %i with arg %i (%i in queue)", intno, subintr, arg,
                  (u32)pendingInterrupts.size());
		__TriggerRunInterrupts(type);
	}
}

void __KernelReturnFromInterrupt()
{
	DEBUG_LOG(CPU, "Left interrupt handler at %08x", currentMIPS->pc);
	inInterrupt = false;

	// This is what we just ran.
	PendingInterrupt pend = pendingInterrupts.front();
	pendingInterrupts.pop_front();

	SubIntrHandler *handler = intrHandlers[pend.intr].get(pend.subintr);
	if (handler != NULL)
		handler->handleResult(currentMIPS->r[MIPS_REG_V0]);
	else
		ERROR_LOG(HLE, "Interrupt released itself?  Should not happen.");

	// Restore context after running the interrupt.
	intState.restore();
	// All should now be back to normal, including PC.

	// Alright, let's see if there's any more interrupts queued...
	if (!__RunOnePendingInterrupt())
		__KernelReSchedule("return from interrupt");
}

void __RegisterSubIntrCreator(u32 intrNumber, SubIntrCreator creator)
{
	intrHandlers[intrNumber].setCreator(creator);
}

SubIntrHandler *__RegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 &error)
{
	SubIntrHandler *subIntrHandler = intrHandlers[intrNumber].add(subIntrNumber);
	subIntrHandler->number = subIntrNumber;
	subIntrHandler->intrNumber = intrNumber;
	error = 0;
	return subIntrHandler;
}

u32 __ReleaseSubIntrHandler(u32 intrNumber, u32 subIntrNumber)
{
	if (!intrHandlers[intrNumber].has(subIntrNumber))
		return -1;

	for (std::list<PendingInterrupt>::iterator it = pendingInterrupts.begin(); it != pendingInterrupts.end(); )
	{
		if (it->intr == intrNumber && it->subintr == subIntrNumber)
			pendingInterrupts.erase(it++);
		else
			++it;
	}

	intrHandlers[intrNumber].remove(subIntrNumber);
	return 0;
}

u32 sceKernelRegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 handler, u32 handlerArg)
{
	DEBUG_LOG(HLE,"sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x)", intrNumber, subIntrNumber, handler, handlerArg);

	if (intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	u32 error;
	SubIntrHandler *subIntrHandler = __RegisterSubIntrHandler(intrNumber, subIntrNumber, error);
	if (subIntrHandler)
	{
		subIntrHandler->enabled = false;
		subIntrHandler->handlerAddress = handler;
		subIntrHandler->handlerArg = handlerArg;
	}
	return error;
}

u32 sceKernelReleaseSubIntrHandler(u32 intrNumber, u32 subIntrNumber)
{
	DEBUG_LOG(HLE,"sceKernelReleaseSubIntrHandler(%i, %i)", PARAM(0), PARAM(1));

	if (intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	return __ReleaseSubIntrHandler(intrNumber, subIntrNumber);
}

u32 sceKernelEnableSubIntr(u32 intrNumber, u32 subIntrNumber)
{
	DEBUG_LOG(HLE,"sceKernelEnableSubIntr(%i, %i)", intrNumber, subIntrNumber);
	if (intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	if (!intrHandlers[intrNumber].has(subIntrNumber))
		return -1;

	intrHandlers[intrNumber].get(subIntrNumber)->enabled = true;
	return 0;
}

u32 sceKernelDisableSubIntr(u32 intrNumber, u32 subIntrNumber)
{
	DEBUG_LOG(HLE,"sceKernelDisableSubIntr(%i, %i)", intrNumber, subIntrNumber);
	if (intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	if (!intrHandlers[intrNumber].has(subIntrNumber))
		return -1;

	intrHandlers[intrNumber].get(subIntrNumber)->enabled = false;
	return 0;
}


struct PspIntrHandlerOptionParam {
	int size;															 //+00
	u32 entry;													//+04
	u32 common;												 //+08
	u32 gp;																		 //+0C
	u16 intr_code;											//+10
	u16 sub_count;											//+12
	u16 intr_level;										 //+14
	u16 enabled;												//+16
	u32 calls;													//+18
	u32 field_1C;											 //+1C
	u32 total_clock_lo;				 //+20
	u32 total_clock_hi;				 //+24
	u32 min_clock_lo;					 //+28
	u32 min_clock_hi;					 //+2C
	u32 max_clock_lo;					 //+30
	u32 max_clock_hi;					 //+34
} ;		//=38

void QueryIntrHandlerInfo()
{
	RETURN(0);
}

u32 sceKernelMemset(u32 addr, u32 fillc, u32 n)
{
	u8 c = fillc & 0xff;
	DEBUG_LOG(HLE, "sceKernelMemset(ptr = %08x, c = %02x, n = %08x)", addr, c, n);
	Memory::Memset(addr, c, n);
	return addr;
}

u32 sceKernelMemcpy(u32 dst, u32 src, u32 size)
{
	DEBUG_LOG(HLE, "sceKernelMemcpy(dest=%08x, src=%08x, size=%i)", dst, src, size);
	// Technically should crash if these are invalid and size > 0...
	if (Memory::IsValidAddress(dst) && Memory::IsValidAddress(src + size - 1))
	{
		u8 *dstp = Memory::GetPointer(dst);
		u8 *srcp = Memory::GetPointer(src);
		u32 size64 = size / 8;
		u32 size8 = size % 8;

		// Try to handle overlapped copies with similar properties to hardware, just in case.
		// Not that anyone ought to rely on it.
		while (size64-- > 0)
		{
			*(u64 *) dstp = *(u64 *) srcp;
			srcp += 8;
			dstp += 8;
		}
		while (size8-- > 0)
			*dstp++ = *srcp++;
	}
	return dst;
}

const HLEFunction Kernel_Library[] =
{
	{0x092968F4,sceKernelCpuSuspendIntr,"sceKernelCpuSuspendIntr"},
	{0x5F10D406,WrapV_U<sceKernelCpuResumeIntr>, "sceKernelCpuResumeIntr"}, //int oldstat
	{0x3b84732d,WrapV_U<sceKernelCpuResumeIntrWithSync>, "sceKernelCpuResumeIntrWithSync"},
	{0x47a0b729,sceKernelIsCpuIntrSuspended, "sceKernelIsCpuIntrSuspended"}, //flags
	{0xb55249d2,sceKernelIsCpuIntrEnable, "sceKernelIsCpuIntrEnable"},
	{0xa089eca4,WrapU_UUU<sceKernelMemset>, "sceKernelMemset"},
	{0xDC692EE3,WrapI_UI<sceKernelTryLockLwMutex>, "sceKernelTryLockLwMutex"},
	{0x37431849,WrapI_UI<sceKernelTryLockLwMutex_600>, "sceKernelTryLockLwMutex_600"},
	{0xbea46419,WrapI_UIU<sceKernelLockLwMutex>, "sceKernelLockLwMutex"},
	{0x1FC64E09,WrapI_UIU<sceKernelLockLwMutexCB>, "sceKernelLockLwMutexCB"},
	{0x15b6446b,WrapI_UI<sceKernelUnlockLwMutex>, "sceKernelUnlockLwMutex"},
	{0x293b45b8,sceKernelGetThreadId, "sceKernelGetThreadId"},
	{0x1839852A,WrapU_UUU<sceKernelMemcpy>,"sce_paf_private_memcpy"},
};

void Register_Kernel_Library()
{
	RegisterModule("Kernel_Library", ARRAY_SIZE(Kernel_Library), Kernel_Library);
}


const HLEFunction InterruptManager[] =
{
	{0xCA04A2B9, WrapU_UUUU<sceKernelRegisterSubIntrHandler>, "sceKernelRegisterSubIntrHandler"},
	{0xD61E6961, WrapU_UU<sceKernelReleaseSubIntrHandler>, "sceKernelReleaseSubIntrHandler"},
	{0xFB8E22EC, WrapU_UU<sceKernelEnableSubIntr>, "sceKernelEnableSubIntr"},
	{0x8A389411, WrapU_UU<sceKernelDisableSubIntr>, "sceKernelDisableSubIntr"},
	{0x5CB5A78B, 0, "sceKernelSuspendSubIntr"},
	{0x7860E0DC, 0, "sceKernelResumeSubIntr"},
	{0xFC4374B8, 0, "sceKernelIsSubInterruptOccurred"},
	{0xD2E8363F, QueryIntrHandlerInfo, "QueryIntrHandlerInfo"},	// No sce prefix for some reason
	{0xEEE43F47, 0, "sceKernelRegisterUserSpaceIntrStack"},
};


void Register_InterruptManager()
{
	RegisterModule("InterruptManager", ARRAY_SIZE(InterruptManager), InterruptManager);
}
