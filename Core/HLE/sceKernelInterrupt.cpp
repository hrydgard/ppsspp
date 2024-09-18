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

#include <algorithm>
#include <list>
#include <map>
#include <string>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeList.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"

#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelMutex.h"

#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

// Seems like some > 16 are taken but not available.  Probably kernel only?
static const u32 PSP_NUMBER_SUBINTERRUPTS = 32;

// InterruptsManager
//////////////////////////////////////////////////////////////////////////
// INTERRUPT MANAGEMENT
//////////////////////////////////////////////////////////////////////////

class InterruptState {
public:
	void save();
	void restore();
	void clear();

	void DoState(PointerWrap &p) {
		auto s = p.Section("InterruptState", 1);
		if (!s)
			return;

		Do(p, savedCpu);
	}

	PSPThreadContext savedCpu;
};

// STATE

InterruptState intState;
IntrHandler* intrHandlers[PSP_NUMBER_INTERRUPTS];
std::list<PendingInterrupt> pendingInterrupts;

// Yeah, this bit is a bit silly.
static int interruptsEnabled = 1;
static bool inInterrupt;
static SceUID threadBeforeInterrupt;


static int sceKernelCpuSuspendIntr()
{
	VERBOSE_LOG(Log::sceIntc, "sceKernelCpuSuspendIntr");
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
	return returnValue;
}

static void sceKernelCpuResumeIntr(u32 enable)
{
	VERBOSE_LOG(Log::sceIntc, "sceKernelCpuResumeIntr(%i)", enable);
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

static int sceKernelIsCpuIntrEnable()
{
	u32 retVal = __InterruptsEnabled(); 
	DEBUG_LOG(Log::sceIntc, "%i=sceKernelIsCpuIntrEnable()", retVal);
	return retVal;
}

static int sceKernelIsCpuIntrSuspended(int flag)
{
	int retVal = flag == 0 ? 1 : 0;
	DEBUG_LOG(Log::sceIntc, "%i=sceKernelIsCpuIntrSuspended(%d)", retVal, flag);
	return retVal;
}

static void sceKernelCpuResumeIntrWithSync(u32 enable)
{
	sceKernelCpuResumeIntr(enable);
}

bool IntrHandler::run(PendingInterrupt& pend)
{
	SubIntrHandler *handler = get(pend.subintr);
	if (handler == NULL)
	{
		WARN_LOG(Log::sceIntc, "Ignoring interrupt, already been released.");
		return false;
	}

	copyArgsToCPU(pend);

	return true;
}

void IntrHandler::copyArgsToCPU(PendingInterrupt& pend)
{
	SubIntrHandler* handler = get(pend.subintr);
	DEBUG_LOG(Log::CPU, "Entering interrupt handler %08x", handler->handlerAddress);
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

	Do(p, intrNumber);
	Do<int, SubIntrHandler>(p, subIntrHandlers);
}

void PendingInterrupt::DoState(PointerWrap &p)
{
	auto s = p.Section("PendingInterrupt", 1);
	if (!s)
		return;

	Do(p, intr);
	Do(p, subintr);
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
	Do(p, numInterrupts);
	if (numInterrupts != PSP_NUMBER_INTERRUPTS)
	{
		p.SetError(p.ERROR_FAILURE);
		ERROR_LOG(Log::sceIntc, "Savestate failure: wrong number of interrupts, can't load.");
		return;
	}

	intState.DoState(p);
	PendingInterrupt pi(0, 0);
	Do(p, pendingInterrupts, pi);
	Do(p, interruptsEnabled);
	Do(p, inInterrupt);
	Do(p, threadBeforeInterrupt);
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
	savedCpu.reset();
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
			WARN_LOG(Log::sceIntc, "Ignoring interrupt");
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
		// DEBUG_LOG(Log::sceIntc, "No more interrupts!");
		return false;
	}
}

static void __TriggerRunInterrupts(int type)
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
		VERBOSE_LOG(Log::sceIntc, "Triggering subinterrupts for interrupt %i sub %i (%i in queue)", intno, subintr, (u32)pendingInterrupts.size());
		__TriggerRunInterrupts(type);
	}
}

