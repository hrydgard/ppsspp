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

#include "Common/CommonTypes.h"
#include "Core/CoreParameter.h"

class MetaFileSystem;
class ParamSFOData;

extern MetaFileSystem pspFileSystem;
extern ParamSFOData g_paramSFO;

// To synchronize the two UIs, we need to know which state we're in.
enum GlobalUIState {
	UISTATE_MENU,
	UISTATE_PAUSEMENU,
	UISTATE_INGAME,
	UISTATE_EXIT,
};

// Use these in conjunction with GetSysDirectory.
enum PSPDirectories {
	DIRECTORY_CHEATS,
	DIRECTORY_SCREENSHOT,
	DIRECTORY_SYSTEM,
	DIRECTORY_GAME,
	DIRECTORY_SAVEDATA,
	DIRECTORY_PAUTH,
	DIRECTORY_DUMP,
	DIRECTORY_SAVESTATE,
	DIRECTORY_CACHE,
	DIRECTORY_TEXTURES,
	DIRECTORY_APP_CACHE,  // Use the OS app cache if available
	DIRECTORY_VIDEO,
	DIRECTORY_AUDIO
};

class GraphicsContext;
enum class GPUBackend;

void ResetUIState();
void UpdateUIState(GlobalUIState newState);
GlobalUIState GetUIState();

void SetGPUBackend(GPUBackend type);
GPUBackend GetGPUBackend();

bool PSP_Init(const CoreParameter &coreParam, std::string *error_string);
bool PSP_InitStart(const CoreParameter &coreParam, std::string *error_string);
bool PSP_InitUpdate(std::string *error_string);
bool PSP_IsIniting();
bool PSP_IsInited();
void PSP_Shutdown();

void PSP_BeginHostFrame();
void PSP_EndHostFrame();
void PSP_RunLoopUntil(u64 globalticks);
void PSP_RunLoopFor(int cycles);

// Call before PSP_BeginHostFrame() in order to not miss any GPU stats.
void Core_UpdateDebugStats(bool collectStats);

void Audio_Init();
void Audio_Shutdown();
bool IsAudioInitialised();

void UpdateLoadedFile(FileLoader *fileLoader);

std::string GetSysDirectory(PSPDirectories directoryType);
#ifdef _WIN32
void InitSysDirectories();
#endif

// RUNNING must be at 0, NEXTFRAME must be at 1.
enum CoreState {
	CORE_RUNNING = 0,
	CORE_NEXTFRAME = 1,
	CORE_STEPPING,
	CORE_POWERUP,
	CORE_POWERDOWN,
	CORE_ERROR,
};

extern bool coreCollectDebugStats;

extern volatile CoreState coreState;
extern volatile bool coreStatePending;
void Core_UpdateState(CoreState newState);

CoreParameter &PSP_CoreParameter();
