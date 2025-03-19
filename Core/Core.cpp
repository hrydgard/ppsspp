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

#include "ppsspp_config.h"

#include <cstdint>
#include <mutex>
#include <set>
#include <condition_variable>

#include "Common/System/System.h"
#include "Common/Profiler/Profiler.h"

#include "Common/GraphicsContext.h"
#include "Common/Log.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/MemFault.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/MIPS/MIPSTracer.h"

#include "GPU/Debugger/Stepping.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

// Step command to execute next
static std::mutex g_stepMutex;

struct CPUStepCommand {
	CPUStepType type;
	int stepSize;
	BreakReason reason;
	u32 relatedAddr;
	bool empty() const {
		return type == CPUStepType::None;
	}
	void clear() {
		type = CPUStepType::None;
		stepSize = 0;
		reason = BreakReason::None;
		relatedAddr = 0;
	}
};

static CPUStepCommand g_cpuStepCommand;

// This is so that external threads can wait for the CPU to become inactive.
static std::condition_variable m_InactiveCond;
static std::mutex m_hInactiveMutex;

static int steppingCounter = 0;
static std::set<CoreLifecycleFunc> lifecycleFuncs;

// This can be read and written from ANYWHERE.
volatile CoreState coreState = CORE_STEPPING_CPU;
CoreState preGeCoreState = CORE_BOOT_ERROR;
// If true, core state has been changed, but JIT has probably not noticed yet.
volatile bool coreStatePending = false;

static bool powerSaving = false;
static bool g_breakAfterFrame = false;
static BreakReason g_breakReason = BreakReason::None;

static MIPSExceptionInfo g_exceptionInfo;

// This is called on EmuThread before RunLoop.
static bool Core_ProcessStepping(MIPSDebugInterface *cpu);

BreakReason Core_BreakReason() {
	return g_breakReason;
}

const char *BreakReasonToString(BreakReason reason) {
	switch (reason) {
	case BreakReason::None: return "None";
	case BreakReason::AssertChoice: return "cpu.assert";
	case BreakReason::DebugBreak: return "cpu.debugbreak";
	case BreakReason::DebugStep: return "cpu.stepping";
	case BreakReason::DebugStepInto: return "cpu.stepInto";
	case BreakReason::UIFocus: return "ui.lost_focus";
	case BreakReason::AfterFrame: return "frame.after";
	case BreakReason::MemoryException: return "memory.exception";
	case BreakReason::CpuException: return "cpu.exception";
	case BreakReason::BreakInstruction: return "cpu.breakInstruction";
	case BreakReason::SavestateLoad: return "savestate.load";
	case BreakReason::SavestateSave: return "savestate.save";
	case BreakReason::SavestateRewind: return "savestate.rewind";
	case BreakReason::SavestateCrash: return "savestate.crash";
	case BreakReason::MemoryBreakpoint: return "memory.breakpoint";
	case BreakReason::CpuBreakpoint: return "cpu.breakpoint";
	case BreakReason::MemoryAccess: return "memory.access";  // ???
	case BreakReason::JitBranchDebug: return "jit.branchdebug";
	case BreakReason::RABreak: return "ra.break";
	case BreakReason::BreakOnBoot: return "ui.boot";
	case BreakReason::AddBreakpoint: return "cpu.breakpoint.add";
	case BreakReason::FrameAdvance: return "ui.frameAdvance";
	case BreakReason::UIPause: return "ui.pause";
	case BreakReason::HLEDebugBreak: return "hle.step";
	default: return "Unknown";
	}
}

void Core_SetGraphicsContext(GraphicsContext *ctx) {
	PSP_CoreParameter().graphicsContext = ctx;
}

void Core_ListenLifecycle(CoreLifecycleFunc func) {
	lifecycleFuncs.insert(func);
}

void Core_NotifyLifecycle(CoreLifecycle stage) {
	if (stage == CoreLifecycle::STARTING) {
		Core_ResetException();
	}

	for (auto func : lifecycleFuncs) {
		func(stage);
	}
}

void Core_Stop() {
	Core_ResetException();
	Core_UpdateState(CORE_POWERDOWN);
}

void Core_UpdateState(CoreState newState) {
	if ((coreState == CORE_RUNNING_CPU || coreState == CORE_NEXTFRAME) && newState != CORE_RUNNING_CPU)
		coreStatePending = true;
	coreState = newState;
}

