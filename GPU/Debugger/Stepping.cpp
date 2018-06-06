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

#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/GPUState.h"
#include "Core/Core.h"

namespace GPUStepping {

enum PauseAction {
	PAUSE_CONTINUE,
	PAUSE_BREAK,
	PAUSE_GETFRAMEBUF,
	PAUSE_GETDEPTHBUF,
	PAUSE_GETSTENCILBUF,
	PAUSE_GETTEX,
	PAUSE_GETCLUT,
	PAUSE_SETCMDVALUE,
};

static bool isStepping;

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
static u32 pauseSetCmdValue;

static void SetPauseAction(PauseAction act, bool waitComplete = true) {
	pauseLock.lock();
	std::unique_lock<std::mutex> guard(actionLock);
	pauseAction = act;
	pauseLock.unlock();

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
		bufferResult = gpuDebug->GetCurrentTexture(bufferTex, bufferLevel);
		break;

	case PAUSE_GETCLUT:
		bufferResult = gpuDebug->GetCurrentClut(bufferClut);
		break;

	case PAUSE_SETCMDVALUE:
		gpuDebug->SetCmdValue(pauseSetCmdValue);
		break;

	default:
		ERROR_LOG(G3D, "Unsupported pause action, forgot to add it to the switch.");
	}

	actionComplete = true;
	actionWait.notify_all();
	pauseAction = PAUSE_BREAK;
}

bool EnterStepping(std::function<void()> callback) {
	std::unique_lock<std::mutex> guard(pauseLock);
	if (coreState != CORE_RUNNING && coreState != CORE_NEXTFRAME) {
		// Shutting down, don't try to step.
		return false;
	}
	if (!gpuDebug) {
		return false;
	}

	gpuDebug->NotifySteppingEnter();

	// Just to be sure.
	if (pauseAction == PAUSE_CONTINUE) {
		pauseAction = PAUSE_BREAK;
	}
	isStepping = true;

	callback();

	do {
		RunPauseAction();
		pauseWait.wait(guard);
	} while (pauseAction != PAUSE_CONTINUE);

	gpuDebug->NotifySteppingExit();
	isStepping = false;
	return true;
}

bool IsStepping() {
	return isStepping;
}

static bool GetBuffer(const GPUDebugBuffer *&buffer, PauseAction type, const GPUDebugBuffer &resultBuffer) {
	if (!isStepping) {
		return false;
	}

	SetPauseAction(type);
	buffer = &resultBuffer;
	return bufferResult;
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

bool GPU_GetCurrentTexture(const GPUDebugBuffer *&buffer, int level) {
	bufferLevel = level;
	return GetBuffer(buffer, PAUSE_GETTEX, bufferTex);
}

bool GPU_GetCurrentClut(const GPUDebugBuffer *&buffer) {
	return GetBuffer(buffer, PAUSE_GETCLUT, bufferClut);
}

bool GPU_SetCmdValue(u32 op) {
	if (!isStepping) {
		return false;
	}

	pauseSetCmdValue = op;
	SetPauseAction(PAUSE_SETCMDVALUE);
	return true;
}

void ResumeFromStepping() {
	SetPauseAction(PAUSE_CONTINUE, false);
}

void ForceUnpause(CoreLifecycle stage) {
	if (stage == CoreLifecycle::STOPPING) {
		SetPauseAction(PAUSE_CONTINUE, false);
		actionComplete = true;
		actionWait.notify_all();
	}
}

}  // namespace
