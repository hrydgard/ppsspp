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

#include "base/mutex.h"
#include "base/timeutil.h"

#include "../Globals.h"
#include "Core.h"
#include "MemMap.h"
#include "MIPS/MIPS.h"
#ifdef _WIN32
#include "Windows/OpenGLBase.h"
#endif

#include "Host.h"

#include "Debugger/Breakpoints.h"

// HANDLE m_hStepEvent;
event m_hStepEvent;
recursive_mutex m_hStepMutex;
event m_hInactiveEvent;
recursive_mutex m_hInactiveMutex;

// This can be read and written from ANYWHERE.
volatile CoreState coreState = CORE_STEPPING;
// Note: intentionally not used for CORE_NEXTFRAME.
volatile bool coreStatePending = false;

void Core_UpdateState(CoreState newState)
{
	if ((coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME) && newState != CORE_RUNNING)
		coreStatePending = true;
	coreState = newState;
}

void Core_ErrorPause()
{
	Core_UpdateState(CORE_ERROR);
}

void Core_Pause()
{
	Core_EnableStepping(true);
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

bool Core_IsInactive()
{
	return coreState != CORE_RUNNING && coreState != CORE_NEXTFRAME && !coreStatePending;
}

void Core_WaitInactive()
{
	while (!Core_IsInactive())
		m_hInactiveEvent.wait(m_hInactiveMutex);
}

void Core_WaitInactive(int milliseconds)
{
	while (!Core_IsInactive())
		m_hInactiveEvent.wait_for(m_hInactiveMutex, milliseconds);
}

void Core_RunLoop()
{
	while (!coreState) {
		currentMIPS->RunLoopUntil(0xFFFFFFFFFFFFFFFULL);
		if (coreState == CORE_NEXTFRAME)
		{
#ifdef _WIN32
			coreState = CORE_RUNNING;
			GL_SwapBuffers();
#endif
		}
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
			if (coreStatePending)
				m_hInactiveEvent.notify_one();
			coreStatePending = false;

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
			if (coreStatePending)
				m_hInactiveEvent.notify_one();
			coreStatePending = false;
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
