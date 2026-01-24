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

#include <string_view>
#include "Common/CommonTypes.h"
#include "Common/File/Path.h"
#include "Core/CoreParameter.h"
#include "Core/ConfigValues.h"
#include "Core/Util/PathUtil.h"

class MetaFileSystem;
class ParamSFOData;

extern MetaFileSystem pspFileSystem;
extern ParamSFOData g_paramSFO;
extern ParamSFOData g_paramSFORaw;

// To synchronize the two UIs, we need to know which state we're in.
enum GlobalUIState {
	UISTATE_MENU,
	UISTATE_PAUSEMENU,
	UISTATE_INGAME,
	UISTATE_EXIT,
	UISTATE_EXCEPTION,
};

class GraphicsContext;
enum class GPUBackend;

void ResetUIState();
void UpdateUIState(GlobalUIState newState);
GlobalUIState GetUIState();

void SetGPUBackend(GPUBackend type, std::string_view device = "");
GPUBackend GetGPUBackend();
std::string GetGPUBackendDevice();

enum class BootState {
	Off,
	Booting,
	Complete,
	Failed,
};

BootState PSP_GetBootState();
inline bool PSP_IsInited() {
	return PSP_GetBootState() == BootState::Complete;
}

// Call this once, then call PSP_InitUpdate repeatedly to monitor progress.
bool PSP_InitStart(const CoreParameter &coreParam);

// Check the return value of this - if Booting, keep calling.
// If Complete or Failed, handle as appropriate, and stop calling.
BootState PSP_InitUpdate(std::string *error_string);

// Blocking wrapper around the two above functions, used for convenience in a couple of places.
// Should be avoided/removed eventually.
// Returns either BootState::Complete or BootState::Failed.
BootState PSP_Init(const CoreParameter &coreParam, std::string *error_string);

void PSP_Shutdown(bool success);

FileLoader *PSP_LoadedFile();

void PSP_RunLoopWhileState();
void PSP_RunLoopFor(int cycles);

// Call before gpu->BeginHostFrame() in order to not miss any GPU stats.
void PSP_UpdateDebugStats(bool collectStats);
// Increments or decrements an internal counter.  Intended to be used by debuggers.
void PSP_ForceDebugStats(bool enable);

void UpdateLoadedFile(FileLoader *fileLoader);

// NOTE: These are almost all derived from g_Config.memStickDirectory directly -
// they are not stored anywhere.
Path GetSysDirectory(PSPDirectories directoryType);

bool CreateSysDirectories();

extern bool coreCollectDebugStats;

inline CoreParameter &PSP_CoreParameter() {
	extern CoreParameter g_CoreParameter;
	return g_CoreParameter;
}

// Centralized place for dumping useful files, also takes care of checking for dupes and creating a clickable UI popup.
void DumpFileIfEnabled(const u8 *dataPtr, const u32 length, std::string_view name, DumpFileType type);
