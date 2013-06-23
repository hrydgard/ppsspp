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

#include <vector>
#include <cmath>

// TODO: Move the relevant parts into common. Don't want the core
// to be dependent on "native", I think. Or maybe should get rid of common
// and move everything into native...
#include "base/timeutil.h"

#include "Thread.h"
#include "../Core/CoreTiming.h"
#include "../Core/CoreParameter.h"
#include "Core/Reporting.h"
#include "../MIPS/MIPS.h"
#include "../HLE/HLE.h"
#include "sceAudio.h"
#include "../Host.h"
#include "../Config.h"
#include "../System.h"
#include "../Core/Core.h"
#include "sceDisplay.h"
#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelInterrupt.h"

// TODO: This file should not depend directly on GLES code.
#include "../../GPU/GLES/Framebuffer.h"
#include "../../GPU/GLES/ShaderManager.h"
#include "../../GPU/GLES/TextureCache.h"
#include "../../GPU/GPUState.h"
#include "../../GPU/GPUInterface.h"

#ifdef _WIN32
// Windows defines min/max which conflict with std::min/std::max.
#undef min
#undef max
#endif

struct FrameBufferState {
	u32 topaddr;
	PspDisplayPixelFormat pspFramebufFormat;
	int pspFramebufLinesize;
};

struct WaitVBlankInfo
{
	WaitVBlankInfo(u32 tid) : threadID(tid), vcountUnblock(1) {}
	WaitVBlankInfo(u32 tid, int vcount) : threadID(tid), vcountUnblock(vcount) {}
	u32 threadID;
	int vcountUnblock; // what was this for again?

	void DoState(PointerWrap &p)
	{
		p.Do(threadID);
		p.Do(vcountUnblock);
	}
};

// STATE BEGIN
static FrameBufferState framebuf;
static FrameBufferState latchedFramebuf;
static bool framebufIsLatched;

static int enterVblankEvent = -1;
static int leaveVblankEvent = -1;
static int afterFlipEvent = -1;

// hCount is computed now.
static int vCount;
static int isVblank;
static int numSkippedFrames;
static bool hasSetMode;
static int resumeMode;
static int holdMode;
static int mode;
static int width;
static int height;
// Don't include this in the state, time increases regardless of state.
static double curFrameTime;
static double nextFrameTime;

static u64 frameStartTicks;
const float hCountPerVblank = 285.72f; // insprired by jpcsp


std::vector<WaitVBlankInfo> vblankWaitingThreads;

// STATE END

// Called when vblank happens (like an internal interrupt.)  Not part of state, should be static.
std::vector<VblankCallback> vblankListeners;

// The vblank period is 731.5 us (0.7315 ms)
const double vblankMs = 0.7315;
const double frameMs = 1000.0 / 60.0;

enum {
	PSP_DISPLAY_SETBUF_IMMEDIATE = 0,
	PSP_DISPLAY_SETBUF_NEXTFRAME = 1
};

static int lastFpsFrame = 0;
static double lastFpsTime = 0.0;
static double fps = 0.0;
static double fpsHistory[120];
static size_t fpsHistoryPos = 0;
static size_t fpsHistoryValid = 0;
static int lastNumFlips = 0;
static int numFlips = 0;
static float flips = 0.0f;

void hleEnterVblank(u64 userdata, int cyclesLate);
void hleLeaveVblank(u64 userdata, int cyclesLate);
void hleAfterFlip(u64 userdata, int cyclesLate);

void __DisplayInit() {
	gpuStats.reset();
	hasSetMode = false;
	mode = 0;
	resumeMode = 0;
	holdMode = 0;
	width = 480;
	height = 272;
	numSkippedFrames = 0;
	framebufIsLatched = false;
	framebuf.topaddr = 0x04000000;
	framebuf.pspFramebufFormat = PSP_DISPLAY_PIXEL_FORMAT_8888;
	framebuf.pspFramebufLinesize = 480; // ??

	enterVblankEvent = CoreTiming::RegisterEvent("EnterVBlank", &hleEnterVblank);
	leaveVblankEvent = CoreTiming::RegisterEvent("LeaveVBlank", &hleLeaveVblank);
	afterFlipEvent = CoreTiming::RegisterEvent("AfterFlip", &hleAfterFlip);

	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs), enterVblankEvent, 0);
	isVblank = 0;
	vCount = 0;
	curFrameTime = 0.0;
	nextFrameTime = 0.0;

	fpsHistoryPos = 0;
	fpsHistoryValid = 0;

	InitGfxState();
}