void __KernelReturnFromInterrupt()
{
	VERBOSE_LOG(Log::sceIntc, "Left interrupt handler at %08x", currentMIPS->pc);

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
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): invalid interrupt", intrNumber, subIntrNumber, handler, handlerArg);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	if (subIntrNumber >= PSP_NUMBER_SUBINTERRUPTS) {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): invalid subinterrupt", intrNumber, subIntrNumber, handler, handlerArg);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}

	u32 error;
	SubIntrHandler *subIntrHandler = __RegisterSubIntrHandler(intrNumber, subIntrNumber, handler, handlerArg, error);
	if (subIntrHandler) {
		if (handler == 0) {
			WARN_LOG_REPORT(Log::sceIntc, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): ignored NULL handler", intrNumber, subIntrNumber, handler, handlerArg);
		} else {
			DEBUG_LOG(Log::sceIntc, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x)", intrNumber, subIntrNumber, handler, handlerArg);
		}
	} else if (error == SCE_KERNEL_ERROR_FOUND_HANDLER) {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): duplicate handler", intrNumber, subIntrNumber, handler, handlerArg);
	} else {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelRegisterSubIntrHandler(%i, %i, %08x, %08x): error %08x", intrNumber, subIntrNumber, handler, handlerArg, error);
	}
	return error;
}

u32 sceKernelReleaseSubIntrHandler(u32 intrNumber, u32 subIntrNumber) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelReleaseSubIntrHandler(%i, %i): invalid interrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	if (subIntrNumber >= PSP_NUMBER_SUBINTERRUPTS) {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelReleaseSubIntrHandler(%i, %i): invalid subinterrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}

	u32 error = __ReleaseSubIntrHandler(intrNumber, subIntrNumber);
	if (error != SCE_KERNEL_ERROR_OK) {
		ERROR_LOG(Log::sceIntc, "sceKernelReleaseSubIntrHandler(%i, %i): error %08x", intrNumber, subIntrNumber, error);
	}
	return error;
}

u32 sceKernelEnableSubIntr(u32 intrNumber, u32 subIntrNumber) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelEnableSubIntr(%i, %i): invalid interrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	if (subIntrNumber >= PSP_NUMBER_SUBINTERRUPTS) {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelEnableSubIntr(%i, %i): invalid subinterrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}

	DEBUG_LOG(Log::sceIntc, "sceKernelEnableSubIntr(%i, %i)", intrNumber, subIntrNumber);
	u32 error;
	if (!intrHandlers[intrNumber]->has(subIntrNumber)) {
		// Enableing a handler before registering it works fine.
		__RegisterSubIntrHandler(intrNumber, subIntrNumber, 0, 0, error);
	}

	intrHandlers[intrNumber]->enable(subIntrNumber);
	return 0;
}

