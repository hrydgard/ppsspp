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

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <memory>
#include <set>
#include <thread>
#include <vector>
#include <condition_variable>

#include "Common/System/System.h"
#include "Common/Profiler/Profiler.h"

#include "Common/GPU/GraphicsContext.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/MemFault.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/WebSocket.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/MIPS/MIPSTracer.h"
#include "Core/CoreTiming.h"

#include "GPU/Debugger/Stepping.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

// Step command to execute next
static std::mutex g_stepMutex;

struct CPUStepCommand {
	CPUStepType type;
	BreakReason reason;
	u32 relatedAddr;
	bool empty() const {
		return type == CPUStepType::None;
	}
	void clear() {
		type = CPUStepType::None;
		// Deliberately NOT resetting reason/relatedAddr here: they describe why we're
		// currently paused (not whether a step is pending), and for CPUStepType::Into this
		// clear() runs immediately after finishing the step, before SteppingBroadcaster gets
		// a chance to read them via Core_GetSteppingReason(). Over/Out/Frame instead call
		// Core_Resume() before reaching here, so a stale reason left behind is harmless -
		// it'll be overwritten by the next Core_Break()/Core_RequestCPUStep() before anything
		// re-enters stepping.
	}
};

// The step currently being carried out. Also doubles as the record of why we're stopped
// (reason/relatedAddr), which is why clear() only resets the type - see the comment above.
static CPUStepCommand g_cpuStepCommand;

// Steps asked for while one is already in flight. Only one step can be performed per pass through
// Core_ProcessStepping(), i.e. roughly one per host frame, and a client that fires several in
// quick succession (a script, or someone leaning on the step key) used to have all but the first
// rejected outright with "Can't submit two steps in one host frame" and no step performed - so it
// had to notice and retry. They queue up instead now.
//
// Deliberately not cleared by Core_Break(): completing a step-over or step-out *goes through*
// Core_Break() (their temporary breakpoint is what stops us), so dropping the queue there would
// throw away the rest of any sequence after its first entry.
static std::deque<CPUStepCommand> g_cpuStepQueue;
// Enough for any plausible burst. Past this something is wrong - a client in a loop, say - and
// silently growing the queue would just defer the problem, so it's reported instead.
static constexpr size_t MAX_PENDING_STEPS = 8;

// Task queue for Core_RunOnCPUThread(), see Core.h for the rationale. Drained from Core_RunLoopUntil()
// below, so at least once per call to it (i.e. about once per host frame) even while the CPU is fully
// running, and continuously (in a tight spin) while it's stepping/paused.
struct CPUThreadTask {
	std::function<void()> func;
	bool done = false;
};
static std::mutex g_cpuQueueMutex;
static std::condition_variable g_cpuQueueCond;
static std::vector<std::shared_ptr<CPUThreadTask>> g_cpuQueue;
static std::once_flag g_cpuThreadIdOnce;
static std::thread::id g_cpuThreadId;
// Published via release/acquire around g_cpuThreadIdOnce, so it's safe to check from other threads
// without taking g_cpuQueueMutex - g_cpuThreadId itself never changes once this becomes true.
static std::atomic<bool> g_cpuThreadIdValid{ false };

// Bumped by anything that gives a paused CPU thread something to do - a queued task, a step
// request, a resume. Core_IdleWaitWhileStepping() blocks on this rather than spinning; see there.
static std::mutex g_idleMutex;
static std::condition_variable g_idleCond;
static u64 g_idleWakeCounter = 0;

void Core_WakeIdleCPUThread() {
	{
		std::lock_guard<std::mutex> guard(g_idleMutex);
		g_idleWakeCounter++;
	}
	g_idleCond.notify_all();
}

