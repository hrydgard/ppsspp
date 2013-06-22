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
#include "ChunkFile.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelInterrupt.h"
#include "sceKernelMemory.h"
#include "sceKernelMutex.h"

void __DisableInterrupts();
void __EnableInterrupts();
bool __InterruptsEnabled();

// InterruptsManager
//////////////////////////////////////////////////////////////////////////
// INTERRUPT MANAGEMENT
//////////////////////////////////////////////////////////////////////////

class InterruptState
{
public:
	void save();
	void restore();
	void clear();

	void DoState(PointerWrap &p)
	{
		p.Do(savedCpu);
		p.DoMarker("InterruptState");
	}

	ThreadContext savedCpu;
};

// STATE

InterruptState intState;
IntrHandler* intrHandlers[PSP_NUMBER_INTERRUPTS];
std::list<PendingInterrupt> pendingInterrupts;

// Yeah, this bit is a bit silly.
static int interruptsEnabled = 1;
static bool inInterrupt;
static SceUID threadBeforeInterrupt;


void sceKernelCpuSuspendIntr()
{
	VERBOSE_LOG(HLE, "sceKernelCpuSuspendIntr");
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
	VERBOSE_LOG(HLE, "sceKernelCpuResumeIntr(%i)", enable);
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

bool IntrHandler::run(PendingInterrupt& pend)
{
	SubIntrHandler *handler = get(pend.subintr);
	if (handler == NULL)
	{
		WARN_LOG(HLE, "Ignoring interrupt, already been released.");
		return false;
	}

	copyArgsToCPU(pend);

	return true;
}

void IntrHandler::copyArgsToCPU(PendingInterrupt& pend)
{
	SubIntrHandler* handler = get(pend.subintr);
	DEBUG_LOG(CPU, "Entering interrupt handler %08x", handler->handlerAddress);
	currentMIPS->pc = handler->handlerAddress;
	currentMIPS->r[MIPS_REG_A0] = handler->subIntrNumber;
	currentMIPS->r[MIPS_REG_A1] = handler->handlerArg;
	// RA is already taken care of
}

void IntrHandler::handleResult(PendingInterrupt& pend)
{
	//u32 result = currentMIPS->r[MIPS_REG_V0];
}

SubIntrHandler* IntrHandler::add(int subIntrNum)
{
	return &subIntrHandlers[subIntrNum];
}
void IntrHandler::remove(int subIntrNum)
{
	if (has(subIntrNum))
	{
		subIntrHandlers.erase(subIntrNum);
	}
}
bool IntrHandler::has(int subIntrNum) const
{
	return subIntrHandlers.find(subIntrNum) != subIntrHandlers.end();
}
void IntrHandler::enable(int subIntrNum)
{
	subIntrHandlers[subIntrNum].enabled = true;
}
void IntrHandler::disable(int subIntrNum)
{
	subIntrHandlers[subIntrNum].enabled = true;
}
SubIntrHandler* IntrHandler::get(int subIntrNum)
{
	if (has(subIntrNum))
		return &subIntrHandlers[subIntrNum];
	else
		return NULL;
}
void IntrHandler::clear()
{
	subIntrHandlers.clear();
}

void IntrHandler::queueUp(int subintr)
{
	if(subintr == PSP_INTR_SUB_NONE)
	{
		pendingInterrupts.push_back(PendingInterrupt(intrNumber, subintr));
	}
	else
	{
		// Just call execute on all the subintr handlers for this interrupt.
		// They will get queued up.
		for (std::map<int, SubIntrHandler>::iterator iter = subIntrHandlers.begin(); iter != subIntrHandlers.end(); ++iter)
		{
			if ((subintr == PSP_INTR_SUB_ALL || iter->first == subintr) && iter->second.enabled)
				pendingInterrupts.push_back(PendingInterrupt(intrNumber, iter->first));
		}
	}
}

void IntrHandler::DoState(PointerWrap &p)
{
	p.Do(intrNumber);
	p.Do<int, SubIntrHandler>(subIntrHandlers);
	p.DoMarker("IntrHandler");
}

void PendingInterrupt::DoState(PointerWrap &p)
{
	p.Do(intr);
	p.Do(subintr);
	p.DoMarker("PendingInterrupt");
}

void __InterruptsInit()
{
	interruptsEnabled = 1;
	inInterrupt = false;
	for (int i = 0; i < (int)ARRAY_SIZE(intrHandlers); ++i)
		intrHandlers[i] = new IntrHandler(i);
	intState.clear();
	threadBeforeInterrupt = 0;
}

void __InterruptsDoState(PointerWrap &p)
{
	int numInterrupts = PSP_NUMBER_INTERRUPTS;
	p.Do(numInterrupts);
	if (numInterrupts != PSP_NUMBER_INTERRUPTS)
	{
		p.SetError(p.ERROR_FAILURE);
		ERROR_LOG(HLE, "Savestate failure: wrong number of interrupts, can't load.");
		return;
	}

	intState.DoState(p);
	PendingInterrupt pi(0, 0);
	p.Do(pendingInterrupts, pi);
	p.Do(interruptsEnabled);
	p.Do(inInterrupt);
	p.Do(threadBeforeInterrupt);
	p.DoMarker("sceKernelInterrupt");
}

void __InterruptsDoStateLate(PointerWrap &p)
{
	// We do these later to ensure the handlers have been registered.
	for (int i = 0; i < PSP_NUMBER_INTERRUPTS; ++i)
		intrHandlers[i]->DoState(p);
	p.DoMarker("sceKernelInterrupt Late");
}

void __InterruptsShutdown()
{
	for (size_t i = 0; i < ARRAY_SIZE(intrHandlers); ++i)
		intrHandlers[i]->clear();
	for (size_t i = 0; i < ARRAY_SIZE(intrHandlers); ++i)
	{
		if (intrHandlers[i])
		{
			delete intrHandlers[i];
			intrHandlers[i] = 0;
		}
	}
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
	__KernelSaveContext(&savedCpu, true);
}

void InterruptState::restore()
{
	__KernelLoadContext(&savedCpu, true);
}

void InterruptState::clear()
{
}

// http://forums.ps2dev.org/viewtopic.php?t=5687

// http://www.google.se/url?sa=t&rct=j&q=&esrc=s&source=web&cd=7&ved=0CFYQFjAG&url=http%3A%2F%2Fdev.psnpt.com%2Fredmine%2Fprojects%2Fuofw%2Frepository%2Frevisions%2F65%2Fraw%2Ftrunk%2Finclude%2Finterruptman.h&ei=J4pCUKvyK4nl4QSu-YC4Cg&usg=AFQjCNFxJcgzQnv6dK7aiQlht_BM9grfQQ&sig2=GGk5QUEWI6qouYDoyE07YQ


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
	if (!pendingInterrupts.empty())
	{
		// If we came from CoreTiming::Advance(), we might've come from a waiting thread's callback.
		// To avoid "injecting" return values into our saved state, we context switch here.
		SceUID savedThread = __KernelGetCurThread();
		if (__KernelSwitchOffThread("interrupt"))
			threadBeforeInterrupt = savedThread;

		PendingInterrupt pend = pendingInterrupts.front();

		IntrHandler* handler = intrHandlers[pend.intr];
		if(handler == NULL)
		{
			WARN_LOG(HLE, "Ignoring interrupt");
			pendingInterrupts.pop_front();
			goto retry;
		}

		intState.save();
		inInterrupt = true;

		if(!handler->run(pend)) {
			pendingInterrupts.pop_front();
			inInterrupt = false;
			goto retry;
		}

		currentMIPS->r[MIPS_REG_RA] = __KernelInterruptReturnAddress();
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
			// "Always" only means if dispatch is enabled.
			if (!__RunOnePendingInterrupt() && __KernelIsDispatchEnabled())
			{
				SceUID savedThread = __KernelGetCurThread();
				if (__KernelSwitchOffThread("interrupt"))
					threadBeforeInterrupt = savedThread;
			}
		}
		else
			__RunOnePendingInterrupt();
	}
}

