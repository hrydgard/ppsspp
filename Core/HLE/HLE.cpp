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

#include <map>
#include <vector>
#include <string>

#include "base/timeutil.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"

#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/HLE/HLETables.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceIo.h"
#include "Core/HLE/sceAudio.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/HLE.h"

enum
{
	// Do nothing after the syscall.
	HLE_AFTER_NOTHING           = 0x00,
	// Reschedule immediately after the syscall.
	HLE_AFTER_RESCHED           = 0x01,
	// Call current thread's callbacks after the syscall.
	HLE_AFTER_CURRENT_CALLBACKS = 0x02,
	// Check all threads' callbacks after the syscall.
	HLE_AFTER_ALL_CALLBACKS     = 0x04,
	// Reschedule and process current thread's callbacks after the syscall.
	HLE_AFTER_RESCHED_CALLBACKS = 0x08,
	// Run interrupts (and probably reschedule) after the syscall.
	HLE_AFTER_RUN_INTERRUPTS    = 0x10,
	// Switch to CORE_STEPPING after the syscall (for debugging.)
	HLE_AFTER_DEBUG_BREAK       = 0x20,
	// Don't fill temp regs with 0xDEADBEEF.
	HLE_AFTER_SKIP_DEADBEEF     = 0x40,
};

typedef std::vector<Syscall> SyscallVector;
typedef std::map<std::string, SyscallVector> SyscallVectorByModule;

static std::vector<HLEModule> moduleDB;
static int delayedResultEvent = -1;
static int hleAfterSyscall = HLE_AFTER_NOTHING;
static const char *hleAfterSyscallReschedReason;

void hleDelayResultFinish(u64 userdata, int cycleslate)
{
	u32 error;
	SceUID threadID = (SceUID) userdata;
	SceUID verify = __KernelGetWaitID(threadID, WAITTYPE_HLEDELAY, error);
	// The top 32 bits of userdata are the top 32 bits of the 64 bit result.
	// We can't just put it all in userdata because we need to know the threadID...
	u64 result = (userdata & 0xFFFFFFFF00000000ULL) | __KernelGetWaitValue(threadID, error);

	if (error == 0 && verify == 1)
		__KernelResumeThreadFromWait(threadID, result);
	else
		WARN_LOG(HLE, "Someone else woke up HLE-blocked thread?");
}

void HLEInit()
{
	RegisterAllModules();
	delayedResultEvent = CoreTiming::RegisterEvent("HLEDelayedResult", hleDelayResultFinish);
}

void HLEDoState(PointerWrap &p)
{
	auto s = p.Section("HLE", 1);
	if (!s)
		return;

	p.Do(delayedResultEvent);
	CoreTiming::RestoreRegisterEvent(delayedResultEvent, "HLEDelayedResult", hleDelayResultFinish);
}

void HLEShutdown()
{
	hleAfterSyscall = HLE_AFTER_NOTHING;
	moduleDB.clear();
}

void RegisterModule(const char *name, int numFunctions, const HLEFunction *funcTable)
{
	HLEModule module = {name, numFunctions, funcTable};
	moduleDB.push_back(module);
}

int GetModuleIndex(const char *moduleName)
{
	for (size_t i = 0; i < moduleDB.size(); i++)
		if (strcmp(moduleName, moduleDB[i].name) == 0)
			return (int)i;
	return -1;
}

int GetFuncIndex(int moduleIndex, u32 nib)
{
	const HLEModule &module = moduleDB[moduleIndex];
	for (int i = 0; i < module.numFunctions; i++)
	{
		if (module.funcTable[i].ID == nib)
			return i;
	}
	return -1;
}

u32 GetNibByName(const char *moduleName, const char *function)
{
	int moduleIndex = GetModuleIndex(moduleName);
	const HLEModule &module = moduleDB[moduleIndex];
	for (int i = 0; i < module.numFunctions; i++)
	{
		if (!strcmp(module.funcTable[i].name, function))
			return module.funcTable[i].ID;
	}
	return -1;
}

const HLEFunction *GetFunc(const char *moduleName, u32 nib)
{
	int moduleIndex = GetModuleIndex(moduleName);
	if (moduleIndex != -1)
	{
		int idx = GetFuncIndex(moduleIndex, nib);
		if (idx != -1)
			return &(moduleDB[moduleIndex].funcTable[idx]);
	}
	return 0;
}

const char *GetFuncName(const char *moduleName, u32 nib)
{
	_dbg_assert_msg_(HLE, moduleName != NULL, "Invalid module name.");

	const HLEFunction *func = GetFunc(moduleName,nib);
	if (func)
		return func->name;

	static char temp[256];
	sprintf(temp,"[UNK: 0x%08x]", nib);
	return temp;
}

