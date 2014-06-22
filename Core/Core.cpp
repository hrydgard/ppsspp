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

#include <set>

#include "base/NativeApp.h"
#include "base/display.h"
#include "base/mutex.h"
#include "base/timeutil.h"
#include "input/input_state.h"

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"

#ifdef _WIN32
#ifndef _XBOX
#include "Windows/OpenGLBase.h"
#endif
#include "Windows/InputDevice.h"
#endif

#include "Host.h"

#include "Core/Debugger/Breakpoints.h"

static event m_hStepEvent;
static recursive_mutex m_hStepMutex;
static event m_hInactiveEvent;
static recursive_mutex m_hInactiveMutex;
static bool singleStepPending = false;
static std::set<Core_ShutdownFunc> shutdownFuncs;

#ifdef _WIN32
InputState input_state;
#else
extern InputState input_state;
#endif

void Core_ListenShutdown(Core_ShutdownFunc func) {
	shutdownFuncs.insert(func);
}

void Core_NotifyShutdown() {
	for (auto it = shutdownFuncs.begin(); it != shutdownFuncs.end(); ++it) {
		(*it)();
	}
}

void Core_ErrorPause() {
	Core_UpdateState(CORE_ERROR);
}

void Core_Halt(const char *msg)  {
	Core_EnableStepping(true);
	ERROR_LOG(CPU, "CPU HALTED : %s",msg);
	_dbg_update_();
}

void Core_Stop() {
	Core_UpdateState(CORE_POWERDOWN);
	Core_NotifyShutdown();
	m_hStepEvent.notify_one();
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
		m_hInactiveEvent.wait(m_hInactiveMutex);
	}
}

void Core_WaitInactive(int milliseconds) {
	if (Core_IsActive()) {
		m_hInactiveEvent.wait_for(m_hInactiveMutex, milliseconds);
	}
}

void UpdateScreenScale(int width, int height) {
	dp_xres = width;
	dp_yres = height;
	pixel_xres = width;
	pixel_yres = height;
	g_dpi = 72;
	g_dpi_scale = 1.0f;
#ifdef __SYMBIAN32__
	dp_xres *= 1.4f;
	dp_yres *= 1.4f;
	g_dpi_scale = 1.4f;
#endif
#ifdef _WIN32
	if (pixel_xres < 480 + 80) {
		dp_xres *= 2;
		dp_yres *= 2;
		g_dpi_scale = 2.0f;
	}
	else
#endif
	pixel_in_dps = (float)pixel_xres / dp_xres;
	NativeResized();
}

static inline void UpdateRunLoop() {
	NativeUpdate(input_state);

	{
		lock_guard guard(input_state.lock);
		EndInputState(&input_state);
	}

	if (GetUIState() != UISTATE_EXIT) {
		NativeRender();
	}
}

void Core_RunLoop() {
	while ((GetUIState() != UISTATE_INGAME || !PSP_IsInited()) && GetUIState() != UISTATE_EXIT) {
		time_update();

#if defined(USING_WIN_UI)
		double startTime = time_now_d();
		UpdateRunLoop();

		// Simple throttling to not burn the GPU in the menu.
		time_update();
		double diffTime = time_now_d() - startTime;
		int sleepTime = (int) (1000000.0 / 60.0) - (int) (diffTime * 1000000.0);
		if (sleepTime > 0)
			Sleep(sleepTime / 1000);
		GL_SwapBuffers();
#else
		UpdateRunLoop();
#endif
	}

	while (!coreState && GetUIState() == UISTATE_INGAME) {
		time_update();
		UpdateRunLoop();
#if defined(USING_WIN_UI)
		if (!Core_IsStepping()) {
			GL_SwapBuffers();
		}
#endif
	}
}

void Core_DoSingleStep() {
	singleStepPending = true;
	m_hStepEvent.notify_one();
}

void Core_UpdateSingleStep() {
	m_hStepEvent.notify_one();
}

void Core_SingleStep() {
	currentMIPS->SingleStep();
}

static inline void CoreStateProcessed() {
	if (coreStatePending) {
		coreStatePending = false;
		m_hInactiveEvent.notify_one();
	}
}

// Some platforms, like Android, do not call this function but handle things on their own.
void Core_Run()
{
#if defined(_DEBUG)
	host->UpdateDisassembly();
#endif
#if !defined(USING_QT_UI) || defined(MOBILE_DEVICE)
	while (true)
#endif
	{
reswitch:
		if (GetUIState() != UISTATE_INGAME) {
			CoreStateProcessed();
			if (GetUIState() == UISTATE_EXIT) {
				return;
			}
			Core_RunLoop();
#if defined(USING_QT_UI) && !defined(MOBILE_DEVICE)
			return;
#else
			continue;
#endif
		}

		switch (coreState)
		{
		case CORE_RUNNING:
			// enter a fast runloop
			Core_RunLoop();
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
#if defined(USING_QT_UI) || defined(_DEBUG)
			host->UpdateDisassembly();
			host->UpdateMemView();
			host->SendCoreWait(true);
#endif

			m_hStepEvent.wait(m_hStepMutex);

#if defined(USING_QT_UI) || defined(_DEBUG)
			host->SendCoreWait(false);
#endif
#if defined(USING_QT_UI) && !defined(MOBILE_DEVICE)
			if (coreState != CORE_STEPPING)
				return;
#endif
			// No step pending?  Let's go back to the wait.
			if (!singleStepPending || coreState != CORE_STEPPING) {
				if (coreState == CORE_POWERDOWN) {
					return;
				}
				goto reswitch;
			}

			Core_SingleStep();
			// update disasm dialog
#if defined(USING_QT_UI) || defined(_DEBUG)
			host->UpdateDisassembly();
			host->UpdateMemView();
#endif
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
#if defined(_DEBUG)
		host->SetDebugMode(true);
#endif
		m_hStepEvent.reset();
		Core_UpdateState(CORE_STEPPING);
	} else {
#if defined(_DEBUG)
		host->SetDebugMode(false);
#endif
		coreState = CORE_RUNNING;
		coreStatePending = false;
		m_hStepEvent.notify_one();
	}
}
