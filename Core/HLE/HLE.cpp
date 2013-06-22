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

#include "base/timeutil.h"
#include "HLE.h"
#include <map>
#include <vector>
#include "../MemMap.h"
#include "../Config.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"

#include "HLETables.h"
#include "../System.h"
#include "sceDisplay.h"
#include "sceIo.h"
#include "sceAudio.h"
#include "sceKernelMemory.h"
#include "sceKernelThread.h"
#include "sceKernelInterrupt.h"
#include "../MIPS/MIPSCodeUtils.h"
#include "../Host.h"

enum
{
	// Do nothing after the syscall.
	HLE_AFTER_NOTHING = 0x00,
	// Reschedule immediately after the syscall.
	HLE_AFTER_RESCHED = 0x01,
	// Call current thread's callbacks after the syscall.
	HLE_AFTER_CURRENT_CALLBACKS = 0x02,
	// Check all threads' callbacks after the syscall.
	HLE_AFTER_ALL_CALLBACKS = 0x04,
	// Reschedule and process current thread's callbacks after the syscall.
	HLE_AFTER_RESCHED_CALLBACKS = 0x08,
	// Run interrupts (and probably reschedule) after the syscall.
	HLE_AFTER_RUN_INTERRUPTS = 0x10,
	// Switch to CORE_STEPPING after the syscall (for debugging.)
	HLE_AFTER_DEBUG_BREAK = 0x20,
};

static std::vector<HLEModule> moduleDB;
static std::vector<Syscall> unresolvedSyscalls;
static std::vector<Syscall> exportedCalls;
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
	Syscall sc = {""};
	p.Do(unresolvedSyscalls, sc);
	p.Do(exportedCalls, sc);
	p.Do(delayedResultEvent);
	CoreTiming::RestoreRegisterEvent(delayedResultEvent, "HLEDelayedResult", hleDelayResultFinish);
	p.DoMarker("HLE");
}

void HLEShutdown()
{
	hleAfterSyscall = HLE_AFTER_NOTHING;
	moduleDB.clear();
	unresolvedSyscalls.clear();
	exportedCalls.clear();
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

	// Was this function exported previously?
	static char temp[256];
	for (auto it = exportedCalls.begin(), end = exportedCalls.end(); it != end; ++it)
	{
		if (!strncmp(it->moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH) && it->nid == nib)
		{
			sprintf(temp, "[EXP: 0x%08x]", nib);
			return temp;
		}
	}

	// No good, we can't find it.
	sprintf(temp,"[UNK: 0x%08x]", nib);
	return temp;
}

u32 GetSyscallOp(const char *moduleName, u32 nib)
{
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
			Reporting::ReportMessage("Unknown syscall in known module: %s 0x%08x", moduleName, nib);
			return (0x0003FFCC | (modindex<<18));  // invalid syscall
		}
	}
	else
	{
		ERROR_LOG(HLE, "Unknown module %s!", moduleName);
		return (0x03FFFFCC);	// invalid syscall
	}
}

void WriteSyscall(const char *moduleName, u32 nib, u32 address)
{
	if (nib == 0)
	{
		Memory::Write_U32(MIPS_MAKE_JR_RA(), address); //patched out?
		Memory::Write_U32(MIPS_MAKE_NOP(), address+4); //patched out?
		return;
	}
	int modindex = GetModuleIndex(moduleName);
	if (modindex != -1)
	{
		Memory::Write_U32(MIPS_MAKE_JR_RA(), address); // jr ra
		Memory::Write_U32(GetSyscallOp(moduleName, nib), address + 4);
	}
	else
	{
		// Did another module export this already?
		for (auto it = exportedCalls.begin(), end = exportedCalls.end(); it != end; ++it)
		{
			if (!strncmp(it->moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH) && it->nid == nib)
			{
				Memory::Write_U32(MIPS_MAKE_J(it->symAddr), address); // j symAddr
				Memory::Write_U32(MIPS_MAKE_NOP(), address + 4); // nop (delay slot)
				return;
			}
		}

		// Module inexistent.. for now; let's store the syscall for it to be resolved later
		INFO_LOG(HLE,"Syscall (%s,%08x) unresolved, storing for later resolving", moduleName, nib);
		Syscall sysc = {"", address, nib};
		strncpy(sysc.moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH);
		sysc.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';
		unresolvedSyscalls.push_back(sysc);

		// Write a trap so we notice this func if it's called before resolving.
		Memory::Write_U32(MIPS_MAKE_JR_RA(), address); // jr ra
		Memory::Write_U32(GetSyscallOp("(invalid syscall)", nib), address + 4);
	}
}