void __TriggerInterrupt(int type, PSPInterrupt intno, int subintr)
{
	if (interruptsEnabled || (type & PSP_INTR_ONLY_IF_ENABLED) == 0)
	{
		intrHandlers[intno]->queueUp(subintr);
		VERBOSE_LOG(HLE, "Triggering subinterrupts for interrupt %i sub %i (%i in queue)", intno, subintr, (u32)pendingInterrupts.size());
		__TriggerRunInterrupts(type);
	}
}

void __KernelReturnFromInterrupt()
{
	VERBOSE_LOG(CPU, "Left interrupt handler at %08x", currentMIPS->pc);

	// This is what we just ran.
	PendingInterrupt pend = pendingInterrupts.front();
	pendingInterrupts.pop_front();

	intrHandlers[pend.intr]->handleResult(pend);
	inInterrupt = false;

	// Restore context after running the interrupt.
	intState.restore();
	// All should now be back to normal, including PC.

	// Alright, let's see if there's any more interrupts queued...
	if (!__RunOnePendingInterrupt())
	{
		// Otherwise, we reschedule when dispatch was enabled, or switch back otherwise.
		if (__KernelIsDispatchEnabled())
			__KernelReSchedule("return from interrupt");
		else
			__KernelSwitchToThread(threadBeforeInterrupt, "return from interrupt");
	}
}

void __RegisterIntrHandler(u32 intrNumber, IntrHandler* handler)
{
	if(intrHandlers[intrNumber])
		delete intrHandlers[intrNumber];
	intrHandlers[intrNumber] = handler;
}

SubIntrHandler *__RegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 &error)
{
	SubIntrHandler *subIntrHandler = intrHandlers[intrNumber]->add(subIntrNumber);
	subIntrHandler->subIntrNumber = subIntrNumber;
	subIntrHandler->intrNumber = intrNumber;
	error = 0;
	return subIntrHandler;
}

