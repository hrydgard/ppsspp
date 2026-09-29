// Copyright (c) 2013- PPSSPP Project.

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

#include <chrono>
#include <mutex>
#include <condition_variable>

#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Core.h"
#include "Core/HW/Display.h"
#include "GPU/GPUCommon.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/GPUState.h"

namespace GPUStepping {

enum PauseAction {
	PAUSE_CONTINUE,
	PAUSE_BREAK,
	PAUSE_GETOUTPUTBUF,
	PAUSE_GETFRAMEBUF,
	PAUSE_GETDEPTHBUF,
	PAUSE_GETSTENCILBUF,
	PAUSE_GETTEX,
	PAUSE_GETCLUT,
	PAUSE_SETCMDVALUE,
	PAUSE_FLUSHDRAW,
};

static bool isStepping;
// Number of times we've entered stepping, to detect a resume asynchronously.
static int stepCounter = 0;

static std::mutex pauseLock;
static PauseAction pauseAction = PAUSE_CONTINUE;
static std::mutex actionLock;
static std::condition_variable actionWait;
// Protected by actionLock. Set when the emu thread has run the requested action.
static bool actionComplete;
// Held by a requesting thread for the whole request, so two debuggers can't overwrite each other's action.
static std::mutex requestLock;

// Many things need to run on the GPU thread.  For example, reading the framebuffer.
// A message system is used to achieve this (temporarily "unpausing" the thread.)
// Below are values used to perform actions that return results.

static bool bufferResult;
static GPUDebugFramebufferType bufferType = GPU_DBG_FRAMEBUF_RENDER;
static GPUDebugBuffer bufferFrame;
static GPUDebugBuffer bufferDepth;
static GPUDebugBuffer bufferStencil;
static GPUDebugBuffer bufferTex;
static GPUDebugBuffer bufferClut;
static int bufferLevel;
static bool lastWasFramebuffer;
static u32 pauseSetCmdValue;

// This is used only to highlight differences. Should really be owned by the debugger.
static GEState lastGState;

const char *PauseActionToString(PauseAction action) {
	switch (action) {
	case PAUSE_CONTINUE: return "CONTINUE";
	case PAUSE_BREAK: return "BREAK";
	case PAUSE_GETOUTPUTBUF: return "GETOUTPUTBUF";
	case PAUSE_GETFRAMEBUF: return "GETFRAMEBUF";
	case PAUSE_GETDEPTHBUF: return "GETDEPTHBUF";
	case PAUSE_GETSTENCILBUF: return "GETSTENCILBUF";
	case PAUSE_GETTEX: return "GETTEX";
	case PAUSE_GETCLUT: return "GETCLUT";
	case PAUSE_SETCMDVALUE: return "SETCMDVALUE";
	case PAUSE_FLUSHDRAW: return "FLUSHDRAW";
	default: return "N/A";
	}
}

static void SetPauseAction(PauseAction act) {
	std::lock_guard<std::mutex> pauseGuard(pauseLock);
	std::lock_guard<std::mutex> guard(actionLock);
	pauseAction = act;
	actionComplete = false;
}

static bool CanRunActions() {
	// During CPU stepping, the GE isn't inside a list, so it's safe to run actions then too.
	return coreState == CORE_STEPPING_GE || coreState == CORE_STEPPING_CPU;
}

// Called with pauseLock held.
static void RunPauseAction() {
	std::lock_guard<std::mutex> guard(actionLock);
	if (pauseAction == PAUSE_BREAK || pauseAction == PAUSE_CONTINUE) {
		// Nothing requested.
		return;
	}

	DEBUG_LOG(Log::GeDebugger, "RunPauseAction: %s", PauseActionToString(pauseAction));

	switch (pauseAction) {
	case PAUSE_BREAK:
		break;

	case PAUSE_GETOUTPUTBUF:
		bufferResult = gpu->GetOutputFramebuffer(bufferFrame);
		break;

	case PAUSE_GETFRAMEBUF:
		bufferResult = gpu->GetCurrentFramebuffer(bufferFrame, bufferType);
		break;

	case PAUSE_GETDEPTHBUF:
		bufferResult = gpu->GetCurrentDepthbuffer(bufferDepth);
		break;

	case PAUSE_GETSTENCILBUF:
		bufferResult = gpu->GetCurrentStencilbuffer(bufferStencil);
		break;

	case PAUSE_GETTEX:
		bufferResult = gpu->GetCurrentTexture(bufferTex, bufferLevel, &lastWasFramebuffer);
		break;

	case PAUSE_GETCLUT:
		bufferResult = gpu->GetCurrentClut(bufferClut);
		break;

	case PAUSE_SETCMDVALUE:
		gpu->SetCmdValue(pauseSetCmdValue);
		break;

	case PAUSE_FLUSHDRAW:
		gpu->Flush();
		break;

	default:
		ERROR_LOG(Log::GeDebugger, "Unsupported pause action, forgot to add it to the switch.");
		break;
	}

	actionComplete = true;
	actionWait.notify_all();

	pauseAction = PAUSE_BREAK;
}

// Requests an action from the emu thread and waits for it to run. Returns false if stepping ended first
// (resume, game shutdown), in which case the action is withdrawn.
static bool RequestPauseAction(PauseAction act) {
	_dbg_assert_(strcmp(GetCurrentThreadName(), "EmuThread") != 0);

	SetPauseAction(act);

	std::unique_lock<std::mutex> guard(actionLock);
	while (!actionComplete) {
		if (!CanRunActions()) {
			// Nobody will run it. Don't leave it for a later break to run at some unrelated point.
			// (Lock order is pauseLock before actionLock.)
			guard.unlock();
			std::lock_guard<std::mutex> pauseGuard(pauseLock);
			guard.lock();
			if (actionComplete) {
				break;
			}
			if (pauseAction == act) {
				pauseAction = PAUSE_BREAK;
			}
			return false;
		}
		// Polls coreState, since leaving stepping doesn't notify.
		actionWait.wait_for(guard, std::chrono::milliseconds(10));
	}
	return true;
}

bool ProcessStepping() {
	_dbg_assert_(gpu);

	std::unique_lock<std::mutex> guard(pauseLock);
	if (coreState == CORE_STEPPING_CPU) {
		RunPauseAction();
		return true;
	}
	if (coreState != CORE_STEPPING_GE) {
		// Not stepping any more, don't try.
		return false;
	}

	if (pauseAction == PAUSE_CONTINUE) {
		// This is fine, can just mean to run to the next breakpoint/event.
		DEBUG_LOG(Log::GeDebugger, "Continuing...");
		coreState = CORE_RUNNING_GE;
		return false;
	}

	RunPauseAction();
	return true;
}

bool EnterStepping(CoreState coreState) {
	_dbg_assert_(gpu);

	std::unique_lock<std::mutex> guard(pauseLock);
	if (coreState == CORE_STEPPING_GE) {
		// Already there. Should avoid this happening, I think.
		return true;
	}
	if (coreState != CORE_RUNNING_CPU && coreState != CORE_RUNNING_GE) {
		// ?? Shutting down, don't try to step.
		return false;
	}

	// StartStepping
	if (lastGState.cmdmem[1] == 0) {
		lastGState = gstate;
		// Play it safe so we don't keep resetting.
		lastGState.cmdmem[1] |= 0x01000000;
	}

	isStepping = true;
	stepCounter++;

	// Just to be sure.
	if (pauseAction == PAUSE_CONTINUE) {
		pauseAction = PAUSE_BREAK;
	}

	::coreState = CORE_STEPPING_GE;
	return true;
}

void ResumeFromStepping() {
	lastGState = gstate;
	isStepping = false;
	SetPauseAction(PAUSE_CONTINUE);
}

void Reset() {
	std::lock_guard<std::mutex> pauseGuard(pauseLock);
	std::lock_guard<std::mutex> guard(actionLock);
	isStepping = false;
	pauseAction = PAUSE_CONTINUE;
	lastGState = {};
}

bool IsStepping() {
	return isStepping;
}

int GetSteppingCounter() {
	return stepCounter;
}

// NOTE: This can't be called on the EmuThread!
// Called with requestLock held.
static bool GetBuffer(const GPUDebugBuffer *&buffer, PauseAction type, const GPUDebugBuffer &resultBuffer) {
	if (!isStepping && coreState != CORE_STEPPING_CPU) {
		return false;
	}

	if (!RequestPauseAction(type)) {
		return false;
	}
	buffer = &resultBuffer;
	return bufferResult;
}

bool GPU_GetOutputFramebuffer(const GPUDebugBuffer *&buffer) {
	std::lock_guard<std::mutex> guard(requestLock);
	return GetBuffer(buffer, PAUSE_GETOUTPUTBUF, bufferFrame);
}

bool GPU_GetCurrentFramebuffer(const GPUDebugBuffer *&buffer, GPUDebugFramebufferType type) {
	std::lock_guard<std::mutex> guard(requestLock);
	bufferType = type;
	return GetBuffer(buffer, PAUSE_GETFRAMEBUF, bufferFrame);
}

bool GPU_GetCurrentDepthbuffer(const GPUDebugBuffer *&buffer) {
	std::lock_guard<std::mutex> guard(requestLock);
	return GetBuffer(buffer, PAUSE_GETDEPTHBUF, bufferDepth);
}

bool GPU_GetCurrentStencilbuffer(const GPUDebugBuffer *&buffer) {
	std::lock_guard<std::mutex> guard(requestLock);
	return GetBuffer(buffer, PAUSE_GETSTENCILBUF, bufferStencil);
}

bool GPU_GetCurrentTexture(const GPUDebugBuffer *&buffer, int level, bool *isFramebuffer) {
	std::lock_guard<std::mutex> guard(requestLock);
	bufferLevel = level;
	bool result = GetBuffer(buffer, PAUSE_GETTEX, bufferTex);
	*isFramebuffer = lastWasFramebuffer;
	return result;
}

bool GPU_GetCurrentClut(const GPUDebugBuffer *&buffer) {
	std::lock_guard<std::mutex> guard(requestLock);
	return GetBuffer(buffer, PAUSE_GETCLUT, bufferClut);
}

bool GPU_SetCmdValue(u32 op) {
	std::lock_guard<std::mutex> guard(requestLock);
	if (!isStepping && coreState != CORE_STEPPING_CPU) {
		return false;
	}

	pauseSetCmdValue = op;
	return RequestPauseAction(PAUSE_SETCMDVALUE);
}

bool GPU_FlushDrawing() {
	std::lock_guard<std::mutex> guard(requestLock);
	if (!isStepping && coreState != CORE_STEPPING_CPU) {
		return false;
	}

	return RequestPauseAction(PAUSE_FLUSHDRAW);
}

const GEState &LastState() {
	return lastGState;
}

}  // namespace
