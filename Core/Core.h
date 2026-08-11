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

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>

#include "Common/CommonTypes.h"
#include "Core/ConfigValues.h"

class GraphicsContext;

// For platforms that don't call Run
void Core_SetGraphicsContext(GraphicsContext *ctx);

// Returns false when an UI exit state is detected.
void Core_Stop();

// X11, sigh.
#ifdef None
#undef None
#endif

enum class CPUStepType {
	None,
	Into,
	Over,
	Out,
	Frame,
};

// Must be set when breaking.
enum class BreakReason {
	None,
	AssertChoice,
	DebugBreak,
	DebugStep,
	DebugStepInto,
	UIFocus,
	AfterFrame,
	MemoryException,
	CpuException,
	BreakInstruction,
	SavestateLoad,
	SavestateSave,
	SavestateRewind,
	SavestateCrash,
	MemoryBreakpoint,
	CpuBreakpoint,
	MemoryAccess,  // ???
	JitBranchDebug,
	BreakOnBoot,
	RABreak,
	AddBreakpoint,
	FrameAdvance,
	UIPause,
	HLEDebugBreak,
};
const char *BreakReasonToString(BreakReason reason);

// Async, called from gui
void Core_Break(BreakReason reason, u32 relatedAddress = 0);

// Resumes execution. Works both when stepping the CPU and the GE.
void Core_Resume();

BreakReason Core_BreakReason();

// This should be called externally.
// Can fail if another step type was requested this frame.
// stepSize is always in instructions (4 bytes each), never bytes - see Core_PerformCPUStep in Core.cpp.
bool Core_RequestCPUStep(CPUStepType stepType, int stepSize);

bool Core_NextFrame();
void Core_SwitchToGe();  // Switches from CPU emulation to GE display list execution.

// Changes every time we enter stepping.
int Core_GetSteppingCounter();
struct SteppingReason {
	BreakReason reason;
	u32 relatedAddress = 0;
};
SteppingReason Core_GetSteppingReason();

enum class CoreLifecycle {
	STARTING,
	// Note: includes failure cases.  Guaranteed call after STARTING.
	START_COMPLETE,
	STOPPING,
	// Guaranteed call after STOPPING.
	STOPPED,

	// Sometimes called for save states.  Guaranteed sequence, and never during STARTING or STOPPING.
	MEMORY_REINITING,
	MEMORY_REINITED,
};

// RUNNING must be at 0, NEXTFRAME must be at 1.
enum CoreState {
	// Emulation is running normally.
	CORE_RUNNING_CPU = 0,
	// Emulation was running normally, just reached the end of a frame.
	CORE_NEXTFRAME = 1,
	// Emulation is paused, CPU thread is sleeping.
	CORE_STEPPING_CPU,  // Can be used for recoverable runtime errors (ignored memory exceptions)
	// Core is not running.
	CORE_POWERDOWN,
	// Unrecoverable runtime error. Recoverable errors should use CORE_STEPPING.
	CORE_RUNTIME_ERROR,
	// Stepping the GPU. When done, will switch over to STEPPING_CPU.
	CORE_STEPPING_GE,
	// Running the GPU. When done, will switch over to RUNNING_CPU.
	CORE_RUNNING_GE,
};
const char *CoreStateToString(CoreState state);

// Callback is called on the Emu thread.
typedef void (* CoreLifecycleFunc)(CoreLifecycle stage);
void Core_ListenLifecycle(CoreLifecycleFunc func);
void Core_NotifyLifecycle(CoreLifecycle stage);

bool Core_IsStepping();

bool Core_IsActive();
bool Core_IsInactive();

// Warning: these three are only used on Windows - debugger integration.
void Core_StateProcessed();
void Core_WaitInactive();

void Core_SetPowerSaving(bool mode);
bool Core_GetPowerSaving();

void Core_RunLoopUntil(u64 globalticks);