u32 GetSyscallOp(const char *moduleName, u32 nib)
{
	// Special case to hook up bad imports.
	if (moduleName == NULL)
	{
		return (0x03FFFFCC);	// invalid syscall
	}

	int modindex = GetModuleIndex(moduleName);
	if (modindex != -1)
	{
		int funcindex = GetFuncIndex(modindex, nib);
		if (funcindex != -1)
		{
			return (0x0000000c | (modindex<<18) | (funcindex<<6));
		}
		else
		{
			INFO_LOG(HLE, "Syscall (%s, %08x) unknown", moduleName, nib);
			return (0x0003FFCC | (modindex<<18));  // invalid syscall
		}
	}
	else
	{
		ERROR_LOG(HLE, "Unknown module %s!", moduleName);
		return (0x03FFFFCC);	// invalid syscall
	}
}

bool FuncImportIsSyscall(const char *module, u32 nib)
{
	return GetFunc(module, nib) != NULL;
}

void WriteFuncStub(u32 stubAddr, u32 symAddr)
{
	// Note that this should be J not JAL, as otherwise control will return to the stub..
	Memory::Write_U32(MIPS_MAKE_J(symAddr), stubAddr);
	// Note: doing that, we can't trace external module calls, so maybe something else should be done to debug more efficiently
	// Perhaps a syscall here (and verify support in jit), marking the module by uid (debugIdentifier)?
	Memory::Write_U32(MIPS_MAKE_NOP(), stubAddr + 4);
}

void WriteFuncMissingStub(u32 stubAddr, u32 nid)
{
	// Write a trap so we notice this func if it's called before resolving.
	Memory::Write_U32(MIPS_MAKE_JR_RA(), stubAddr); // jr ra
	Memory::Write_U32(GetSyscallOp(NULL, nid), stubAddr + 4);
}

bool WriteSyscall(const char *moduleName, u32 nib, u32 address)
{
	if (nib == 0)
	{
		WARN_LOG_REPORT(HLE, "Wrote patched out nid=0 syscall (%s)", moduleName);
		Memory::Write_U32(MIPS_MAKE_JR_RA(), address); //patched out?
		Memory::Write_U32(MIPS_MAKE_NOP(), address+4); //patched out?
		return true;
	}
	int modindex = GetModuleIndex(moduleName);
	if (modindex != -1)
	{
		Memory::Write_U32(MIPS_MAKE_JR_RA(), address); // jr ra
		Memory::Write_U32(GetSyscallOp(moduleName, nib), address + 4);
		return true;
	}
	else
	{
		ERROR_LOG_REPORT(HLE, "Unable to write unknown syscall: %s/%08x", moduleName, nib);
		return false;
	}
}

const char *GetFuncName(int moduleIndex, int func)
{
	if (moduleIndex >= 0 && moduleIndex < (int)moduleDB.size())
	{
		const HLEModule &module = moduleDB[moduleIndex];
		if (func >= 0 && func < module.numFunctions)
		{
			return module.funcTable[func].name;
		}
	}
	return "[unknown]";
}

void hleCheckAllCallbacks()
{
	hleAfterSyscall |= HLE_AFTER_ALL_CALLBACKS;
}

void hleCheckCurrentCallbacks()
{
	hleAfterSyscall |= HLE_AFTER_CURRENT_CALLBACKS;
}

void hleReSchedule(const char *reason)
{
#ifdef _DEBUG
	_dbg_assert_msg_(HLE, reason != 0 && strlen(reason) < 256, "hleReSchedule: Invalid or too long reason.");
#endif

	hleAfterSyscall |= HLE_AFTER_RESCHED;

	if (!reason)
		hleAfterSyscallReschedReason = "Invalid reason";
	else
		hleAfterSyscallReschedReason = reason;
}

void hleReSchedule(bool callbacks, const char *reason)
{
	hleReSchedule(reason);
	if (callbacks)
		hleAfterSyscall |= HLE_AFTER_RESCHED_CALLBACKS;
}

void hleRunInterrupts()
{
	hleAfterSyscall |= HLE_AFTER_RUN_INTERRUPTS;
}

void hleDebugBreak()
{
	hleAfterSyscall |= HLE_AFTER_DEBUG_BREAK;
}

void hleSkipDeadbeef()
{
	hleAfterSyscall |= HLE_AFTER_SKIP_DEADBEEF;
}

