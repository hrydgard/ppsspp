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

#include <set>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <condition_variable>

#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/Display.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Profiler/Profiler.h"

#include "Common/GraphicsContext.h"
#include "Common/Log.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/MemFault.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HW/Display.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "GPU/Debugger/Stepping.h"
#include "Core/MIPS/MIPSTracer.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include "Windows/InputDevice.h"
#endif

// Step command to execute next
static std::mutex g_stepMutex;
struct StepCommand {
	CPUStepType type;
	int param;
	const char *reason;
	u32 relatedAddr;
	bool empty() const {
		return type == CPUStepType::None;
	}
	void clear() {
		type = CPUStepType::None;
		param = 0;
		reason = "";
		relatedAddr = 0;
	}
};
static StepCommand g_stepCommand;

// This is so that external threads can wait for the CPU to become inactive.
static std::condition_variable m_InactiveCond;
static std::mutex m_hInactiveMutex;

static int steppingCounter = 0;
static std::set<CoreLifecycleFunc> lifecycleFuncs;
static std::set<CoreStopRequestFunc> stopFuncs;

static bool windowHidden = false;
static bool powerSaving = false;

static MIPSExceptionInfo g_exceptionInfo;

void Core_SetGraphicsContext(GraphicsContext *ctx) {
	PSP_CoreParameter().graphicsContext = ctx;
}

void Core_NotifyWindowHidden(bool hidden) {
	windowHidden = hidden;
	// TODO: Wait until we can react?
}