void __DisplayDoState(PointerWrap &p) {
	p.Do(framebuf);
	p.Do(latchedFramebuf);
	p.Do(framebufIsLatched);
	p.Do(frameStartTicks);
	p.Do(vCount);
	p.Do(isVblank);
	p.Do(hasSetMode);
	p.Do(mode);
	p.Do(resumeMode);
	p.Do(holdMode);
	p.Do(width);
	p.Do(height);
	WaitVBlankInfo wvi(0);
	p.Do(vblankWaitingThreads, wvi);

	p.Do(enterVblankEvent);
	CoreTiming::RestoreRegisterEvent(enterVblankEvent, "EnterVBlank", &hleEnterVblank);
	p.Do(leaveVblankEvent);
	CoreTiming::RestoreRegisterEvent(leaveVblankEvent, "LeaveVBlank", &hleLeaveVblank);
	p.Do(afterFlipEvent);
	CoreTiming::RestoreRegisterEvent(afterFlipEvent, "AfterFlipEVent", &hleAfterFlip);

	p.Do(gstate);
	p.Do(gstate_c);
	p.Do(gpuStats);
	gpu->DoState(p);

	ReapplyGfxState();

	if (p.mode == p.MODE_READ) {
		if (hasSetMode) {
			gpu->InitClear();
		}
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
	}

	p.DoMarker("sceDisplay");
}

void __DisplayShutdown() {
	vblankListeners.clear();
	vblankWaitingThreads.clear();
	ShutdownGfxState();
}

void __DisplayListenVblank(VblankCallback callback) {
	vblankListeners.push_back(callback);
}

void __DisplayFireVblank() {
	for (std::vector<VblankCallback>::iterator iter = vblankListeners.begin(), end = vblankListeners.end(); iter != end; ++iter) {
		VblankCallback cb = *iter;
		cb();
	}
}

void __DisplayGetFPS(float *out_vps, float *out_fps) {
	*out_vps = fps;
	*out_fps = flips;
}

void __DisplayGetAveragedFPS(float *out_vps, float *out_fps) {
	float avg = 0.0;
	if (fpsHistoryValid > 0) {
		if (fpsHistoryValid > ARRAY_SIZE(fpsHistory)) {
			fpsHistoryValid = ARRAY_SIZE(fpsHistory);
		}
		for (size_t i = 0; i < fpsHistoryValid; ++i) {
			avg += fpsHistory[i];
		}
		avg /= (double) fpsHistoryValid;
	}

	*out_vps = *out_fps = avg;
}

void CalculateFPS()
{
	time_update();
	double now = time_now_d();

	if (now >= lastFpsTime + 1.0)
	{
		double frames = (gpuStats.numFrames - lastFpsFrame);
		fps = frames / (now - lastFpsTime);
		flips = 60.0 * (double) (numFlips - lastNumFlips) / frames;

		lastFpsFrame = gpuStats.numFrames;
		lastNumFlips = numFlips;
		lastFpsTime = now;

		fpsHistory[fpsHistoryPos++] = fps;
		fpsHistoryPos = fpsHistoryPos % ARRAY_SIZE(fpsHistory);
		++fpsHistoryValid;
	}
}

