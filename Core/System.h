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
#include "Common/File/Path.h"
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
	UISTATE_EXCEPTION,
};

// Use these in conjunction with GetSysDirectory.
enum PSPDirectories {
	DIRECTORY_PSP,
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
	DIRECTORY_PLUGINS,
	DIRECTORY_APP_CACHE,  // Use the OS app cache if available
	DIRECTORY_VIDEO,
	DIRECTORY_AUDIO,
	DIRECTORY_MEMSTICK_ROOT,
	DIRECTORY_EXDATA,
	DIRECTORY_CUSTOM_SHADERS,
	DIRECTORY_CUSTOM_THEMES,
};

class GraphicsContext;
enum class GPUBackend;

void ResetUIState();
void UpdateUIState(GlobalUIState newState);
GlobalUIState GetUIState();

void SetGPUBackend(GPUBackend type, const std::string &device = "");
GPUBackend GetGPUBackend();
std::string GetGPUBackendDevice();

bool PSP_Init(const CoreParameter &coreParam, std::string *error_string);
bool PSP_InitStart(const CoreParameter &coreParam, std::string *error_string);
bool PSP_InitUpdate(std::string *error_string);
bool PSP_IsIniting();
bool PSP_IsInited();
bool PSP_IsQuitting();
void PSP_Shutdown();

void PSP_BeginHostFrame();
void PSP_EndHostFrame();
void PSP_RunLoopWhileState();
void PSP_RunLoopUntil(u64 globalticks);
void PSP_RunLoopFor(int cycles);

void PSP_SetLoading(const std::string &reason);
std::string PSP_GetLoading();

// Used to wait for background loading thread.
struct PSP_LoadingLock {
	PSP_LoadingLock();
	~PSP_LoadingLock();
};

// Call before PSP_BeginHostFrame() in order to not miss any GPU stats.
void Core_UpdateDebugStats(bool collectStats);
// Increments or decrements an internal counter.  Intended to be used by debuggers.
void Core_ForceDebugStats(bool enable);

void Audio_Init();
void Audio_Shutdown();
bool IsAudioInitialised();

void UpdateLoadedFile(FileLoader *fileLoader);

// NOTE: These are almost all derived from g_Config.memStickDirectory directly -
// they are not stored anywhere.
Path GetSysDirectory(PSPDirectories directoryType);

#ifdef _WIN32
void InitSysDirectories();
#endif

// RUNNING must be at 0, NEXTFRAME must be at 1.
enum CoreState {
	// Emulation is running normally.
	CORE_RUNNING = 0,
	// Emulation was running normally, just reached the end of a frame.
	CORE_NEXTFRAME = 1,
	// Emulation is paused, CPU thread is sleeping.
	CORE_STEPPING,  // Can be used for recoverable runtime errors (ignored memory exceptions)
	// Core is being powered up.
	CORE_POWERUP,
	// Core is being powered down.
	CORE_POWERDOWN,
	// An error happened at boot.
	CORE_BOOT_ERROR,
	// Unrecoverable runtime error. Recoverable errors should use CORE_STEPPING.
	CORE_RUNTIME_ERROR,
};

extern bool coreCollectDebugStats;

extern volatile CoreState coreState;
extern volatile bool coreStatePending;
void Core_UpdateState(CoreState newState);

inline CoreParameter &PSP_CoreParameter() {
	extern CoreParameter g_CoreParameter;
	return g_CoreParameter;
}