// Called on the CPU thread when it's stopped and has nothing queued. Without this the whole
// pause is a busy-wait: Core_ProcessStepping() returns immediately when idle, so Core_RunLoopUntil()
// returns immediately, so whatever drives it goes straight back round. headless does that with no
// frame pacing whatsoever, which measured at a full core burned for as long as the CPU stayed
// stopped - and since a debugger session is stopped most of the time, it dominates any profile
// taken of one (as sync overhead around Core_RunOnCPUThread, which is simply the hottest thing in
// the spin).
//
// The wait is deliberately short rather than indefinite. Callers do real work after we return -
// most importantly the app build renders the ImGui debugger from this same thread - so this must
// bound how long a paused frame takes, not replace the frame loop. Anything that actually wants
// the CPU thread also calls Core_WakeIdleCPUThread(), so the timeout is only a backstop for state
// changed without one, never the normal path to noticing work.
static void Core_IdleWaitWhileStepping() {
	constexpr auto kMaxIdleWait = std::chrono::milliseconds(2);
	{
		// Don't sleep if something is already waiting on us.
		std::lock_guard<std::mutex> guard(g_cpuQueueMutex);
		if (!g_cpuQueue.empty())
			return;
	}
	std::unique_lock<std::mutex> guard(g_idleMutex);
	const u64 seen = g_idleWakeCounter;
	g_idleCond.wait_for(guard, kMaxIdleWait, [&] { return g_idleWakeCounter != seen; });
}

void Core_RunOnCPUThread(std::function<void()> func) {
	if (g_cpuThreadIdValid.load(std::memory_order_acquire) && std::this_thread::get_id() == g_cpuThreadId) {
		// Already on the CPU thread (or called before it's ever run) - just do it now, avoids deadlock.
		func();
		return;
	}

	auto task = std::make_shared<CPUThreadTask>();
	task->func = std::move(func);

	std::unique_lock<std::mutex> guard(g_cpuQueueMutex);
	g_cpuQueue.push_back(task);
	if (!System_GetPropertyBool(SystemProperty::SYSPROP_IS_HEADLESS)) {
		// Do this with g_cpuQueueMutex held: the CPU thread checks that queue before it decides to
		// sleep, so waking it after the push (and before we block) can't leave it asleep on a task
		// that's already there.
		Core_WakeIdleCPUThread();
	}
	g_cpuQueueCond.wait(guard, [&] { return task->done; });
}

// Called from the CPU thread only.
void Core_ProcessCPUQueue() {
	std::call_once(g_cpuThreadIdOnce, [] {
		g_cpuThreadId = std::this_thread::get_id();
		g_cpuThreadIdValid.store(true, std::memory_order_release);
	});

	// Piggybacking on the one function that's reliably called on the CPU thread both in game
	// (Core_RunLoopUntil) and at the menu (NativeFrame) - see WebSocketDebuggerTick().
	WebSocketDebuggerTick();

	std::vector<std::shared_ptr<CPUThreadTask>> tasks;
	{
		std::lock_guard<std::mutex> guard(g_cpuQueueMutex);
		if (g_cpuQueue.empty())
			return;
		tasks = std::move(g_cpuQueue);
		g_cpuQueue.clear();
	}

	for (auto &task : tasks)
		task->func();

	{
		std::lock_guard<std::mutex> guard(g_cpuQueueMutex);
		for (auto &task : tasks)
			task->done = true;
	}
	g_cpuQueueCond.notify_all();
}

// See Core.h. Recursive because Memory::Shutdown() nests inside CPU_Shutdown()'s acquire.
static std::recursive_mutex g_shutdownLock;

CoreShutdownLock::CoreShutdownLock() {
	g_shutdownLock.lock();
}

CoreShutdownLock::~CoreShutdownLock() {
	g_shutdownLock.unlock();
}

CoreShutdownLock Core_LockAgainstShutdown() {
	return CoreShutdownLock();
}

// See Core.h for the rationale. Held by NativeFrame() (in NativeApp.cpp) around the span where it
// actually touches CPU-thread-owned debugger state.
std::mutex g_frameMutex;

// This is so that external threads can wait for the CPU to become inactive.
static std::condition_variable m_InactiveCond;
static std::mutex m_hInactiveMutex;

static int steppingCounter = 0;
static std::set<CoreLifecycleFunc> lifecycleFuncs;

// This can be read and written from ANYWHERE.
volatile CoreState coreState = CORE_POWERDOWN;
CoreState preGeCoreState = CORE_POWERDOWN;
// If true, core state has been changed, but JIT has probably not noticed yet.
volatile bool coreStatePending = false;

static bool powerSaving = false;
static bool g_breakAfterFrame = false;
static BreakReason g_breakReason = BreakReason::None;
// Detail about the breakpoint that caused the current break, if it was one. Guarded by g_stepMutex
// alongside g_cpuStepCommand, which is what it belongs to.
static BreakpointHit g_breakHit;

static MIPSExceptionInfo g_exceptionInfo;

