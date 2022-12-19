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
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HW/Display.h"
#include "Core/MIPS/MIPS.h"
#include "GPU/Debugger/Stepping.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include "Windows/InputDevice.h"
#endif

// Time until we stop considering the core active without user input.
// Should this be configurable?  2 hours currently.
static const double ACTIVITY_IDLE_TIMEOUT = 2.0 * 3600.0;

static std::condition_variable m_StepCond;
static std::mutex m_hStepMutex;
static std::condition_variable m_InactiveCond;
static std::mutex m_hInactiveMutex;
static bool singleStepPending = false;
static int steppingCounter = 0;
static const char *steppingReason = "";
static uint32_t steppingAddress = 0;
static std::set<CoreLifecycleFunc> lifecycleFuncs;
static std::set<CoreStopRequestFunc> stopFuncs;
static bool windowHidden = false;
static double lastActivity = 0.0;
static double lastKeepAwake = 0.0;
static GraphicsContext *graphicsContext;
static bool powerSaving = false;

static ExceptionInfo g_exceptionInfo;

void Core_SetGraphicsContext(GraphicsContext *ctx) {
	graphicsContext = ctx;
	PSP_CoreParameter().graphicsContext = graphicsContext;
}

void Core_NotifyWindowHidden(bool hidden) {
	windowHidden = hidden;
	// TODO: Wait until we can react?
}