void __DisplayGetDebugStats(char stats[2048])
{
	gpu->UpdateStats();

	float vertexAverageCycles = gpuStats.numVertsSubmitted > 0 ? (float)gpuStats.vertexGPUCycles / (float)gpuStats.numVertsSubmitted : 0.0f;

	sprintf(stats,
		"Frames: %i\n"
		"DL processing time: %0.2f ms\n"
		"Kernel processing time: %0.2f ms\n"
		"Slowest syscall: %s : %0.2f ms\n"
		"Most active syscall: %s : %0.2f ms\n"
		"Draw calls: %i, flushes %i\n"
		"Cached Draw calls: %i\n"
		"Num Tracked Vertex Arrays: %i\n"
		"Cycles executed: %d (%f per vertex)\n"
		"Vertices Submitted: %i\n"
		"Cached Vertices Drawn: %i\n"
		"Uncached Vertices Drawn: %i\n"
		"FBOs active: %i\n"
		"Textures active: %i, decoded: %i\n"
		"Texture invalidations: %i\n"
		"Vertex shaders loaded: %i\n"
		"Fragment shaders loaded: %i\n"
		"Combined shaders loaded: %i\n",
		gpuStats.numFrames,
		gpuStats.msProcessingDisplayLists * 1000.0f,
		kernelStats.msInSyscalls * 1000.0f,
		kernelStats.slowestSyscallName ? kernelStats.slowestSyscallName : "(none)",
		kernelStats.slowestSyscallTime * 1000.0f,
		kernelStats.summedSlowestSyscallName ? kernelStats.summedSlowestSyscallName : "(none)",
		kernelStats.summedSlowestSyscallTime * 1000.0f,
		gpuStats.numDrawCalls,
		gpuStats.numFlushes,
		gpuStats.numCachedDrawCalls,
		gpuStats.numTrackedVertexArrays,
		gpuStats.vertexGPUCycles + gpuStats.otherGPUCycles,
		vertexAverageCycles,
		gpuStats.numVertsSubmitted,
		gpuStats.numCachedVertsDrawn,
		gpuStats.numUncachedVertsDrawn,
		gpuStats.numFBOs,
		gpuStats.numTextures,
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		gpuStats.numVertexShaders,
		gpuStats.numFragmentShaders,
		gpuStats.numShaders
		);

	gpuStats.resetFrame();
	kernelStats.ResetFrame();
}

enum {
	FPS_LIMIT_NORMAL = 0,
	FPS_LIMIT_CUSTOM = 1,
	FPS_LIMIT_TURBO = 2,
};

// Let's collect all the throttling and frameskipping logic here.
void DoFrameTiming(bool &throttle, bool &skipFrame) {
	int fpsLimiter = PSP_CoreParameter().fpsLimit;
	throttle = !PSP_CoreParameter().unthrottle && fpsLimiter != FPS_LIMIT_TURBO;
	skipFrame = false;

	// Check if the frameskipping code should be enabled. If neither throttling or frameskipping is on,
	// we have nothing to do here.
	bool doFrameSkip = g_Config.iFrameSkip != 0;

	// On non windows, which is always vsync locked, we need to force frameskip when
	// unthrottled.
#ifndef _WIN32
	if (!throttle) {
		doFrameSkip = true;
		skipFrame = true;
		if (numSkippedFrames >= 6) {
			skipFrame = false;
		}
		return;	
	}
#endif

	if (!throttle && !doFrameSkip)
		return;
	
	time_update();
	
	curFrameTime = time_now_d();
	if (nextFrameTime == 0.0)
		nextFrameTime = time_now_d() + 1.0 / 60.0;
	
	if (curFrameTime > nextFrameTime && doFrameSkip) {
		// Argh, we are falling behind! Let's skip a frame and see if we catch up.
		skipFrame = true;
		// INFO_LOG(HLE,"FRAMESKIP %i", numSkippedFrames);
	}

	
	if (curFrameTime < nextFrameTime && throttle)
	{
		// If time gap is huge just jump (somebody unthrottled)
		if (nextFrameTime - curFrameTime > 1.0 / 30.0) {
			nextFrameTime = curFrameTime + 1.0 / 60.0;
		} else {
			// Wait until we've caught up.
			while (time_now_d() < nextFrameTime) {
				Common::SleepCurrentThread(1);
				time_update();
			}
		}
		curFrameTime = time_now_d();
	}
	// Advance lastFrameTime by a constant amount each frame,
	// but don't let it get too far behind as things can get very jumpy.
	const double maxFallBehindFrames = 5.5;

	// 3 states of fps limiter
	if (fpsLimiter == FPS_LIMIT_NORMAL) {
		nextFrameTime = std::max(nextFrameTime + 1.0 / 60.0, time_now_d() - maxFallBehindFrames / 60.0);
	} else if (fpsLimiter == FPS_LIMIT_CUSTOM) {
		double customLimiter = g_Config.iFpsLimit;
		nextFrameTime = std::max(nextFrameTime + 1.0 / customLimiter, time_now_d() - maxFallBehindFrames / customLimiter);
	} else {
		nextFrameTime = nextFrameTime + 1.0 / 60.0;
	}

	// Max 4 skipped frames in a row - 15 fps is really the bare minimum for playability.
	// We check for 3 here so it's 3 skipped frames, 1 non skipped, 3 skipped, etc.
	if (numSkippedFrames >= g_Config.iFrameSkip) {
		skipFrame = false;
	}
}