// Pauses execution after an HLE call.
bool hleExecuteDebugBreak(const HLEFunction &func)
{
	const u32 NID_SUSPEND_INTR = 0x092968F4, NID_RESUME_INTR = 0x5F10D406;

	// Never break on these, they're noise.
	u32 blacklistedNIDs[] = {NID_SUSPEND_INTR, NID_RESUME_INTR, NID_IDLE};
	for (size_t i = 0; i < ARRAY_SIZE(blacklistedNIDs); ++i)
	{
		if (func.ID == blacklistedNIDs[i])
			return false;
	}

	Core_EnableStepping(true);
	host->SetDebugMode(true);
	return true;
}

u32 hleDelayResult(u32 result, const char *reason, int usec)
{
	if (__KernelIsDispatchEnabled())
	{
		CoreTiming::ScheduleEvent(usToCycles(usec), delayedResultEvent, __KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_HLEDELAY, 1, result, 0, false, reason);
	}
	else
		WARN_LOG(HLE, "Dispatch disabled, not delaying HLE result (right thing to do?)");
	return result;
}

u64 hleDelayResult(u64 result, const char *reason, int usec)
{
	if (__KernelIsDispatchEnabled())
	{
		u64 param = (result & 0xFFFFFFFF00000000) | __KernelGetCurThread();
		CoreTiming::ScheduleEvent(usToCycles(usec), delayedResultEvent, param);
		__KernelWaitCurThread(WAITTYPE_HLEDELAY, 1, (u32) result, 0, false, reason);
	}
	else
		WARN_LOG(HLE, "Dispatch disabled, not delaying HLE result (right thing to do?)");
	return result;
}

void hleEatCycles(int cycles)
{
	// Maybe this should Idle, at least for larger delays?  Could that cause issues?
	currentMIPS->downcount -= cycles;
}

void hleEatMicro(int usec)
{
	hleEatCycles((int) usToCycles(usec));
}

const static u32 deadbeefRegs[12] = {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF};
inline static void SetDeadbeefRegs()
{
	if (g_Config.bSkipDeadbeefFilling)
		return;

	currentMIPS->r[MIPS_REG_COMPILER_SCRATCH] = 0xDEADBEEF;
	// Set all the arguments and temp regs.
	memcpy(&currentMIPS->r[MIPS_REG_A0], deadbeefRegs, sizeof(deadbeefRegs));
	// Using a magic number since there's confusion/disagreement on reg names.
	currentMIPS->r[24] = 0xDEADBEEF;
	currentMIPS->r[25] = 0xDEADBEEF;

	currentMIPS->lo = 0xDEADBEEF;
	currentMIPS->hi = 0xDEADBEEF;
}

inline void hleFinishSyscall(const HLEFunction &info)
{
	if ((hleAfterSyscall & HLE_AFTER_SKIP_DEADBEEF) == 0)
		SetDeadbeefRegs();

	if ((hleAfterSyscall & HLE_AFTER_CURRENT_CALLBACKS) != 0)
		__KernelForceCallbacks();

	if ((hleAfterSyscall & HLE_AFTER_RUN_INTERRUPTS) != 0)
		__RunOnePendingInterrupt();

	// Rescheduling will also do HLE_AFTER_ALL_CALLBACKS.
	if ((hleAfterSyscall & HLE_AFTER_RESCHED_CALLBACKS) != 0)
		__KernelReSchedule(true, hleAfterSyscallReschedReason);
	else if ((hleAfterSyscall & HLE_AFTER_RESCHED) != 0)
		__KernelReSchedule(hleAfterSyscallReschedReason);
	else if ((hleAfterSyscall & HLE_AFTER_ALL_CALLBACKS) != 0)
		__KernelCheckCallbacks();

	if ((hleAfterSyscall & HLE_AFTER_DEBUG_BREAK) != 0)
	{
		if (!hleExecuteDebugBreak(info))
		{
			// We'll do it next syscall.
			hleAfterSyscall = HLE_AFTER_DEBUG_BREAK;
			hleAfterSyscallReschedReason = 0;
			return;
		}
	}

	hleAfterSyscall = HLE_AFTER_NOTHING;
	hleAfterSyscallReschedReason = 0;
}

inline void updateSyscallStats(int modulenum, int funcnum, double total)
{
	const char *name = moduleDB[modulenum].funcTable[funcnum].name;
	// Ignore this one, especially for msInSyscalls (although that ignores CoreTiming events.)
	if (0 == strcmp(name, "_sceKernelIdle"))
		return;

	if (total > kernelStats.slowestSyscallTime)
	{
		kernelStats.slowestSyscallTime = total;
		kernelStats.slowestSyscallName = name;
	}
	kernelStats.msInSyscalls += total;

	KernelStatsSyscall statCall(modulenum, funcnum);
	auto summedStat = kernelStats.summedMsInSyscalls.find(statCall);
	if (summedStat == kernelStats.summedMsInSyscalls.end())
	{
		kernelStats.summedMsInSyscalls[statCall] = total;
		if (total > kernelStats.summedSlowestSyscallTime)
		{
			kernelStats.summedSlowestSyscallTime = total;
			kernelStats.summedSlowestSyscallName = name;
		}
	}
	else
	{
		double newTotal = kernelStats.summedMsInSyscalls[statCall] += total;
		if (newTotal > kernelStats.summedSlowestSyscallTime)
		{
			kernelStats.summedSlowestSyscallTime = newTotal;
			kernelStats.summedSlowestSyscallName = name;
		}
	}
}

