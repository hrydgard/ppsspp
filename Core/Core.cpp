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

#include "Host.h"

#include "Debugger/Breakpoints.h"

// HANDLE m_hStepEvent;
event m_hStepEvent;
recursive_mutex m_hStepMutex;

// This can be read and written from ANYWHERE.
volatile CoreState coreState = CORE_STEPPING;

void Core_ErrorPause()
{
	coreState = CORE_ERROR;
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
	coreState = CORE_POWERDOWN;
	m_hStepEvent.notify_one();
}

bool Core_IsStepping()
{
	return coreState == CORE_STEPPING || coreState == CORE_POWERDOWN;
}

void Core_RunLoop()
{
	currentMIPS->RunLoopUntil(0xFFFFFFFFFFFFFFFULL);
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
#ifndef LINUX
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
			//1: wait for step command..
			m_hStepEvent.wait(m_hStepMutex);
			if (coreState == CORE_POWERDOWN)
				return;
			if (coreState != CORE_STEPPING)
#ifdef LINUX
				return;
#else
				goto reswitch;
#endif

			currentCPU = &mipsr4k;
			Core_SingleStep();
			//4: update disasm dialog
#ifdef _DEBUG
			host->UpdateDisassembly();
			host->UpdateMemView();
#endif
			break;

		case CORE_POWERDOWN:
		case CORE_ERROR:
			//1: Exit loop!!
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
		coreState = CORE_STEPPING;
	}
	else
	{
#if defined(_DEBUG)
		host->SetDebugMode(false);
#endif
		coreState = CORE_RUNNING;
		m_hStepEvent.notify_one();
	}
}