void hleEnterVblank(u64 userdata, int cyclesLate) {
	int vbCount = userdata;

	DEBUG_LOG(HLE, "Enter VBlank %i", vbCount);

	isVblank = 1;
	vCount++; // // vCount increases at each VBLANK.

	// Fire the vblank listeners before we wake threads.
	__DisplayFireVblank();

	// Wake up threads waiting for VBlank
	for (size_t i = 0; i < vblankWaitingThreads.size(); i++) {
		if (--vblankWaitingThreads[i].vcountUnblock == 0) {
			__KernelResumeThreadFromWait(vblankWaitingThreads[i].threadID, 0);
			vblankWaitingThreads.erase(vblankWaitingThreads.begin() + i--);
		}
	}

	// Trigger VBlank interrupt handlers.
	__TriggerInterrupt(PSP_INTR_IMMEDIATE | PSP_INTR_ONLY_IF_ENABLED | PSP_INTR_ALWAYS_RESCHED, PSP_VBLANK_INTR, PSP_INTR_SUB_ALL);

	CoreTiming::ScheduleEvent(msToCycles(vblankMs) - cyclesLate, leaveVblankEvent, vbCount + 1);

	// TODO: Should this be done here or in hleLeaveVblank?
	if (framebufIsLatched) {
		DEBUG_LOG(HLE, "Setting latched framebuffer %08x (prev: %08x)", latchedFramebuf.topaddr, framebuf.topaddr);
		framebuf = latchedFramebuf;
		framebufIsLatched = false;
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
	}

	gpuStats.numFrames++;

	if (g_Config.iShowFPSCounter) {
		CalculateFPS();
	}

	bool skipFlip = false;

	// This frame was skipped, so no need to flip.
	if (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) {
		skipFlip = true;
	}

	// Draw screen overlays before blitting. Saves and restores the Ge context.
	// Yeah, this has to be the right moment to end the frame. Give the graphics backend opportunity
	// to blit the framebuffer, in order to support half-framerate games that otherwise wouldn't have
	// anything to draw here.
	gstate_c.skipDrawReason &= ~SKIPDRAW_SKIPFRAME;

	bool throttle, skipFrame;
	DoFrameTiming(throttle, skipFrame);

	if (skipFrame) {
		gstate_c.skipDrawReason |= SKIPDRAW_SKIPFRAME;
		numSkippedFrames++;
	} else {
		numSkippedFrames = 0;
	}

	if (!skipFlip) {
		// Setting CORE_NEXTFRAME causes a swap.
		// Check first though, might've just quit / been paused.
		if (coreState == CORE_RUNNING && gpu->FramebufferDirty()) {
			coreState = CORE_NEXTFRAME;
		}

		gpu->CopyDisplayToOutput();
	}

	// Returning here with coreState == CORE_NEXTFRAME causes a buffer flip to happen (next frame).
	// Right after, we regain control for a little bit in hleAfterFlip. I think that's a great
	// place to do housekeeping.
	CoreTiming::ScheduleEvent(0 - cyclesLate, afterFlipEvent, 0);
}

void hleAfterFlip(u64 userdata, int cyclesLate)
{
	gpu->BeginFrame();  // doesn't really matter if begin or end of frame.
}

