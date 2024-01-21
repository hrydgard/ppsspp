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

#pragma once

#include <string>
#include <string_view>

#include "Core/System.h"
#include "Core/CoreParameter.h"

class GraphicsContext;

// called from emu thread
void UpdateRunLoop(GraphicsContext *ctx);

// Returns false when an UI exit state is detected.
bool Core_Run(GraphicsContext *ctx);
void Core_Stop();

// For platforms that don't call Core_Run
void Core_SetGraphicsContext(GraphicsContext *ctx);

// called from gui
void Core_EnableStepping(bool step, const char *reason = nullptr, u32 relatedAddress = 0);

bool Core_ShouldRunBehind();
bool Core_MustRunBehind();

bool Core_NextFrame();
void Core_DoSingleStep();
void Core_UpdateSingleStep();
void Core_ProcessStepping();

// Changes every time we enter stepping.
int Core_GetSteppingCounter();
struct SteppingReason {
	const char *reason = nullptr;
	u32 relatedAddress = 0;
};
SteppingReason Core_GetSteppingReason();

enum class CoreLifecycle {
	STARTING,
	// Note: includes failure cases.  Guaranteed call after STARTING.
	START_COMPLETE,
	STOPPING,
	// Guaranteed call after STOPPING.
	STOPPED,

	// Sometimes called for save states.  Guaranteed sequence, and never during STARTING or STOPPING.
	MEMORY_REINITING,
	MEMORY_REINITED,
};

// Callback is called on the Emu thread.
typedef void (* CoreLifecycleFunc)(CoreLifecycle stage);
void Core_ListenLifecycle(CoreLifecycleFunc func);
void Core_NotifyLifecycle(CoreLifecycle stage);

// Callback is executed on requesting thread.
typedef void (* CoreStopRequestFunc)();
void Core_ListenStopRequest(CoreStopRequestFunc callback);

bool Core_IsStepping();

bool Core_IsActive();
bool Core_IsInactive();
// Warning: these currently work only on Windows.
void Core_WaitInactive();
void Core_WaitInactive(int milliseconds);

bool UpdateScreenScale(int width, int height);

// Don't run the core when minimized etc.
void Core_NotifyWindowHidden(bool hidden);
bool Core_IsWindowHidden();

void Core_SetPowerSaving(bool mode);
bool Core_GetPowerSaving();

enum class MemoryExceptionType {
	NONE,
	UNKNOWN,
	READ_WORD,
	WRITE_WORD,
	READ_BLOCK,
	WRITE_BLOCK,
	ALIGNMENT,
};
enum class ExecExceptionType {
	JUMP,
	THREAD,
};

// Separate one for without info, to avoid having to allocate a string
void Core_MemoryException(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type);

void Core_MemoryExceptionInfo(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type, std::string_view additionalInfo, bool forceReport);

void Core_ExecException(u32 address, u32 pc, ExecExceptionType type);
void Core_Break(u32 pc);
// Call when loading save states, etc.
void Core_ResetException();

enum class MIPSExceptionType {
	NONE,
	MEMORY,
	BREAK,
	BAD_EXEC_ADDR,
};

struct MIPSExceptionInfo {
	MIPSExceptionType type;
	std::string info;
	std::string stackTrace;  // if available.

	// Memory exception info
	MemoryExceptionType memory_type;
	uint32_t pc;
	uint32_t address;
	uint32_t accessSize;
	uint32_t ra = 0;

	// Reuses pc and address from memory type, where address is the failed destination.
	ExecExceptionType exec_type;
};

const MIPSExceptionInfo &Core_GetExceptionInfo();

const char *ExceptionTypeAsString(MIPSExceptionType type);
const char *MemoryExceptionTypeAsString(MemoryExceptionType type);
const char *ExecExceptionTypeAsString(ExecExceptionType type);
