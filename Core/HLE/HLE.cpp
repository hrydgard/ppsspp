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

#include <cstdarg>
#include <map>
#include <vector>
#include <string>

#include "base/logging.h"
#include "base/timeutil.h"
#include "profiler/profiler.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
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
	// Reschedule and process current thread's callbacks after the syscall.
	HLE_AFTER_RESCHED_CALLBACKS = 0x08,
	// Run interrupts (and probably reschedule) after the syscall.
	HLE_AFTER_RUN_INTERRUPTS    = 0x10,
	// Switch to CORE_STEPPING after the syscall (for debugging.)
	HLE_AFTER_DEBUG_BREAK       = 0x20,
	// Don't fill temp regs with 0xDEADBEEF.
	HLE_AFTER_SKIP_DEADBEEF     = 0x40,
};

static std::vector<HLEModule> moduleDB;
static int delayedResultEvent = -1;
static int hleAfterSyscall = HLE_AFTER_NOTHING;
static const char *hleAfterSyscallReschedReason;
static const HLEFunction *latestSyscall = nullptr;
static int idleOp;

void hleDelayResultFinish(u64 userdata, int cycleslate)
{
	u32 error;
	SceUID threadID = (SceUID) userdata;
	SceUID verify = __KernelGetWaitID(threadID, WAITTYPE_HLEDELAY, error);
	// The top 32 bits of userdata are the top 32 bits of the 64 bit result.
	// We can't just put it all in userdata because we need to know the threadID...
	u64 result = (userdata & 0xFFFFFFFF00000000ULL) | __KernelGetWaitValue(threadID, error);

	if (error == 0 && verify == 1)
	{
		__KernelResumeThreadFromWait(threadID, result);
		__KernelReSchedule("woke from hle delay");
	}
	else
		WARN_LOG(HLE, "Someone else woke up HLE-blocked thread?");
}

void HLEInit()
{
	RegisterAllModules();
	delayedResultEvent = CoreTiming::RegisterEvent("HLEDelayedResult", hleDelayResultFinish);
	idleOp = GetSyscallOp("FakeSysCalls", NID_IDLE);
}

void HLEDoState(PointerWrap &p)
{
	auto s = p.Section("HLE", 1);
	if (!s)
		return;

	// Can't be inside a syscall, reset this so errors aren't misleading.
	latestSyscall = nullptr;
	p.Do(delayedResultEvent);
	CoreTiming::RestoreRegisterEvent(delayedResultEvent, "HLEDelayedResult", hleDelayResultFinish);
}

