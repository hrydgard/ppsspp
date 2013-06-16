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

#include "base/NativeApp.h"
#include "base/display.h"
#include "base/mutex.h"
#include "base/timeutil.h"
#include "input/input_state.h"

#include "Globals.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#ifdef _WIN32
#include "Windows/OpenGLBase.h"
#include "Windows/InputDevice.h"
#endif

#include "Host.h"

#include "Core/Debugger/Breakpoints.h"

event m_hStepEvent;
recursive_mutex m_hStepMutex;
event m_hInactiveEvent;
recursive_mutex m_hInactiveMutex;

#ifdef _WIN32
InputState input_state;
#else
extern InputState input_state;
#endif

void Core_ErrorPause()
{
	Core_UpdateState(CORE_ERROR);
}

void Core_Halt(const char *msg) 
{
	Core_EnableStepping(true);
	ERROR_LOG(CPU, "CPU HALTED : %s",msg);
	_dbg_update_();
}

void Core_Stop()
{
	Core_UpdateState(CORE_POWERDOWN);
	m_hStepEvent.notify_one();
}

bool Core_IsStepping()
{
	return coreState == CORE_STEPPING || coreState == CORE_POWERDOWN;
}

bool Core_IsActive()
{
	return coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME || coreStatePending;
}

bool Core_IsInactive()
{
	return coreState != CORE_RUNNING && coreState != CORE_NEXTFRAME && !coreStatePending;
}

void Core_WaitInactive()
{
	while (Core_IsActive())
		m_hInactiveEvent.wait(m_hInactiveMutex);
}

void Core_WaitInactive(int milliseconds)
{
	m_hInactiveEvent.wait_for(m_hInactiveMutex, milliseconds);
}

void UpdateScreenScale() {
	dp_xres = PSP_CoreParameter().pixelWidth;
	dp_yres = PSP_CoreParameter().pixelHeight;
#ifdef _WIN32
	if (g_Config.iWindowZoom == 1)
	{
		dp_xres *= 2;
		dp_yres *= 2;
	}
#endif
	pixel_xres = PSP_CoreParameter().pixelWidth;
	pixel_yres = PSP_CoreParameter().pixelHeight;
	g_dpi = 72;
	g_dpi_scale = 1.0f;
	pixel_in_dps = (float)pixel_xres / dp_xres;
}

void Core_RunLoop()
{
	while (!coreState) {
		time_update();
		double startTime = time_now_d();
		UpdateScreenScale();
		{
			{
#ifdef _WIN32
				lock_guard guard(input_state.lock);
				input_state.pad_buttons = 0;
				input_state.pad_lstick_x = 0;
				input_state.pad_lstick_y = 0;
				input_state.pad_rstick_x = 0;
				input_state.pad_rstick_y = 0;
				host->PollControllers(input_state);
				UpdateInputState(&input_state);
#endif
			}
			NativeUpdate(input_state);
			EndInputState(&input_state);
		}
		NativeRender();
		time_update();
		// Simple throttling to not burn the GPU in the menu.
#ifdef _WIN32
		if (globalUIState != UISTATE_INGAME) {
			double diffTime = time_now_d() - startTime;
			int sleepTime = (int) (1000000.0 / 60.0) - (int) (diffTime * 1000000.0);
			if (sleepTime > 0)
				Sleep(sleepTime / 1000);
			GL_SwapBuffers();
		} else if (!Core_IsStepping()) {
			GL_SwapBuffers();
		}
#endif
	}
}

void Core_DoSingleStep()
{
	m_hStepEvent.notify_one();
}

void Core_SingleStep()
{
	currentMIPS->SingleStep();
}


// Some platforms, like Android, do not call this function but handle things on their own.
void Core_Run()
{
#if defined(_DEBUG)
	host->UpdateDisassembly();
#endif
#if !defined(USING_QT_UI) || defined(USING_GLES2)
	while (true)
#endif
	{
reswitch:
		switch (coreState)
		{
		case CORE_RUNNING:
			//1: enter a fast runloop
			Core_RunLoop();
			break;

		// We should never get here on Android.
		case CORE_STEPPING:
			if (coreStatePending) {
				coreStatePending = false;
				m_hInactiveEvent.notify_one();
			}

			//1: wait for step command..
#if defined(USING_QT_UI) || defined(_DEBUG)
			host->UpdateDisassembly();
			host->UpdateMemView();
			host->SendCoreWait(true);
#endif

			m_hStepEvent.wait(m_hStepMutex);

#if defined(USING_QT_UI) || defined(_DEBUG)
			host->SendCoreWait(false);
#endif
			if (coreState == CORE_POWERDOWN)
				return;
			if (coreState != CORE_STEPPING)
#if defined(USING_QT_UI) && !defined(USING_GLES2)
				return;
#else
				goto reswitch;
#endif

			currentCPU = &mipsr4k;
			Core_SingleStep();
			//4: update disasm dialog
#if defined(USING_QT_UI) || defined(_DEBUG)
			host->UpdateDisassembly();
			host->UpdateMemView();
#endif
			break;

		case CORE_POWERDOWN:
		case CORE_ERROR:
			//1: Exit loop!!
			if (coreStatePending) {
				coreStatePending = false;
				m_hInactiveEvent.notify_one();
			}

			return;

		case CORE_NEXTFRAME:
			return;
		}
	}

}


void Core_EnableStepping(bool step)
{
	if (step)
	{
		sleep_ms(1);
#if defined(_DEBUG)
		host->SetDebugMode(true);
#endif
		m_hStepEvent.reset();
		Core_UpdateState(CORE_STEPPING);
	}
	else
	{
#if defined(_DEBUG)
		host->SetDebugMode(false);
#endif
		coreState = CORE_RUNNING;
		coreStatePending = false;
		m_hStepEvent.notify_one();
	}
}
