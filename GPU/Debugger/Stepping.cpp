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
static std::condition_variable pauseWait;
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

static void SetPauseAction(PauseAction act, bool waitComplete = true) {
	pauseLock.lock();
	std::unique_lock<std::mutex> guard(actionLock);
	pauseAction = act;
	pauseLock.unlock();

	if (coreState == CORE_STEPPING && act != PAUSE_CONTINUE)
		Core_UpdateSingleStep();

	actionComplete = false;
	pauseWait.notify_all();
	while (waitComplete && !actionComplete) {
		actionWait.wait(guard);
	}
}

static void RunPauseAction() {
	std::lock_guard<std::mutex> guard(actionLock);

	switch (pauseAction) {
	case PAUSE_CONTINUE:
		// Don't notify, just go back, woke up by accident.
		return;

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
		ERROR_LOG(Log::G3D, "Unsupported pause action, forgot to add it to the switch.");
	}

	actionComplete = true;
	actionWait.notify_all();
	pauseAction = PAUSE_BREAK;
}

static void StartStepping() {
	if (lastGState.cmdmem[1] == 0) {
		lastGState = gstate;
		// Play it safe so we don't keep resetting.
		lastGState.cmdmem[1] |= 0x01000000;
	}
	gpuDebug->NotifySteppingEnter();
	isStepping = true;
}

static void StopStepping() {
	gpuDebug->NotifySteppingExit();
	lastGState = gstate;
	isStepping = false;
}

bool SingleStep() {
	std::unique_lock<std::mutex> guard(pauseLock);
	if (coreState != CORE_RUNNING && coreState != CORE_NEXTFRAME && coreState != CORE_STEPPING) {
		// Shutting down, don't try to step.
		actionComplete = true;
		actionWait.notify_all();
		return false;
	}
	if (!gpuDebug || pauseAction == PAUSE_CONTINUE) {
		actionComplete = true;
		actionWait.notify_all();
		return false;
	}

	StartStepping();
	RunPauseAction();
	StopStepping();
	return true;
}

bool EnterStepping() {
	std::unique_lock<std::mutex> guard(pauseLock);
	if (coreState != CORE_RUNNING && coreState != CORE_NEXTFRAME && coreState != CORE_STEPPING) {
		// Shutting down, don't try to step.
		actionComplete = true;
		actionWait.notify_all();
		return false;
	}
	if (!gpuDebug) {
		actionComplete = true;
		actionWait.notify_all();
		return false;
	}

	StartStepping();

	// Just to be sure.
	if (pauseAction == PAUSE_CONTINUE) {
		pauseAction = PAUSE_BREAK;
	}
	stepCounter++;

	do {
		RunPauseAction();
		pauseWait.wait(guard);
	} while (pauseAction != PAUSE_CONTINUE);

	StopStepping();
	return true;
}

bool IsStepping() {
	return isStepping;
}

int GetSteppingCounter() {
	return stepCounter;
}

static bool GetBuffer(const GPUDebugBuffer *&buffer, PauseAction type, const GPUDebugBuffer &resultBuffer) {
	if (!isStepping && coreState != CORE_STEPPING) {
		return false;
	}

	SetPauseAction(type);
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
	if (!isStepping && coreState != CORE_STEPPING) {
		return false;
	}

	pauseSetCmdValue = op;
	SetPauseAction(PAUSE_SETCMDVALUE);
	return true;
}

bool GPU_FlushDrawing() {
	if (!isStepping && coreState != CORE_STEPPING) {
		return false;
	}

	SetPauseAction(PAUSE_FLUSHDRAW);
	return true;
}

void ResumeFromStepping() {
	SetPauseAction(PAUSE_CONTINUE, false);
}

void ForceUnpause() {
	SetPauseAction(PAUSE_CONTINUE, false);
	actionComplete = true;
	actionWait.notify_all();
}

GPUgstate LastState() {
	return lastGState;
}

}  // namespace