// This is called on EmuThread before RunLoop.
static bool Core_ProcessStepping(MIPSDebugInterface *cpu);

static std::function<void(std::string_view)> g_debugOutputListener;
static std::function<void(const DebugScreenshotDesc &)> g_debugScreenshotListener;

void Core_RegisterDebugOutputListeners(std::function<void(std::string_view)> listener, std::function<void(const DebugScreenshotDesc &)> screenshotListener) {
	g_debugOutputListener = std::move(listener);
	g_debugScreenshotListener = std::move(screenshotListener);
}

void Core_SendDebugOutput(LogLevel level, std::string_view string) {
	if (g_debugOutputListener) {
		g_debugOutputListener(string);
	} else {
		GENERIC_LOG(Log::sceIo, level, "%.*s", STR_VIEW(string));
	}
}

void Core_SendDebugScreenshot(const DebugScreenshotDesc &desc) {
	if (g_debugScreenshotListener) {
		g_debugScreenshotListener(desc);
	}
}

BreakReason Core_BreakReason() {
	return g_breakReason;
}

const char *CoreStateToString(CoreState state) {
	switch (state) {
	case CORE_RUNNING_CPU: return "RUNNING_CPU";
	case CORE_NEXTFRAME: return "NEXTFRAME";
	case CORE_STEPPING_CPU: return "STEPPING_CPU";
	case CORE_POWERDOWN: return "POWERDOWN";
	case CORE_RUNTIME_ERROR: return "RUNTIME_ERROR";
	case CORE_STEPPING_GE: return "STEPPING_GE";
	case CORE_RUNNING_GE: return "RUNNING_GE";
	default: return "N/A";
	}
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
	case BreakReason::RegBreakpoint: return "cpu.regBreakpoint";
	case BreakReason::MemoryAccess: return "memory.access";  // ???
	case BreakReason::JitBranchDebug: return "jit.branchdebug";
	case BreakReason::RABreak: return "ra.break";
	case BreakReason::BreakOnBoot: return "ui.boot";
	case BreakReason::AddBreakpoint: return "cpu.breakpoint.add";
	case BreakReason::FrameAdvance: return "ui.frameAdvance";
	case BreakReason::UIPause: return "ui.pause";
	case BreakReason::HLEDebugBreak: return "hle.step";
	case BreakReason::RunUntilTime: return "cpu.runUntilTime";
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
		// A step queued against the game that just went away must not run against the new one.
		std::lock_guard<std::mutex> guard(g_stepMutex);
		g_cpuStepQueue.clear();
		g_cpuStepCommand.clear();
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
	const CoreState state = coreState;
	if ((state == CORE_RUNNING_CPU || state == CORE_NEXTFRAME) && newState != CORE_RUNNING_CPU)
		coreStatePending = true;
	coreState = newState;
}

bool Core_IsStepping() {
	const CoreState state = coreState;
	return state == CORE_STEPPING_CPU || state == CORE_STEPPING_GE || state == CORE_POWERDOWN;
}

bool Core_IsActive() {
	const CoreState state = coreState;
	return state == CORE_RUNNING_CPU || state == CORE_NEXTFRAME || coreStatePending;
}

bool Core_IsInactive() {
	const CoreState state = coreState;
	return state != CORE_RUNNING_CPU && state != CORE_NEXTFRAME && !coreStatePending;
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
		m_InactiveCond.wait_for(guard, std::chrono::seconds(1));
	}
}

void Core_SetPowerSaving(bool mode) {
	powerSaving = mode;
}

bool Core_GetPowerSaving() {
	return powerSaving;
}

void Core_ReenterDispatcher() {
	if (coreState == CORE_RUNNING_CPU) {
		// This will flip back into CORE_RUNNING_CPU.
		coreState = CORE_REENTER_DISPATCH;
	}
}