bool Core_IsStepping() {
	return coreState == CORE_STEPPING_CPU || coreState == CORE_POWERDOWN;
}

bool Core_IsActive() {
	return coreState == CORE_RUNNING_CPU || coreState == CORE_NEXTFRAME || coreStatePending;
}

bool Core_IsInactive() {
	return coreState != CORE_RUNNING_CPU && coreState != CORE_NEXTFRAME && !coreStatePending;
}

void Core_StateProcessed() {
	if (coreStatePending) {
		std::lock_guard<std::mutex> guard(m_hInactiveMutex);
		coreStatePending = false;
		m_InactiveCond.notify_all();
	}
}

void Core_WaitInactive() {
	while (Core_IsActive() && !GPUStepping::IsStepping()) {
		std::unique_lock<std::mutex> guard(m_hInactiveMutex);
		m_InactiveCond.wait(guard);
	}
}

void Core_SetPowerSaving(bool mode) {
	powerSaving = mode;
}

bool Core_GetPowerSaving() {
	return powerSaving;
}

void Core_RunLoopUntil(u64 globalticks) {
	while (true) {
		switch (coreState) {
		case CORE_POWERUP:
		case CORE_POWERDOWN:
		case CORE_BOOT_ERROR:
		case CORE_RUNTIME_ERROR:
		case CORE_NEXTFRAME:
			return;
		case CORE_STEPPING_CPU:
		case CORE_STEPPING_GE:
			if (Core_ProcessStepping(currentDebugMIPS)) {
				return;
			}
			break;
		case CORE_RUNNING_CPU:
			mipsr4k.RunLoopUntil(globalticks);
			if (g_breakAfterFrame && coreState == CORE_NEXTFRAME) {
				g_breakAfterFrame = false;
				g_breakReason = BreakReason::AfterFrame;
				coreState = CORE_STEPPING_CPU;
			}
			break;  // Will loop around to go to RUNNING_GE or NEXTFRAME, which will exit.
		case CORE_RUNNING_GE:
			switch (gpu->ProcessDLQueue()) {
			case DLResult::DebugBreak:
				GPUStepping::EnterStepping(coreState);
				break;
			case DLResult::Error:
				// We should elegantly report the error somehow, or I guess ignore it.
				hleFinishSyscallAfterGe();
				coreState = preGeCoreState;
				break;
			case DLResult::Done:
				// Done executing for now
				hleFinishSyscallAfterGe();
				coreState = preGeCoreState;
				break;
			default:
				// Not a valid return value.
				_dbg_assert_(false);
				break;
			}
			break;
		}
	}
}

// Should only be called from GPUCommon functions (called from sceGe functions).
void Core_SwitchToGe() {
	// TODO: This should be an atomic exchange. Or we add bitflags into coreState.
	preGeCoreState = coreState;
	coreState = CORE_RUNNING_GE;
}

bool Core_RequestCPUStep(CPUStepType type, int stepSize) {
	std::lock_guard<std::mutex> guard(g_stepMutex);
	if (g_cpuStepCommand.type != CPUStepType::None) {
		ERROR_LOG(Log::CPU, "Can't submit two steps in one host frame");
		return false;
	}
	// Some step types don't need a size.
	switch (type) {
	case CPUStepType::Out:
	case CPUStepType::Frame:
		break;
	default:
		_dbg_assert_(stepSize != 0);
		break;
	}
	g_cpuStepCommand = { type, stepSize };
	return true;
}

