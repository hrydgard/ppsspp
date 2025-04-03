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

#include "Common/Math/CrossSIMD.h"

#include "Common/Profiler/Profiler.h"

#include "Common/Log.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/HLE/HLETables.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/HLE.h"

enum {
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
	// Execute pending mips calls.
	HLE_AFTER_QUEUED_CALLS      = 0x80,
	// Call CoreTiming::ForceCheck
	HLE_AFTER_CORETIMING_FORCE_CHECK = 0x100,
	// Split syscall over GE execution
	HLE_SPLIT_SYSCALL_OVER_GE = 0x200,
	HLE_SPLIT_SYSCALL_PART2 = 0x400,
};

static std::vector<HLEModule> moduleDB;
static int delayedResultEvent = -1;
static int hleAfterSyscall = HLE_AFTER_NOTHING;
static const char *hleAfterSyscallReschedReason;

#define MAX_SYSCALL_RECURSION 16

// Keep track of syscalls in flight. Note that they can call each other! But they'll have to use hleCall<>.
static const HLEFunction *g_stack[MAX_SYSCALL_RECURSION];
u32 g_syscallPC;
int g_stackSize;

static int idleOp;

// Split syscall support. NOTE: This needs to be saved in DoState somehow!
static int splitSyscallEatCycles = 0;

// Stats
static double hleSteppingTime = 0.0;
static double hleFlipTime = 0.0;

struct HLEMipsCallInfo {
	u32 func;
	PSPAction *action;
	std::vector<u32> args;
};

struct HLEMipsCallStack {
	u32_le nextOff;
	union {
		struct {
			u32_le func;
			u32_le actionIndex;
			u32_le argc;
		};
		struct {
			u32_le ra;
			u32_le v0;
			u32_le v1;
		};
	};
};

// No need to save state, always flushed at a syscall end.
static std::vector<HLEMipsCallInfo> enqueuedMipsCalls;
// Does need to be saved, referenced by the stack and owned.
static std::vector<PSPAction *> mipsCallActions;

// Modules that games try to load from disk, that we should not load normally. Instead we just HLE hook them.
// TODO: Merge with moduleDB?
static const HLEModuleMeta g_moduleMeta[] = {
	{"sceATRAC3plus_Library", "sceAtrac3plus", DisableHLEFlags::sceAtrac},
	{"sceFont_Library", "sceLibFont", DisableHLEFlags::sceFont},
	{"SceFont_Library", "sceLibFont", DisableHLEFlags::sceFont},
	{"SceFont_Library", "sceLibFttt", DisableHLEFlags::sceFont},
	{"SceHttp_Library", "sceHttp"},
	{"sceMpeg_library", "sceMpeg", DisableHLEFlags::sceMpeg},
	{"sceMp3_Library", "sceMp3", DisableHLEFlags::sceMp3},
	{"sceNetAdhocctl_Library"},
	{"sceNetAdhocDownload_Library"},
	{"sceNetAdhocMatching_Library"},
	{"sceNetApDialogDummy_Library"},
	{"sceNetAdhoc_Library"},
	{"sceNetApctl_Library"},
	{"sceNetInet_Library"},
	{"sceNetResolver_Library"},
	{"sceNet_Library"},
	{"sceNetAdhoc_Library"},
	{"sceNetAdhocAuth_Service"},
	{"sceNetAdhocctl_Library"},
	{"sceNetIfhandle_Service"},
	{"sceSsl_Module"},
	{"sceDEFLATE_Library"},
	{"sceMD5_Library"},
	{"sceMemab"},
	{"sceAvcodec_driver"},
	{"sceAudiocodec_Driver"},
	{"sceAudiocodec"},
	{"sceVideocodec_Driver"},
	{"sceVideocodec"},
	{"sceMpegbase_Driver"},
	{"sceMpegbase"},
	{"scePsmf_library", "scePsmf", DisableHLEFlags::scePsmf},
	{"scePsmfP_library", "scePsmfPlayer", DisableHLEFlags::scePsmfPlayer},
	{"scePsmfPlayer", "scePsmfPlayer", DisableHLEFlags::scePsmfPlayer},
	{"sceSAScore", "sceSasCore"},
	{"sceCcc_Library", "sceCcc"},
	{"SceParseHTTPheader_Library", "sceParseHttp", DisableHLEFlags::sceParseHttp},
	{"SceParseURI_Library"},
	// Guessing these names
	{"sceJpeg", "sceJpeg"},
	{"sceJpeg_library", "sceJpeg"},
	{"sceJpeg_Library", "sceJpeg"},
};

const HLEModuleMeta *GetHLEModuleMeta(std::string_view modname) {
	for (size_t i = 0; i < ARRAY_SIZE(g_moduleMeta); i++) {
		if (equalsNoCase(modname, g_moduleMeta[i].modname)) {
			return &g_moduleMeta[i];
		}
	}
	return nullptr;
}