int __ReleaseSubIntrHandler(int intrNumber, int subIntrNumber)
{
	if (intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;
	if (!intrHandlers[intrNumber]->has(subIntrNumber))
		return -1;

	for (std::list<PendingInterrupt>::iterator it = pendingInterrupts.begin(); it != pendingInterrupts.end(); )
	{
		if (it->intr == intrNumber && it->subintr == subIntrNumber)
			pendingInterrupts.erase(it++);
		else
			++it;
	}

	intrHandlers[intrNumber]->remove(subIntrNumber);
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

int sceKernelReleaseSubIntrHandler(int intrNumber, int subIntrNumber)
{
	DEBUG_LOG(HLE, "sceKernelReleaseSubIntrHandler(%i, %i)", intrNumber, subIntrNumber);

	if (intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	return __ReleaseSubIntrHandler(intrNumber, subIntrNumber);
}

u32 sceKernelEnableSubIntr(u32 intrNumber, u32 subIntrNumber)
{
	DEBUG_LOG(HLE,"sceKernelEnableSubIntr(%i, %i)", intrNumber, subIntrNumber);
	if (intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	if (!intrHandlers[intrNumber]->has(subIntrNumber))
		return -1;

	intrHandlers[intrNumber]->enable(subIntrNumber);
	return 0;
}

u32 sceKernelDisableSubIntr(u32 intrNumber, u32 subIntrNumber)
{
	DEBUG_LOG(HLE,"sceKernelDisableSubIntr(%i, %i)", intrNumber, subIntrNumber);
	if (intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	if (!intrHandlers[intrNumber]->has(subIntrNumber))
		return -1;

	intrHandlers[intrNumber]->disable(subIntrNumber);
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
	if (Memory::IsValidAddress(dst) && Memory::IsValidAddress(src) && Memory::IsValidAddress(dst + size - 1) && Memory::IsValidAddress(src + size - 1))
	{
		u8 *dstp = Memory::GetPointer(dst);
		u8 *srcp = Memory::GetPointer(src);

		// If it's non-overlapping, just do it in one go.
		if (dst + size < src || src + size < dst)
			memcpy(dstp, srcp, size);
		else
		{
			// Try to handle overlapped copies with similar properties to hardware, just in case.
			// Not that anyone ought to rely on it.
			for (u32 size64 = size / 8; size64 > 0; --size64)
			{
				memmove(dstp, srcp, 8);
				dstp += 8;
				srcp += 8;
			}
			for (u32 size8 = size % 8; size8 > 0; --size8)
				*dstp++ = *srcp++;
		}
	}
	return dst;
}

const HLEFunction Kernel_Library[] =
{
	{0x092968F4,sceKernelCpuSuspendIntr, "sceKernelCpuSuspendIntr"},
	{0x5F10D406,WrapV_U<sceKernelCpuResumeIntr>, "sceKernelCpuResumeIntr"}, //int oldstat
	{0x3b84732d,WrapV_U<sceKernelCpuResumeIntrWithSync>, "sceKernelCpuResumeIntrWithSync"},
	{0x47a0b729,sceKernelIsCpuIntrSuspended, "sceKernelIsCpuIntrSuspended"}, //flags
	{0xb55249d2,sceKernelIsCpuIntrEnable, "sceKernelIsCpuIntrEnable"},
	{0xa089eca4,WrapU_UUU<sceKernelMemset>, "sceKernelMemset"},
	{0xDC692EE3,WrapI_UI<sceKernelTryLockLwMutex>, "sceKernelTryLockLwMutex"},
	{0x37431849,WrapI_UI<sceKernelTryLockLwMutex_600>, "sceKernelTryLockLwMutex_600"},
	{0xbea46419,WrapI_UIU<sceKernelLockLwMutex>, "sceKernelLockLwMutex", HLE_NOT_DISPATCH_SUSPENDED},
	{0x1FC64E09,WrapI_UIU<sceKernelLockLwMutexCB>, "sceKernelLockLwMutexCB", HLE_NOT_DISPATCH_SUSPENDED},
	{0x15b6446b,WrapI_UI<sceKernelUnlockLwMutex>, "sceKernelUnlockLwMutex"},
	{0xc1734599,WrapI_UU<sceKernelReferLwMutexStatus>, "sceKernelReferLwMutexStatus"},
	{0x293b45b8,WrapI_V<sceKernelGetThreadId>, "sceKernelGetThreadId"},
	{0xD13BDE95,WrapI_V<sceKernelCheckThreadStack>, "sceKernelCheckThreadStack"},
	{0x1839852A,WrapU_UUU<sceKernelMemcpy>, "sceKernelMemcpy"},
	// Name is only a guess.
	{0xfa835cde,WrapI_I<sceKernelAllocateTls>, "sceKernelAllocateTls"},
};

void Register_Kernel_Library()
{
	RegisterModule("Kernel_Library", ARRAY_SIZE(Kernel_Library), Kernel_Library);
}


const HLEFunction InterruptManager[] =
{
	{0xCA04A2B9, WrapU_UUUU<sceKernelRegisterSubIntrHandler>, "sceKernelRegisterSubIntrHandler"},
	{0xD61E6961, WrapI_II<sceKernelReleaseSubIntrHandler>, "sceKernelReleaseSubIntrHandler"},
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