void hleLeaveVblank(u64 userdata, int cyclesLate) {
	isVblank = 0;
	DEBUG_LOG(HLE,"Leave VBlank %i", (int)userdata - 1);
	frameStartTicks = CoreTiming::GetTicks();
	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs) - cyclesLate, enterVblankEvent, userdata);
}

u32 sceDisplayIsVblank() {
	DEBUG_LOG(HLE,"%i=sceDisplayIsVblank()",isVblank);
	return isVblank;
}

u32 sceDisplaySetMode(int displayMode, int displayWidth, int displayHeight) {
	DEBUG_LOG(HLE,"sceDisplaySetMode(%i, %i, %i)", displayMode, displayWidth, displayHeight);

	if (!hasSetMode) {
		gpu->InitClear();
		hasSetMode = true;
	}
	mode = displayMode;
	width = displayWidth;
	height = displayHeight;
	return 0;
}

u32 sceDisplaySetFramebuf(u32 topaddr, int linesize, int pixelformat, int sync) {
	FrameBufferState fbstate;
	DEBUG_LOG(HLE,"sceDisplaySetFramebuf(topaddr=%08x,linesize=%d,pixelsize=%d,sync=%d)", topaddr, linesize, pixelformat, sync);
	if (topaddr == 0) {
		DEBUG_LOG(HLE,"- screen off");
	} else {
		fbstate.topaddr = topaddr;
		fbstate.pspFramebufFormat = (PspDisplayPixelFormat)pixelformat;
		fbstate.pspFramebufLinesize = linesize;
	}

	if (topaddr != framebuf.topaddr) {
		++numFlips;
	}

	if (sync == PSP_DISPLAY_SETBUF_IMMEDIATE) {
		// Write immediately to the current framebuffer parameters
		if (topaddr != 0) {
			framebuf = fbstate;
			gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
		} else {
			WARN_LOG(HLE, "%s: PSP_DISPLAY_SETBUF_IMMEDIATE without topaddr?", __FUNCTION__);
		}
	} else if (topaddr != 0) {
		// Delay the write until vblank
		latchedFramebuf = fbstate;
		framebufIsLatched = true;
	}
	return 0;
}

bool __DisplayGetFramebuf(u8 **topaddr, u32 *linesize, u32 *pixelFormat, int latchedMode) {
	const FrameBufferState &fbState = latchedMode == 1 ? latchedFramebuf : framebuf;
	if (topaddr != NULL)
		*topaddr = Memory::GetPointer(fbState.topaddr);
	if (linesize != NULL)
		*linesize = fbState.pspFramebufLinesize;
	if (pixelFormat != NULL)
		*pixelFormat = fbState.pspFramebufFormat;

	return true;
}

u32 sceDisplayGetFramebuf(u32 topaddrPtr, u32 linesizePtr, u32 pixelFormatPtr, int latchedMode) {
	const FrameBufferState &fbState = latchedMode == 1 ? latchedFramebuf : framebuf;
	DEBUG_LOG(HLE,"sceDisplayGetFramebuf(*%08x = %08x, *%08x = %08x, *%08x = %08x, %i)", topaddrPtr, fbState.topaddr, linesizePtr, fbState.pspFramebufLinesize, pixelFormatPtr, fbState.pspFramebufFormat, mode);

	if (Memory::IsValidAddress(topaddrPtr))
		Memory::Write_U32(fbState.topaddr, topaddrPtr);
	if (Memory::IsValidAddress(linesizePtr))
		Memory::Write_U32(fbState.pspFramebufLinesize, linesizePtr);
	if (Memory::IsValidAddress(pixelFormatPtr))
		Memory::Write_U32(fbState.pspFramebufFormat, pixelFormatPtr);

	return 0;
}

u32 sceDisplayWaitVblankStart() {
	VERBOSE_LOG(HLE,"sceDisplayWaitVblankStart()");
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, false, "vblank start waited");
	return 0;
}

u32 sceDisplayWaitVblank() {
	if (!isVblank) {
		VERBOSE_LOG(HLE,"sceDisplayWaitVblank()");
		vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
		__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, false, "vblank waited");
		return 0;
	} else {
		DEBUG_LOG(HLE,"sceDisplayWaitVblank() - not waiting since in vBlank");
		hleEatCycles(1110);
		return 1;
	}
}