const HLEModuleMeta *GetHLEModuleMetaByFlag(DisableHLEFlags flag) {
	for (size_t i = 0; i < ARRAY_SIZE(g_moduleMeta); i++) {
		if (g_moduleMeta[i].disableFlag == flag) {
			return &g_moduleMeta[i];
		}
	}
	return nullptr;
}

const HLEModuleMeta *GetHLEModuleMetaByImport(std::string_view importModuleName) {
	for (size_t i = 0; i < ARRAY_SIZE(g_moduleMeta); i++) {
		if (g_moduleMeta[i].importName && equalsNoCase(importModuleName, g_moduleMeta[i].importName)) {
			return &g_moduleMeta[i];
		}
	}
	return nullptr;
}

DisableHLEFlags AlwaysDisableHLEFlags() {
	// Once a module seems stable to load properly, we'll graduate it here.
	// This will hide the checkbox in Developer Settings, too.
	//
	// PSMF testing issue: #20200
	return DisableHLEFlags::scePsmf | DisableHLEFlags::scePsmfPlayer;
}

// Process compat flags.
static DisableHLEFlags GetDisableHLEFlags() {
	DisableHLEFlags flags = (DisableHLEFlags)g_Config.iDisableHLE | AlwaysDisableHLEFlags();
	if (PSP_CoreParameter().compat.flags().DisableHLESceFont) {
		flags |= DisableHLEFlags::sceFont;
	}

	flags &= ~(DisableHLEFlags)g_Config.iForceEnableHLE;
	return flags;
}

// Note: name is the modname from prx, not the export module name!
bool ShouldHLEModule(std::string_view modname, bool *wasDisabledManually) {
	if (wasDisabledManually) {
		*wasDisabledManually = false;
	}
	const HLEModuleMeta *meta = GetHLEModuleMeta(modname);
	if (!meta) {
		return false;
	}

	bool disabled = meta->disableFlag & GetDisableHLEFlags();
	if (disabled) {
		if (wasDisabledManually) {
			// We don't show notifications if a flag has "graduated".
			if (!(meta->disableFlag & AlwaysDisableHLEFlags())) {
				*wasDisabledManually = true;
			}
		}
		return false;
	}
	return true;
}

bool ShouldHLEModuleByImportName(std::string_view name) {
	// Check our special metadata lookup. Should probably be merged with the main one.
	const HLEModuleMeta *meta = GetHLEModuleMetaByImport(name);
	if (meta) {
		bool disabled = meta->disableFlag & GetDisableHLEFlags();
		return !disabled;
	}

	// Otherwise, just fall back to the regular db. If it's in there, we should HLE it.
	return GetHLEModuleByName(name) != nullptr;
}

static void hleDelayResultFinish(u64 userdata, int cycleslate) {
	u32 error;
	SceUID threadID = (SceUID) userdata;
	SceUID verify = __KernelGetWaitID(threadID, WAITTYPE_HLEDELAY, error);
	// The top 32 bits of userdata are the top 32 bits of the 64 bit result.
	// We can't just put it all in userdata because we need to know the threadID...
	u64 result = (userdata & 0xFFFFFFFF00000000ULL) | __KernelGetWaitValue(threadID, error);

	if (error == 0 && verify == 1) {
		__KernelResumeThreadFromWait(threadID, result);
		__KernelReSchedule("woke from hle delay");
	}
	else
		WARN_LOG(Log::HLE, "Someone else woke up HLE-blocked thread %d?", threadID);
}

void HLEInit() {
	RegisterAllModules();
	g_stackSize = 0;
	delayedResultEvent = CoreTiming::RegisterEvent("HLEDelayedResult", hleDelayResultFinish);
	idleOp = GetSyscallOp("FakeSysCalls", NID_IDLE);
}

void HLEDoState(PointerWrap &p) {
	auto s = p.Section("HLE", 1, 2);
	if (!s)
		return;

	// Can't be inside a syscall when saving state, reset this so errors aren't misleading.
	if (g_stackSize) {
		ERROR_LOG(Log::HLE, "Can't save state while in a HLE syscall");
	}

	g_stackSize = 0;
	Do(p, delayedResultEvent);
	CoreTiming::RestoreRegisterEvent(delayedResultEvent, "HLEDelayedResult", hleDelayResultFinish);

	if (s >= 2) {
		int actions = (int)mipsCallActions.size();
		Do(p, actions);
		if (actions != (int)mipsCallActions.size()) {
			mipsCallActions.resize(actions);
		}

		for (auto &action : mipsCallActions) {
			int actionTypeID = action != nullptr ? action->actionTypeID : -1;
			Do(p, actionTypeID);
			if (actionTypeID != -1) {
				if (p.mode == p.MODE_READ)
					action = __KernelCreateAction(actionTypeID);
				action->DoState(p);
			}
		}
	}
}

void HLEShutdown() {
	hleAfterSyscall = HLE_AFTER_NOTHING;
	moduleDB.clear();
	enqueuedMipsCalls.clear();
	for (auto p : mipsCallActions) {
		delete p;
	}
	mipsCallActions.clear();
}

