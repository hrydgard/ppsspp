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
#include <mutex>
#include <condition_variable>

#include "base/NativeApp.h"
#include "base/display.h"
#include "base/timeutil.h"
#include "thread/threadutil.h"
#include "profiler/profiler.h"

#include "Common/GraphicsContext.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MIPS/MIPS.h"

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
static std::set<CoreLifecycleFunc> shutdownFuncs;
static bool windowHidden = false;
static double lastActivity = 0.0;
static double lastKeepAwake = 0.0;
static GraphicsContext *graphicsContext;
static bool powerSaving = false;

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
	shutdownFuncs.insert(func);
}

void Core_NotifyLifecycle(CoreLifecycle stage) {
	for (auto it = shutdownFuncs.begin(); it != shutdownFuncs.end(); ++it) {
		(*it)(stage);
	}
}

void Core_Stop() {
	Core_UpdateState(CORE_POWERDOWN);
	m_StepCond.notify_all();
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
	while (Core_IsActive()) {
		std::unique_lock<std::mutex> guard(m_hInactiveMutex);
		m_InactiveCond.wait(guard);
	}
}

void Core_WaitInactive(int milliseconds) {
	if (Core_IsActive()) {
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
#ifdef _WIN32
	g_dpi = (float)System_GetPropertyInt(SYSPROP_DISPLAY_DPI);
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

	int new_dp_xres = width * g_dpi_scale_x;
	int new_dp_yres = height * g_dpi_scale_y;

	bool dp_changed = new_dp_xres != dp_xres || new_dp_yres != dp_yres;
	bool px_changed = pixel_xres != width || pixel_yres != height;

	if (dp_changed || px_changed) {
		dp_xres = new_dp_xres;
		dp_yres = new_dp_yres;
		pixel_xres = width;
		pixel_yres = height;
		DEBUG_LOG(SYSTEM, "pixel_res: %dx%d. Calling NativeResized()", pixel_xres, pixel_yres);
		NativeResized();
		return true;
	}
	return false;
}

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
		time_update();
		double startTime = time_now_d();
		UpdateRunLoop();

		// Simple throttling to not burn the GPU in the menu.
		time_update();
		double diffTime = time_now_d() - startTime;
		int sleepTime = (int)(1000.0 / 60.0) - (int)(diffTime * 1000.0);
		if (sleepTime > 0)
			sleep_ms(sleepTime);
		if (!windowHidden) {
			ctx->SwapBuffers();
		}
	}

	while (!coreState && GetUIState() == UISTATE_INGAME) {
		time_update();
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
	singleStepPending = true;
	m_StepCond.notify_all();
}

void Core_UpdateSingleStep() {
	m_StepCond.notify_all();
}

void Core_SingleStep() {
	currentMIPS->SingleStep();
}

static inline void CoreStateProcessed() {
	if (coreStatePending) {
		coreStatePending = false;
		m_InactiveCond.notify_all();
	}
}

// Many platforms, like Android, do not call this function but handle things on their own.
// Instead they simply call NativeRender and NativeUpdate directly.
void Core_Run(GraphicsContext *ctx) {
	host->UpdateDisassembly();
	while (true) {
reswitch:
		if (GetUIState() != UISTATE_INGAME) {
			CoreStateProcessed();
			if (GetUIState() == UISTATE_EXIT) {
				UpdateRunLoop();
				return;
			}
			Core_RunLoop(ctx);
			continue;
		}

		switch (coreState) {
		case CORE_RUNNING:
			// enter a fast runloop
			Core_RunLoop(ctx);
			break;

		// We should never get here on Android.
		case CORE_STEPPING:
			singleStepPending = false;
			CoreStateProcessed();

			// Check if there's any pending savestate actions.
			SaveState::Process();
			if (coreState == CORE_POWERDOWN) {
				return;
			}

			// wait for step command..
			host->UpdateDisassembly();
			host->UpdateMemView();
			host->SendCoreWait(true);

			{
				std::unique_lock<std::mutex> guard(m_hStepMutex);
				m_StepCond.wait(guard);
			}

			host->SendCoreWait(false);
			// No step pending?  Let's go back to the wait.
			if (!singleStepPending || coreState != CORE_STEPPING) {
				if (coreState == CORE_POWERDOWN) {
					return;
				}
				goto reswitch;
			}

			Core_SingleStep();
			// update disasm dialog
			host->UpdateDisassembly();
			host->UpdateMemView();
			break;

		case CORE_POWERUP:
		case CORE_POWERDOWN:
		case CORE_ERROR:
			// Exit loop!!
			CoreStateProcessed();

			return;

		case CORE_NEXTFRAME:
			return;
		}
	}
}

void Core_EnableStepping(bool step) {
	if (step) {
		sleep_ms(1);
		host->SetDebugMode(true);
		Core_UpdateState(CORE_STEPPING);
		steppingCounter++;
	} else {
		host->SetDebugMode(false);
		coreState = CORE_RUNNING;
		coreStatePending = false;
		m_StepCond.notify_all();
	}
}

int Core_GetSteppingCounter() {
	return steppingCounter;
}