static u32 sceKernelDisableSubIntr(u32 intrNumber, u32 subIntrNumber) {
	if (intrNumber >= PSP_NUMBER_INTERRUPTS) {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelDisableSubIntr(%i, %i): invalid interrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}
	if (subIntrNumber >= PSP_NUMBER_SUBINTERRUPTS) {
		ERROR_LOG_REPORT(Log::sceIntc, "sceKernelDisableSubIntr(%i, %i): invalid subinterrupt", intrNumber, subIntrNumber);
		return SCE_KERNEL_ERROR_ILLEGAL_INTRCODE;
	}

	DEBUG_LOG(Log::sceIntc, "sceKernelDisableSubIntr(%i, %i)", intrNumber, subIntrNumber);

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

static int QueryIntrHandlerInfo()
{
	ERROR_LOG_REPORT(Log::sceIntc, "QueryIntrHandlerInfo()");
	return 0;
}

static u32 sceKernelMemset(u32 addr, u32 fillc, u32 n)
{
	u8 c = fillc & 0xff;
	DEBUG_LOG(Log::sceIntc, "sceKernelMemset(ptr = %08x, c = %02x, n = %08x)", addr, c, n);
	bool skip = false;
	if (n != 0) {
		if (Memory::IsVRAMAddress(addr)) {
			skip = gpu->PerformMemorySet(addr, fillc, n);
		}
		if (!skip) {
			Memory::Memset(addr, c, n);
		}
	}
	NotifyMemInfo(MemBlockFlags::WRITE, addr, n, "KernelMemset");
	return addr;
}

static u32 sceKernelMemcpy(u32 dst, u32 src, u32 size)
{
	DEBUG_LOG(Log::sceKernel, "sceKernelMemcpy(dest=%08x, src=%08x, size=%i)", dst, src, size);

	// Some games copy from executable code.  We need to flush emuhack ops.
	if (size != 0)
		currentMIPS->InvalidateICache(src, size);

	bool skip = false;
	if (Memory::IsVRAMAddress(src) || Memory::IsVRAMAddress(dst)) {
		skip = gpu->PerformMemoryCopy(dst, src, size);
	}

	// Technically should crash if these are invalid and size > 0...
	if (!skip && Memory::IsValidAddress(dst) && Memory::IsValidAddress(src) && Memory::IsValidAddress(dst + size - 1) && Memory::IsValidAddress(src + size - 1))
	{
		u8 *dstp = Memory::GetPointerWriteUnchecked(dst);
		const u8 *srcp = Memory::GetPointerUnchecked(src);

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

	if (MemBlockInfoDetailed(size)) {
		NotifyMemInfoCopy(dst, src, size, "KernelMemcpy/");
	}

	return dst;
}

const HLEFunction Kernel_Library[] =
{
	{0x092968F4, &WrapI_V<sceKernelCpuSuspendIntr>,            "sceKernelCpuSuspendIntr",             'i', ""     },
	{0X5F10D406, &WrapV_U<sceKernelCpuResumeIntr>,             "sceKernelCpuResumeIntr",              'v', "x"    },
	{0X3B84732D, &WrapV_U<sceKernelCpuResumeIntrWithSync>,     "sceKernelCpuResumeIntrWithSync",      'v', "x"    },
	{0X47A0B729, &WrapI_I<sceKernelIsCpuIntrSuspended>,        "sceKernelIsCpuIntrSuspended",         'i', "i"    },
	{0xb55249d2, &WrapI_V<sceKernelIsCpuIntrEnable>,           "sceKernelIsCpuIntrEnable",            'i', "",    },
	{0XA089ECA4, &WrapU_UUU<sceKernelMemset>,                  "sceKernelMemset",                     'x', "xxx"  },
	{0XDC692EE3, &WrapI_UI<sceKernelTryLockLwMutex>,           "sceKernelTryLockLwMutex",             'i', "xi"   },
	{0X37431849, &WrapI_UI<sceKernelTryLockLwMutex_600>,       "sceKernelTryLockLwMutex_600",         'i', "xi"   },
	{0XBEA46419, &WrapI_UIU<sceKernelLockLwMutex>,             "sceKernelLockLwMutex",                'i', "xix", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X1FC64E09, &WrapI_UIU<sceKernelLockLwMutexCB>,           "sceKernelLockLwMutexCB",              'i', "xix", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X15B6446B, &WrapI_UI<sceKernelUnlockLwMutex>,            "sceKernelUnlockLwMutex",              'i', "xi"   },
	{0XC1734599, &WrapI_UU<sceKernelReferLwMutexStatus>,       "sceKernelReferLwMutexStatus",         'i', "xp"   },
	{0X293B45B8, &WrapI_V<sceKernelGetThreadId>,               "sceKernelGetThreadId",                'i', ""     },
	{0XD13BDE95, &WrapI_V<sceKernelCheckThreadStack>,          "sceKernelCheckThreadStack",           'i', ""     },
	{0X1839852A, &WrapU_UUU<sceKernelMemcpy>,                  "sceKernelMemcpy",                     'x', "xxx"  },
	{0XFA835CDE, &WrapI_I<sceKernelGetTlsAddr>,                "sceKernelGetTlsAddr",                 'i', "i"    },
	{0X05572A5F, &WrapV_V<sceKernelExitGame>,                  "sceKernelExitGame",                   'v', ""     },
	{0X4AC57943, &WrapI_I<sceKernelRegisterExitCallback>,      "sceKernelRegisterExitCallback",       'i', "i"    },
};

static u32 sysclib_memcpy(u32 dst, u32 src, u32 size) {	
	if (Memory::IsValidRange(dst, size) && Memory::IsValidRange(src, size)) {
		memcpy(Memory::GetPointerWriteUnchecked(dst), Memory::GetPointerUnchecked(src), size);
	}
	if (MemBlockInfoDetailed(size)) {
		NotifyMemInfoCopy(dst, src, size, "KernelMemcpy/");
	}
	return dst;
}

static u32 sysclib_strcat(u32 dst, u32 src) {
	ERROR_LOG(Log::sceKernel, "Untested sysclib_strcat(dest=%08x, src=%08x)", dst, src);
	if (Memory::IsValidNullTerminatedString(dst) && Memory::IsValidNullTerminatedString(src)) {
		strcat((char *)Memory::GetPointerWriteUnchecked(dst), (const char *)Memory::GetPointerUnchecked(src));
	}
	return dst;
}

static int sysclib_strcmp(u32 dst, u32 src) {
	ERROR_LOG(Log::sceKernel, "Untested sysclib_strcmp(dest=%08x, src=%08x)", dst, src);
	if (Memory::IsValidNullTerminatedString(dst) && Memory::IsValidNullTerminatedString(src)) {
		return strcmp((const char *)Memory::GetPointerUnchecked(dst), (const char *)Memory::GetPointerUnchecked(src));
	} else {
		// What to do? Crash, probably.
		return 0;
	}
}

static u32 sysclib_strcpy(u32 dst, u32 src) {
	ERROR_LOG(Log::sceKernel, "Untested sysclib_strcpy(dest=%08x, src=%08x)", dst, src);
	if (Memory::IsValidAddress(dst) && Memory::IsValidNullTerminatedString(src)) {
		strcpy((char *)Memory::GetPointerWriteUnchecked(dst), (const char *)Memory::GetPointerUnchecked(src));
	}
	return dst;
}

static u32 sysclib_strlen(u32 src) {
	ERROR_LOG(Log::sceKernel, "Untested sysclib_strlen(src=%08x)", src);
	if (Memory::IsValidNullTerminatedString(src)) {  // TODO: This computes the length, could reuse it maybe.
		return (u32)strlen(Memory::GetCharPointerUnchecked(src));
	} else {
		// What to do? Crash, probably.
		return 0;
	}
}

static int sysclib_memcmp(u32 dst, u32 src, u32 size) {
	ERROR_LOG(Log::sceKernel, "Untested sysclib_memcmp(dest=%08x, src=%08x, size=%i)", dst, src, size);
	if (Memory::IsValidRange(dst, size) && Memory::IsValidRange(src, size)) {
		return memcmp(Memory::GetCharPointerUnchecked(dst), Memory::GetCharPointerUnchecked(src), size);
	} else {
		// What to do? Crash, probably.
		return 0;
	}
}

static int sysclib_sprintf(u32 dst, u32 fmt) {
	ERROR_LOG(Log::sceKernel, "Untested sysclib_sprintf(dst=%08x, fmt=%08x)", dst, fmt);

	if (!Memory::IsValidNullTerminatedString(fmt)) {
		ERROR_LOG(Log::sceKernel, "sysclib_sprintf bad fmt");
		return 0;
	}

	DEBUG_LOG(Log::sceKernel, "sysclib_sprintf fmt: %s", Memory::GetCharPointerUnchecked(fmt));
	DEBUG_LOG(Log::sceKernel, "sysclib_sprintf a0-a4, t0-t4: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x",
	currentMIPS->r[MIPS_REG_A0],
	currentMIPS->r[MIPS_REG_A1],
	currentMIPS->r[MIPS_REG_A2],
	currentMIPS->r[MIPS_REG_A3],
	currentMIPS->r[MIPS_REG_T0],
	currentMIPS->r[MIPS_REG_T1],
	currentMIPS->r[MIPS_REG_T2],
	currentMIPS->r[MIPS_REG_T3]
	);

	bool processing_specifier = false;
	std::string specifier = "";
	int bytes_to_read = 0;
	int arg_idx = 0;
	std::string result = "";
	for (const char *c = Memory::GetCharPointerUnchecked(fmt); *c != '\0'; c++) {
		if (!processing_specifier) {
			if (*c == '%') {
				specifier = "%";
				processing_specifier = true;
				bytes_to_read = 0;
			} else {
				result.append(1, *c);
			}
		} else {
			specifier.append(1, *c);

			// going by https://cplusplus.com/reference/cstdio/printf/#compatibility
			// no idea what the kernel module really supports as of writing this
			switch (*c) {
			case '%':
			{
				result.append(specifier);
				processing_specifier = false;
				break;
			}
			case 's':
			{
				// consume 4 bytes from arguments
				u32 val = 0;
				if (arg_idx <= 1) {
					val = currentMIPS->r[MIPS_REG_A2 + arg_idx];
				} else if(arg_idx <= 5) {
					val = currentMIPS->r[MIPS_REG_T0 + arg_idx - 2];
				} else {
					int stack_idx = arg_idx - 6;
					u32 stack_cur = currentMIPS->r[MIPS_REG_SP] + stack_idx * 4;

					if (!Memory::IsValidAddress(stack_cur)) {
						ERROR_LOG(Log::sceKernel, "sysclib_sprintf bad stack pointer %08x", stack_cur);
						return 0;
					}
					val = Memory::Read_U32(stack_cur);
					DEBUG_LOG(Log::sceKernel, "sysclib_sprintf fetching %08x from sp + %u", val, stack_idx * 4);
				}
				arg_idx++;

				if (!Memory::IsValidNullTerminatedString(val)) {
					ERROR_LOG(Log::sceKernel, "sysclib_sprintf bad string reference at %08x", val);
					return 0;
				}
				result.append(Memory::GetCharPointerUnchecked(val));
				processing_specifier = false;
				break;
			}
			case 'd':
			case 'i':
			case 'u':
			case 'o':
			case 'x':
			case 'X':
			case 'f':
			case 'e':
			case 'E':
			case 'g':
			case 'G':
			case 'c':
			case 'p':
			case 'n':
			{
				u64 val = 0;
				if (bytes_to_read == 0) {
					bytes_to_read = 4;
				}
				int read_cnt = 0;
				while (bytes_to_read != 0) {
					u32 val_from_arg = 0;
					if (arg_idx <= 1) {
						val_from_arg = currentMIPS->r[MIPS_REG_A2 + arg_idx];
					} else if (arg_idx <= 5) {
						val_from_arg = currentMIPS->r[MIPS_REG_T0 + arg_idx - 2];
					} else {
						int stack_idx = arg_idx - 6;
						u32 stack_cur = currentMIPS->r[MIPS_REG_SP] + stack_idx * 4;

						if (!Memory::IsValidAddress(stack_cur)) {
							ERROR_LOG(Log::sceKernel, "sysclib_sprintf bad stack pointer %08x", stack_cur);
							return 0;
						}
						val_from_arg = Memory::Read_U32(stack_cur);
						DEBUG_LOG(Log::sceKernel, "sysclib_sprintf fetching %08x from sp + %u", val_from_arg, stack_idx * 4);
					}
					arg_idx++;

					val = val | ((u64)val_from_arg << (read_cnt * 32));

					bytes_to_read = bytes_to_read - 4;
					read_cnt++;
				}
				char buf[128] = {0};
				snprintf(buf, sizeof(buf), specifier.c_str(), val);
				buf[sizeof(buf) - 1] = '\0';
				result.append(buf);
				processing_specifier = false;
				break;
			}
			case 'h':
			{
				// allegrex calling convention is 4 bytes aligned
				bytes_to_read = 4;
				break;
			}
			case 'l':
			{
				bytes_to_read = bytes_to_read + 4;
				break;
			}
			}
		}
	}

	DEBUG_LOG(Log::sceKernel, "sysclib_sprintf result string has length %d, content:", (int)result.length());
	DEBUG_LOG(Log::sceKernel, "%s", result.c_str());
	if (!Memory::IsValidRange(dst, (u32)result.length() + 1)) {
		ERROR_LOG(Log::sceKernel, "sysclib_sprintf result string is too long or dst is invalid");
		return 0;
	}
	memcpy((char *)Memory::GetPointerUnchecked(dst), result.c_str(), (int)result.length() + 1);
	return (int)result.length();
}

static u32 sysclib_memset(u32 destAddr, int data, int size) {
	DEBUG_LOG(Log::sceKernel, "Untested sysclib_memset(dest=%08x, data=%d ,size=%d)", destAddr, data, size);
	if (Memory::IsValidRange(destAddr, size)) {
		memset(Memory::GetPointerWriteUnchecked(destAddr), data, size);
	}
	NotifyMemInfo(MemBlockFlags::WRITE, destAddr, size, "KernelMemset");
	return 0;
}

static int sysclib_strstr(u32 s1, u32 s2) {
	DEBUG_LOG(Log::sceKernel, "Untested sysclib_strstr(%08x, %08x)", s1, s2);
	if (Memory::IsValidNullTerminatedString(s1) && Memory::IsValidNullTerminatedString(s2)) {
		std::string str1 = Memory::GetCharPointerUnchecked(s1);
		std::string str2 = Memory::GetCharPointerUnchecked(s2);
		size_t index = str1.find(str2);
		if (index == str1.npos) {
			return 0;
		}
		return s1 + (uint32_t)index;
	}
	return 0;
}

static int sysclib_strncmp(u32 s1, u32 s2, u32 size) {
	DEBUG_LOG(Log::sceKernel, "Untested sysclib_strncmp(%08x, %08x, %08x)", s1, s2, size);
	if (Memory::IsValidRange(s1, size) && Memory::IsValidRange(s2, size)) {
		const char * str1 = Memory::GetCharPointerUnchecked(s1);
		const char * str2 = Memory::GetCharPointerUnchecked(s2);
		return strncmp(str1, str2, size);
	}
	return 0;
}

static u32 sysclib_memmove(u32 dst, u32 src, u32 size) {
	DEBUG_LOG(Log::sceKernel, "Untested sysclib_memmove(%08x, %08x, %08x)", dst, src, size);
	if (Memory::IsValidRange(dst, size) && Memory::IsValidRange(src, size)) {
		memmove(Memory::GetPointerWriteUnchecked(dst), Memory::GetPointerUnchecked(src), size);
	}
	if (MemBlockInfoDetailed(size)) {
		NotifyMemInfoCopy(dst, src, size, "KernelMemmove/");
	}
	return 0;
}

static u32 sysclib_strncpy(u32 dest, u32 src, u32 size) {
	if (!Memory::IsValidAddress(dest) || !Memory::IsValidAddress(src)) {
		return hleLogError(Log::sceKernel, 0, "invalid address");
	}

	// This is just regular strncpy, but being explicit to avoid warnings/safety fixes on missing null.
	u32 i = 0;
	u32 srcSize = Memory::ValidSize(src, size);
	const u8 *srcp = Memory::GetPointerUnchecked(src);
	u8 *destp = Memory::GetPointerWriteUnchecked(dest);
	for (i = 0; i < srcSize; ++i) {
		u8 c = *srcp++;
		if (c == 0)
			break;
		*destp++ = c;
	}

	u32 destSize = Memory::ValidSize(dest, size);
	for (; i < destSize; ++i) {
		*destp++ = 0;
	}

	return hleLogSuccessX(Log::sceKernel, dest);
}

static u32 sysclib_strtol(u32 strPtr, u32 endPtrPtr, int base) {	
	if (!Memory::IsValidNullTerminatedString(strPtr)) {
		return hleLogError(Log::sceKernel, 0, "invalid address");
	}
	const char* str = Memory::GetCharPointer(strPtr);
	char* end = nullptr;
	int result = (int)strtol(str, &end, base);
	if (Memory::IsValidRange(endPtrPtr, 4))
		Memory::WriteUnchecked_U32(strPtr + (end - str), endPtrPtr);
	return result;
}

static u32 sysclib_strchr(u32 src, int c) {
	if (!Memory::IsValidNullTerminatedString(src)) {
		return hleLogError(Log::sceKernel, 0, "invalid address");
	}
	const std::string str = Memory::GetCharPointer(src);
	size_t cpos = str.find(str, c);
	if (cpos == std::string::npos) {
		return 0;
	}
	return src + (int)cpos;
}

static u32 sysclib_strrchr(u32 src, int c) {
	if (!Memory::IsValidNullTerminatedString(src)) {
		return hleLogError(Log::sceKernel, 0, "invalid address");
	}
	const std::string str = Memory::GetCharPointer(src);
	size_t cpos = str.rfind(str, c);
	if (cpos == std::string::npos) {
		return 0;
	}
	return src + (int)cpos;
}

static u32 sysclib_toupper(u32 c) {
	return toupper(c);
}


const HLEFunction SysclibForKernel[] =
{
	{0xAB7592FF, &WrapU_UUU<sysclib_memcpy>,                   "memcpy",                              'x', "xxx",    HLE_KERNEL_SYSCALL },
	{0x476FD94A, &WrapU_UU<sysclib_strcat>,                    "strcat",                              'x', "xx",     HLE_KERNEL_SYSCALL },
	{0xC0AB8932, &WrapI_UU<sysclib_strcmp>,                    "strcmp",                              'i', "xx",     HLE_KERNEL_SYSCALL },
	{0xEC6F1CF2, &WrapU_UU<sysclib_strcpy>,                    "strcpy",                              'x', "xx",     HLE_KERNEL_SYSCALL },
	{0x52DF196C, &WrapU_U<sysclib_strlen>,                     "strlen",                              'x', "x",      HLE_KERNEL_SYSCALL },
	{0x81D0D1F7, &WrapI_UUU<sysclib_memcmp>,                   "memcmp",                              'i', "xxx",    HLE_KERNEL_SYSCALL },
	{0x7661E728, &WrapI_UU<sysclib_sprintf>,                   "sprintf",                             'i', "xx",     HLE_KERNEL_SYSCALL },
	{0x10F3BB61, &WrapU_UII<sysclib_memset>,                   "memset",                              'x', "xii",    HLE_KERNEL_SYSCALL },
	{0x0D188658, &WrapI_UU<sysclib_strstr>,                    "strstr",                              'i', "xx",     HLE_KERNEL_SYSCALL },
	{0x7AB35214, &WrapI_UUU<sysclib_strncmp>,                  "strncmp",                             'i', "xxx",    HLE_KERNEL_SYSCALL },
	{0xA48D2592, &WrapU_UUU<sysclib_memmove>,                  "memmove",                             'x', "xxx",    HLE_KERNEL_SYSCALL },
	{0xB49A7697, &WrapU_UUU<sysclib_strncpy>,                  "strncpy",                             'x', "xxi",    HLE_KERNEL_SYSCALL },
	{0x47DD934D, &WrapU_UUI<sysclib_strtol>,                   "strtol",                              'x', "xxi",    HLE_KERNEL_SYSCALL },
	{0xB1DC2AE8, &WrapU_UI<sysclib_strchr>,                    "strchr",                              'x', "xx",    HLE_KERNEL_SYSCALL },
	{0x4C0E0274, &WrapU_UI<sysclib_strrchr>,                   "strrchr",                             'x', "xx",    HLE_KERNEL_SYSCALL },
	{0xCE2F7487, &WrapU_U<sysclib_toupper>,                    "toupper",                             'x', "x",     HLE_KERNEL_SYSCALL },
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
	{0XCA04A2B9, &WrapU_UUUU<sceKernelRegisterSubIntrHandler>, "sceKernelRegisterSubIntrHandler",     'x', "xxxx" },
	{0XD61E6961, &WrapU_UU<sceKernelReleaseSubIntrHandler>,    "sceKernelReleaseSubIntrHandler",      'x', "xx"   },
	{0XFB8E22EC, &WrapU_UU<sceKernelEnableSubIntr>,            "sceKernelEnableSubIntr",              'x', "xx"   },
	{0X8A389411, &WrapU_UU<sceKernelDisableSubIntr>,           "sceKernelDisableSubIntr",             'x', "xx"   },
	{0X5CB5A78B, nullptr,                                      "sceKernelSuspendSubIntr",             '?', ""     },
	{0X7860E0DC, nullptr,                                      "sceKernelResumeSubIntr",              '?', ""     },
	{0XFC4374B8, nullptr,                                      "sceKernelIsSubInterruptOccurred",     '?', ""     },
	{0xD2E8363F, &WrapI_V<QueryIntrHandlerInfo>,               "QueryIntrHandlerInfo",                'i', ""     },  // No sce prefix for some reason
	{0XEEE43F47, nullptr,                                      "sceKernelRegisterUserSpaceIntrStack", '?', ""     },
};


void Register_InterruptManager()
{
	RegisterModule("InterruptManager", ARRAY_SIZE(InterruptManager), InterruptManager);
}


const HLEFunction InterruptManagerForKernel[] =
{
	{0x092968F4, &WrapI_V<sceKernelCpuSuspendIntr>,            "sceKernelCpuSuspendIntr",             'i', ""    ,HLE_KERNEL_SYSCALL },
	{0X5F10D406, &WrapV_U<sceKernelCpuResumeIntr>,             "sceKernelCpuResumeIntr",              'v', "x"   ,HLE_KERNEL_SYSCALL },
	{0X3B84732D, &WrapV_U<sceKernelCpuResumeIntrWithSync>,     "sceKernelCpuResumeIntrWithSync",      'v', "x"   ,HLE_KERNEL_SYSCALL },
	{0X47A0B729, &WrapI_I<sceKernelIsCpuIntrSuspended>,        "sceKernelIsCpuIntrSuspended",         'i', "i"   ,HLE_KERNEL_SYSCALL },
	{0xb55249d2, &WrapI_V<sceKernelIsCpuIntrEnable>,           "sceKernelIsCpuIntrEnable",            'i', "",    HLE_KERNEL_SYSCALL },
	{0XA089ECA4, &WrapU_UUU<sceKernelMemset>,                  "sceKernelMemset",                     'x', "xxx" ,HLE_KERNEL_SYSCALL },
	{0XDC692EE3, &WrapI_UI<sceKernelTryLockLwMutex>,           "sceKernelTryLockLwMutex",             'i', "xi"  ,HLE_KERNEL_SYSCALL },
	{0X37431849, &WrapI_UI<sceKernelTryLockLwMutex_600>,       "sceKernelTryLockLwMutex_600",         'i', "xi"  ,HLE_KERNEL_SYSCALL },
	{0XBEA46419, &WrapI_UIU<sceKernelLockLwMutex>,             "sceKernelLockLwMutex",                'i', "xix", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED | HLE_KERNEL_SYSCALL},
	{0X1FC64E09, &WrapI_UIU<sceKernelLockLwMutexCB>,           "sceKernelLockLwMutexCB",              'i', "xix", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED | HLE_KERNEL_SYSCALL},
	{0X15B6446B, &WrapI_UI<sceKernelUnlockLwMutex>,            "sceKernelUnlockLwMutex",              'i', "xi"  ,HLE_KERNEL_SYSCALL },
	{0XC1734599, &WrapI_UU<sceKernelReferLwMutexStatus>,       "sceKernelReferLwMutexStatus",         'i', "xp"  ,HLE_KERNEL_SYSCALL },
	{0X293B45B8, &WrapI_V<sceKernelGetThreadId>,               "sceKernelGetThreadId",                'i', ""    ,HLE_KERNEL_SYSCALL },
	{0XD13BDE95, &WrapI_V<sceKernelCheckThreadStack>,          "sceKernelCheckThreadStack",           'i', ""    ,HLE_KERNEL_SYSCALL },
	{0X1839852A, &WrapU_UUU<sceKernelMemcpy>,                  "sceKernelMemcpy",                     'x', "xxx" ,HLE_KERNEL_SYSCALL },
	{0XFA835CDE, &WrapI_I<sceKernelGetTlsAddr>,                "sceKernelGetTlsAddr",                 'i', "i"   ,HLE_KERNEL_SYSCALL },
	{0X05572A5F, &WrapV_V<sceKernelExitGame>,                  "sceKernelExitGame",                   'v', ""    ,HLE_KERNEL_SYSCALL },
	{0X4AC57943, &WrapI_I<sceKernelRegisterExitCallback>,      "sceKernelRegisterExitCallback",       'i', "i"   ,HLE_KERNEL_SYSCALL },
};

void Register_InterruptManagerForKernel()
{
	RegisterModule("InterruptManagerForKernel", ARRAY_SIZE(InterruptManagerForKernel), InterruptManagerForKernel);
}
