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

#include "Action.h"
#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelInterrupt.h"

struct Interrupt 
{
	PSPInterrupt intno;
};

// Yeah, this bit is a bit silly.
static int interruptsEnabled = 1;

static bool inInterrupt;

void __InterruptsInit()
{
	interruptsEnabled = 1;
}

void __InterruptsShutdown()
{
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



bool __IsInInterrupt() 
{
	return inInterrupt;
}

bool __CanExecuteInterrupt()
{
	return !inInterrupt;
}

class AllegrexInterruptHandler;

struct PendingInterrupt {
	AllegrexInterruptHandler *handler;
	int arg;
	bool hasArg;
};


class AllegrexInterruptHandler
{
public:
	virtual ~AllegrexInterruptHandler() {}
	virtual void copyArgsToCPU(const PendingInterrupt &pend) = 0;
	virtual void queueUp() = 0;
	virtual void queueUpWithArg(int arg) = 0;
};

std::list<PendingInterrupt> pendingInterrupts;

class SubIntrHandler : public AllegrexInterruptHandler
{
public:
	SubIntrHandler() {}
	virtual void queueUp()
	{
		PendingInterrupt pend;
		pend.handler = this;
		pend.hasArg = false;
		pendingInterrupts.push_back(pend);
	}
	virtual void queueUpWithArg(int arg)
	{
		PendingInterrupt pend;
		pend.handler = this;
		pend.arg = arg;
		pend.hasArg = true;
		pendingInterrupts.push_back(pend);
	}

	virtual void copyArgsToCPU(const PendingInterrupt &pend)
	{
		DEBUG_LOG(CPU, "Entering interrupt handler %08x", handlerAddress);
		currentMIPS->pc = handlerAddress;
		currentMIPS->r[MIPS_REG_A0] = pend.hasArg ? pend.arg : number;
		currentMIPS->r[MIPS_REG_A1] = handlerArg;
		// RA is already taken care of
	}

	bool enabled;
	int number;
	u32 handlerAddress;
	u32 handlerArg;
};

class IntrHandler {
public:
	void add(int subIntrNum, SubIntrHandler handler)
	{
		subIntrHandlers[subIntrNum] = handler;
	}
	void remove(int subIntrNum)
	{
		subIntrHandlers.erase(subIntrNum);
	}
	bool has(int subIntrNum) const
	{
		return subIntrHandlers.find(subIntrNum) != subIntrHandlers.end();
	}
	SubIntrHandler &get(int subIntrNum)
	{
		if (has(subIntrNum))
			return subIntrHandlers[subIntrNum];
		// what to do, what to do...
	}

	void queueUp(int subintr)
	{
		// Just call execute on all the subintr handlers for this interrupt.
		// They will get queued up.
		for (std::map<int, SubIntrHandler>::iterator iter = subIntrHandlers.begin(); iter != subIntrHandlers.end(); ++iter)
		{
			if (subintr == -1 || iter->first == subintr)
				iter->second.queueUp();
		}
	}

	void queueUpWithArg(int subintr, int arg)
	{
		// Just call execute on all the subintr handlers for this interrupt.
		// They will get queued up.
		for (std::map<int, SubIntrHandler>::iterator iter = subIntrHandlers.begin(); iter != subIntrHandlers.end(); ++iter)
		{
			if (subintr == -1 || iter->first == subintr)
				iter->second.queueUpWithArg(arg);
		}
	}

private:
	std::map<int, SubIntrHandler> subIntrHandlers;
};


class InterruptState
{
public:
	void save()
	{
		insideInterrupt = __IsInInterrupt();
		__KernelSaveContext(&savedCpu);
	}

	void restore()
	{
		::inInterrupt = insideInterrupt;
		__KernelLoadContext(&savedCpu);
	}

	bool insideInterrupt;
	ThreadContext savedCpu;
//	Action afterInterruptAction;
//	Action afterHandlerAction;
};

// STATE

InterruptState intState;
IntrHandler intrHandlers[PSP_NUMBER_INTERRUPTS];

// http://forums.ps2dev.org/viewtopic.php?t=5687

// http://www.google.se/url?sa=t&rct=j&q=&esrc=s&source=web&cd=7&ved=0CFYQFjAG&url=http%3A%2F%2Fdev.psnpt.com%2Fredmine%2Fprojects%2Fuofw%2Frepository%2Frevisions%2F65%2Fraw%2Ftrunk%2Finclude%2Finterruptman.h&ei=J4pCUKvyK4nl4QSu-YC4Cg&usg=AFQjCNFxJcgzQnv6dK7aiQlht_BM9grfQQ&sig2=GGk5QUEWI6qouYDoyE07YQ


// Returns true if anything was executed.
bool __RunOnePendingInterrupt()
{
	if (inInterrupt)
	{
		// Already in an interrupt! We'll keep going when it's done.
		return false;
	}
	// Can easily prioritize between different kinds of interrupts if necessary.
	if (pendingInterrupts.size())
	{
		PendingInterrupt pend = pendingInterrupts.front();
		pendingInterrupts.pop_front();
		intState.save();
		pend.handler->copyArgsToCPU(pend);

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

void __TriggerInterrupt(PSPInterrupt intno, int subintr)
{
	intrHandlers[intno].queueUp(subintr);
	DEBUG_LOG(HLE, "Triggering subinterrupts for interrupt %i sub %i (%i in queue)", intno, subintr, pendingInterrupts.size());
	if (!inInterrupt)
		__RunOnePendingInterrupt();
}

void __TriggerInterruptWithArg(PSPInterrupt intno, int subintr, int arg)
{
	intrHandlers[intno].queueUpWithArg(subintr, arg);
	DEBUG_LOG(HLE, "Triggering subinterrupts for interrupt %i sub %i with arg %i (%i in queue)", intno, subintr, arg, pendingInterrupts.size());
	if (!inInterrupt)
		__RunOnePendingInterrupt();
}

void _sceKernelReturnFromInterrupt()
{
	DEBUG_LOG(CPU, "Left interrupt handler at %08x", currentMIPS->pc);
	inInterrupt = false;
	// Restore context after running the interrupt.
	intState.restore();
	// All should now be back to normal, including PC.

	// Alright, let's see if there's any more interrupts queued...

	if (!__RunOnePendingInterrupt())
	{
		// Hmmm...
		//__KernelReSchedule("return from interrupt");
	}
}

u32 sceKernelRegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 handler, u32 handlerArg)
{
	DEBUG_LOG(HLE,"sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x)", intrNumber, subIntrNumber, handler, handlerArg);

	if (intrNumber < 0 || intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	SubIntrHandler subIntrHandler;
	subIntrHandler.number = subIntrNumber;
	subIntrHandler.enabled = false;
	subIntrHandler.handlerAddress = handler;
	subIntrHandler.handlerArg = handlerArg;
	intrHandlers[intrNumber].add(subIntrNumber, subIntrHandler);
	return 0;
}

u32 sceKernelReleaseSubIntrHandler(u32 intrNumber, u32 subIntrNumber)
{
	DEBUG_LOG(HLE,"sceKernelReleaseSubIntrHandler(%i, %i)", PARAM(0), PARAM(1));

	// TODO: should check if it's pending and remove it from pending list! (although that's probably unlikely)

	if (intrNumber < 0 || intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	if (!intrHandlers[intrNumber].has(subIntrNumber))
		return -1; 
	
	intrHandlers[intrNumber].remove(subIntrNumber);
	return 0;
}

u32 sceKernelEnableSubIntr(u32 intrNumber, u32 subIntrNumber)
{
	DEBUG_LOG(HLE,"sceKernelEnableSubIntr(%i, %i)", intrNumber, subIntrNumber);
	if (intrNumber < 0 || intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	if (!intrHandlers[intrNumber].has(subIntrNumber))
		return -1;
	
	intrHandlers[intrNumber].get(subIntrNumber).enabled = true;
	return 0;
}

u32 sceKernelDisableSubIntr(u32 intrNumber, u32 subIntrNumber)
{
	DEBUG_LOG(HLE,"sceKernelDisableSubIntr(%i, %i)", intrNumber, subIntrNumber);
	if (intrNumber < 0 || intrNumber >= PSP_NUMBER_INTERRUPTS)
		return -1;

	if (!intrHandlers[intrNumber].has(subIntrNumber))
		return -1;

	intrHandlers[intrNumber].get(subIntrNumber).enabled = false;
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

void sceKernelMemset()
{
	u32 addr = PARAM(0);
	u8 c = PARAM(1) & 0xff;
	u32 n = PARAM(2);
	DEBUG_LOG(HLE, "sceKernelMemset(ptr = %08x, c = %02x, n = %08x)", addr, c, n);
	for (size_t i = 0; i < n; i++)
		Memory::Write_U8((u8)c, addr + i);
	RETURN(addr); /* TODO: verify it should return this */
}

const HLEFunction Kernel_Library[] = 
{
	{0x092968F4,sceKernelCpuSuspendIntr,"sceKernelCpuSuspendIntr"},
	{0x5F10D406,WrapV_U<sceKernelCpuResumeIntr>, "sceKernelCpuResumeIntr"}, //int oldstat
	{0x3b84732d,WrapV_U<sceKernelCpuResumeIntrWithSync>, "sceKernelCpuResumeIntrWithSync"},
	{0x47a0b729,sceKernelIsCpuIntrSuspended, "sceKernelIsCpuIntrSuspended"}, //flags
	{0xb55249d2,sceKernelIsCpuIntrEnable, "sceKernelIsCpuIntrEnable"}, 
	{0xa089eca4,sceKernelMemset, "sceKernelMemset"}, 
	{0xDC692EE3,0, "sceKernelTryLockLwMutex"},
	{0xbea46419,0, "sceKernelLockLwMutex"}, 
	{0x15b6446b,0, "sceKernelUnlockLwMutex"}, 
	{0x293b45b8,sceKernelGetThreadId, "sceKernelGetThreadId"}, 
	{0x1839852A,0,"sce_paf_private_memcpy"},
	{0xA089ECA4,0,"sce_paf_private_memset"},
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
	{0xD2E8363F, 0, "QueryIntrHandlerInfo"},	// No sce prefix for some reason
	{0xEEE43F47, 0, "sceKernelRegisterUserSpaceIntrStack"},
};


void Register_InterruptManager()
{
	RegisterModule("InterruptManager", ARRAY_SIZE(InterruptManager), InterruptManager);
}