int GetNumRegisteredHLEModules() {
	return (int)moduleDB.size();
}

void RegisterHLEModule(std::string_view name, int numFunctions, const HLEFunction *funcTable) {
	HLEModule module = {name, numFunctions, funcTable};
	moduleDB.push_back(module);
}

const HLEModule *GetHLEModuleByIndex(int index) {
	return &moduleDB[index];
}

// TODO: Do something faster.
const HLEModule *GetHLEModuleByName(std::string_view name) {
	for (auto &module : moduleDB) {
		if (name == module.name) {
			return &module;
		}
	}
	return nullptr;
}

// TODO: Do something faster.
const HLEFunction *GetHLEFuncByName(const HLEModule *module, std::string_view name) {
	for (int i = 0; i < module->numFunctions; i++) {
		auto &func = module->funcTable[i];
		if (func.name == name) {
			return &func;
		}
	}
	return nullptr;
}

int GetHLEModuleIndex(std::string_view moduleName) {
	for (size_t i = 0; i < moduleDB.size(); i++)
		if (moduleDB[i].name == moduleName)
			return (int)i;
	return -1;
}

int GetHLEFuncIndexByNib(int moduleIndex, u32 nib) {
	const HLEModule &module = moduleDB[moduleIndex];
	for (int i = 0; i < module.numFunctions; i++) {
		if (module.funcTable[i].ID == nib)
			return i;
	}
	return -1;
}

u32 GetNibByName(std::string_view moduleName, std::string_view function) {
	int moduleIndex = GetHLEModuleIndex(moduleName);
	if (moduleIndex == -1)
		return -1;

	const HLEModule &module = moduleDB[moduleIndex];
	for (int i = 0; i < module.numFunctions; i++) {
		if (function == module.funcTable[i].name)
			return module.funcTable[i].ID;
	}
	return -1;
}

const HLEFunction *GetHLEFunc(std::string_view moduleName, u32 nib) {
	int moduleIndex = GetHLEModuleIndex(moduleName);
	if (moduleIndex != -1) {
		int idx = GetHLEFuncIndexByNib(moduleIndex, nib);
		if (idx != -1)
			return &(moduleDB[moduleIndex].funcTable[idx]);
	}
	return 0;
}

// WARNING: Not thread-safe!
const char *GetHLEFuncName(std::string_view moduleName, u32 nib) {
	_dbg_assert_msg_(!moduleName.empty(), "Invalid module name.");

	const HLEFunction *func = GetHLEFunc(moduleName, nib);
	if (func)
		return func->name;

	static char temp[64];
	snprintf(temp, sizeof(temp), "[UNK: 0x%08x]", nib);
	return temp;
}

const char *GetHLEFuncName(int moduleIndex, int func) {
	if (moduleIndex >= 0 && moduleIndex < (int)moduleDB.size()) {
		const HLEModule &module = moduleDB[moduleIndex];
		if (func >= 0 && func < module.numFunctions) {
			return module.funcTable[func].name;
		}
	}
	return "[unknown]";
}

u32 GetSyscallOp(std::string_view moduleName, u32 nib) {
	// Special case to hook up bad imports.
	if (moduleName.empty()) {
		return 0x03FFFFCC;  // invalid syscall
	}

	int modindex = GetHLEModuleIndex(moduleName);
	if (modindex != -1) {
		int funcindex = GetHLEFuncIndexByNib(modindex, nib);
		if (funcindex != -1) {
			return (0x0000000c | (modindex<<18) | (funcindex<<6));
		} else {
			INFO_LOG(Log::HLE, "Syscall (%.*s, %08x) unknown", (int)moduleName.size(), moduleName.data(), nib);
			return (0x0003FFCC | (modindex<<18));  // invalid syscall
		}
	} else {
		ERROR_LOG(Log::HLE, "Unknown module %.*s!", (int)moduleName.size(), moduleName.data());
		return 0x03FFFFCC;	// invalid syscall (invalid mod index and func index..)
	}
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
	Memory::Write_U32(GetSyscallOp("", nid), stubAddr + 4);
}

bool WriteHLESyscall(std::string_view moduleName, u32 nib, u32 address)
{
	if (nib == 0)
	{
		WARN_LOG_REPORT(Log::HLE, "Wrote patched out nid=0 syscall (%.*s)", (int)moduleName.size(), moduleName.data());
		Memory::Write_U32(MIPS_MAKE_JR_RA(), address); //patched out?
		Memory::Write_U32(MIPS_MAKE_NOP(), address+4); //patched out?
		return true;
	}
	int modindex = GetHLEModuleIndex(moduleName);
	if (modindex != -1)
	{
		Memory::Write_U32(MIPS_MAKE_JR_RA(), address); // jr ra
		Memory::Write_U32(GetSyscallOp(moduleName, nib), address + 4);
		return true;
	}
	else
	{
		ERROR_LOG_REPORT(Log::HLE, "Unable to write unknown syscall: %.*s/%08x", (int)moduleName.size(), moduleName.data(), nib);
		return false;
	}
}

