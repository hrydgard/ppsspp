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
#include <string>
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
	RegBreakpoint,
	MemoryAccess,  // ???
	JitBranchDebug,
	BreakOnBoot,
	RABreak,
	AddBreakpoint,
	FrameAdvance,
	UIPause,
	HLEDebugBreak,
	RunUntilTime,
};
const char *BreakReasonToString(BreakReason reason);

enum class BreakpointKind {
	None,
	Exec,
	Memory,
	Register,
};

// What tripped a breakpoint, captured where it happened.
//
// All three kinds know a lot more than the single address Core_Break() carries - which register,
// which byte of a watched range, read or write, how big - and until this existed it was formatted
// straight into a log line and discarded, so a debugger over the wire couldn't see any of it. For
// a memcheck in particular the address that reached the client was the start of the watched range,
// not the address actually touched.
struct BreakpointHit {
	BreakpointKind kind = BreakpointKind::None;
	u32 pc = 0;            // The instruction responsible.
	u32 address = 0;       // Exec: the instruction itself. Memory: the address actually accessed.
	int size = 0;          // Memory only, in bytes.
	bool write = false;    // Memory only.
	int reg = -1;          // Register only: a GPR index.
	// Which breakpoint this was, so a client can match it against cpu.breakpoint.list and friends.
	// For a memcheck that's the watched range, which is exactly what 'address' is not.
	u32 rangeStart = 0;
	u32 rangeEnd = 0;
	u32 numHits = 0;
	bool logged = false;   // Had the LOG action.
	bool paused = false;   // Had the PAUSE action, so the CPU stopped for it.
	std::string condition; // Empty when unconditional.
	// Memory only: who performed the access - "interpret", "CPU", "HLE", or an allocation tag.
	// Copied rather than kept as a pointer; callers pass buffers that are gone by the time the
	// event gets formatted.
	std::string source;
};

// Async, called from gui
// hit is optional detail for the breakpoint kinds, forwarded to the debugger. Only stored when
// the break actually takes effect, so a rejected Core_Break() can't leave a stale one behind.
void Core_Break(BreakReason reason, u32 relatedAddress = 0, const BreakpointHit *hit = nullptr);

// Resumes execution. Works both when stepping the CPU and the GE.
void Core_Resume();

BreakReason Core_BreakReason();

// This should be called externally.
// Can fail if another step type was requested this frame.
// stepSize is always in instructions (4 bytes each), never bytes - see Core_PerformCPUStep in Core.cpp.
bool Core_RequestCPUStep(CPUStepType stepType);

bool Core_NextFrame();
void Core_SwitchToGe();  // Switches from CPU emulation to GE display list execution.

// Changes every time we enter stepping.
int Core_GetSteppingCounter();
struct SteppingReason {
	BreakReason reason;
	u32 relatedAddress = 0;
	// Only filled in when the break came from a breakpoint - kind is None otherwise.
	BreakpointHit hit;
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
	CORE_NEXTFRAME,
	// Set this when running to bounce out from the dispatcher and just go back in again. Useful for things like cache clears.
	CORE_REENTER_DISPATCH,
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
void Core_ReenterDispatcher();  // If you've done things that mess with caches, call this so we can run deferred operations.

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

// Held while the core is being torn down (CPU_Shutdown) or its memory map reinitialized. Take it on
// any thread other than the CPU thread before reading core state - emulated memory, the symbol map,
// kernel objects - so none of it can be freed mid-read. Recursive, so nesting is fine.
//
// It is not a lock on memory *access*: it doesn't stop the CPU thread mutating anything, only stop
// it going away. If you also need a stable snapshot, take g_frameMutex first - see the ordering
// rule in AGENTS.md.
class CoreShutdownLock {
public:
	CoreShutdownLock();
	~CoreShutdownLock();
};
CoreShutdownLock Core_LockAgainstShutdown();

// Drains the queue Core_RunOnCPUThread() feeds. Normally called from the top of every
// Core_RunLoopUntil() iteration, but that function is only reached while a game is actually
// loaded/running (via EmuScreen) - so NativeFrame() (UI/NativeApp.cpp) also calls this directly,
// just before it calls into the screen manager's render(), so queued work doesn't hang forever
// waiting for a CPU loop that isn't running (e.g. from the main menu with no game loaded).
// Called from the CPU thread only - which is whatever thread NativeFrame() itself runs on.
void Core_ProcessCPUQueue();

// While the CPU is stopped with nothing pending, the CPU thread blocks briefly rather than
// spinning (see Core_IdleWaitWhileStepping() in Core.cpp). Call this after making state changes
// that give it something to do, so it notices immediately instead of at the next timeout. Already
// called by Core_RunOnCPUThread(), Core_RequestCPUStep() and Core_Resume(); free-threaded.
void Core_WakeIdleCPUThread();

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
	PERM,  // trying to execute kernel space instructions in user space
	ILLEGAL,
};
// The IEEE 754 exceptions the FPU can raise. Only the ones we actually detect are listed;
// they all live in fcr31 in the standard MIPS bit positions, see FCR31_* in MIPS.h.
enum class FPUExceptionType {
	DIVIDE_BY_ZERO,
};

void Core_MemoryException(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type, std::string_view additionalInfo = "");
void Core_ExecException(u32 address, u32 pc, ExecExceptionType type);
void Core_BreakException(u32 pc);
// Only called for exceptions the game has unmasked in fcr31 - a masked one just sets the flag bit
// and produces the IEEE default result, without coming through here.
void Core_FPUException(u32 pc, FPUExceptionType type);
// Call when loading save states, etc.
void Core_ResetException();

// Used by headless/pspautotest to collect data for the diffs. Crash reports are also sent here.
// Log level is only used if the listener is not registered.
enum class LogLevel : int;

enum GEBufferFormat : uint8_t;
struct DebugScreenshotDesc {
	const uint8_t *data;
	u32 stride;
	u32 height;
	GEBufferFormat format;
};
void Core_SendDebugOutput(LogLevel level, std::string_view string);
void Core_SendDebugScreenshot(const DebugScreenshotDesc &desc);
void Core_RegisterDebugOutputListeners(std::function<void(std::string_view)> listener, std::function<void(const DebugScreenshotDesc &)> screenshotListener);

class MIPSState;
// Shortcut, just calls Core_MemoryException with automatically determined parameters (function name, etc).
void Core_MemoryExceptionHLE(MIPSState *mips, u32 address, u32 accessSize, MemoryExceptionType type);

enum class MIPSExceptionType {
	NONE,
	MEMORY,
	BREAK,
	BAD_EXEC_ADDR,
	FPU,
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

	// FPU exception info. Only pc is meaningful alongside it.
	FPUExceptionType fpu_type;
};

const MIPSExceptionInfo &Core_GetExceptionInfo();

const char *ExceptionTypeAsString(MIPSExceptionType type);
const char *MemoryExceptionTypeAsString(MemoryExceptionType type);
const char *ExecExceptionTypeAsString(ExecExceptionType type);
const char *FPUExceptionTypeAsString(FPUExceptionType type);