// Handles more advanced step types (used by the debugger).
// stepSize is to support stepping through compound instructions like fused lui+ladd (li).
// Yes, our disassembler does support those.
// Doesn't return the new address, as that's just mips->getPC().
// Internal use.
static void Core_PerformCPUStep(MIPSDebugInterface *cpu, CPUStepType stepType, int stepSize) {
	switch (stepType) {
	case CPUStepType::Into:
	{
		u32 currentPc = cpu->GetPC();
		u32 newAddress = currentPc + stepSize;
		// If the current PC is on a breakpoint, the user still wants the step to happen.
		g_breakpoints.SetSkipFirst(currentPc);
		for (int i = 0; i < (int)(newAddress - currentPc) / 4; i++) {
			currentMIPS->SingleStep();
		}
		break;
	}
	case CPUStepType::Over:
	{
		u32 currentPc = cpu->GetPC();
		u32 breakpointAddress = currentPc + stepSize;

		g_breakpoints.SetSkipFirst(currentPc);
		MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(cpu, cpu->GetPC());

		// TODO: Doing a step over in a delay slot is a bit .. unclear. Maybe just do a single step.

		if (info.isBranch) {
			if (info.isConditional == false) {
				if (info.isLinkedBranch) { // jal, jalr
					// it's a function call with a delay slot - skip that too
					breakpointAddress += cpu->getInstructionSize(0);
				} else {					// j, ...
					// in case of absolute branches, set the breakpoint at the branch target
					breakpointAddress = info.branchTarget;
				}
			} else {						// beq, ...
				if (info.conditionMet) {
					breakpointAddress = info.branchTarget;
				} else {
					breakpointAddress = currentPc + 2 * cpu->getInstructionSize(0);
				}
			}
			g_breakpoints.AddBreakPoint(breakpointAddress, true);
			Core_Resume();
		} else {
			// If not a branch, just do a simple single-step, no point in involving the breakpoint machinery.
			for (int i = 0; i < (int)(breakpointAddress - currentPc) / 4; i++) {
				currentMIPS->SingleStep();
			}
		}
		break;
	}
	case CPUStepType::Out:
	{
		u32 entry = cpu->GetPC();
		u32 stackTop = 0;

		auto threads = GetThreadsInfo();
		for (size_t i = 0; i < threads.size(); i++) {
			if (threads[i].isCurrent) {
				entry = threads[i].entrypoint;
				stackTop = threads[i].initialStack;
				break;
			}
		}

		auto frames = MIPSStackWalk::Walk(cpu->GetPC(), cpu->GetRegValue(0, 31), cpu->GetRegValue(0, 29), entry, stackTop);
		if (frames.size() < 2) {
			// Failure. PC not moving.
			return;
		}

		u32 breakpointAddress = frames[1].pc;

		g_breakpoints.AddBreakPoint(breakpointAddress, true);
		Core_Resume();
		break;
	}
	case CPUStepType::Frame:
	{
		g_breakAfterFrame = true;
		Core_Resume();
		break;
	}
	default:
		// Not yet implemented
		break;
	}
}

static bool Core_ProcessStepping(MIPSDebugInterface *cpu) {
	Core_StateProcessed();

	// Check if there's any pending save state actions.
	SaveState::Process();

	switch (coreState) {
	case CORE_STEPPING_CPU:
	case CORE_STEPPING_GE:
	case CORE_RUNNING_GE:
		// All good
		break;
	default:
		// Nothing to do.
		return true;
	}

	// Or any GPU actions.
	// Legacy stepping code.
	GPUStepping::ProcessStepping();

	if (coreState == CORE_RUNNING_GE) {
		// Retry, to get it done this frame.
		return false;
	}

	// We're not inside jit now, so it's safe to clear the breakpoints.
	static int lastSteppingCounter = -1;
	if (lastSteppingCounter != steppingCounter) {
		g_breakpoints.ClearTemporaryBreakPoints();
		System_Notify(SystemNotification::DISASSEMBLY_AFTERSTEP);
		System_Notify(SystemNotification::MEM_VIEW);
		lastSteppingCounter = steppingCounter;
	}

	// Need to check inside the lock to avoid races.
	std::lock_guard<std::mutex> guard(g_stepMutex);

	if (coreState != CORE_STEPPING_CPU || g_cpuStepCommand.empty()) {
		return true;
	}

	Core_ResetException();

	if (!g_cpuStepCommand.empty()) {
		Core_PerformCPUStep(cpu, g_cpuStepCommand.type, g_cpuStepCommand.stepSize);
		if (g_cpuStepCommand.type == CPUStepType::Into) {
			// We're already done. The other step types will resume the CPU.
			System_Notify(SystemNotification::DISASSEMBLY_AFTERSTEP);
		}
		g_cpuStepCommand.clear();
		steppingCounter++;
	}

	// Update disasm dialog.
	System_Notify(SystemNotification::MEM_VIEW);
	return true;
}

