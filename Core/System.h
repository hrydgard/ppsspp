// Copyright (C) 2012 PPSSPP Project

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

#include "../Globals.h"
#include "MemMap.h"
#include "FileSystems/MetaFileSystem.h"
#include "CoreParameter.h"
#include "ELF/ParamSFO.h"

extern MetaFileSystem pspFileSystem;
extern ParamSFOData g_paramSFO;


// To synchronize the two UIs, we need to know which state we're in.
enum GlobalUIState {
	UISTATE_MENU,
	UISTATE_PAUSEMENU,
	UISTATE_INGAME,
	UISTATE_EXIT,
};


extern GlobalUIState globalUIState;

bool PSP_Init(const CoreParameter &coreParam, std::string *error_string);
bool PSP_IsInited();
void PSP_Shutdown();
void PSP_HWAdvance(int cycles);
void PSP_SWI();

void GetSysDirectories(std::string &memstickpath, std::string &flash0path);

// RUNNING must be at 0.
enum CoreState
{
	CORE_RUNNING = 0,
	CORE_STEPPING,
	CORE_POWERDOWN,
	CORE_ERROR,
	CORE_NEXTFRAME,
};

extern volatile CoreState coreState;
extern volatile bool coreStatePending;
void Core_UpdateState(CoreState newState);

CoreParameter &PSP_CoreParameter();