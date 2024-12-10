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

#include <mutex>
#include <condition_variable>

#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Core.h"
#include "GPU/Common/GPUDebugInterface.h"
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
// In case of accidental wakeup.
static volatile bool actionComplete;

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

static GPUgstate lastGState;

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

static void SetPauseAction(PauseAction act, bool waitComplete = true) {
	pauseLock.lock();
	std::unique_lock<std::mutex> guard(actionLock);
	pauseAction = act;
	pauseLock.unlock();

	// if (coreState == CORE_STEPPING && act != PAUSE_CONTINUE)
	// 	Core_UpdateSingleStep();
	actionComplete = false;
}

static void RunPauseAction() {
	std::lock_guard<std::mutex> guard(actionLock);
	if (pauseAction == PAUSE_BREAK) {
		// Don't notify, just go back, woke up by accident.
		return;
	}

	DEBUG_LOG(Log::GeDebugger, "RunPauseAction: %s", PauseActionToString(pauseAction));

	switch (pauseAction) {
	case PAUSE_BREAK:
		break;

	case PAUSE_GETOUTPUTBUF:
		bufferResult = gpuDebug->GetOutputFramebuffer(bufferFrame);
		break;

	case PAUSE_GETFRAMEBUF:
		bufferResult = gpuDebug->GetCurrentFramebuffer(bufferFrame, bufferType);
		break;

	case PAUSE_GETDEPTHBUF:
		bufferResult = gpuDebug->GetCurrentDepthbuffer(bufferDepth);
		break;

	case PAUSE_GETSTENCILBUF:
		bufferResult = gpuDebug->GetCurrentStencilbuffer(bufferStencil);
		break;

	case PAUSE_GETTEX:
		bufferResult = gpuDebug->GetCurrentTexture(bufferTex, bufferLevel, &lastWasFramebuffer);
		break;

	case PAUSE_GETCLUT:
		bufferResult = gpuDebug->GetCurrentClut(bufferClut);
		break;

	case PAUSE_SETCMDVALUE:
		gpuDebug->SetCmdValue(pauseSetCmdValue);
		break;

	case PAUSE_FLUSHDRAW:
		gpuDebug->DispatchFlush();
		break;

	default:
		ERROR_LOG(Log::GeDebugger, "Unsupported pause action, forgot to add it to the switch.");
		break;
	}

	actionComplete = true;
	actionWait.notify_all();

	pauseAction = PAUSE_BREAK;
}

void WaitForPauseAction() {
	std::unique_lock<std::mutex> guard(actionLock);
	actionWait.wait(guard);
}

static void StartStepping() {
	if (lastGState.cmdmem[1] == 0) {
		lastGState = gstate;
		// Play it safe so we don't keep resetting.
		lastGState.cmdmem[1] |= 0x01000000;
	}
	gpuDebug->NotifySteppingEnter();
	isStepping = true;
	stepCounter++;
}

static void StopStepping() {
	gpuDebug->NotifySteppingExit();
	lastGState = gstate;
	isStepping = false;
}

bool ProcessStepping() {
	_dbg_assert_(gpuDebug);

	std::unique_lock<std::mutex> guard(pauseLock);
	if (coreState != CORE_STEPPING_GE) {
		// Not stepping any more, don't try.
		actionComplete = true;
		actionWait.notify_all();
		return false;
	}

	if (pauseAction == PAUSE_CONTINUE) {
		// This is fine, can just mean to run to the next breakpoint/event.
		DEBUG_LOG(Log::GeDebugger, "Continuing...");
		actionComplete = true;
		actionWait.notify_all();
		coreState = CORE_RUNNING_GE;
		return false;
	}

	RunPauseAction();
	return true;
}

bool EnterStepping() {
	_dbg_assert_(gpuDebug);

	std::unique_lock<std::mutex> guard(pauseLock);
	if (coreState == CORE_STEPPING_GE) {
		// Already there. Should avoid this happening, I think.
		return true;
	}
	if (coreState != CORE_RUNNING_CPU && coreState != CORE_RUNNING_GE) {
		// ?? Shutting down, don't try to step.
		actionComplete = true;
		actionWait.notify_all();
		return false;
	}

	StartStepping();

	// Just to be sure.
	if (pauseAction == PAUSE_CONTINUE) {
		pauseAction = PAUSE_BREAK;
	}

	coreState = CORE_STEPPING_GE;
	return true;
}

void ResumeFromStepping() {
	StopStepping();
	SetPauseAction(PAUSE_CONTINUE, false);
}

bool IsStepping() {
	return isStepping;
}

int GetSteppingCounter() {
	return stepCounter;
}

// NOTE: This can't be called on the EmuThread!
static bool GetBuffer(const GPUDebugBuffer *&buffer, PauseAction type, const GPUDebugBuffer &resultBuffer) {
	if (!isStepping && coreState != CORE_STEPPING_CPU) {
		return false;
	}

	_dbg_assert_(strcmp(GetCurrentThreadName(), "EmuThread") != 0);

	SetPauseAction(type);
	WaitForPauseAction();
	buffer = &resultBuffer;
	return bufferResult;
}

bool GPU_GetOutputFramebuffer(const GPUDebugBuffer *&buffer) {
	return GetBuffer(buffer, PAUSE_GETOUTPUTBUF, bufferFrame);
}

bool GPU_GetCurrentFramebuffer(const GPUDebugBuffer *&buffer, GPUDebugFramebufferType type) {
	bufferType = type;
	return GetBuffer(buffer, PAUSE_GETFRAMEBUF, bufferFrame);
}

bool GPU_GetCurrentDepthbuffer(const GPUDebugBuffer *&buffer) {
	return GetBuffer(buffer, PAUSE_GETDEPTHBUF, bufferDepth);
}

bool GPU_GetCurrentStencilbuffer(const GPUDebugBuffer *&buffer) {
	return GetBuffer(buffer, PAUSE_GETSTENCILBUF, bufferStencil);
}

bool GPU_GetCurrentTexture(const GPUDebugBuffer *&buffer, int level, bool *isFramebuffer) {
	bufferLevel = level;
	bool result = GetBuffer(buffer, PAUSE_GETTEX, bufferTex);
	*isFramebuffer = lastWasFramebuffer;
	return result;
}

bool GPU_GetCurrentClut(const GPUDebugBuffer *&buffer) {
	return GetBuffer(buffer, PAUSE_GETCLUT, bufferClut);
}

bool GPU_SetCmdValue(u32 op) {
	if (!isStepping && coreState != CORE_STEPPING_CPU) {
		return false;
	}

	pauseSetCmdValue = op;
	SetPauseAction(PAUSE_SETCMDVALUE);
	return true;
}

bool GPU_FlushDrawing() {
	if (!isStepping && coreState != CORE_STEPPING_CPU) {
		return false;
	}

	SetPauseAction(PAUSE_FLUSHDRAW);
	return true;
}

GPUgstate LastState() {
	return lastGState;
}

}  // namespace