bool Core_IsWindowHidden() {
	return windowHidden;
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

void Core_ListenStopRequest(CoreStopRequestFunc func) {
	stopFuncs.insert(func);
}

void Core_Stop() {
	Core_ResetException();
	Core_UpdateState(CORE_POWERDOWN);
	for (auto func : stopFuncs) {
		func();
	}
}

bool Core_ShouldRunBehind() {
	// Enforce run-behind if ad-hoc connected
	return g_Config.bRunBehindPauseMenu || Core_MustRunBehind();
}

bool Core_MustRunBehind() {
	return __NetAdhocConnected();
}

bool Core_IsStepping() {
	return coreState == CORE_STEPPING || coreState == CORE_POWERDOWN;
}

bool Core_IsActive() {
	return coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME || coreStatePending;
}

bool Core_IsInactive() {
	return coreState != CORE_RUNNING && coreState != CORE_NEXTFRAME && !coreStatePending;
}

void Core_WaitInactive() {
	while (Core_IsActive() && !GPUStepping::IsStepping()) {
		std::unique_lock<std::mutex> guard(m_hInactiveMutex);
		m_InactiveCond.wait(guard);
	}
}

void Core_WaitInactive(int milliseconds) {
	if (Core_IsActive() && !GPUStepping::IsStepping()) {
		std::unique_lock<std::mutex> guard(m_hInactiveMutex);
		m_InactiveCond.wait_for(guard, std::chrono::milliseconds(milliseconds));
	}
}

void Core_SetPowerSaving(bool mode) {
	powerSaving = mode;
}

bool Core_GetPowerSaving() {
	return powerSaving;
}

static bool IsWindowSmall(int pixelWidth, int pixelHeight) {
	// Can't take this from config as it will not be set if windows is maximized.
	int w = (int)(pixelWidth * g_display.dpi_scale_x);
	int h = (int)(pixelHeight * g_display.dpi_scale_y);
	return g_Config.IsPortrait() ? (h < 480 + 80) : (w < 480 + 80);
}

// TODO: Feels like this belongs elsewhere.
bool UpdateScreenScale(int width, int height) {
	bool smallWindow;

	float g_logical_dpi = System_GetPropertyFloat(SYSPROP_DISPLAY_LOGICAL_DPI);
	g_display.dpi = System_GetPropertyFloat(SYSPROP_DISPLAY_DPI);

	if (g_display.dpi < 0.0f) {
		g_display.dpi = 96.0f;
	}
	if (g_logical_dpi < 0.0f) {
		g_logical_dpi = 96.0f;
	}

	g_display.dpi_scale_x = g_logical_dpi / g_display.dpi;
	g_display.dpi_scale_y = g_logical_dpi / g_display.dpi;
	g_display.dpi_scale_real_x = g_display.dpi_scale_x;
	g_display.dpi_scale_real_y = g_display.dpi_scale_y;

	smallWindow = IsWindowSmall(width, height);
	if (smallWindow) {
		g_display.dpi /= 2.0f;
		g_display.dpi_scale_x *= 2.0f;
		g_display.dpi_scale_y *= 2.0f;
	}
	g_display.pixel_in_dps_x = 1.0f / g_display.dpi_scale_x;
	g_display.pixel_in_dps_y = 1.0f / g_display.dpi_scale_y;

	int new_dp_xres = (int)(width * g_display.dpi_scale_x);
	int new_dp_yres = (int)(height * g_display.dpi_scale_y);

	bool dp_changed = new_dp_xres != g_display.dp_xres || new_dp_yres != g_display.dp_yres;
	bool px_changed = g_display.pixel_xres != width || g_display.pixel_yres != height;

	if (dp_changed || px_changed) {
		g_display.dp_xres = new_dp_xres;
		g_display.dp_yres = new_dp_yres;
		g_display.pixel_xres = width;
		g_display.pixel_yres = height;
		NativeResized();
		return true;
	}
	return false;
}

// Used by Windows, SDL, Qt.
void UpdateRunLoop(GraphicsContext *ctx) {
	NativeFrame(ctx);
	if (windowHidden && g_Config.bPauseWhenMinimized) {
		sleep_ms(16, "window-hidden");
		return;
	}
}

// Note: not used on Android.
void Core_RunLoop(GraphicsContext *ctx) {
	if (windowHidden && g_Config.bPauseWhenMinimized) {
		sleep_ms(16, "window-hidden");
		return;
	}

	NativeFrame(ctx);
}

bool Core_RequestSingleStep(CPUStepType type, int stepSize) {
	std::lock_guard<std::mutex> guard(g_stepMutex);
	if (g_stepCommand.type != CPUStepType::None) {
		ERROR_LOG(Log::CPU, "Can't submit two steps in one frame");
		return false;
	}
	g_stepCommand = { type, stepSize };
	return true;
}

// See comment in header.
// Handles more advanced step types (used by the debugger).
// stepSize is to support stepping through compound instructions like fused lui+ladd (li).
// Yes, our disassembler does support those.
// Doesn't return the new address, as that's just mips->getPC().
// Internal use.
static void Core_PerformStep(MIPSDebugInterface *cpu, CPUStepType stepType, int stepSize) {
	switch (stepType) {
	case CPUStepType::Into:
	{
		u32 currentPc = cpu->GetPC();
		u32 newAddress = currentPc + stepSize;
		// If the current PC is on a breakpoint, the user still wants the step to happen.
		CBreakPoints::SetSkipFirst(currentPc);
		for (int i = 0; i < (int)(newAddress - currentPc) / 4; i++) {
			currentMIPS->SingleStep();
		}
		return;
	}
	case CPUStepType::Over:
	{
		u32 currentPc = cpu->GetPC();
		u32 breakpointAddress = currentPc + stepSize;

		CBreakPoints::SetSkipFirst(currentPc);

		MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(cpu, cpu->GetPC());
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
		}

		CBreakPoints::AddBreakPoint(breakpointAddress, true);
		Core_Resume();
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

		// If the current PC is on a breakpoint, the user doesn't want to do nothing.
		CBreakPoints::SetSkipFirst(currentMIPS->pc);
		CBreakPoints::AddBreakPoint(breakpointAddress, true);
		Core_Resume();
		break;
	}
	default:
		// Not yet implemented
		break;
	}
}

void Core_ProcessStepping(MIPSDebugInterface *cpu) {
	coreStatePending = false;

	// Check if there's any pending save state actions.
	SaveState::Process();
	if (coreState != CORE_STEPPING) {
		return;
	}

	// Or any GPU actions.
	GPUStepping::SingleStep();

	// We're not inside jit now, so it's safe to clear the breakpoints.
	static int lastSteppingCounter = -1;
	if (lastSteppingCounter != steppingCounter) {
		CBreakPoints::ClearTemporaryBreakPoints();
		System_Notify(SystemNotification::DISASSEMBLY_AFTERSTEP);
		System_Notify(SystemNotification::MEM_VIEW);
		lastSteppingCounter = steppingCounter;
	}

	// Need to check inside the lock to avoid races.
	std::lock_guard<std::mutex> guard(g_stepMutex);

	if (coreState != CORE_STEPPING || g_stepCommand.empty()) {
		return;
	}

	Core_ResetException();

	if (!g_stepCommand.empty()) {
		Core_PerformStep(cpu, g_stepCommand.type, g_stepCommand.param);
		if (g_stepCommand.type == CPUStepType::Into) {
			// We're already done. The other step types will resume the CPU.
			System_Notify(SystemNotification::DISASSEMBLY_AFTERSTEP);
		}
		g_stepCommand.clear();
		steppingCounter++;
	}

	// Update disasm dialog.
	System_Notify(SystemNotification::MEM_VIEW);
}