void ResolveSyscall(const char *moduleName, u32 nib, u32 address)
{
	_dbg_assert_msg_(HLE, moduleName != NULL, "Invalid module name.");

	for (size_t i = 0; i < unresolvedSyscalls.size(); i++)
	{
		Syscall *sysc = &unresolvedSyscalls[i];
		if (strncmp(sysc->moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH) == 0 && sysc->nid == nib)
		{
			INFO_LOG(HLE,"Resolving %s/%08x",moduleName,nib);
			// Note: doing that, we can't trace external module calls, so maybe something else should be done to debug more efficiently
			// Note that this should be J not JAL, as otherwise control will return to the stub..
			Memory::Write_U32(MIPS_MAKE_J(address), sysc->symAddr);
			Memory::Write_U32(MIPS_MAKE_NOP(), sysc->symAddr + 4);
		}
	}

	Syscall ex = {"", address, nib};
	strncpy(ex.moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH);
	ex.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';
	exportedCalls.push_back(ex);
}

const char *GetFuncName(int moduleIndex, int func)
{
	if (moduleIndex >= 0 && moduleIndex < (int)moduleDB.size())
	{
		const HLEModule &module = moduleDB[moduleIndex];
		if (func>=0 && func <= module.numFunctions)
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
	_dbg_assert_msg_(HLE, reason != 0, "hleReSchedule: Expecting a valid reason.");
	_dbg_assert_msg_(HLE, reason != 0 && strlen(reason) < 256, "hleReSchedule: Not too long reason.");

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

inline void hleFinishSyscall(int modulenum, int funcnum)
{
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
		if (!hleExecuteDebugBreak(moduleDB[modulenum].funcTable[funcnum]))
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

void CallSyscall(u32 op)
{
	double start = 0.0;  // need to initialize to fix the race condition where g_Config.bShowDebugStats is enabled in the middle of this func.
	if (g_Config.bShowDebugStats)
	{
		time_update();
		start = time_now_d();
	}
	u32 callno = (op >> 6) & 0xFFFFF; //20 bits
	int funcnum = callno & 0xFFF;
	int modulenum = (callno & 0xFF000) >> 12;
	if (funcnum == 0xfff || op == 0xffff)
	{
		_dbg_assert_msg_(HLE,0,"Unknown syscall");
		ERROR_LOG(HLE,"Unknown syscall: Module: %s", modulenum > (int) moduleDB.size() ? "(unknown)" : moduleDB[modulenum].name); 
		return;
	}
	HLEFunc func = moduleDB[modulenum].funcTable[funcnum].func;
	if (func)
	{
		// TODO: Move to jit/interp.
		u32 flags = moduleDB[modulenum].funcTable[funcnum].flags;
		if (flags & HLE_NOT_DISPATCH_SUSPENDED)
		{
			if (!__KernelIsDispatchEnabled())
				RETURN(SCE_KERNEL_ERROR_CAN_NOT_WAIT);
			else
				func();
		}
		else
			func();

		if (hleAfterSyscall != HLE_AFTER_NOTHING)
			hleFinishSyscall(modulenum, funcnum);
	}
	else
	{
		ERROR_LOG_REPORT(HLE, "Unimplemented HLE function %s", moduleDB[modulenum].funcTable[funcnum].name);
	}
	if (g_Config.bShowDebugStats)
	{
		time_update();
		updateSyscallStats(modulenum, funcnum, time_now_d() - start);
	}
}