void Core_NotifyActivity() {
	lastActivity = time_now_d();
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

bool Core_IsStepping() {
	return coreState == CORE_STEPPING || coreState == CORE_POWERDOWN;
}

bool Core_IsActive() {
	return coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME || coreStatePending;
}

bool Core_IsInactive() {
	return coreState != CORE_RUNNING && coreState != CORE_NEXTFRAME && !coreStatePending;
}

static inline void Core_StateProcessed() {
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
	int w = (int)(pixelWidth * g_dpi_scale_x);
	int h = (int)(pixelHeight * g_dpi_scale_y);
	return g_Config.IsPortrait() ? (h < 480 + 80) : (w < 480 + 80);
}

// TODO: Feels like this belongs elsewhere.
bool UpdateScreenScale(int width, int height) {
	bool smallWindow;
#if defined(USING_QT_UI)
	g_dpi = System_GetPropertyFloat(SYSPROP_DISPLAY_DPI);
	float g_logical_dpi = System_GetPropertyFloat(SYSPROP_DISPLAY_LOGICAL_DPI);
	g_dpi_scale_x = g_logical_dpi / g_dpi;
	g_dpi_scale_y = g_logical_dpi / g_dpi;
#elif PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	g_dpi = System_GetPropertyFloat(SYSPROP_DISPLAY_DPI);
	g_dpi_scale_x = 96.0f / g_dpi;
	g_dpi_scale_y = 96.0f / g_dpi;
#else
	g_dpi = 96.0f;
	g_dpi_scale_x = 1.0f;
	g_dpi_scale_y = 1.0f;
#endif
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;

	smallWindow = IsWindowSmall(width, height);
	if (smallWindow) {
		g_dpi /= 2.0f;
		g_dpi_scale_x *= 2.0f;
		g_dpi_scale_y *= 2.0f;
	}
	pixel_in_dps_x = 1.0f / g_dpi_scale_x;
	pixel_in_dps_y = 1.0f / g_dpi_scale_y;

	int new_dp_xres = (int)(width * g_dpi_scale_x);
	int new_dp_yres = (int)(height * g_dpi_scale_y);

	bool dp_changed = new_dp_xres != dp_xres || new_dp_yres != dp_yres;
	bool px_changed = pixel_xres != width || pixel_yres != height;

	if (dp_changed || px_changed) {
		dp_xres = new_dp_xres;
		dp_yres = new_dp_yres;
		pixel_xres = width;
		pixel_yres = height;
		INFO_LOG(G3D, "pixel_res: %dx%d. Calling NativeResized()", pixel_xres, pixel_yres);
		NativeResized();
		return true;
	}
	return false;
}

// Note: not used on Android.
void UpdateRunLoop() {
	if (windowHidden && g_Config.bPauseWhenMinimized) {
		sleep_ms(16);
		return;
	}
	NativeUpdate();
	NativeRender(graphicsContext);
}

void KeepScreenAwake() {
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
}

void Core_RunLoop(GraphicsContext *ctx) {
	graphicsContext = ctx;
	while ((GetUIState() != UISTATE_INGAME || !PSP_IsInited()) && GetUIState() != UISTATE_EXIT) {
		// In case it was pending, we're not in game anymore.  We won't get to Core_Run().
		Core_StateProcessed();
		double startTime = time_now_d();
		UpdateRunLoop();

		// Simple throttling to not burn the GPU in the menu.
		double diffTime = time_now_d() - startTime;
		int sleepTime = (int)(1000.0 / 60.0) - (int)(diffTime * 1000.0);
		if (sleepTime > 0)
			sleep_ms(sleepTime);
		if (!windowHidden) {
			ctx->SwapBuffers();
		}
	}

	while ((coreState == CORE_RUNNING || coreState == CORE_STEPPING) && GetUIState() == UISTATE_INGAME) {
		UpdateRunLoop();
		if (!windowHidden && !Core_IsStepping()) {
			ctx->SwapBuffers();

			// Keep the system awake for longer than normal for cutscenes and the like.
			const double now = time_now_d();
			if (now < lastActivity + ACTIVITY_IDLE_TIMEOUT) {
				// Only resetting it ever prime number seconds in case the call is expensive.
				// Using a prime number to ensure there's no interaction with other periodic events.
				if (now - lastKeepAwake > 89.0 || now < lastKeepAwake) {
					KeepScreenAwake();
					lastKeepAwake = now;
				}
			}
		}
	}
}

void Core_DoSingleStep() {
	std::lock_guard<std::mutex> guard(m_hStepMutex);
	singleStepPending = true;
	m_StepCond.notify_all();
}

void Core_UpdateSingleStep() {
	std::lock_guard<std::mutex> guard(m_hStepMutex);
	m_StepCond.notify_all();
}

void Core_SingleStep() {
	Core_ResetException();
	currentMIPS->SingleStep();
	if (coreState == CORE_STEPPING)
		steppingCounter++;
}

static inline bool Core_WaitStepping() {
	std::unique_lock<std::mutex> guard(m_hStepMutex);
	// We only wait 16ms so that we can still draw UI or react to events.
	double sleepStart = time_now_d();
	if (!singleStepPending && coreState == CORE_STEPPING)
		m_StepCond.wait_for(guard, std::chrono::milliseconds(16));
	double sleepEnd = time_now_d();
	DisplayNotifySleep(sleepEnd - sleepStart);

	bool result = singleStepPending;
	singleStepPending = false;
	return result;
}

void Core_ProcessStepping() {
	Core_StateProcessed();

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
		host->UpdateDisassembly();
		host->UpdateMemView();
		lastSteppingCounter = steppingCounter;
	}

	// Need to check inside the lock to avoid races.
	bool doStep = Core_WaitStepping();

	// We may still be stepping without singleStepPending to process a save state.
	if (doStep && coreState == CORE_STEPPING) {
		Core_SingleStep();
		// Update disasm dialog.
		host->UpdateDisassembly();
		host->UpdateMemView();
	}
}

// Many platforms, like Android, do not call this function but handle things on their own.
// Instead they simply call NativeRender and NativeUpdate directly.
void Core_Run(GraphicsContext *ctx) {
	host->UpdateDisassembly();
	while (true) {
		if (GetUIState() != UISTATE_INGAME) {
			Core_StateProcessed();
			if (GetUIState() == UISTATE_EXIT) {
				UpdateRunLoop();
				return;
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
				Core_StateProcessed();
				return;
			}
			break;

		case CORE_POWERUP:
		case CORE_POWERDOWN:
		case CORE_BOOT_ERROR:
		case CORE_RUNTIME_ERROR:
			// Exit loop!!
			Core_StateProcessed();

			return;

		case CORE_NEXTFRAME:
			return;
		}
	}
}