void HLEShutdown()
{
	hleAfterSyscall = HLE_AFTER_NOTHING;
	latestSyscall = nullptr;
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
	if (moduleIndex == -1)
		return -1;

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

u32 GetSyscallOp(const char *moduleName, u32 nib) {
	// Special case to hook up bad imports.
	if (moduleName == NULL) {
		return (0x03FFFFCC);	// invalid syscall
	}

	int modindex = GetModuleIndex(moduleName);
	if (modindex != -1) {
		int funcindex = GetFuncIndex(modindex, nib);
		if (funcindex != -1) {
			return (0x0000000c | (modindex<<18) | (funcindex<<6));
		} else {
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

void hleCheckCurrentCallbacks()
{
	hleAfterSyscall |= HLE_AFTER_CURRENT_CALLBACKS;
}

void hleReSchedule(const char *reason)
{
#ifdef _DEBUG
	_dbg_assert_msg_(HLE, reason != nullptr && strlen(reason) < 256, "hleReSchedule: Invalid or too long reason.");
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

void hleEatCycles(int cycles) {
	// Maybe this should Idle, at least for larger delays?  Could that cause issues?
	currentMIPS->downcount -= cycles;
}

void hleEatMicro(int usec) {
	hleEatCycles((int) usToCycles(usec));
}

bool hleIsKernelMode() {
	return latestSyscall && (latestSyscall->flags & HLE_KERNEL_SYSCALL) != 0;
}

const static u32 deadbeefRegs[12] = {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF};
inline static void SetDeadbeefRegs()
{
	if (g_Config.bSkipDeadbeefFilling)
		return;

	currentMIPS->r[MIPS_REG_COMPILER_SCRATCH] = 0xDEADBEEF;
	// Set all the arguments and temp regs.
	memcpy(&currentMIPS->r[MIPS_REG_A0], deadbeefRegs, sizeof(deadbeefRegs));
	currentMIPS->r[MIPS_REG_T8] = 0xDEADBEEF;
	currentMIPS->r[MIPS_REG_T9] = 0xDEADBEEF;

	currentMIPS->lo = 0xDEADBEEF;
	currentMIPS->hi = 0xDEADBEEF;
}

inline void hleFinishSyscall(const HLEFunction &info)
{
	if ((hleAfterSyscall & HLE_AFTER_SKIP_DEADBEEF) == 0)
		SetDeadbeefRegs();

	if ((hleAfterSyscall & HLE_AFTER_CURRENT_CALLBACKS) != 0 && (hleAfterSyscall & HLE_AFTER_RESCHED_CALLBACKS) == 0)
		__KernelForceCallbacks();

	if ((hleAfterSyscall & HLE_AFTER_RUN_INTERRUPTS) != 0)
		__RunOnePendingInterrupt();

	if ((hleAfterSyscall & HLE_AFTER_RESCHED_CALLBACKS) != 0)
		__KernelReSchedule(true, hleAfterSyscallReschedReason);
	else if ((hleAfterSyscall & HLE_AFTER_RESCHED) != 0)
		__KernelReSchedule(hleAfterSyscallReschedReason);

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

static void updateSyscallStats(int modulenum, int funcnum, double total)
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
	latestSyscall = info;
	const u32 flags = info->flags;

	if (flags & HLE_CLEAR_STACK_BYTES) {
		u32 stackStart = __KernelGetCurThreadStackStart();
		if (currentMIPS->r[MIPS_REG_SP] - info->stackBytesToClear >= stackStart) {
			Memory::Memset(currentMIPS->r[MIPS_REG_SP] - info->stackBytesToClear, 0, info->stackBytesToClear);
		}
	}

	if ((flags & HLE_NOT_DISPATCH_SUSPENDED) && !__KernelIsDispatchEnabled()) {
		RETURN(hleLogDebug(HLE, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch suspended"));
	} else if ((flags & HLE_NOT_IN_INTERRUPT) && __IsInInterrupt()) {
		RETURN(hleLogDebug(HLE, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "in interrupt"));
	} else {
		info->func();
	}

	if (hleAfterSyscall != HLE_AFTER_NOTHING)
		hleFinishSyscall(*info);
	else
		SetDeadbeefRegs();
}

inline void CallSyscallWithoutFlags(const HLEFunction *info)
{
	latestSyscall = info;
	info->func();

	if (hleAfterSyscall != HLE_AFTER_NOTHING)
		hleFinishSyscall(*info);
	else
		SetDeadbeefRegs();
}

const HLEFunction *GetSyscallFuncPointer(MIPSOpcode op)
{
	u32 callno = (op >> 6) & 0xFFFFF; //20 bits
	int funcnum = callno & 0xFFF;
	int modulenum = (callno & 0xFF000) >> 12;
	if (funcnum == 0xfff) {
		ERROR_LOG(HLE, "Unknown syscall: Module: %s (module: %d func: %d)", modulenum > (int)moduleDB.size() ? "(unknown)" : moduleDB[modulenum].name, modulenum, funcnum);
		return NULL;
	}
	if (modulenum >= (int)moduleDB.size()) {
		ERROR_LOG(HLE, "Syscall had bad module number %d - probably executing garbage", modulenum);
		return NULL;
	}
	if (funcnum >= moduleDB[modulenum].numFunctions) {
		ERROR_LOG(HLE, "Syscall had bad function number %d in module %d - probably executing garbage", funcnum, modulenum);
		return NULL;
	}
	return &moduleDB[modulenum].funcTable[funcnum];
}

void *GetQuickSyscallFunc(MIPSOpcode op) {
	if (coreCollectDebugStats)
		return nullptr;

	const HLEFunction *info = GetSyscallFuncPointer(op);
	if (!info || !info->func)
		return nullptr;
	DEBUG_LOG(HLE, "Compiling syscall to %s", info->name);

	// TODO: Do this with a flag?
	if (op == idleOp)
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
	PROFILE_THIS_SCOPE("syscall");
	double start = 0.0;  // need to initialize to fix the race condition where coreCollectDebugStats is enabled in the middle of this func.
	if (coreCollectDebugStats) {
		time_update();
		start = time_now_d();
	}

	const HLEFunction *info = GetSyscallFuncPointer(op);
	if (!info) {
		RETURN(SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED);
		return;
	}

	if (info->func) {
		if (op == idleOp)
			info->func();
		else if (info->flags != 0)
			CallSyscallWithFlags(info);
		else
			CallSyscallWithoutFlags(info);
	}
	else {
		RETURN(SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED);
		ERROR_LOG_REPORT(HLE, "Unimplemented HLE function %s", info->name ? info->name : "(\?\?\?)");
	}

	if (coreCollectDebugStats) {
		time_update();
		u32 callno = (op >> 6) & 0xFFFFF; //20 bits
		int funcnum = callno & 0xFFF;
		int modulenum = (callno & 0xFF000) >> 12;
		double total = time_now_d() - start - hleSteppingTime;
		hleSteppingTime = 0.0;
		updateSyscallStats(modulenum, funcnum, total);
	}
}

size_t hleFormatLogArgs(char *message, size_t sz, const char *argmask) {
	char *p = message;
	size_t used = 0;

#define APPEND_FMT(...) do { \
	if (used < sz) { \
		size_t c = snprintf(p, sz - used, __VA_ARGS__); \
		used += c; \
		p += c; \
	} \
} while (false)

	int reg = 0;
	int regf = 0;
	for (size_t i = 0, n = strlen(argmask); i < n; ++i, ++reg) {
		u32 regval;
		if (reg < 8) {
			regval = PARAM(reg);
		} else {
			u32 sp = currentMIPS->r[MIPS_REG_SP];
			// Goes upward on stack.
			// NOTE: Currently we only support > 8 for 32-bit integer args.
			regval = Memory::Read_U32(sp + (reg - 8) * 4);
		}

		switch (argmask[i]) {
		case 'p':
			if (Memory::IsValidAddress(regval)) {
				APPEND_FMT("%08x[%08x]", regval, Memory::Read_U32(regval));
			} else {
				APPEND_FMT("%08x[invalid]", regval);
			}
			break;

		case 'P':
			if (Memory::IsValidAddress(regval)) {
				APPEND_FMT("%08x[%016llx]", regval, Memory::Read_U64(regval));
			} else {
				APPEND_FMT("%08x[invalid]", regval);
			}
			break;

		case 's':
			if (Memory::IsValidAddress(regval)) {
				const char *s = Memory::GetCharPointer(regval);
				if (strnlen(s, 64) >= 64) {
					APPEND_FMT("%.64s...", Memory::GetCharPointer(regval));
				} else {
					APPEND_FMT("%s", Memory::GetCharPointer(regval));
				}
			} else {
				APPEND_FMT("(invalid)");
			}
			break;

		case 'x':
			APPEND_FMT("%08x", regval);
			break;

		case 'i':
			APPEND_FMT("%d", regval);
			break;

		case 'X':
		case 'I':
			// 64-bit regs are always aligned.
			if ((reg & 1))
				++reg;
			APPEND_FMT("%016llx", PARAM64(reg));
			++reg;
			break;

		case 'f':
			APPEND_FMT("%f", PARAMF(regf++));
			// This doesn't consume a gp reg.
			--reg;
			break;

		// TODO: Double?  Does it ever happen?

		default:
			_dbg_assert_msg_(HLE, false, "Invalid argmask character: %c", argmask[i]);
			APPEND_FMT(" -- invalid arg format: %c -- %08x", argmask[i], regval);
			break;
		}
		if (i + 1 < n) {
			APPEND_FMT(", ");
		}
	}

	if (used > sz) {
		message[sz - 1] = '\0';
	} else {
		message[used] = '\0';
	}

#undef APPEND_FMT
	return used;
}

void hleDoLogInternal(LogTypes::LOG_TYPE t, LogTypes::LOG_LEVELS level, u64 res, const char *file, int line, const char *reportTag, char retmask, const char *reason, const char *formatted_reason) {
	char formatted_args[4096];
	const char *funcName = "?";
	u32 funcFlags = 0;
	if (latestSyscall) {
		hleFormatLogArgs(formatted_args, sizeof(formatted_args), latestSyscall->argmask);

		// This acts as an override (for error returns which are usually hex.)
		if (retmask == '\0')
			retmask = latestSyscall->retmask;

		funcName = latestSyscall->name;
		funcFlags = latestSyscall->flags;
	}

	const char *fmt;
	if (retmask == 'x') {
		fmt = "%s%08llx=%s(%s)%s";
		// Truncate the high bits of the result (from any sign extension.)
		res = (u32)res;
	} else if (retmask == 'i' || retmask == 'I') {
		fmt = "%s%lld=%s(%s)%s";
	} else if (retmask == 'f') {
		// TODO: For now, floats are just shown as bits.
		fmt = "%s%08x=%s(%s)%s";
	} else {
		_dbg_assert_msg_(HLE, false, "Invalid return format: %c", retmask);
		fmt = "%s%08llx=%s(%s)%s";
	}

	const char *kernelFlag = (funcFlags & HLE_KERNEL_SYSCALL) != 0 ? "K " : "";
	GenericLog(level, t, file, line, fmt, kernelFlag, res, funcName, formatted_args, formatted_reason);

	if (reportTag != nullptr) {
		// A blank string means always log, not just once.
		if (reportTag[0] == '\0' || Reporting::ShouldLogOnce(reportTag)) {
			// Here we want the original key, so that different args, etc. group together.
			std::string key = std::string(kernelFlag) + std::string("%08x=") + funcName + "(%s)";
			if (reason != nullptr)
				key += std::string(": ") + reason;

			char formatted_message[8192];
			snprintf(formatted_message, sizeof(formatted_message), fmt, kernelFlag, res, funcName, formatted_args, formatted_reason);
			Reporting::ReportMessageFormatted(key.c_str(), formatted_message);
		}
	}
}