void hleCheckCurrentCallbacks()
{
	hleAfterSyscall |= HLE_AFTER_CURRENT_CALLBACKS;
}

void hleReSchedule(const char *reason)
{
#ifdef _DEBUG
	_dbg_assert_msg_(reason != nullptr && strlen(reason) < 256, "hleReSchedule: Invalid or too long reason.");
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

void hleCoreTimingForceCheck() {
	hleAfterSyscall |= HLE_AFTER_CORETIMING_FORCE_CHECK;
}

// Pauses execution after an HLE call.
static bool hleExecuteDebugBreak(const HLEFunction *func) {
	if (!func || coreState == CORE_RUNNING_GE) {
		// Let's break on the next one.
		return false;
	}

	const u32 NID_SUSPEND_INTR = 0x092968F4, NID_RESUME_INTR = 0x5F10D406;

	// Never break on these, they're noise.
	u32 blacklistedNIDs[] = {NID_SUSPEND_INTR, NID_RESUME_INTR, NID_IDLE};
	for (size_t i = 0; i < ARRAY_SIZE(blacklistedNIDs); ++i)
	{
		if (func->ID == blacklistedNIDs[i])
			return false;
	}

	INFO_LOG(Log::CPU, "Broke after syscall: %s", func->name);
	Core_Break(BreakReason::HLEDebugBreak, g_syscallPC);
	return true;
}

// Should be used *outside* hleLogError for example. Not the other way around.
u32 hleDelayResult(u32 result, const char *reason, int usec) {
	// _dbg_assert_(g_stackSize == 0);

	if (!__KernelIsDispatchEnabled()) {
		WARN_LOG(Log::HLE, "%s: Dispatch disabled, not delaying HLE result (right thing to do?)", g_stackSize ? g_stack[0]->name : "?");
	} else {
		SceUID thread = __KernelGetCurThread();
		if (KernelIsThreadWaiting(thread))
			ERROR_LOG(Log::HLE, "%s: Delaying a thread that's already waiting", g_stackSize ? g_stack[0]->name : "?");
		CoreTiming::ScheduleEvent(usToCycles(usec), delayedResultEvent, thread);
		__KernelWaitCurThread(WAITTYPE_HLEDELAY, 1, result, 0, false, reason);
	}
	return result;
}

u64 hleDelayResult(u64 result, const char *reason, int usec) {
	// Note: hleDelayResult is called at the outer level, *outside* logging.
	// So, we read from the entry that was just popped. This is OK.
	// _dbg_assert_(g_stackSize == 0);
	if (!__KernelIsDispatchEnabled()) {
		WARN_LOG(Log::HLE, "%s: Dispatch disabled, not delaying HLE result (right thing to do?)", g_stack[0]->name ? g_stack[0]->name : "N/A");
	} else {
		// TODO: Defer this, so you can call this multiple times, in case of syscalls calling syscalls? Although, return values are tricky.
		SceUID thread = __KernelGetCurThread();
		if (KernelIsThreadWaiting(thread))
			ERROR_LOG(Log::HLE, "%s: Delaying a thread that's already waiting", g_stack[0]->name ? g_stack[0]->name : "N/A");
		u64 param = (result & 0xFFFFFFFF00000000) | thread;
		CoreTiming::ScheduleEvent(usToCycles(usec), delayedResultEvent, param);
		__KernelWaitCurThread(WAITTYPE_HLEDELAY, 1, (u32)result, 0, false, reason);
	}
	return result;
}

void hleEatCycles(int cycles) {
	if (hleAfterSyscall & HLE_SPLIT_SYSCALL_OVER_GE) {
		splitSyscallEatCycles = cycles;
	} else {
		// Maybe this should Idle, at least for larger delays?  Could that cause issues?
		currentMIPS->downcount -= cycles;
	}
}

void hleSplitSyscallOverGe() {
	hleAfterSyscall |= HLE_SPLIT_SYSCALL_OVER_GE;
}

void hleEatMicro(int usec) {
	hleEatCycles((int) usToCycles(usec));
}

bool hleIsKernelMode() {
	return g_stackSize && (g_stack[0]->flags & HLE_KERNEL_SYSCALL) != 0;
}

void hleEnqueueCall(u32 func, int argc, const u32 *argv, PSPAction *afterAction) {
	std::vector<u32> args;
	args.resize(argc);
	memcpy(args.data(), argv, argc * sizeof(u32));

	enqueuedMipsCalls.push_back({ func, afterAction, args });

	hleAfterSyscall |= HLE_AFTER_QUEUED_CALLS;
}

void hleFlushCalls() {
	u32 &sp = currentMIPS->r[MIPS_REG_SP];
	PSPPointer<HLEMipsCallStack> stackData;
	_dbg_assert_(g_stackSize == 0);
	VERBOSE_LOG(Log::HLE, "Flushing %d HLE mips calls from %s, sp=%08x", (int)enqueuedMipsCalls.size(), g_stackSize ? g_stack[0]->name : "?", sp);

	// First, we'll add a marker for the final return.
	sp -= sizeof(HLEMipsCallStack);
	stackData.ptr = sp;
	stackData->nextOff = 0xFFFFFFFF;
	stackData->ra = currentMIPS->pc;
	stackData->v0 = currentMIPS->r[MIPS_REG_V0];
	stackData->v1 = currentMIPS->r[MIPS_REG_V1];

	// Now we'll set up the first in the chain.
	currentMIPS->pc = enqueuedMipsCalls[0].func;
	currentMIPS->r[MIPS_REG_RA] = HLEMipsCallReturnAddress();
	for (int i = 0; i < (int)enqueuedMipsCalls[0].args.size(); i++) {
		currentMIPS->r[MIPS_REG_A0 + i] = enqueuedMipsCalls[0].args[i];
	}

	// For stack info, process the first enqueued call last, so we run it first.
	// We don't actually need to store 0's args, but keep it consistent.
	for (int i = (int)enqueuedMipsCalls.size() - 1; i >= 0; --i) {
		auto &info = enqueuedMipsCalls[i];
		u32 stackRequired = (int)info.args.size() * sizeof(u32) + sizeof(HLEMipsCallStack);
		u32 stackAligned = (stackRequired + 0xF) & ~0xF;

		sp -= stackAligned;
		stackData.ptr = sp;
		stackData->nextOff = stackAligned;
		stackData->func = info.func;
		if (info.action) {
			stackData->actionIndex = (int)mipsCallActions.size();
			mipsCallActions.push_back(info.action);
		} else {
			stackData->actionIndex = 0xFFFFFFFF;
		}
		stackData->argc = (int)info.args.size();
		for (int j = 0; j < (int)info.args.size(); ++j) {
			Memory::Write_U32(info.args[j], sp + sizeof(HLEMipsCallStack) + j * sizeof(u32));
		}
	}
	enqueuedMipsCalls.clear();

	DEBUG_LOG(Log::HLE, "Executing HLE mips call at %08x, sp=%08x", currentMIPS->pc, sp);
}

void HLEReturnFromMipsCall() {
	u32 &sp = currentMIPS->r[MIPS_REG_SP];
	PSPPointer<HLEMipsCallStack> stackData;

	// At this point, we may have another mips call to run, or be at the end...
	stackData.ptr = sp;

	if ((stackData->nextOff & 0x0000000F) != 0 || !Memory::IsValidAddress(sp + stackData->nextOff)) {
		ERROR_LOG(Log::HLE, "Corrupt stack on HLE mips call return: %08x", stackData->nextOff);
		Core_UpdateState(CORE_RUNTIME_ERROR);
		hleNoLogVoid();
		return;
	}

	if (stackData->actionIndex != 0xFFFFFFFF && stackData->actionIndex < (u32)mipsCallActions.size()) {
		PSPAction *&action = mipsCallActions[stackData->actionIndex];
		VERBOSE_LOG(Log::HLE, "Executing action for HLE mips call at %08x, sp=%08x", stackData->func, sp);

		// Search for the saved v0/v1 values, to preserve the PSPAction API...
		PSPPointer<HLEMipsCallStack> finalMarker = stackData;
		while ((finalMarker->nextOff & 0x0000000F) == 0 && Memory::IsValidAddress(finalMarker.ptr + finalMarker->nextOff)) {
			finalMarker.ptr += finalMarker->nextOff;
		}

		if (finalMarker->nextOff != 0xFFFFFFFF) {
			ERROR_LOG(Log::HLE, "Corrupt stack on HLE mips call return action: %08x", finalMarker->nextOff);
			Core_UpdateState(CORE_RUNTIME_ERROR);
			hleNoLogVoid();
			return;
		}

		MipsCall mc;
		mc.savedV0 = finalMarker->v0;
		mc.savedV1 = finalMarker->v1;
		action->run(mc);
		finalMarker->v0 = mc.savedV0;
		finalMarker->v1 = mc.savedV1;

		delete action;
		action = nullptr;

		// Note: the action could actually enqueue more, adding another layer on stack after this.
	}

	sp += stackData->nextOff;
	stackData.ptr = sp;

	if (stackData->nextOff == 0xFFFFFFFF) {
		// We're done.  Grab the HLE result's v0/v1 and return from the syscall.
		currentMIPS->pc = stackData->ra;
		currentMIPS->r[MIPS_REG_V0] = stackData->v0;
		currentMIPS->r[MIPS_REG_V1] = stackData->v1;

		sp += sizeof(HLEMipsCallStack);

		bool canClear = true;
		for (auto p : mipsCallActions) {
			canClear = canClear && p == nullptr;
		}
		if (canClear) {
			mipsCallActions.clear();
		}

		VERBOSE_LOG(Log::HLE, "Finished HLE mips calls, v0=%08x, sp=%08x", currentMIPS->r[MIPS_REG_V0], sp);
		hleNoLogVoid();
		return;
	}

	// Alright, we have another to call.
	hleSkipDeadbeef();
	currentMIPS->pc = stackData->func;
	currentMIPS->r[MIPS_REG_RA] = HLEMipsCallReturnAddress();
	for (int i = 0; i < (int)stackData->argc; i++) {
		currentMIPS->r[MIPS_REG_A0 + i] = Memory::Read_U32(sp + sizeof(HLEMipsCallStack) + i * sizeof(u32));
	}
	DEBUG_LOG(Log::HLE, "Executing next HLE mips call at %08x, sp=%08x", currentMIPS->pc, sp);
	hleNoLogVoid();
}

const static u32 deadbeefRegs[12] = {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF};
inline static void SetDeadbeefRegs()
{
	// Not exactly the same, but any time a syscall happens, it should clear ll.
	currentMIPS->llBit = 0;

	if (g_Config.bSkipDeadbeefFilling)
		return;

	currentMIPS->r[MIPS_REG_COMPILER_SCRATCH] = 0xDEADBEEF;
	// Set all the arguments and temp regs. TODO: Use SIMD to just do three writes.
	memcpy(&currentMIPS->r[MIPS_REG_A0], deadbeefRegs, sizeof(deadbeefRegs));
	currentMIPS->r[MIPS_REG_T8] = 0xDEADBEEF;
	currentMIPS->r[MIPS_REG_T9] = 0xDEADBEEF;

	currentMIPS->lo = 0xDEADBEEF;
	currentMIPS->hi = 0xDEADBEEF;
}

static void hleFinishSyscall(const HLEFunction *info) {
	if (hleAfterSyscall & HLE_SPLIT_SYSCALL_OVER_GE) {
		hleAfterSyscall &= ~HLE_SPLIT_SYSCALL_OVER_GE;
		hleAfterSyscall |= HLE_SPLIT_SYSCALL_PART2;
		// Switch to GE execution immediately.
		// coreState is checked after the syscall, always.
		Core_SwitchToGe();
		return;
	}

	if (hleAfterSyscall & HLE_SPLIT_SYSCALL_PART2) {
		// Eat the extra cycle we added above.
		hleEatCycles(splitSyscallEatCycles + 1);
		// Make sure to zero it so it's not accidentally re-used.
		splitSyscallEatCycles = 0;
	}

	if (hleAfterSyscall & HLE_AFTER_CORETIMING_FORCE_CHECK) {
		CoreTiming::ForceCheck();
	}

	if ((hleAfterSyscall & HLE_AFTER_SKIP_DEADBEEF) == 0)
		SetDeadbeefRegs();

	if ((hleAfterSyscall & HLE_AFTER_QUEUED_CALLS) != 0)
		hleFlushCalls();
	if ((hleAfterSyscall & HLE_AFTER_CURRENT_CALLBACKS) != 0 && (hleAfterSyscall & HLE_AFTER_RESCHED_CALLBACKS) == 0)
		__KernelForceCallbacks();

	if ((hleAfterSyscall & HLE_AFTER_RUN_INTERRUPTS) != 0)
		__RunOnePendingInterrupt();

	if ((hleAfterSyscall & HLE_AFTER_RESCHED_CALLBACKS) != 0)
		__KernelReSchedule(true, hleAfterSyscallReschedReason);
	else if ((hleAfterSyscall & HLE_AFTER_RESCHED) != 0)
		__KernelReSchedule(hleAfterSyscallReschedReason);

	if ((hleAfterSyscall & HLE_AFTER_DEBUG_BREAK) != 0) {
		if (!hleExecuteDebugBreak(info)) {
			// We'll do it next syscall.
			hleAfterSyscall = HLE_AFTER_DEBUG_BREAK;
			hleAfterSyscallReschedReason = 0;
			return;
		}
	}

	hleAfterSyscall = HLE_AFTER_NOTHING;
	hleAfterSyscallReschedReason = 0;
}

void hleFinishSyscallAfterGe() {
	hleFinishSyscall(nullptr);
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

static void CallSyscallWithFlags(const HLEFunction *info) {
	// _dbg_assert_(g_stackSize == 0);
	g_stackSize = 0;

	const int stackSize = g_stackSize;
	if (stackSize == 0) {
		g_stack[0] = info;
		g_stackSize = 1;
	}
	g_syscallPC = currentMIPS->pc;

	const u32 flags = info->flags;

	if (flags & HLE_CLEAR_STACK_BYTES) {
		u32 stackStart = __KernelGetCurThreadStackStart();
		if (currentMIPS->r[MIPS_REG_SP] - info->stackBytesToClear >= stackStart) {
			Memory::Memset(currentMIPS->r[MIPS_REG_SP] - info->stackBytesToClear, 0, info->stackBytesToClear, "HLEStackClear");
		}
	}

	if ((flags & HLE_NOT_DISPATCH_SUSPENDED) && !__KernelIsDispatchEnabled()) {
		RETURN(hleLogDebug(Log::HLE, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch suspended"));
	} else if ((flags & HLE_NOT_IN_INTERRUPT) && __IsInInterrupt()) {
		RETURN(hleLogDebug(Log::HLE, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "in interrupt"));
	} else {
		info->func();
	}

	// Now, g_stackSize should be back to 0. Enable this for "pedantic mode", will find a lot of problems.
	// Check g_stack[0] in the debugger.
	// _dbg_assert_(g_stackSize == 0);

	if (hleAfterSyscall != HLE_AFTER_NOTHING)
		hleFinishSyscall(info);
	else
		SetDeadbeefRegs();

	g_stackSize = 0;
}

static void CallSyscallWithoutFlags(const HLEFunction *info) {
	// _dbg_assert_(g_stackSize == 0);
	g_stackSize = 0;

	const int stackSize = g_stackSize;
	if (stackSize == 0) {
		g_stack[0] = info;
		g_stackSize = 1;
	}
	g_syscallPC = currentMIPS->pc;

	info->func();

	// Now, g_stackSize should be back to 0. Enable this for "pedantic mode", will find a lot of problems.
	// Check g_stack[0] in the debugger.
	// _dbg_assert_(g_stackSize == 0);

	if (hleAfterSyscall != HLE_AFTER_NOTHING)
		hleFinishSyscall(info);
	else
		SetDeadbeefRegs();

	g_stackSize = 0;
}

const HLEFunction *GetSyscallFuncPointer(MIPSOpcode op) {
	u32 callno = (op >> 6) & 0xFFFFF; //20 bits
	int funcnum = callno & 0xFFF;
	int modulenum = (callno & 0xFF000) >> 12;
	if (funcnum == 0xfff) {
		std::string_view modName = modulenum > (int)moduleDB.size() ? "(unknown)" : moduleDB[modulenum].name;
		ERROR_LOG(Log::HLE, "Unknown syscall: Module: '%.*s' (module: %d func: %d)", (int)modName.size(), modName.data(), modulenum, funcnum);
		return NULL;
	}
	if (modulenum >= (int)moduleDB.size()) {
		ERROR_LOG(Log::HLE, "Syscall had bad module number %d - probably executing garbage", modulenum);
		return NULL;
	}
	if (funcnum >= moduleDB[modulenum].numFunctions) {
		ERROR_LOG(Log::HLE, "Syscall had bad function number %d in module %d - probably executing garbage", funcnum, modulenum);
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

	VERBOSE_LOG(Log::HLE, "Compiling syscall to '%s'", info->name);

	// TODO: Do this with a flag?
	if (op == idleOp)
		return (void *)info->func;
	if (info->flags != 0)
		return (void *)&CallSyscallWithFlags;
	return (void *)&CallSyscallWithoutFlags;
}

void hleSetFlipTime(double t) {
	hleFlipTime = t;
}

void CallSyscall(MIPSOpcode op) {
	PROFILE_THIS_SCOPE("syscall");
	double start = 0.0;  // need to initialize to fix the race condition where coreCollectDebugStats is enabled in the middle of this func.
	if (coreCollectDebugStats) {
		start = time_now_d();
	}

	const HLEFunction *info = GetSyscallFuncPointer(op);
	if (!info) {
		// We haven't incremented the stack yet.
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
	} else {
		// We haven't incremented the stack yet.
		RETURN(SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED);
		ERROR_LOG_REPORT(Log::HLE, "Unimplemented HLE function %s", info->name ? info->name : "(\?\?\?)");
	}

	if (coreCollectDebugStats) {
		u32 callno = (op >> 6) & 0xFFFFF; //20 bits
		int funcnum = callno & 0xFFF;
		int modulenum = (callno & 0xFF000) >> 12;
		double total = time_now_d() - start;
		if (total >= hleFlipTime)
			total -= hleFlipTime;
		_dbg_assert_msg_(total >= 0.0, "Time spent in syscall became negative");
		hleFlipTime = 0.0;
		updateSyscallStats(modulenum, funcnum, total);
	}
}

void hlePushFuncDesc(std::string_view module, std::string_view funcName) {
	const HLEModule *mod = GetHLEModuleByName(module);
	_dbg_assert_(mod != nullptr);
	if (!mod) {
		return;
	}
	const HLEFunction *func = GetHLEFuncByName(mod, funcName);
	_dbg_assert_(func != nullptr);
	// Push to the stack. Be careful (due to the nasty adhoc thread..)
	int stackSize = g_stackSize;
	if (stackSize >= 0 && stackSize < ARRAY_SIZE(g_stack)) {
		g_stack[stackSize] = func;
		g_stackSize = stackSize + 1;
	}
}

// TODO: Also add support for argument names.
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
				const int safeLen = Memory::ValidSize(regval, 128);
				if (strnlen(s, safeLen) >= safeLen) {
					APPEND_FMT("%.*s...", safeLen, Memory::GetCharPointer(regval));
				} else {
					APPEND_FMT("%.*s", safeLen, Memory::GetCharPointer(regval));
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
			_dbg_assert_msg_(false, "Invalid argmask character: %c", argmask[i]);
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

void hleLeave() {
	int stackSize = g_stackSize;
	//_dbg_assert_(stackSize > 0);
	if (stackSize > 0) {
		g_stackSize = stackSize - 1;
	}  // else warn?
}

void hleDoLogInternal(Log t, LogLevel level, u64 res, const char *file, int line, const char *reportTag, const char *reason, const char *formatted_reason) {
	char formatted_args[2048];
	const char *funcName = "?";
	u32 funcFlags = 0;

	const int stackSize = g_stackSize;
	if (stackSize <= 0) {
		ERROR_LOG(Log::HLE, "HLE function stack mismatch (%s:%d)! stackSize = %d", file, line, stackSize);
		return;
	}

	const HLEFunction *hleFunc = g_stack[g_stackSize - 1];

	char retmask = hleFunc->retmask;
	if (stackSize) {
		_dbg_assert_(hleFunc->argmask != nullptr);

		// NOTE: For second stack level, we can't get arguments (unless we somehow get them from the host stack!)
		// Need to do something smart in hleCall. But it's better than printing function name and args from the wrong function.
		
		if (stackSize == 1) {
			hleFormatLogArgs(formatted_args, sizeof(formatted_args), hleFunc->argmask);
		} else {
			truncate_cpy(formatted_args, "...N/A...");
		}

		funcName = hleFunc->name;
		funcFlags = hleFunc->flags;
	} else {
		formatted_args[0] = '?';
		formatted_args[1] = '\0';
	}

	const char *fmt;
	const char *errStr = nullptr;
	switch (retmask) {
	case 'x':
		// Truncate the high bits of the result (from any sign extension.)
		res = (u32)res;
		if ((int)res < 0 && (errStr = KernelErrorToString((u32)res))) {
			// It's a known syscall error code, let's display it as string.
			fmt = "%sSCE_KERNEL_ERROR_%s=%s(%s)%s";
		} else {
			fmt = "%s%08llx=%s(%s)%s";
			errStr = nullptr;  // We check errstr later to determine which format to use.
		}
		break;
	case 'i':
	case 'I':
		if ((int)res < 0 && (errStr = KernelErrorToString((u32)res))) {
			// It's a known syscall error code, let's display it as string.
			fmt = "%s%s=%s(%s)%s";
		} else {
			fmt = "%s%lld=%s(%s)%s";
			errStr = nullptr;  // We check errstr later to determine which format to use.
		}
		break;
	case 'f':
		// TODO: For now, floats are just shown as bits.
		fmt = "%s%08llx=%s(%s)%s";
		break;
	case 'v':
		// Void. Return value should not be shown. (the first %s is the "K " string, see below).
		fmt = "%s%s(%s)%s";
		break;
	default:
		_dbg_assert_msg_(false, "Invalid return format: %c", retmask);
		fmt = "%s%08llx=%s(%s)%s";
		break;
	}

	const char *kernelFlag = (funcFlags & HLE_KERNEL_SYSCALL) ? "K " : "";
	if (retmask != 'v') {
		if (errStr) {
			GenericLog(t, level, file, line, fmt, kernelFlag, errStr, funcName, formatted_args, formatted_reason);
		} else {
			GenericLog(t, level, file, line, fmt, kernelFlag, res, funcName, formatted_args, formatted_reason);
		}
	} else {
		// Skipping the res argument for this format string.
		GenericLog(t, level, file, line, fmt, kernelFlag, funcName, formatted_args, formatted_reason);
	}

	if (reportTag) {
		// A blank string means always log, not just once.
		if (reportTag[0] == '\0' || Reporting::ShouldLogNTimes(reportTag, 1)) {
			// Here we want the original key, so that different args, etc. group together.
			std::string key = std::string(kernelFlag) + std::string("%08x=") + funcName + "(%s)";
			if (reason != nullptr) {
				key += ": ";
				key += reason;
			}

			char formatted_message[8192];
			if (retmask != 'v') {
				if (errStr) {
					snprintf(formatted_message, sizeof(formatted_message), fmt, kernelFlag, errStr, funcName, formatted_args, formatted_reason);
				} else {
					snprintf(formatted_message, sizeof(formatted_message), fmt, kernelFlag, res, funcName, formatted_args, formatted_reason);
				}
			} else {
				snprintf(formatted_message, sizeof(formatted_message), fmt, kernelFlag, funcName, formatted_args, formatted_reason);
			}
			Reporting::ReportMessageFormatted(key.c_str(), formatted_message);
		}
	}
}
