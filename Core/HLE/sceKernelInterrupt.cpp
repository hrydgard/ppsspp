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

#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Common/ChunkFile.h"

#include "Core/Debugger/Breakpoints.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelMutex.h"
#include "GPU/GPUCommon.h"

void __DisableInterrupts();
void __EnableInterrupts();
bool __InterruptsEnabled();

// Seems like some > 16 are taken but not available.  Probably kernel only?
static const u32 PSP_NUMBER_SUBINTERRUPTS = 32;

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
		auto s = p.Section("InterruptState", 1);
		if (!s)
			return;

		p.Do(savedCpu);
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
	VERBOSE_LOG(SCEINTC, "sceKernelCpuSuspendIntr");
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
	hleEatCycles(15);
	RETURN(returnValue);
}

void sceKernelCpuResumeIntr(u32 enable)
{
	VERBOSE_LOG(SCEINTC, "sceKernelCpuResumeIntr(%i)", enable);
	if (enable)
	{
		__EnableInterrupts();
		hleRunInterrupts();
		hleReSchedule("interrupts resumed");
	}
	else
	{
		__DisableInterrupts();
	}
	hleEatCycles(15);
}

void sceKernelIsCpuIntrEnable()
{
	u32 retVal = __InterruptsEnabled(); 
	DEBUG_LOG(SCEINTC, "%i=sceKernelIsCpuIntrEnable()", retVal);
	RETURN(retVal);
}

int sceKernelIsCpuIntrSuspended(int flag)
{
	int retVal = flag == 0 ? 1 : 0;
	DEBUG_LOG(SCEINTC, "%i=sceKernelIsCpuIntrSuspended(%d)", retVal, flag);
	return retVal;
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
		WARN_LOG(SCEINTC, "Ignoring interrupt, already been released.");
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
	subIntrHandlers[subIntrNum].enabled = false;
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

void IntrHandler::queueUp(int subintr) {
	if (subintr == PSP_INTR_SUB_NONE) {
		pendingInterrupts.push_back(PendingInterrupt(intrNumber, subintr));
	} else {
		// Just call execute on all the subintr handlers for this interrupt.
		// They will get queued up.
		for (auto iter = subIntrHandlers.begin(); iter != subIntrHandlers.end(); ++iter) {
			if ((subintr == PSP_INTR_SUB_ALL || iter->first == subintr) && iter->second.enabled && iter->second.handlerAddress != 0) {
				pendingInterrupts.push_back(PendingInterrupt(intrNumber, iter->first));
			}
		}
	}
}

void IntrHandler::DoState(PointerWrap &p)
{
	auto s = p.Section("IntrHandler", 1);
	if (!s)
		return;

	p.Do(intrNumber);
	p.Do<int, SubIntrHandler>(subIntrHandlers);
}

void PendingInterrupt::DoState(PointerWrap &p)
{
	auto s = p.Section("PendingInterrupt", 1);
	if (!s)
		return;

	p.Do(intr);
	p.Do(subintr);
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
	auto s = p.Section("sceKernelInterrupt", 1);
	if (!s)
		return;

	int numInterrupts = PSP_NUMBER_INTERRUPTS;
	p.Do(numInterrupts);
	if (numInterrupts != PSP_NUMBER_INTERRUPTS)
	{
		p.SetError(p.ERROR_FAILURE);
		ERROR_LOG(SCEINTC, "Savestate failure: wrong number of interrupts, can't load.");
		return;
	}

	intState.DoState(p);
	PendingInterrupt pi(0, 0);
	p.Do(pendingInterrupts, pi);
	p.Do(interruptsEnabled);
	p.Do(inInterrupt);
	p.Do(threadBeforeInterrupt);
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
	bool needsThreadReturn = false;

	if (inInterrupt || !interruptsEnabled) {
		// Already in an interrupt! We'll keep going when it's done.
		return false;
	}
	// Can easily prioritize between different kinds of interrupts if necessary.
retry:
	if (!pendingInterrupts.empty()) {
		PendingInterrupt pend = pendingInterrupts.front();

		IntrHandler* handler = intrHandlers[pend.intr];
		if (handler == NULL) {
			WARN_LOG(SCEINTC, "Ignoring interrupt");
			pendingInterrupts.pop_front();
			goto retry;
		}

		// If we came from CoreTiming::Advance(), we might've come from a waiting thread's callback.
		// To avoid "injecting" return values into our saved state, we context switch here.
		SceUID savedThread = __KernelGetCurThread();
		if (__KernelSwitchOffThread("interrupt")) {
			threadBeforeInterrupt = savedThread;
			needsThreadReturn = true;
		}

		intState.save();
		inInterrupt = true;

		if (!handler->run(pend)) {
			pendingInterrupts.pop_front();
			inInterrupt = false;
			goto retry;
		}

		currentMIPS->r[MIPS_REG_RA] = __KernelInterruptReturnAddress();
		return true;
	} else {
		if (needsThreadReturn)
			__KernelSwitchToThread(threadBeforeInterrupt, "left interrupt");
		// DEBUG_LOG(SCEINTC, "No more interrupts!");
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
		VERBOSE_LOG(SCEINTC, "Triggering subinterrupts for interrupt %i sub %i (%i in queue)", intno, subintr, (u32)pendingInterrupts.size());
		__TriggerRunInterrupts(type);
	}
}

void __KernelReturnFromInterrupt()
{
	VERBOSE_LOG(SCEINTC, "Left interrupt handler at %08x", currentMIPS->pc);

	hleSkipDeadbeef();

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
			__KernelReSchedule("left interrupt");
		else
			__KernelSwitchToThread(threadBeforeInterrupt, "left interrupt");
	}
}