void Core_RunLoopUntil(u64 globalticks) {
	while (true) {
		// Drain any functions queued up by Core_RunOnCPUThread() from other threads. Doing this at the
		// top of this loop means it's reached at least once per call (i.e. about once per host frame)
		// whether the CPU is running or not.
		Core_ProcessCPUQueue();
		g_breakpoints.Frame();
		switch (coreState) {
		case CORE_POWERDOWN:
		case CORE_RUNTIME_ERROR:
		case CORE_NEXTFRAME:
			return;
		case CORE_STEPPING_CPU:
		case CORE_STEPPING_GE:
		{
			CoreState preState = coreState;
			if (Core_ProcessStepping(currentDebugMIPS)) {
				if (coreState == CORE_REENTER_DISPATCH) {
					coreState = preState;
				}
				// Still stopped with nothing pending, so block briefly instead of handing straight
				// back to a caller that will just call us again - see Core_IdleWaitWhileStepping().
				if (coreState == CORE_STEPPING_CPU || coreState == CORE_STEPPING_GE) {
					Core_IdleWaitWhileStepping();
				}
				return;
			}
			break;
		}
		case CORE_RUNNING_CPU:
			mipsr4k.RunLoopUntil(globalticks);
			if (coreState == CORE_RUNNING_CPU) {
				// If we are still running, we must have reached the end of a frame.
				coreState = CORE_NEXTFRAME;
			} else if (coreState == CORE_REENTER_DISPATCH) {
				// Back to running right away.
				coreState = CORE_RUNNING_CPU;
			}
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

			case DLResult::Error: // We should elegantly report the error somehow, or I guess ignore it.
			case DLResult::Done: // Done executing for now
				hleFinishSyscallAfterGe();
				coreState = preGeCoreState;
				break;

			default:
				// Not a valid return value.
				_dbg_assert_(false);
				break;
			}
			break;
		case CORE_REENTER_DISPATCH:
			// Resume
			coreState = CORE_RUNNING_CPU;
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

bool Core_RequestCPUStep(CPUStepType type) {
	std::lock_guard<std::mutex> guard(g_stepMutex);
	if (g_cpuStepQueue.size() >= MAX_PENDING_STEPS) {
		ERROR_LOG(Log::CPU, "Too many steps queued (%d), dropping this one", (int)g_cpuStepQueue.size());
		return false;
	}
	BreakReason reason = type == CPUStepType::Into ? BreakReason::DebugStepInto : BreakReason::DebugStep;
	g_cpuStepQueue.push_back({ type, reason, 0 });
	Core_WakeIdleCPUThread();
	return true;
}

// Handles more advanced step types (used by the debugger).
// stepSize is always in instructions (4 bytes each), never bytes.
// Doesn't return the new address, as that's just mips->getPC().
// Internal use.
static void Core_PerformCPUStep(MIPSDebugInterface *cpu, CPUStepType stepType) {
	switch (stepType) {
	case CPUStepType::Into:
	{
		u32 currentPc = cpu->GetPC();
		// If the current PC is on a breakpoint, the user still wants the step to happen.
		g_breakpoints.SetSkipFirst(currentPc);
		currentMIPS->SingleStep();
		CoreTiming::Advance(currentMIPS);
		break;
	}
	case CPUStepType::Over:
	{
		u32 currentPc = cpu->GetPC();

		g_breakpoints.SetSkipFirst(currentPc);
		MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(cpu, cpu->GetPC());

		// TODO: Doing a step over in a delay slot is a bit .. unclear. Maybe just do a single step.

		if (info.isBranch) {
			u32 breakpointAddress = currentPc + 4;
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
			g_breakpoints.SetTempBreakPoint(breakpointAddress);
			Core_Resume();
		} else {
			// If not a branch, just do a simple single-step, no point in involving the breakpoint machinery.
			currentMIPS->SingleStep();
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

		g_breakpoints.SetTempBreakPoint(breakpointAddress);
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
	case CORE_REENTER_DISPATCH:
		_dbg_assert_(false);
		return true;
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
		System_Notify(SystemNotification::DISASSEMBLY_AFTERSTEP);
		System_Notify(SystemNotification::MEM_VIEW);
		lastSteppingCounter = steppingCounter;
	}

	// Need to check inside the lock to avoid races.
	std::lock_guard<std::mutex> guard(g_stepMutex);

	if (coreState != CORE_STEPPING_CPU) {
		return true;
	}
	// Take the next queued step, if nothing is in flight already.
	if (g_cpuStepCommand.empty() && !g_cpuStepQueue.empty()) {
		g_cpuStepCommand = g_cpuStepQueue.front();
		g_cpuStepQueue.pop_front();
	}
	if (g_cpuStepCommand.empty()) {
		return true;
	}

	Core_ResetException();

	if (!g_cpuStepCommand.empty()) {
		Core_PerformCPUStep(cpu, g_cpuStepCommand.type);
		g_breakReason = g_cpuStepCommand.reason;
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
void Core_Break(BreakReason reason, u32 relatedAddress, const BreakpointHit *hit) {
	const CoreState state = coreState;
	if (state != CORE_RUNNING_CPU) {
		if (state == CORE_STEPPING_CPU) {
			// Already stepping.
			INFO_LOG(Log::CPU, "Core_Break(%s), already in break mode", BreakReasonToString(reason));
			return;
		}
		WARN_LOG(Log::CPU, "Core_Break(%s) only works in the CORE_RUNNING_CPU state (was in state %s)", BreakReasonToString(reason), CoreStateToString(state));
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
				ERROR_LOG(Log::CPU, "Core_Break(%s) called with a step-command already in progress", BreakReasonToString(g_cpuStepCommand.reason));
				return;
			}
		}

		// Stop the tracer
		mipsTracer.stop_tracing();

		// Execution stopped, so whatever step-over/step-out/run-until was in flight is over - either
		// it just completed, or something else (a breakpoint, a memcheck, the user hitting pause)
		// got there first. Either way its one-shot breakpoint must not stay armed, or it'd fire
		// later at an address nobody is waiting for anymore. Same as gdb dropping its step-resume
		// breakpoint, or lldb discarding the thread plan, on any stop.
		g_breakpoints.ClearTempBreakPoint();

		// Same reasoning for a cpu.runUntilTime deadline - it belonged to the run that just ended.
		CoreTiming::SetBreakDeadlineUs(0);

		g_breakReason = reason;
		// Cleared rather than left alone when there's no hit, so the detail from an earlier
		// breakpoint can't be reported against, say, the user pressing pause afterwards.
		if (hit)
			g_breakHit = *hit;
		else
			g_breakHit = BreakpointHit{};
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
		Core_WakeIdleCPUThread();
		return;
	}

	// Clear the exception if we resume.
	Core_ResetException();
	coreState = CORE_RUNNING_CPU;
	g_breakReason = BreakReason::None;
	Core_WakeIdleCPUThread();
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
	SteppingReason r{};
	std::lock_guard<std::mutex> lock(g_stepMutex);
	// Deliberately not gated on g_cpuStepCommand.empty(): that's true whenever there's no
	// pending step *type* to execute, which is also the normal state right after Core_Break()
	// records a reason (it sets type = CPUStepType::None on purpose - there's no step operation
	// to perform, just a pause). Gating on empty() here used to throw the reason away in
	// exactly that case, i.e. for every breakpoint/exception/savestate-load/etc break, which
	// covers the vast majority of stepping events. .reason is already None whenever there's
	// genuinely nothing to report.
	r.reason = g_cpuStepCommand.reason;
	r.relatedAddress = g_cpuStepCommand.relatedAddr;
	r.hit = g_breakHit;
	return r;
}

const char *ExceptionTypeAsString(MIPSExceptionType type) {
	switch (type) {
	case MIPSExceptionType::MEMORY: return "Invalid Memory Access";
	case MIPSExceptionType::BREAK: return "Break";
	case MIPSExceptionType::BAD_EXEC_ADDR: return "Bad Execution Address";
	case MIPSExceptionType::FPU: return "FPU Exception";
	default: return "N/A";
	}
}

const char *MemoryExceptionTypeAsString(MemoryExceptionType type) {
	switch (type) {
	case MemoryExceptionType::UNKNOWN: return "Unknown";
	case MemoryExceptionType::READ_WORD: return "Read Word";
	case MemoryExceptionType::READ_BLOCK: return "Read Block";
	case MemoryExceptionType::WRITE_WORD: return "Write Word";
	case MemoryExceptionType::WRITE_BLOCK: return "Read/Write Block";
	case MemoryExceptionType::HLE_READ: return "HLE Read";
	case MemoryExceptionType::HLE_WRITE: return "HLE Write";
	case MemoryExceptionType::ALIGNMENT: return "Alignment";
	default:
		return "N/A";
	}
}

const char *ExecExceptionTypeAsString(ExecExceptionType type) {
	switch (type) {
	case ExecExceptionType::JUMP: return "CPU Jump";
	case ExecExceptionType::THREAD: return "Thread switch";
	case ExecExceptionType::PERM: return "Kernel permission";
	case ExecExceptionType::ILLEGAL: return "Illegal instruction";   // or unknown, but I think we have all now.
	default:
		return "N/A";
	}
}

const char *FPUExceptionTypeAsString(FPUExceptionType type) {
	switch (type) {
	case FPUExceptionType::DIVIDE_BY_ZERO: return "Divide by zero";
	default:
		return "N/A";
	}
}

static ExceptionAction ResolveExceptionAction(ExceptionAction action) {
	if (action == ExceptionAction::Default) {
		return g_Config.bIgnoreBadMemAccess ? ExceptionAction::Ignore : ExceptionAction::Break;
	}
	return action;
}

// Looks up which loaded module (and section) an address falls in, formatted for appending
// straight after an address in a log line, e.g. " [EBOOT.BIN.text+1234]". Empty if no match.
static std::string ModuleAddressSuffix(u32 address) {
	char desc[96];
	if (DescribeModuleAddress(address, desc, sizeof(desc))) {
		return std::string(" [") + desc + "]";
	} else {
		return std::string();
	}
}

void Core_MemoryException(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type, std::string_view additionalInfo) {
	// In jit, we only flush PC when bIgnoreBadMemAccess is off.
	char pcDetails[128];
	pcDetails[0] = 0;
	switch ((CPUCore)g_Config.iCpuCore) {
	case CPUCore::INTERPRETER:
		snprintf(pcDetails, sizeof(pcDetails), "Interpreter: PC %08x%s RA %08x%s",
			currentMIPS->pc, ModuleAddressSuffix(currentMIPS->pc).c_str(),
			currentMIPS->r[MIPS_REG_RA], ModuleAddressSuffix(currentMIPS->r[MIPS_REG_RA]).c_str());
		break;
	case CPUCore::JIT:
		snprintf(pcDetails, sizeof(pcDetails), "JIT: (PC approximate)=%08x%s", pc, ModuleAddressSuffix(pc).c_str());
		break;
	case CPUCore::JIT_IR:
		snprintf(pcDetails, sizeof(pcDetails), "JIT_IR: (PC approximate)=%08x%s", pc, ModuleAddressSuffix(pc).c_str());
		break;
	case CPUCore::IR_INTERPRETER:
		snprintf(pcDetails, sizeof(pcDetails), "IR_INTERPRETER: (PC approximate)=%08x%s", pc, ModuleAddressSuffix(pc).c_str());
		break;
	default:
		break;
	}

	const std::string addressSuffix = ModuleAddressSuffix(address);

	ExceptionAction action;
	switch (type) {
	case MemoryExceptionType::WRITE_WORD:
	case MemoryExceptionType::WRITE_BLOCK:
		action = ResolveExceptionAction((ExceptionAction)g_Config.iExceptionActionMemWrite);
		break;
	case MemoryExceptionType::READ_WORD:
	case MemoryExceptionType::READ_BLOCK:
	default:
		action = ResolveExceptionAction((ExceptionAction)g_Config.iExceptionActionMemRead);
		break;
	}

	const char *desc = MemoryExceptionTypeAsString(type);
	char msg[512];
	snprintf(msg, sizeof(msg), "%s: SIGSEGV at %08x%s (size: %d bytes) %s\nHost:%.*s", desc, address, addressSuffix.c_str(), accessSize, pcDetails, STR_VIEW(additionalInfo));
	if (action == ExceptionAction::Ignore) {
		Core_SendDebugOutput(LogLevel::LWARNING, msg);
		return;
	}
	const std::string stackTrace = FormatStackTrace(WalkCurrentStack(-1));
	// Do the most detailed logging we can.
	Core_SendDebugOutput(LogLevel::LERROR, StringFromFormat("%sMIPS call stack:\n%s", msg, stackTrace.c_str()));
	if (action == ExceptionAction::Break) {
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

void Core_MemoryExceptionHLE(MIPSState *mips, u32 address, u32 accessSize, MemoryExceptionType type) {
	ExceptionAction action;
	switch (type) {
	case MemoryExceptionType::HLE_WRITE:
		action = ResolveExceptionAction((ExceptionAction)g_Config.iExceptionActionMemWrite);
		break;
	case MemoryExceptionType::HLE_READ:
		action = ResolveExceptionAction((ExceptionAction)g_Config.iExceptionActionMemRead);
		break;
	default:
		_dbg_assert_(false);
		action = ExceptionAction::Break;
		break;
	}

	const HLEFunction *func = HLEGetFunctionBeingCalled();
	const char *funcName = func ? func->name : "unknown";

	char args[256] = "";
	if (func) {
		HLEFormatLogArgs(mips, args, sizeof(args), func->argmask);
	}

	const char *extra = "";
	// We do report some unaligned addresses. There are probably more that should report.
	// We try to derive the reason here, though maybe it should be passed in explicitly?
	// TODO: This check should probably be added to regular memory accesses too.
	if (Memory::IsValidAddress(address)) {
		if ((accessSize == 2 || accessSize == 4 || accessSize == 8) && (address & (accessSize - 1))) {
			extra = " (unaligned)";
		} else if (accessSize > 8 && (accessSize & 3)) {
			extra = " (unaligned struct)";
		}
	}

	const u32 pc = mips->pc;
	const char *desc = MemoryExceptionTypeAsString(type);

	char msg[512];
	snprintf(msg, sizeof(msg), "%s: Invalid access %s in %s(%s) at %08x%s (size %08x) PC %08x%s RA %08x%s",
		desc, extra, funcName, args,
		address, ModuleAddressSuffix(address).c_str(), accessSize,
		pc, ModuleAddressSuffix(pc).c_str(),
		mips->r[MIPS_REG_RA], ModuleAddressSuffix(mips->r[MIPS_REG_RA]).c_str());

	if (action == ExceptionAction::Ignore) {
		// Simplest logging and continue.
		Core_SendDebugOutput(LogLevel::LWARNING, msg);
		return;
	}

	const std::string stackTrace = FormatStackTrace(WalkCurrentStack(-1));
	Core_SendDebugOutput(LogLevel::LERROR, StringFromFormat("%s\n%s", msg, stackTrace.c_str()));
	if (action == ExceptionAction::Break) {
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

// Can't be ignored, must break. If JUMP, not sure we can get a meaningful stack trace here (since the PC is invalid).
// address != pc when this is called for a jump instruction. pc then is the source address of the jump.
void Core_ExecException(u32 address, u32 pc, ExecExceptionType type) {
	const char *desc = ExecExceptionTypeAsString(type);

	char pcStr[32] = "(invalid)";
	if (Memory::IsValid4AlignedAddress(pc)) {
		snprintf(pcStr, sizeof(pcStr), "[%08x]", Memory::ReadUnchecked_U32(pc));
	}

	// Each case fills in msg, and a stack trace where one is worth having. Sent once at the end -
	// the cases used to send it themselves *and* fall through to the send below, so every exec
	// exception was reported twice.
	char msg[512];
	std::string stackTrace;
	switch (type) {
	case ExecExceptionType::JUMP:
	{
		snprintf(msg, sizeof(msg), "%s: Invalid jump to %08x%s from PC %08x%s %s RA %08x%s", desc, address, ModuleAddressSuffix(address).c_str(),
			pc, pcStr, ModuleAddressSuffix(pc).c_str(), currentMIPS->r[MIPS_REG_RA], ModuleAddressSuffix(currentMIPS->r[MIPS_REG_RA]).c_str());
		// A jump through a bad pointer is where a stack trace is worth the most - the address it
		// landed on tells you nothing, the callers tell you everything. Execution has already moved
		// to the bad address by the time this is noticed, so a walk from pc finds no function to
		// start from; ra still points into the caller, and that recovers the whole chain.
		std::vector<MIPSStackWalk::StackFrame> frames = WalkCurrentStack(-1);
		if (frames.empty())
			frames = WalkCurrentStack(-1, currentMIPS->r[MIPS_REG_RA]);
		stackTrace = FormatStackTrace(frames);
		break;
	}
	case ExecExceptionType::THREAD:
		snprintf(msg, sizeof(msg), "%s: Invalid thread switch to %08x%s from PC %08x%s RA %08x%s", desc, address, ModuleAddressSuffix(address).c_str(),
			pc, ModuleAddressSuffix(pc).c_str(), currentMIPS->r[MIPS_REG_RA], ModuleAddressSuffix(currentMIPS->r[MIPS_REG_RA]).c_str());
		break;
	case ExecExceptionType::ILLEGAL:
		snprintf(msg, sizeof(msg), "%s: Illegal instruction at %08x%s %s RA %08x%s", desc,
			pc, pcStr, ModuleAddressSuffix(pc).c_str(), currentMIPS->r[MIPS_REG_RA], ModuleAddressSuffix(currentMIPS->r[MIPS_REG_RA]).c_str());
		// For illegal instructions, there might be a useful stack trace.
		stackTrace = FormatStackTrace(WalkCurrentStack(-1));
		break;
	case ExecExceptionType::PERM:
		snprintf(msg, sizeof(msg), "%s: Kernel instruction in user mode at %08x%s %s RA %08x%s", desc,
			pc, pcStr, ModuleAddressSuffix(pc).c_str(), currentMIPS->r[MIPS_REG_RA], ModuleAddressSuffix(currentMIPS->r[MIPS_REG_RA]).c_str());
		// For illegal instructions, there might be a useful stack trace.
		stackTrace = FormatStackTrace(WalkCurrentStack(-1));
		break;
	default:
		truncate_cpy(msg, sizeof(msg), "Unknown exec exception");
		break;
	}

	if (stackTrace.empty()) {
		Core_SendDebugOutput(LogLevel::LERROR, msg);
	} else {
		Core_SendDebugOutput(LogLevel::LERROR, StringFromFormat("%s\nMIPS call stack:\n%s", msg, stackTrace.c_str()));
	}

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
	MIPSExceptionInfo &e = g_exceptionInfo;
	e = {};
	e.type = MIPSExceptionType::BREAK;
	e.info.clear();
	e.pc = pc;

	const std::string pcSuffix = ModuleAddressSuffix(pc);

	char msg[512];
	snprintf(msg, sizeof(msg), "CPU exception: break instruction hit at %08x%s. Ignoring (use --break=log for more details or --break=break to break)", pc, pcSuffix.c_str());

	const ExceptionAction action = ResolveExceptionAction((ExceptionAction)g_Config.iExceptionActionBreak);
	if (action == ExceptionAction::Ignore) {
		// Simplest logging and continue.
		Core_SendDebugOutput(LogLevel::LINFO, StringFromFormat("Ignoring CPU exception: break instruction hit at %08x%s", pc, pcSuffix.c_str()));
		return;
	}

	const std::string stackTrace = FormatStackTrace(WalkCurrentStack(-1));
	Core_SendDebugOutput(LogLevel::LERROR, StringFromFormat("%s\n%s", msg, stackTrace.c_str()));
	if (action == ExceptionAction::Break) {
		Core_Break(BreakReason::BreakInstruction, currentMIPS->pc);
	}
}

// We can't actually deliver the trap to the game's exception handler - nothing in PPSSPP emulates
// the FPU exception vector - so all we can do is tell the user about it. Execution continues after
// this returns, just without the destination register having been written (as on hardware).
// Shares the "break instruction" action setting: both are exceptions the game deliberately asked
// for, as opposed to the memory ones, which mean it has gone off the rails.
void Core_FPUException(u32 pc, FPUExceptionType type) {
	const char *desc = FPUExceptionTypeAsString(type);
	const std::string pcSuffix = ModuleAddressSuffix(pc);

	const ExceptionAction action = ResolveExceptionAction((ExceptionAction)g_Config.iExceptionActionBreak);
	if (action == ExceptionAction::Ignore) {
		Core_SendDebugOutput(LogLevel::LINFO, StringFromFormat("Ignoring FPU exception: %s at %08x%s", desc, pc, pcSuffix.c_str()));
		return;
	}

	char msg[512];
	snprintf(msg, sizeof(msg), "FPU exception: %s at %08x%s, unmasked in fcr31 (%08x)", desc, pc, pcSuffix.c_str(), currentMIPS->fcr31);

	const std::string stackTrace = FormatStackTrace(WalkCurrentStack(-1));
	Core_SendDebugOutput(LogLevel::LERROR, StringFromFormat("%s\nMIPS call stack:\n%s", msg, stackTrace.c_str()));
	if (action == ExceptionAction::Break) {
		MIPSExceptionInfo &e = g_exceptionInfo;
		e = {};
		e.type = MIPSExceptionType::FPU;
		e.info.clear();
		e.fpu_type = type;
		e.stackTrace = stackTrace;
		e.pc = pc;
		Core_Break(BreakReason::CpuException, pc);
	}
}

void Core_ResetException() {
	g_exceptionInfo.type = MIPSExceptionType::NONE;
}

const MIPSExceptionInfo &Core_GetExceptionInfo() {
	return g_exceptionInfo;
}