inline void CallSyscallWithFlags(const HLEFunction *info)
{
	const u32 flags = info->flags;
	if ((flags & HLE_NOT_DISPATCH_SUSPENDED) && !__KernelIsDispatchEnabled())
	{
		DEBUG_LOG(HLE, "%s: dispatch suspended", info->name);
		RETURN(SCE_KERNEL_ERROR_CAN_NOT_WAIT);
	}
	else if ((flags & HLE_NOT_IN_INTERRUPT) && __IsInInterrupt())
	{
		DEBUG_LOG(HLE, "%s: in interrupt", info->name);
		RETURN(SCE_KERNEL_ERROR_ILLEGAL_CONTEXT);
	}
	else
		info->func();

	if (hleAfterSyscall != HLE_AFTER_NOTHING)
		hleFinishSyscall(*info);
	else
		SetDeadbeefRegs();
}

inline void CallSyscallWithoutFlags(const HLEFunction *info)
{
	info->func();

	if (hleAfterSyscall != HLE_AFTER_NOTHING)
		hleFinishSyscall(*info);
	else
		SetDeadbeefRegs();
}

const HLEFunction *GetSyscallInfo(MIPSOpcode op)
{
	u32 callno = (op >> 6) & 0xFFFFF; //20 bits
	int funcnum = callno & 0xFFF;
	int modulenum = (callno & 0xFF000) >> 12;
	if (funcnum == 0xfff) {
		ERROR_LOG(HLE, "Unknown syscall: Module: %s", modulenum > (int) moduleDB.size() ? "(unknown)" : moduleDB[modulenum].name); 
		return NULL;
	}
	if (modulenum >= (int)moduleDB.size()) {
		ERROR_LOG(HLE, "Syscall had bad module number %i - probably executing garbage", modulenum);
		return NULL;
	}
	if (funcnum >= moduleDB[modulenum].numFunctions) {
		ERROR_LOG(HLE, "Syscall had bad function number %i in module %i - probably executing garbage", funcnum, modulenum);
		return NULL;
	}
	return &moduleDB[modulenum].funcTable[funcnum];
}

void *GetQuickSyscallFunc(MIPSOpcode op)
{
	// TODO: Clear jit cache on g_Config.bShowDebugStats change?
	if (g_Config.bShowDebugStats)
		return NULL;

	const HLEFunction *info = GetSyscallInfo(op);
	if (!info || !info->func)
		return NULL;

	// TODO: Do this with a flag?
	if (op == GetSyscallOp("FakeSysCalls", NID_IDLE))
		return (void *)info->func;
	if (info->flags != 0)
		return (void *)&CallSyscallWithFlags;
	return (void *)&CallSyscallWithoutFlags;
}

static double hleSteppingTime = 0.0;
void hleSetSteppingTime(double t)
{
	hleSteppingTime += t;
}

void CallSyscall(MIPSOpcode op)
{
	double start = 0.0;  // need to initialize to fix the race condition where g_Config.bShowDebugStats is enabled in the middle of this func.
	if (g_Config.bShowDebugStats)
	{
		time_update();
		start = time_now_d();
	}
	const HLEFunction *info = GetSyscallInfo(op);
	if (!info)
		return;

	if (info->func)
	{
		if (op == GetSyscallOp("FakeSysCalls", NID_IDLE))
			info->func();
		else if (info->flags != 0)
			CallSyscallWithFlags(info);
		else
			CallSyscallWithoutFlags(info);
	}
	else
		ERROR_LOG_REPORT(HLE, "Unimplemented HLE function %s", info->name ? info->name : "(\?\?\?)");

	if (g_Config.bShowDebugStats)
	{
		time_update();
		u32 callno = (op >> 6) & 0xFFFFF; //20 bits
		int funcnum = callno & 0xFFF;
		int modulenum = (callno & 0xFF000) >> 12;
		double total = time_now_d() - start - hleSteppingTime;
		hleSteppingTime = 0.0;
		updateSyscallStats(modulenum, funcnum, total);
	}
}