void __RegisterIntrHandler(u32 intrNumber, IntrHandler* handler)
{
	if(intrHandlers[intrNumber])
		delete intrHandlers[intrNumber];
	intrHandlers[intrNumber] = handler;
}

SubIntrHandler *__RegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 handler, u32 handlerArg, u32 &error) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		error = SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
		return NULL;
	}
	IntrHandler *intr = intrHandlers[intrNumber];
	if (intr->has(subIntrNumber)) {
		if (intr->get(subIntrNumber)->handlerAddress != 0) {
			error = SCE_KERNEL_ERROR_FOUND_HANDLER;
			return NULL;
		} else {
			SubIntrHandler *subIntrHandler = intr->get(subIntrNumber);
			subIntrHandler->handlerAddress = handler;
			subIntrHandler->handlerArg = handlerArg;

			error = SCE_KERNEL_ERROR_OK;
			return subIntrHandler;
		}
	}

	SubIntrHandler *subIntrHandler = intr->add(subIntrNumber);
	subIntrHandler->subIntrNumber = subIntrNumber;
	subIntrHandler->intrNumber = intrNumber;
	subIntrHandler->handlerAddress = handler;
	subIntrHandler->handlerArg = handlerArg;
	subIntrHandler->enabled = false;

	error = SCE_KERNEL_ERROR_OK;
	return subIntrHandler;
}

int __ReleaseSubIntrHandler(int intrNumber, int subIntrNumber) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	IntrHandler *intr = intrHandlers[intrNumber];
	if (!intr->has(subIntrNumber) || intr->get(subIntrNumber)->handlerAddress == 0) {
		return SCE_KERNEL_ERROR_NOTFOUND_HANDLER;
	}

	for (auto it = pendingInterrupts.begin(); it != pendingInterrupts.end(); ) {
		if (it->intr == intrNumber && it->subintr == subIntrNumber) {
			pendingInterrupts.erase(it++);
		} else {
			++it;
		}
	}

	// This also implicitly disables it, which is correct.
	intrHandlers[intrNumber]->remove(subIntrNumber);
	return 0;
}

u32 sceKernelRegisterSubIntrHandler(u32 intrNumber, u32 subIntrNumber, u32 handler, u32 handlerArg) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): invalid interrupt", intrNumber, subIntrNumber, handler, handlerArg);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	if (subIntrNumber >= PSP_NUMBER_SUBINTERRUPTS) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): invalid subinterrupt", intrNumber, subIntrNumber, handler, handlerArg);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}

	u32 error;
	SubIntrHandler *subIntrHandler = __RegisterSubIntrHandler(intrNumber, subIntrNumber, handler, handlerArg, error);
	if (subIntrHandler) {
		if (handler == 0) {
			WARN_LOG_REPORT(SCEINTC, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): ignored NULL handler", intrNumber, subIntrNumber, handler, handlerArg);
		} else {
			DEBUG_LOG(SCEINTC, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x)", intrNumber, subIntrNumber, handler, handlerArg);
		}
	} else if (error = SCE_KERNEL_ERROR_FOUND_HANDLER) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): duplicate handler", intrNumber, subIntrNumber, handler, handlerArg);
	} else {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): error %08x", intrNumber, subIntrNumber, handler, handlerArg, error);
	}
	return error;
}

u32 sceKernelReleaseSubIntrHandler(u32 intrNumber, u32 subIntrNumber) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelReleaseSubIntrHandler(%i, %i): invalid interrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	if (subIntrNumber >= PSP_NUMBER_SUBINTERRUPTS) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelReleaseSubIntrHandler(%i, %i): invalid subinterrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}

	u32 error = __ReleaseSubIntrHandler(intrNumber, subIntrNumber);
	if (error != SCE_KERNEL_ERROR_OK) {
		ERROR_LOG(SCEINTC, "sceKernelReleaseSubIntrHandler(%i, %i): error %08x", intrNumber, subIntrNumber);
	}
	return error;
}