// Runs a function on the CPU thread - the thread that calls Core_RunLoopUntil (and thus, indirectly,
// NativeFrame). Useful for code running on unrelated threads (like the WebSocket debugger) that needs to
// safely touch state that's otherwise only ever touched from that thread (breakpoints, stepping, etc.),
// instead of poking at it directly from wherever the call happens to come from.
//
// Safe to call from any thread, including the CPU thread itself (in which case func just runs immediately).
// Blocks the calling thread until func has actually run, so don't call this from the CPU thread with
// something that would itself try to wait on the CPU thread - that'll deadlock.
//
// Drained at the top of every Core_RunLoopUntil() iteration, so it's reached continuously (in a tight
// spin) while the CPU is stepping/paused, and at least once per call (i.e. about once per host frame)
// even while it's fully running.
void Core_RunOnCPUThread(std::function<void()> func);

// Drains the queue Core_RunOnCPUThread() feeds. Normally called from the top of every
// Core_RunLoopUntil() iteration, but that function is only reached while a game is actually
// loaded/running (via EmuScreen) - so NativeFrame() (UI/NativeApp.cpp) also calls this directly,
// just before it calls into the screen manager's render(), so queued work doesn't hang forever
// waiting for a CPU loop that isn't running (e.g. from the main menu with no game loaded).
// Called from the CPU thread only - which is whatever thread NativeFrame() itself runs on.
void Core_ProcessCPUQueue();

// Guards CPU-thread-owned debugger state (breakpoints, symbol map, registers, memory, etc.)
// against concurrent unsynchronized reads from other threads' paint handlers.
//
// Held by NativeFrame() for the span where it actually touches that state: running the CPU
// (Core_RunLoopUntil(), including draining Core_RunOnCPUThread()'s queue), processing breakpoints,
// and running the ImGui debugger. Not held for the rest of NativeFrame (input handling, present/
// vsync waits, frame pacing, etc).
//
// A paint handler on another thread (e.g. a legacy Win32 debugger window) that wants to read that
// state directly - without the overhead/latency of routing through Core_RunOnCPUThread(), which
// would be too heavy for something called on every WM_PAINT - should hold this lock for the
// duration of the read instead. Since WM_PAINT only fires reactively rather than every frame, and
// NativeFrame's locked span is normally just a couple of milliseconds, this should rarely block
// for long.
extern std::mutex g_frameMutex;

extern volatile CoreState coreState;
extern volatile bool coreStatePending;

void Core_UpdateState(CoreState newState);

enum class MemoryExceptionType {
	NONE,
	UNKNOWN,
	READ_WORD,
	WRITE_WORD,
	HLE_READ,
	HLE_WRITE,
	READ_BLOCK,
	WRITE_BLOCK,
	ALIGNMENT,
};
enum class ExecExceptionType {
	JUMP,
	THREAD,
};

void Core_MemoryException(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type, std::string_view additionalInfo = "");
void Core_ExecException(u32 address, u32 pc, ExecExceptionType type);
void Core_BreakException(u32 pc);
// Call when loading save states, etc.
void Core_ResetException();

class MIPSState;
// Shortcut, just calls Core_MemoryException with automatically determined parameters (function name, etc).
void Core_MemoryExceptionHLE(MIPSState *mips, u32 address, u32 accessSize, MemoryExceptionType type);

enum class MIPSExceptionType {
	NONE,
	MEMORY,
	BREAK,
	BAD_EXEC_ADDR,
};

struct MIPSExceptionInfo {
	MIPSExceptionType type;
	std::string info;
	std::string stackTrace;  // if available.

	// Memory exception info
	MemoryExceptionType memory_type;
	uint32_t pc;
	uint32_t address;
	uint32_t accessSize;
	uint32_t ra = 0;

	// Reuses pc and address from memory type, where address is the failed destination.
	ExecExceptionType exec_type;
};

const MIPSExceptionInfo &Core_GetExceptionInfo();

const char *ExceptionTypeAsString(MIPSExceptionType type);
const char *MemoryExceptionTypeAsString(MemoryExceptionType type);
const char *ExecExceptionTypeAsString(ExecExceptionType type);