// Many platforms, like Android, do not call this function but handle things on their own.
// Instead they simply call NativeFrame directly.
bool Core_Run(GraphicsContext *ctx) {
	System_Notify(SystemNotification::DISASSEMBLY);
	while (true) {
		if (GetUIState() != UISTATE_INGAME) {
			if (GetUIState() == UISTATE_EXIT) {
				// Not sure why we do a final frame here?
				NativeFrame(ctx);
				return false;
			}
			Core_RunLoop(ctx);
			continue;
		}

		switch (coreState) {
		case CORE_RUNNING:
		case CORE_STEPPING:
			// enter a fast runloop
			Core_RunLoop(ctx);
			if (coreState == CORE_POWERDOWN) {
				return true;
			}
			break;

		case CORE_POWERUP:
		case CORE_POWERDOWN:
		case CORE_BOOT_ERROR:
		case CORE_RUNTIME_ERROR:
			// Exit loop!!
			return true;

		case CORE_NEXTFRAME:
			return true;
		}
	}
}

// Free-threaded (hm, possibly except tracing).
void Core_Break(const char *reason, u32 relatedAddress) {
	// Stop the tracer
	{
		std::lock_guard<std::mutex> lock(g_stepMutex);
		if (!g_stepCommand.empty()) {
			// Already broke.
			ERROR_LOG(Log::CPU, "Core_Break called with a break already in progress: %s", g_stepCommand.reason);
			return;
		}
		mipsTracer.stop_tracing();
		g_stepCommand.reason = reason;
		g_stepCommand.relatedAddr = relatedAddress;
		steppingCounter++;
		_assert_msg_(reason != nullptr, "No reason specified for break");
		Core_UpdateState(CORE_STEPPING);
	}
	System_Notify(SystemNotification::DEBUG_MODE_CHANGE);
}

// Free-threaded (or at least should be)
void Core_Resume() {
	// Clear the exception if we resume.
	Core_ResetException();
	coreState = CORE_RUNNING;
	System_Notify(SystemNotification::DEBUG_MODE_CHANGE);
}

// Should be called from the EmuThread.
bool Core_NextFrame() {
	if (coreState == CORE_RUNNING) {
		coreState = CORE_NEXTFRAME;
		return true;
	} else {
		return false;
	}
}

int Core_GetSteppingCounter() {
	return steppingCounter;
}

SteppingReason Core_GetSteppingReason() {
	SteppingReason r;
	std::lock_guard<std::mutex> lock(g_stepMutex);
	if (!g_stepCommand.empty()) {
		r.reason = g_stepCommand.reason;
		r.relatedAddress = g_stepCommand.relatedAddr;
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
		Core_Break("memory.exception", address);
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
		Core_Break("memory.exception", address);
	}
}

// Can't be ignored
void Core_ExecException(u32 address, u32 pc, ExecExceptionType type) {
	const char *desc = ExecExceptionTypeAsString(type);
	WARN_LOG(Log::MemMap, "%s: Invalid exec address %08x PC %08x LR %08x", desc, address, pc, currentMIPS->r[MIPS_REG_RA]);

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
	Core_Break("cpu.exception", address);
}

void Core_BreakException(u32 pc) {
	ERROR_LOG(Log::CPU, "BREAK!");

	MIPSExceptionInfo &e = g_exceptionInfo;
	e = {};
	e.type = MIPSExceptionType::BREAK;
	e.info.clear();
	e.pc = pc;

	if (!g_Config.bIgnoreBadMemAccess) {
		Core_Break("cpu.breakInstruction", currentMIPS->pc);
	}
}

void Core_ResetException() {
	g_exceptionInfo.type = MIPSExceptionType::NONE;
}

const MIPSExceptionInfo &Core_GetExceptionInfo() {
	return g_exceptionInfo;
}