u32 sceKernelEnableSubIntr(u32 intrNumber, u32 subIntrNumber) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelEnableSubIntr(%i, %i): invalid interrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	if (subIntrNumber >= PSP_NUMBER_SUBINTERRUPTS) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelEnableSubIntr(%i, %i): invalid subinterrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}

	DEBUG_LOG(SCEINTC, "sceKernelEnableSubIntr(%i, %i)", intrNumber, subIntrNumber);
	u32 error;
	if (!intrHandlers[intrNumber]->has(subIntrNumber)) {
		// Enableing a handler before registering it works fine.
		__RegisterSubIntrHandler(intrNumber, subIntrNumber, 0, 0, error);
	}

	intrHandlers[intrNumber]->enable(subIntrNumber);
	return 0;
}

u32 sceKernelDisableSubIntr(u32 intrNumber, u32 subIntrNumber) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelDisableSubIntr(%i, %i): invalid interrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	if (subIntrNumber >= PSP_NUMBER_SUBINTERRUPTS) {
		ERROR_LOG_REPORT(SCEINTC, "sceKernelDisableSubIntr(%i, %i): invalid subinterrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}

	DEBUG_LOG(SCEINTC, "sceKernelDisableSubIntr(%i, %i)", intrNumber, subIntrNumber);

	if (!intrHandlers[intrNumber]->has(subIntrNumber)) {
		// Disabling when not registered is not an error.
		return 0;
	}

	intrHandlers[intrNumber]->disable(subIntrNumber);
	return 0;
}


struct PspIntrHandlerOptionParam {
	int size;              //+00
	u32 entry;             //+04
	u32 common;            //+08
	u32 gp;                //+0C
	u16 intr_code;         //+10
	u16 sub_count;         //+12
	u16 intr_level;        //+14
	u16 enabled;           //+16
	u32 calls;             //+18
	u32 field_1C;          //+1C
	u32 total_clock_lo;    //+20
	u32 total_clock_hi;    //+24
	u32 min_clock_lo;      //+28
	u32 min_clock_hi;      //+2C
	u32 max_clock_lo;      //+30
	u32 max_clock_hi;      //+34
};  //=38

void QueryIntrHandlerInfo()
{
	ERROR_LOG_REPORT(SCEINTC, "QueryIntrHandlerInfo()");
	RETURN(0);
}

u32 sceKernelMemset(u32 addr, u32 fillc, u32 n)
{
	u8 c = fillc & 0xff;
	DEBUG_LOG(SCEINTC, "sceKernelMemset(ptr = %08x, c = %02x, n = %08x)", addr, c, n);
	bool skip = false;
	if (Memory::IsVRAMAddress(addr)) {
		skip = gpu->PerformMemorySet(addr, fillc, n);
	}
	if (!skip) {
		Memory::Memset(addr, c, n);
	}
	return addr;
}

u32 sceKernelMemcpy(u32 dst, u32 src, u32 size)
{
	DEBUG_LOG(SCEKERNEL, "sceKernelMemcpy(dest=%08x, src=%08x, size=%i)", dst, src, size);

	bool skip = false;
	if (Memory::IsVRAMAddress(src) || Memory::IsVRAMAddress(dst)) {
		skip = gpu->PerformMemoryCopy(dst, src, size);
	}

	// Technically should crash if these are invalid and size > 0...
	if (!skip && Memory::IsValidAddress(dst) && Memory::IsValidAddress(src) && Memory::IsValidAddress(dst + size - 1) && Memory::IsValidAddress(src + size - 1))
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
#ifndef MOBILE_DEVICE
	CBreakPoints::ExecMemCheck(src, false, size, currentMIPS->pc);
	CBreakPoints::ExecMemCheck(dst, true, size, currentMIPS->pc);
#endif
	return dst;
}

const HLEFunction Kernel_Library[] =
{
	{0x092968F4,sceKernelCpuSuspendIntr, "sceKernelCpuSuspendIntr"},
	{0x5F10D406,WrapV_U<sceKernelCpuResumeIntr>, "sceKernelCpuResumeIntr"}, //int oldstat
	{0x3b84732d,WrapV_U<sceKernelCpuResumeIntrWithSync>, "sceKernelCpuResumeIntrWithSync"},
	{0x47a0b729,WrapI_I<sceKernelIsCpuIntrSuspended>, "sceKernelIsCpuIntrSuspended"}, //flags
	{0xb55249d2,sceKernelIsCpuIntrEnable, "sceKernelIsCpuIntrEnable"},
	{0xa089eca4,WrapU_UUU<sceKernelMemset>, "sceKernelMemset"},
	{0xDC692EE3,WrapI_UI<sceKernelTryLockLwMutex>, "sceKernelTryLockLwMutex"},
	{0x37431849,WrapI_UI<sceKernelTryLockLwMutex_600>, "sceKernelTryLockLwMutex_600"},
	{0xbea46419,WrapI_UIU<sceKernelLockLwMutex>, "sceKernelLockLwMutex", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x1FC64E09,WrapI_UIU<sceKernelLockLwMutexCB>, "sceKernelLockLwMutexCB", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x15b6446b,WrapI_UI<sceKernelUnlockLwMutex>, "sceKernelUnlockLwMutex"},
	{0xc1734599,WrapI_UU<sceKernelReferLwMutexStatus>, "sceKernelReferLwMutexStatus"},
	{0x293b45b8,WrapI_V<sceKernelGetThreadId>, "sceKernelGetThreadId"},
	{0xD13BDE95,WrapI_V<sceKernelCheckThreadStack>, "sceKernelCheckThreadStack"},
	{0x1839852A,WrapU_UUU<sceKernelMemcpy>, "sceKernelMemcpy"},
	{0xfa835cde,WrapI_I<sceKernelGetTlsAddr>, "sceKernelGetTlsAddr"},
};

u32 sysclib_memcpy(u32 dst, u32 src, u32 size) {
	ERROR_LOG(SCEKERNEL, "Untested sysclib_memcpy(dest=%08x, src=%08x, size=%i)", dst, src, size);
	memcpy(Memory::GetPointer(dst), Memory::GetPointer(src), size);
	return dst;
}

u32 sysclib_strcat(u32 dst, u32 src) {
	ERROR_LOG(SCEKERNEL, "Untested sysclib_strcat(dest=%08x, src=%08x)", dst, src);
	strcat((char *)Memory::GetPointer(dst), (char *)Memory::GetPointer(src));
	return dst;
}

int sysclib_strcmp(u32 dst, u32 src) {
	ERROR_LOG(SCEKERNEL, "Untested sysclib_strcmp(dest=%08x, src=%08x)", dst, src);
	return strcmp((char *)Memory::GetPointer(dst), (char *)Memory::GetPointer(src));
}

u32 sysclib_strcpy(u32 dst, u32 src) {
	ERROR_LOG(SCEKERNEL, "Untested sysclib_strcpy(dest=%08x, src=%08x)", dst, src);
	strcpy((char *)Memory::GetPointer(dst), (char *)Memory::GetPointer(src));
	return dst;
}

u32 sysclib_strlen(u32 src) {
	ERROR_LOG(SCEKERNEL, "Untested sysclib_strlen(src=%08x)", src);
	return (u32)strlen(Memory::GetCharPointer(src));
}

int sysclib_memcmp(u32 dst, u32 src, u32 size) {
	ERROR_LOG(SCEKERNEL, "Untested sysclib_memcmp(dest=%08x, src=%08x, size=%i)", dst, src, size);
	return memcmp(Memory::GetCharPointer(dst), Memory::GetCharPointer(src), size);
}

int sysclib_sprintf(u32 dst, u32 fmt) {
	ERROR_LOG(SCEKERNEL, "Unimpl sysclib_sprintf(dest=%08x, src=%08x)", dst, fmt);
	// TODO
	return sprintf((char *)Memory::GetPointer(dst), "%s", Memory::GetCharPointer(fmt));
}

u32 sysclib_memset(u32 destAddr, int data, int size) {
	ERROR_LOG(SCEKERNEL, "Untested sysclib_memset(dest=%08x, data=%d ,size=%d)", destAddr, data, size);
	if (Memory::IsValidAddress(destAddr))
		memset(Memory::GetPointer(destAddr), data, size);
	return 0;
}

const HLEFunction SysclibForKernel[] =
{
	{0xAB7592FF, WrapU_UUU<sysclib_memcpy>, "memcpy"},
	{0x476FD94A, WrapU_UU<sysclib_strcat>, "strcat"},
	{0xC0AB8932, WrapI_UU<sysclib_strcmp>, "strcmp"},
	{0xEC6F1CF2, WrapU_UU<sysclib_strcpy>, "strcpy"},
	{0x52DF196C, WrapU_U<sysclib_strlen>, "strlen"},
	{0x81D0D1F7, WrapI_UUU<sysclib_memcmp>, "memcmp"},
	{0x7661e728, WrapI_UU<sysclib_sprintf>, "sprintf"},
	{0x10F3BB61, WrapU_UII<sysclib_memset>, "memset" },
};

void Register_Kernel_Library()
{
	RegisterModule("Kernel_Library", ARRAY_SIZE(Kernel_Library), Kernel_Library);
}

void Register_SysclibForKernel()
{
	RegisterModule("SysclibForKernel", ARRAY_SIZE(SysclibForKernel), SysclibForKernel);
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