u32 sceDisplayWaitVblankStartMulti(int vblanks) {
	if (vblanks <= 0) {
		WARN_LOG(HLE, "sceDisplayWaitVblankStartMulti(%d): invalid number of vblanks", vblanks);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}
	VERBOSE_LOG(HLE, "sceDisplayWaitVblankStartMulti(%d)", vblanks);
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread(), vblanks));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, false, "vblank start multi waited");
	return 0;
}

u32 sceDisplayWaitVblankCB() {
	if (!isVblank) {
		VERBOSE_LOG(HLE,"sceDisplayWaitVblankCB()");
		vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
		__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true, "vblank waited");
		return 0;
	} else {
		DEBUG_LOG(HLE,"sceDisplayWaitVblank() - not waiting since in vBlank");
		hleEatCycles(1110);
		return 1;
	}
}

u32 sceDisplayWaitVblankStartCB() {
	VERBOSE_LOG(HLE,"sceDisplayWaitVblankStartCB()");
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true, "vblank start waited");
	return 0;
}

u32 sceDisplayWaitVblankStartMultiCB(int vblanks) {
	if (vblanks <= 0) {
		WARN_LOG(HLE, "sceDisplayWaitVblankStartMultiCB(%d): invalid number of vblanks", vblanks);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}
	VERBOSE_LOG(HLE,"sceDisplayWaitVblankStartMultiCB(%d)", vblanks);
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread(), vblanks));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true, "vblank start multi waited");
	return 0;
}

u32 sceDisplayGetVcount() {
	VERBOSE_LOG(HLE,"%i=sceDisplayGetVcount()", vCount);

	hleEatCycles(150);
	return vCount;
}

u32 sceDisplayGetCurrentHcount() {
	u32 currentHCount = (CoreTiming::GetTicks() - frameStartTicks) / ((u64)CoreTiming::GetClockFrequencyMHz() * 1000000 / 60 / hCountPerVblank);
	DEBUG_LOG(HLE,"%i=sceDisplayGetCurrentHcount()", currentHCount);
	hleEatCycles(275);
	return currentHCount;
}

u32 sceDisplayAdjustAccumulatedHcount() {
	ERROR_LOG_REPORT(HLE,"UNIMPL sceDisplayAdjustAccumulatedHcount()");
	return 0;
}

u32 sceDisplayGetAccumulatedHcount() {
	u32 currentHCount = (CoreTiming::GetTicks() - frameStartTicks) / ((u64)CoreTiming::GetClockFrequencyMHz() * 1000000 / 60 / hCountPerVblank);
	u32 accumHCount = currentHCount + (u32) (vCount * hCountPerVblank);
	DEBUG_LOG(HLE,"%i=sceDisplayGetAccumulatedHcount()", accumHCount);
	hleEatCycles(235);
	return accumHCount;
}

float sceDisplayGetFramePerSec() {
	const static float framePerSec = 59.9400599f;
	DEBUG_LOG(HLE,"%f=sceDisplayGetFramePerSec()", framePerSec);
	return framePerSec;	// (9MHz * 1)/(525 * 286)
}

u32 sceDisplayIsForeground() {
  	DEBUG_LOG(HLE,"IMPL sceDisplayIsForeground()");	
	if (!hasSetMode || framebuf.topaddr == 0)
		return 0;
	else
  		return 1;   // return value according to JPCSP comment
}

u32 sceDisplayGetMode(u32 modeAddr, u32 widthAddr, u32 heightAddr) {
	DEBUG_LOG(HLE,"sceDisplayGetMode(%08x, %08x, %08x)", modeAddr, widthAddr, heightAddr);
	if (Memory::IsValidAddress(modeAddr))
		Memory::Write_U32(mode, modeAddr);
	if (Memory::IsValidAddress(widthAddr))
		Memory::Write_U32(width, widthAddr);
	if (Memory::IsValidAddress(heightAddr))
		Memory::Write_U32(height, heightAddr);
	return 0;
}