// Free-threaded (hm, possibly except tracing).
void Core_Break(BreakReason reason, u32 relatedAddress) {
	if (coreState != CORE_RUNNING_CPU) {
		ERROR_LOG(Log::CPU, "Core_Break only works in the CORE_RUNNING_CPU state");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(g_stepMutex);
		if (!g_cpuStepCommand.empty() && Core_IsStepping()) {
			// If we're in a failed step that uses a temp breakpoint, we need to be able to override it here.
			switch (g_cpuStepCommand.type) {
			case CPUStepType::Over:
			case CPUStepType::Out:
				// Allow overwriting the command.
				break;
			default:
				ERROR_LOG(Log::CPU, "Core_Break called with a step-command already in progress: %s", BreakReasonToString(g_cpuStepCommand.reason));
				return;
			}
		}

		// Stop the tracer
		mipsTracer.stop_tracing();

		g_breakReason = reason;
		g_cpuStepCommand.type = CPUStepType::None;
		g_cpuStepCommand.reason = reason;
		g_cpuStepCommand.relatedAddr = relatedAddress;
		steppingCounter++;
		_assert_msg_(reason != BreakReason::None, "No reason specified for break");
		Core_UpdateState(CORE_STEPPING_CPU);
	}
	System_Notify(SystemNotification::DEBUG_MODE_CHANGE);
}

// Free-threaded (or at least should be)
void Core_Resume() {
	// If the current PC is on a breakpoint, the user doesn't want to do nothing.
	if (currentMIPS) {
		g_breakpoints.SetSkipFirst(currentMIPS->pc);
	}

	// Handle resuming from GE.
	if (coreState == CORE_STEPPING_GE) {
		coreState = CORE_RUNNING_GE;
		return;
	}

	// Clear the exception if we resume.
	Core_ResetException();
	coreState = CORE_RUNNING_CPU;
	g_breakReason = BreakReason::None;
	System_Notify(SystemNotification::DEBUG_MODE_CHANGE);
}

// Should be called from the EmuThread.
bool Core_NextFrame() {
	CoreState coreState = ::coreState;

	_dbg_assert_(coreState != CORE_STEPPING_GE && coreState != CORE_RUNNING_GE);

	if (coreState == CORE_RUNNING_CPU) {
		::coreState = CORE_NEXTFRAME;
		return true;
	} else if (coreState == CORE_STEPPING_CPU) {
		// All good, just stepping through so no need to switch to the NextFrame coreState though, that'd
		// just lose our stepping state.
		INFO_LOG(Log::System, "Reached end-of-frame while stepping the CPU (this is ok)");
		return true;
	} else {
		ERROR_LOG(Log::System, "Core_NextFrame called with wrong core state %s", CoreStateToString(coreState));
		return false;
	}
}

int Core_GetSteppingCounter() {
	return steppingCounter;
}

SteppingReason Core_GetSteppingReason() {
	SteppingReason r;
	std::lock_guard<std::mutex> lock(g_stepMutex);
	if (!g_cpuStepCommand.empty()) {
		r.reason = g_cpuStepCommand.reason;
		r.relatedAddress = g_cpuStepCommand.relatedAddr;
	}
	return r;
}

const char *ExceptionTypeAsString(MIPSExceptionType type) {
	switch (type) {
	case MIPSExceptionType::MEMORY: return "Invalid Memory Access";
	case MIPSExceptionType::BREAK: return "Break";
	case MIPSExceptionType::BAD_EXEC_ADDR: return "Bad Execution Address";
	default: return "N/A";
	}
}

const char *MemoryExceptionTypeAsString(MemoryExceptionType type) {
	switch (type) {
	case MemoryExceptionType::UNKNOWN: return "Unknown";
	case MemoryExceptionType::READ_WORD: return "Read Word";
	case MemoryExceptionType::WRITE_WORD: return "Write Word";
	case MemoryExceptionType::READ_BLOCK: return "Read Block";
	case MemoryExceptionType::WRITE_BLOCK: return "Read/Write Block";
	case MemoryExceptionType::ALIGNMENT: return "Alignment";
	default:
		return "N/A";
	}
}

const char *ExecExceptionTypeAsString(ExecExceptionType type) {
	switch (type) {
	case ExecExceptionType::JUMP: return "CPU Jump";
	case ExecExceptionType::THREAD: return "Thread switch";
	default:
		return "N/A";
	}
}

