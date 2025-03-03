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
#include <string>
#include <string_view>

#include "Common/CommonTypes.h"

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
	// Core is being powered up.
	CORE_POWERUP,
	// Core is being powered down.
	CORE_POWERDOWN,
	// An error happened at boot.
	CORE_BOOT_ERROR,
	// Unrecoverable runtime error. Recoverable errors should use CORE_STEPPING.
	CORE_RUNTIME_ERROR,
	// Stepping the GPU. When done, will switch over to STEPPING_CPU.
	CORE_STEPPING_GE,
	// Running the GPU. When done, will switch over to RUNNING_CPU.
	CORE_RUNNING_GE,
};

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

const char *CoreStateToString(CoreState state);

extern volatile CoreState coreState;
extern volatile bool coreStatePending;

void Core_UpdateState(CoreState newState);

enum class MemoryExceptionType {
	NONE,
	UNKNOWN,
	READ_WORD,
	WRITE_WORD,
	READ_BLOCK,
	WRITE_BLOCK,
	ALIGNMENT,
};
enum class ExecExceptionType {
	JUMP,
	THREAD,
};

// Separate one for without info, to avoid having to allocate a string
void Core_MemoryException(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type);

void Core_MemoryExceptionInfo(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type, std::string_view additionalInfo, bool forceReport);

void Core_ExecException(u32 address, u32 pc, ExecExceptionType type);
void Core_BreakException(u32 pc);
// Call when loading save states, etc.
void Core_ResetException();

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