u32 sceDisplayIsVsync() {
	ERROR_LOG(HLE,"UNIMPL sceDisplayIsVsync()");
	return 0;
}

u32 sceDisplayGetResumeMode(u32 resumeModeAddr) {
	ERROR_LOG(HLE,"sceDisplayGetResumeMode(%08x)", resumeModeAddr);
	if (Memory::IsValidAddress(resumeModeAddr))
		Memory::Write_U32(resumeMode, resumeModeAddr);
	return 0;
}

u32 sceDisplaySetResumeMode(u32 rMode) {
	ERROR_LOG(HLE,"sceDisplaySetResumeMode(%08x)", rMode);
	resumeMode = rMode;
	return 0;
}

u32 sceDisplayGetBrightness(u32 levelAddr) {
	ERROR_LOG(HLE,"UNIMPL sceDisplayGetBrightness(%08x)", levelAddr);
	return 0;
}

u32 sceDisplaySetHoldMode(u32 hMode) {
	ERROR_LOG(HLE,"sceDisplaySetHoldMode(%08x)", hMode);
	holdMode = hMode;
	return 0;
}

const HLEFunction sceDisplay[] = {
	{0x0E20F177,WrapU_III<sceDisplaySetMode>, "sceDisplaySetMode"},
	{0x289D82FE,WrapU_UIII<sceDisplaySetFramebuf>, "sceDisplaySetFrameBuf"},
	{0xEEDA2E54,WrapU_UUUI<sceDisplayGetFramebuf>,"sceDisplayGetFrameBuf"},
	{0x36CDFADE,WrapU_V<sceDisplayWaitVblank>, "sceDisplayWaitVblank"},
	{0x984C27E7,WrapU_V<sceDisplayWaitVblankStart>, "sceDisplayWaitVblankStart"},
	{0x40f1469c,WrapU_I<sceDisplayWaitVblankStartMulti>, "sceDisplayWaitVblankStartMulti"},
	{0x8EB9EC49,WrapU_V<sceDisplayWaitVblankCB>, "sceDisplayWaitVblankCB"},
	{0x46F186C3,WrapU_V<sceDisplayWaitVblankStartCB>, "sceDisplayWaitVblankStartCB"},
	{0x77ed8b3a,WrapU_I<sceDisplayWaitVblankStartMultiCB>,"sceDisplayWaitVblankStartMultiCB"},
	{0xdba6c4c4,WrapF_V<sceDisplayGetFramePerSec>,"sceDisplayGetFramePerSec"},
	{0x773dd3a3,WrapU_V<sceDisplayGetCurrentHcount>,"sceDisplayGetCurrentHcount"},
	{0x210eab3a,WrapU_V<sceDisplayGetAccumulatedHcount>,"sceDisplayGetAccumulatedHcount"},
	{0xA83EF139,WrapU_V<sceDisplayAdjustAccumulatedHcount>,"sceDisplayAdjustAccumulatedHcount"},
	{0x9C6EAAD7,WrapU_V<sceDisplayGetVcount>,"sceDisplayGetVcount"},
	{0xDEA197D4,WrapU_UUU<sceDisplayGetMode>,"sceDisplayGetMode"},
	{0x7ED59BC4,WrapU_U<sceDisplaySetHoldMode>,"sceDisplaySetHoldMode"},
	{0xA544C486,WrapU_U<sceDisplaySetResumeMode>,"sceDisplaySetResumeMode"},
	{0xBF79F646,WrapU_U<sceDisplayGetResumeMode>,"sceDisplayGetResumeMode"},
	{0xB4F378FA,WrapU_V<sceDisplayIsForeground>,"sceDisplayIsForeground"},
	{0x31C4BAA8,WrapU_U<sceDisplayGetBrightness>,"sceDisplayGetBrightness"},
	{0x4D4E10EC,WrapU_V<sceDisplayIsVblank>,"sceDisplayIsVblank"},
	{0x21038913,WrapU_V<sceDisplayIsVsync>,"sceDisplayIsVsync"},
};

void Register_sceDisplay() {
	RegisterModule("sceDisplay", ARRAY_SIZE(sceDisplay), sceDisplay);
}