void Core_EnableStepping(bool step, const char *reason, u32 relatedAddress) {
	if (step) {
		host->SetDebugMode(true);
		Core_UpdateState(CORE_STEPPING);
		steppingCounter++;
		_assert_msg_(reason != nullptr, "No reason specified for break");
		steppingReason = reason;
		steppingAddress = relatedAddress;
	} else {
		host->SetDebugMode(false);
		// Clear the exception if we resume.
		Core_ResetException();
		coreState = CORE_RUNNING;
		coreStatePending = false;
		m_StepCond.notify_all();
	}
}

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
	r.reason = steppingReason;
	r.relatedAddress = steppingAddress;
	return r;
}

const char *ExceptionTypeAsString(ExceptionType type) {
	switch (type) {
	case ExceptionType::MEMORY: return "Invalid Memory Access";
	case ExceptionType::BREAK: return "Break";
	case ExceptionType::BAD_EXEC_ADDR: return "Bad Execution Address";
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

void Core_MemoryException(u32 address, u32 pc, MemoryExceptionType type) {
	const char *desc = MemoryExceptionTypeAsString(type);
	// In jit, we only flush PC when bIgnoreBadMemAccess is off.
	if (g_Config.iCpuCore == (int)CPUCore::JIT && g_Config.bIgnoreBadMemAccess) {
		WARN_LOG(MEMMAP, "%s: Invalid address %08x", desc, address);
	} else {
		WARN_LOG(MEMMAP, "%s: Invalid address %08x PC %08x LR %08x", desc, address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA]);
	}

	if (!g_Config.bIgnoreBadMemAccess) {
		ExceptionInfo &e = g_exceptionInfo;
		e = {};
		e.type = ExceptionType::MEMORY;
		e.info.clear();
		e.memory_type = type;
		e.address = address;
		e.pc = pc;
		Core_EnableStepping(true, "memory.exception", address);
	}
}

void Core_MemoryExceptionInfo(u32 address, u32 pc, MemoryExceptionType type, std::string additionalInfo) {
	const char *desc = MemoryExceptionTypeAsString(type);
	// In jit, we only flush PC when bIgnoreBadMemAccess is off.
	if (g_Config.iCpuCore == (int)CPUCore::JIT && g_Config.bIgnoreBadMemAccess) {
		WARN_LOG(MEMMAP, "%s: Invalid address %08x. %s", desc, address, additionalInfo.c_str());
	} else {
		WARN_LOG(MEMMAP, "%s: Invalid address %08x PC %08x LR %08x %s", desc, address, currentMIPS->pc, currentMIPS->r[MIPS_REG_RA], additionalInfo.c_str());
	}

	if (!g_Config.bIgnoreBadMemAccess) {
		ExceptionInfo &e = g_exceptionInfo;
		e = {};
		e.type = ExceptionType::MEMORY;
		e.info = additionalInfo;
		e.memory_type = type;
		e.address = address;
		e.pc = pc;
		Core_EnableStepping(true, "memory.exception", address);
	}
}

void Core_ExecException(u32 address, u32 pc, ExecExceptionType type) {
	const char *desc = ExecExceptionTypeAsString(type);
	WARN_LOG(MEMMAP, "%s: Invalid destination %08x PC %08x LR %08x", desc, address, pc, currentMIPS->r[MIPS_REG_RA]);

	ExceptionInfo &e = g_exceptionInfo;
	e = {};
	e.type = ExceptionType::BAD_EXEC_ADDR;
	e.info.clear();
	e.exec_type = type;
	e.address = address;
	e.pc = pc;
	// This just records the closest value that could be useful as reference.
	e.ra = currentMIPS->r[MIPS_REG_RA];
	Core_EnableStepping(true, "cpu.exception", address);
}

void Core_Break(u32 pc) {
	ERROR_LOG(CPU, "BREAK!");

	ExceptionInfo &e = g_exceptionInfo;
	e = {};
	e.type = ExceptionType::BREAK;
	e.info.clear();
	e.pc = pc;

	if (!g_Config.bIgnoreBadMemAccess) {
		Core_EnableStepping(true, "cpu.breakInstruction", currentMIPS->pc);
	}
}

void Core_ResetException() {
	g_exceptionInfo.type = ExceptionType::NONE;
}

const ExceptionInfo &Core_GetExceptionInfo() {
	return g_exceptionInfo;
}