void Core_MemoryException(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type) {
	const char *desc = MemoryExceptionTypeAsString(type);
	// In jit, we only flush PC when bIgnoreBadMemAccess is off.
	if ((g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR) && g_Config.bIgnoreBadMemAccess) {
		WARN_LOG(Log::MemMap, "%s: Invalid access at %08x (size %08x)", desc, address, accessSize);
	} else {
		WARN_LOG(Log::MemMap, "%s: Invalid access at %08x (size %08x) PC %08x LR %08x", desc, address, accessSize, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
	}

	if (!g_Config.bIgnoreBadMemAccess) {
		// Try to fetch a call stack, to start with.
		std::vector<MIPSStackWalk::StackFrame> stackFrames = WalkCurrentStack(-1);
		std::string stackTrace = FormatStackTrace(stackFrames);
		WARN_LOG(Log::MemMap, "\n%s", stackTrace.c_str());

		MIPSExceptionInfo &e = g_exceptionInfo;
		e = {};
		e.type = MIPSExceptionType::MEMORY;
		e.info.clear();
		e.memory_type = type;
		e.address = address;
		e.accessSize = accessSize;
		e.stackTrace = stackTrace;
		e.pc = pc;
		Core_Break(BreakReason::MemoryException, address);
	}
}

void Core_MemoryExceptionInfo(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type, std::string_view additionalInfo, bool forceReport) {
	const char *desc = MemoryExceptionTypeAsString(type);
	// In jit, we only flush PC when bIgnoreBadMemAccess is off.
	if ((g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR) && g_Config.bIgnoreBadMemAccess) {
		WARN_LOG(Log::MemMap, "%s: Invalid access at %08x (size %08x). %.*s", desc, address, accessSize, (int)additionalInfo.length(), additionalInfo.data());
	} else {
		WARN_LOG(Log::MemMap, "%s: Invalid access at %08x (size %08x) PC %08x LR %08x %.*s", desc, address, accessSize, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA], (int)additionalInfo.length(), additionalInfo.data());
	}

	if (!g_Config.bIgnoreBadMemAccess || forceReport) {
		// Try to fetch a call stack, to start with.
		std::vector<MIPSStackWalk::StackFrame> stackFrames = WalkCurrentStack(-1);
		std::string stackTrace = FormatStackTrace(stackFrames);
		WARN_LOG(Log::MemMap, "\n%s", stackTrace.c_str());

		MIPSExceptionInfo &e = g_exceptionInfo;
		e = {};
		e.type = MIPSExceptionType::MEMORY;
		e.info = additionalInfo;
		e.memory_type = type;
		e.address = address;
		e.accessSize = accessSize;
		e.stackTrace = stackTrace;
		e.pc = pc;
		Core_Break(BreakReason::MemoryException, address);
	}
}

// Can't be ignored
void Core_ExecException(u32 address, u32 pc, ExecExceptionType type) {
	const char *desc = ExecExceptionTypeAsString(type);
	WARN_LOG(Log::MemMap, "%s: Invalid exec address %08x pc=%08x ra=%08x", desc, address, pc, currentMIPS->r[MIPS_REG_RA]);

	MIPSExceptionInfo &e = g_exceptionInfo;
	e = {};
	e.type = MIPSExceptionType::BAD_EXEC_ADDR;
	e.info.clear();
	e.exec_type = type;
	e.address = address;
	e.accessSize = 4;  // size of an instruction
	e.pc = pc;
	// This just records the closest value that could be useful as reference.
	e.ra = currentMIPS->r[MIPS_REG_RA];
	Core_Break(BreakReason::CpuException, address);
}

void Core_BreakException(u32 pc) {
	ERROR_LOG(Log::CPU, "BREAK!");

	MIPSExceptionInfo &e = g_exceptionInfo;
	e = {};
	e.type = MIPSExceptionType::BREAK;
	e.info.clear();
	e.pc = pc;

	if (!g_Config.bIgnoreBadMemAccess) {
		Core_Break(BreakReason::BreakInstruction, currentMIPS->pc);
	}
}

void Core_ResetException() {
	g_exceptionInfo.type = MIPSExceptionType::NONE;
}

const MIPSExceptionInfo &Core_GetExceptionInfo() {
	return g_exceptionInfo;
}
