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
#include <map>
#include <cmath>
#include <algorithm>

// TODO: Move the relevant parts into common. Don't want the core
// to be dependent on "native", I think. Or maybe should get rid of common
// and move everything into native...
#include "base/logging.h"
#include "base/timeutil.h"

#ifndef _XBOX
#include "gfx_es2/gl_state.h"
#endif

#include "Common/ChunkFile.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"

#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"

struct FrameBufferState {
	u32 topaddr;
	GEBufferFormat pspFramebufFormat;
	int pspFramebufLinesize;
};

struct WaitVBlankInfo
{
	WaitVBlankInfo(u32 tid) : threadID(tid), vcountUnblock(1) {}
	WaitVBlankInfo(u32 tid, int vcount) : threadID(tid), vcountUnblock(vcount) {}
	SceUID threadID;
	// Number of vcounts to block for.
	int vcountUnblock;

	void DoState(PointerWrap &p)
	{
		auto s = p.Section("WaitVBlankInfo", 1);
		if (!s)
			return;

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
// The "AccumulatedHcount" can be adjusted, this is the base.
static u32 hCountBase;
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
static int numVBlanksSinceFlip;

static u64 frameStartTicks;
const int hCountPerVblank = 286;


std::vector<WaitVBlankInfo> vblankWaitingThreads;
// Key is the callback id it was for, or if no callback, the thread id.
// Value is the goal vcount number (in case the callback takes >= 1 vcount to return.)
std::map<SceUID, int> vblankPausedWaits;

// STATE END

// Called when vblank happens (like an internal interrupt.)  Not part of state, should be static.
std::vector<VblankCallback> vblankListeners;

// The vblank period is 731.5 us (0.7315 ms)
const double vblankMs = 0.7315;
const double frameMs = 1001.0 / 60.0;

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
static float flips = 0.0f;
static int actualFlips = 0;  // taking frameskip into account
static int lastActualFlips = 0;
static float actualFps = 0;
static u64 lastFlipCycles = 0;
// For the "max 60 fps" setting.
static int lastFlipsTooFrequent = 0;

void hleEnterVblank(u64 userdata, int cyclesLate);
void hleLeaveVblank(u64 userdata, int cyclesLate);
void hleAfterFlip(u64 userdata, int cyclesLate);

void __DisplayVblankBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __DisplayVblankEndCallback(SceUID threadID, SceUID prevCallbackId);
int __DisplayGetFlipCount() { return actualFlips; }
int __DisplayGetVCount() { return vCount; }

void __DisplayInit() {
	gpuStats.Reset();
	hasSetMode = false;
	mode = 0;
	resumeMode = 0;
	holdMode = 0;
	width = 480;
	height = 272;
	numSkippedFrames = 0;
	numVBlanksSinceFlip = 0;
	framebufIsLatched = false;
	framebuf.topaddr = 0x04000000;
	framebuf.pspFramebufFormat = GE_FORMAT_8888;
	framebuf.pspFramebufLinesize = 480; // ??
	lastFlipCycles = 0;
	lastFlipsTooFrequent = 0;

	enterVblankEvent = CoreTiming::RegisterEvent("EnterVBlank", &hleEnterVblank);
	leaveVblankEvent = CoreTiming::RegisterEvent("LeaveVBlank", &hleLeaveVblank);
	afterFlipEvent = CoreTiming::RegisterEvent("AfterFlip", &hleAfterFlip);

	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs), enterVblankEvent, 0);
	isVblank = 0;
	vCount = 0;
	hCountBase = 0;
	curFrameTime = 0.0;
	nextFrameTime = 0.0;

	flips = 0;
	fps = 0.0;
	actualFlips = 0;
	lastActualFlips = 0;
	lastNumFlips = 0;
	fpsHistoryValid = 0;
	fpsHistoryPos = 0;
	fpsHistoryValid = 0;

	InitGfxState();

	__KernelRegisterWaitTypeFuncs(WAITTYPE_VBLANK, __DisplayVblankBeginCallback, __DisplayVblankEndCallback);
}

void __DisplayDoState(PointerWrap &p) {
	auto s = p.Section("sceDisplay", 1, 3);
	if (!s)
		return;

	p.Do(framebuf);
	p.Do(latchedFramebuf);
	p.Do(framebufIsLatched);
	p.Do(frameStartTicks);
	p.Do(vCount);
	if (s <= 2) {
		double oldHCountBase;
		p.Do(oldHCountBase);
		hCountBase = (int) oldHCountBase;
	} else {
		p.Do(hCountBase);
	}
	p.Do(isVblank);
	p.Do(hasSetMode);
	p.Do(mode);
	p.Do(resumeMode);
	p.Do(holdMode);
	p.Do(width);
	p.Do(height);
	WaitVBlankInfo wvi(0);
	p.Do(vblankWaitingThreads, wvi);
	p.Do(vblankPausedWaits);

	p.Do(enterVblankEvent);
	CoreTiming::RestoreRegisterEvent(enterVblankEvent, "EnterVBlank", &hleEnterVblank);
	p.Do(leaveVblankEvent);
	CoreTiming::RestoreRegisterEvent(leaveVblankEvent, "LeaveVBlank", &hleLeaveVblank);
	p.Do(afterFlipEvent);
	CoreTiming::RestoreRegisterEvent(afterFlipEvent, "AfterFlip", &hleAfterFlip);

	p.Do(gstate);
	p.Do(gstate_c);
#ifndef _XBOX
	if (s < 2) {
		// This shouldn't have been savestated anyway, but it was.
		// It's unlikely to overlap with the first value in gpuStats.
		p.ExpectVoid(&gl_extensions.gpuVendor, sizeof(gl_extensions.gpuVendor));
	}
#endif
	p.Do(gpuStats);
	gpu->DoState(p);

	ReapplyGfxState();

	if (p.mode == p.MODE_READ) {
		if (hasSetMode) {
			gpu->InitClear();
		}
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
	}
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

void __DisplayVblankBeginCallback(SceUID threadID, SceUID prevCallbackId) {
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	// This means two callbacks in a row.  PSP crashes if the same callback waits inside itself (may need more testing.)
	// TODO: Handle this better?
	if (vblankPausedWaits.find(pauseKey) != vblankPausedWaits.end()) {
		return;
	}

	WaitVBlankInfo waitData(0);
	for (size_t i = 0; i < vblankWaitingThreads.size(); i++) {
		WaitVBlankInfo *t = &vblankWaitingThreads[i];
		if (t->threadID == threadID)
		{
			waitData = *t;
			vblankWaitingThreads.erase(vblankWaitingThreads.begin() + i);
			break;
		}
	}

	if (waitData.threadID != threadID)
	{
		WARN_LOG_REPORT(SCEDISPLAY, "sceDisplayWaitVblankCB: could not find waiting thread info.");
		return;
	}

	vblankPausedWaits[pauseKey] = vCount + waitData.vcountUnblock;
	DEBUG_LOG(SCEDISPLAY, "sceDisplayWaitVblankCB: Suspending vblank wait for callback")
}

void __DisplayVblankEndCallback(SceUID threadID, SceUID prevCallbackId) {
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	// Probably should not be possible.
	if (vblankPausedWaits.find(pauseKey) == vblankPausedWaits.end()) {
		__KernelResumeThreadFromWait(threadID, 0);
		return;
	}

	int vcountUnblock = vblankPausedWaits[pauseKey];
	vblankPausedWaits.erase(pauseKey);
	if (vcountUnblock <= vCount) {
		__KernelResumeThreadFromWait(threadID, 0);
		return;
	}

	// Still have to wait a bit longer.
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread(), vcountUnblock - vCount));
	DEBUG_LOG(SCEDISPLAY, "sceDisplayWaitVblankCB: Resuming vblank wait from callback")
}

// TODO: Also average actualFps
void __DisplayGetFPS(float *out_vps, float *out_fps, float *out_actual_fps) {
	*out_vps = fps;
	*out_fps = flips;
	*out_actual_fps = actualFps;
}

void __DisplayGetVPS(float *out_vps) {
	*out_vps = fps;
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

void CalculateFPS() {
	time_update();
	double now = time_now_d();

	if (now >= lastFpsTime + 1.0) {
		double frames = (gpuStats.numVBlanks - lastFpsFrame);
		actualFps = (actualFlips - lastActualFlips);

		fps = frames / (now - lastFpsTime);
		flips = 60.0 * (double) (gpuStats.numFlips - lastNumFlips) / frames;

		lastFpsFrame = gpuStats.numVBlanks;
		lastNumFlips = gpuStats.numFlips;
		lastActualFlips = actualFlips;
		lastFpsTime = now;

		fpsHistory[fpsHistoryPos++] = fps;
		fpsHistoryPos = fpsHistoryPos % ARRAY_SIZE(fpsHistory);
		++fpsHistoryValid;
	}
}

void __DisplayGetDebugStats(char stats[2048]) {
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
		"Alpha Tested draws: %i\n"
		"Non Alpha Tested draws: %i\n"
		"Num Tracked Vertex Arrays: %i\n"
		"Cycles executed: %d (%f per vertex)\n"
		"Commands per call level: %i %i %i %i\n"
		"Vertices Submitted: %i\n"
		"Cached Vertices Drawn: %i\n"
		"Uncached Vertices Drawn: %i\n"
		"FBOs active: %i\n"
		"Textures active: %i, decoded: %i\n"
		"Texture invalidations: %i\n"
		"Vertex shaders loaded: %i\n"
		"Fragment shaders loaded: %i\n"
		"Combined shaders loaded: %i\n",
		gpuStats.numVBlanks,
		gpuStats.msProcessingDisplayLists * 1000.0f,
		kernelStats.msInSyscalls * 1000.0f,
		kernelStats.slowestSyscallName ? kernelStats.slowestSyscallName : "(none)",
		kernelStats.slowestSyscallTime * 1000.0f,
		kernelStats.summedSlowestSyscallName ? kernelStats.summedSlowestSyscallName : "(none)",
		kernelStats.summedSlowestSyscallTime * 1000.0f,
		gpuStats.numDrawCalls,
		gpuStats.numFlushes,
		gpuStats.numCachedDrawCalls,
		gpuStats.numAlphaTestedDraws,
		gpuStats.numNonAlphaTestedDraws,
		gpuStats.numTrackedVertexArrays,
		gpuStats.vertexGPUCycles + gpuStats.otherGPUCycles,
		vertexAverageCycles,
		gpuStats.gpuCommandsAtCallLevel[0],gpuStats.gpuCommandsAtCallLevel[1],gpuStats.gpuCommandsAtCallLevel[2],gpuStats.gpuCommandsAtCallLevel[3],
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

	gpuStats.ResetFrame();
	kernelStats.ResetFrame();
}

enum {
	FPS_LIMIT_NORMAL = 0,
	FPS_LIMIT_CUSTOM = 1,
};

// Start out assuming we'll go at 59.94 NTSC.
static float timestepSmooth[8] = {
	1.001f / 60.0f, 1.001f / 60.0f, 1.001f / 60.0f, 1.001f / 60.0f,
	1.001f / 60.0f, 1.001f / 60.0f, 1.001f / 60.0f, 1.001f / 60.0f,
};
static int timestepNext = 0;

static float CalculateSmoothTimestep(float lastTimestep) {
	// Straight up ignore timesteps that would cause sub-10 fps speeds.
	if (lastTimestep < 1.001f / 10.0f) {
		timestepSmooth[timestepNext] = lastTimestep;
	}
	timestepNext = (timestepNext + 1) % ARRAY_SIZE(timestepSmooth);

	float summed = 0.0f;
	for (size_t i = 0; i < ARRAY_SIZE(timestepSmooth); ++i) {
		summed += timestepSmooth[i];
	}
	return summed / (float)ARRAY_SIZE(timestepSmooth);
}

// Let's collect all the throttling and frameskipping logic here.
void DoFrameTiming(bool &throttle, bool &skipFrame, float lastTimestep) {
	float timestep = CalculateSmoothTimestep(lastTimestep);
	int fpsLimiter = PSP_CoreParameter().fpsLimit;
	throttle = !PSP_CoreParameter().unthrottle;
	if (fpsLimiter == FPS_LIMIT_CUSTOM && g_Config.iFpsLimit == 0)
		throttle = false;
	skipFrame = false;

	// Check if the frameskipping code should be enabled. If neither throttling or frameskipping is on,
	// we have nothing to do here.
	bool doFrameSkip = g_Config.iFrameSkip != 0;

	

	if (!throttle && g_Config.bFrameSkipUnthrottle) {
		doFrameSkip = true;
		skipFrame = true;
		if (numSkippedFrames >= 7) {
			skipFrame = false;
		}
		return;	
	}


	if (!throttle && !doFrameSkip)
		return;
	
	time_update();
	
	curFrameTime = time_now_d();
	if (nextFrameTime == 0.0)
		nextFrameTime = time_now_d() + timestep;
	
	// Argh, we are falling behind! Let's skip a frame and see if we catch up.

	// Auto-frameskip automatically if speed limit is set differently than the default.
	if (g_Config.iFrameSkip == 1 || (g_Config.iFrameSkip == 0 && fpsLimiter == FPS_LIMIT_CUSTOM && g_Config.iFpsLimit > 60)) {
		// 1 == autoframeskip
		if (curFrameTime > nextFrameTime && doFrameSkip) {
			skipFrame = true;
		}
	} else if (g_Config.iFrameSkip > 1)	{
		// Other values = fixed frameskip
		if (numSkippedFrames >= g_Config.iFrameSkip - 1)
			skipFrame = false;
		else
			skipFrame = true;
	}

	if (curFrameTime < nextFrameTime && throttle) {
		// If time gap is huge just jump (somebody unthrottled)
		if ((nextFrameTime - curFrameTime > 2*timestep) && fpsLimiter == FPS_LIMIT_NORMAL) {
			nextFrameTime = curFrameTime + timestep;
		} else {
			// Wait until we've caught up.
			while (time_now_d() < nextFrameTime) {
				sleep_ms(1); // Sleep for 1ms on this thread
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
		nextFrameTime = std::max(nextFrameTime + timestep, time_now_d() - maxFallBehindFrames * timestep);
	} else if (fpsLimiter == FPS_LIMIT_CUSTOM) {
		double customLimiter = (g_Config.iFpsLimit / 60.0f) / timestep;
		nextFrameTime = std::max(nextFrameTime + 1.0 / customLimiter, time_now_d() - maxFallBehindFrames / customLimiter);
	}
}


void hleEnterVblank(u64 userdata, int cyclesLate) {
	int vbCount = userdata;

	DEBUG_LOG(SCEDISPLAY, "Enter VBlank %i", vbCount);

	isVblank = 1;
	vCount++; // vCount increases at each VBLANK.
	hCountBase += hCountPerVblank; // This is the "accumulated" hcount base.
	if (hCountBase > 0x7FFFFFFF) {
		hCountBase -= 0x80000000;
	}
	frameStartTicks = CoreTiming::GetTicks();

	// Wake up threads waiting for VBlank
	u32 error;
	for (size_t i = 0; i < vblankWaitingThreads.size(); i++) {
		if (--vblankWaitingThreads[i].vcountUnblock == 0) {
			// Only wake it if it wasn't already released by someone else.
			SceUID waitID = __KernelGetWaitID(vblankWaitingThreads[i].threadID, WAITTYPE_VBLANK, error);
			if (waitID == 1) {
				__KernelResumeThreadFromWait(vblankWaitingThreads[i].threadID, 0);
			}
			vblankWaitingThreads.erase(vblankWaitingThreads.begin() + i--);
		}
	}

	// Trigger VBlank interrupt handlers.
	__TriggerInterrupt(PSP_INTR_IMMEDIATE | PSP_INTR_ONLY_IF_ENABLED | PSP_INTR_ALWAYS_RESCHED, PSP_VBLANK_INTR, PSP_INTR_SUB_ALL);

	CoreTiming::ScheduleEvent(msToCycles(vblankMs) - cyclesLate, leaveVblankEvent, vbCount + 1);

	gpuStats.numVBlanks++;

	numVBlanksSinceFlip++;

	// TODO: Should this be done here or in hleLeaveVblank?
	if (framebufIsLatched) {
		DEBUG_LOG(SCEDISPLAY, "Setting latched framebuffer %08x (prev: %08x)", latchedFramebuf.topaddr, framebuf.topaddr);
		framebuf = latchedFramebuf;
		framebufIsLatched = false;
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
	}
	// We flip only if the framebuffer was dirty. This eliminates flicker when using
	// non-buffered rendering. The interaction with frame skipping seems to need
	// some work.
	// But, let's flip at least once every 10 frames if possible, since there may be sound effects.
	if (gpu->FramebufferDirty() || (g_Config.iRenderingMode != 0 && numVBlanksSinceFlip >= 10)) {
		if (g_Config.iShowFPSCounter && g_Config.iShowFPSCounter < 4) {
			CalculateFPS();
		}

		// Setting CORE_NEXTFRAME causes a swap.
		// Check first though, might've just quit / been paused.
		if (gpu->FramebufferReallyDirty()) {
			if (coreState == CORE_RUNNING) {
				coreState = CORE_NEXTFRAME;
				gpu->CopyDisplayToOutput();
				actualFlips++;
			}
		}

		gpuStats.numFlips++;

		bool throttle, skipFrame;
		// 1.001f to compensate for the classic 59.94 NTSC framerate that the PSP seems to have.
		DoFrameTiming(throttle, skipFrame, (float)numVBlanksSinceFlip * (1.001f / 60.0f));

		int maxFrameskip = 8;
		if (throttle) {
			if (g_Config.iFrameSkip == 1) {
				// 4 here means 1 drawn, 4 skipped - so 12 fps minimum.
				maxFrameskip = 4;
			} else {
				maxFrameskip = g_Config.iFrameSkip - 1;
			}
		}
		if (numSkippedFrames >= maxFrameskip) {
			skipFrame = false;
		}

		if (skipFrame) {
			gstate_c.skipDrawReason |= SKIPDRAW_SKIPFRAME;
			numSkippedFrames++;
		} else {
			gstate_c.skipDrawReason &= ~SKIPDRAW_SKIPFRAME;
			numSkippedFrames = 0;
		}

		// Returning here with coreState == CORE_NEXTFRAME causes a buffer flip to happen (next frame).
		// Right after, we regain control for a little bit in hleAfterFlip. I think that's a great
		// place to do housekeeping.
		CoreTiming::ScheduleEvent(0 - cyclesLate, afterFlipEvent, 0);
		numVBlanksSinceFlip = 0;
	}
}

void hleAfterFlip(u64 userdata, int cyclesLate)
{
	gpu->BeginFrame();  // doesn't really matter if begin or end of frame.
}

void hleLeaveVblank(u64 userdata, int cyclesLate) {
	isVblank = 0;
	DEBUG_LOG(SCEDISPLAY,"Leave VBlank %i", (int)userdata - 1);
	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs) - cyclesLate, enterVblankEvent, userdata);

	// Fire the vblank listeners after the vblank completes.
	__DisplayFireVblank();
}

u32 sceDisplayIsVblank() {
	DEBUG_LOG(SCEDISPLAY,"%i=sceDisplayIsVblank()",isVblank);
	return isVblank;
}

u32 sceDisplaySetMode(int displayMode, int displayWidth, int displayHeight) {
	DEBUG_LOG(SCEDISPLAY,"sceDisplaySetMode(%i, %i, %i)", displayMode, displayWidth, displayHeight);

	if (!hasSetMode) {
		gpu->InitClear();
		hasSetMode = true;
	}
	mode = displayMode;
	width = displayWidth;
	height = displayHeight;
	return 0;
}

// Some games (GTA) never call this during gameplay, so bad place to put a framerate counter.
u32 sceDisplaySetFramebuf(u32 topaddr, int linesize, int pixelformat, int sync) {
	FrameBufferState fbstate;
	DEBUG_LOG(SCEDISPLAY,"sceDisplaySetFramebuf(topaddr=%08x,linesize=%d,pixelsize=%d,sync=%d)", topaddr, linesize, pixelformat, sync);
	hleEatCycles(290);
	if (topaddr == 0) {
		DEBUG_LOG(SCEDISPLAY,"- screen off");
	} else {
		fbstate.topaddr = topaddr;
		fbstate.pspFramebufFormat = (GEBufferFormat)pixelformat;
		fbstate.pspFramebufLinesize = linesize;
	}

	s64 delayCycles = 0;
	if (topaddr != framebuf.topaddr && g_Config.iForceMaxEmulatedFPS > 0) {
		// Sometimes we get a small number, there's probably no need to delay the thread for this.
		// sceDisplaySetFramebuf() isn't supposed to delay threads at all.  This is a hack.
		const int FLIP_DELAY_CYCLES_MIN = 10;
		// Some games (like Final Fantasy 4) only call this too much in spurts.
		// The goal is to fix games where this would result in a consistent overhead.
		const int FLIP_DELAY_MIN_FLIPS = 30;

		u64 now = CoreTiming::GetTicks();
		u64 expected = msToCycles(1000) / g_Config.iForceMaxEmulatedFPS;
		u64 actual = now - lastFlipCycles;
		if (actual < expected - FLIP_DELAY_CYCLES_MIN) {
			if (lastFlipsTooFrequent >= FLIP_DELAY_MIN_FLIPS) {
				delayCycles = expected - actual;
			} else {
				++lastFlipsTooFrequent;
			}
		} else {
			--lastFlipsTooFrequent;
		}
		lastFlipCycles = CoreTiming::GetTicks();
	}

	if (sync == PSP_DISPLAY_SETBUF_IMMEDIATE) {
		// Write immediately to the current framebuffer parameters
		if (topaddr != 0) {
			framebuf = fbstate;
			gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
		} else {
			WARN_LOG(SCEDISPLAY, "%s: PSP_DISPLAY_SETBUF_IMMEDIATE without topaddr?", __FUNCTION__);
		}
	} else if (topaddr != 0) {
		// Delay the write until vblank
		latchedFramebuf = fbstate;
		framebufIsLatched = true;
	}

	if (delayCycles > 0) {
		// Okay, the game is going at too high a frame rate.  God of War and Fat Princess both do this.
		// Simply eating the cycles works and is fast, but breaks other games (like Jeanne d'Arc.)
		// So, instead, we delay this HLE thread only (a small deviation from correct behavior.)
		return hleDelayResult(0, "set framebuf", cyclesToUs(delayCycles));
	} else {
		return 0;
	}
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
	const FrameBufferState &fbState = latchedMode == 1 && framebufIsLatched ? latchedFramebuf : framebuf;
	DEBUG_LOG(SCEDISPLAY,"sceDisplayGetFramebuf(*%08x = %08x, *%08x = %08x, *%08x = %08x, %i)", topaddrPtr, fbState.topaddr, linesizePtr, fbState.pspFramebufLinesize, pixelFormatPtr, fbState.pspFramebufFormat, latchedMode);

	if (Memory::IsValidAddress(topaddrPtr))
		Memory::Write_U32(fbState.topaddr, topaddrPtr);
	if (Memory::IsValidAddress(linesizePtr))
		Memory::Write_U32(fbState.pspFramebufLinesize, linesizePtr);
	if (Memory::IsValidAddress(pixelFormatPtr))
		Memory::Write_U32(fbState.pspFramebufFormat, pixelFormatPtr);

	return 0;
}

u32 sceDisplayWaitVblankStart() {
	VERBOSE_LOG(SCEDISPLAY,"sceDisplayWaitVblankStart()");
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 1, 0, 0, false, "vblank start waited");
	return 0;
}

u32 sceDisplayWaitVblank() {
	if (!isVblank) {
		VERBOSE_LOG(SCEDISPLAY,"sceDisplayWaitVblank()");
		vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
		__KernelWaitCurThread(WAITTYPE_VBLANK, 1, 0, 0, false, "vblank waited");
		return 0;
	} else {
		DEBUG_LOG(SCEDISPLAY,"sceDisplayWaitVblank() - not waiting since in vBlank");
		hleEatCycles(1110);
		return 1;
	}
}

u32 sceDisplayWaitVblankStartMulti(int vblanks) {
	if (vblanks <= 0) {
		WARN_LOG(SCEDISPLAY, "sceDisplayWaitVblankStartMulti(%d): invalid number of vblanks", vblanks);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}
	VERBOSE_LOG(SCEDISPLAY, "sceDisplayWaitVblankStartMulti(%d)", vblanks);
	if (!__KernelIsDispatchEnabled())
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	if (__IsInInterrupt())
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread(), vblanks));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 1, 0, 0, false, "vblank start multi waited");
	return 0;
}

u32 sceDisplayWaitVblankCB() {
	if (!isVblank) {
		VERBOSE_LOG(SCEDISPLAY,"sceDisplayWaitVblankCB()");
		vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
		__KernelWaitCurThread(WAITTYPE_VBLANK, 1, 0, 0, true, "vblank waited");
		return 0;
	} else {
		DEBUG_LOG(SCEDISPLAY,"sceDisplayWaitVblankCB() - not waiting since in vBlank");
		hleEatCycles(1110);
		return 1;
	}
}

u32 sceDisplayWaitVblankStartCB() {
	VERBOSE_LOG(SCEDISPLAY,"sceDisplayWaitVblankStartCB()");
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 1, 0, 0, true, "vblank start waited");
	return 0;
}

u32 sceDisplayWaitVblankStartMultiCB(int vblanks) {
	if (vblanks <= 0) {
		WARN_LOG(SCEDISPLAY, "sceDisplayWaitVblankStartMultiCB(%d): invalid number of vblanks", vblanks);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}
	VERBOSE_LOG(SCEDISPLAY,"sceDisplayWaitVblankStartMultiCB(%d)", vblanks);
	if (!__KernelIsDispatchEnabled())
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	if (__IsInInterrupt())
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread(), vblanks));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 1, 0, 0, true, "vblank start multi waited");
	return 0;
}

u32 sceDisplayGetVcount() {
	VERBOSE_LOG(SCEDISPLAY,"%i=sceDisplayGetVcount()", vCount);

	hleEatCycles(150);
	return vCount;
}

u32 __DisplayGetCurrentHcount() {
	const static int ticksPerVblank333 = 333 * 1000000 / 60 / hCountPerVblank;
	const int ticksIntoFrame = CoreTiming::GetTicks() - frameStartTicks;
	// Can't seem to produce a 0 on real hardware, offsetting by 1 makes things look right.
	return 1 + (ticksIntoFrame / (CoreTiming::GetClockFrequencyMHz() * ticksPerVblank333 / 333));
}

u32 __DisplayGetAccumulatedHcount() {
	// The hCount is always a positive int, and wraps from 0x7FFFFFFF -> 0.
	int value = hCountBase + __DisplayGetCurrentHcount();
	return value & 0x7FFFFFFF;
}

u32 sceDisplayGetCurrentHcount() {
	u32 currentHCount = __DisplayGetCurrentHcount();
	DEBUG_LOG(SCEDISPLAY, "%i=sceDisplayGetCurrentHcount()", currentHCount);
	hleEatCycles(275);
	return currentHCount;
}

int sceDisplayAdjustAccumulatedHcount(int value) {
	if (value < 0) {
		ERROR_LOG_REPORT(SCEDISPLAY, "sceDisplayAdjustAccumulatedHcount(%d): invalid value", value);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}

	// Since it includes the current hCount, find the difference to apply to the base.
	u32 accumHCount = __DisplayGetAccumulatedHcount();
	int diff = value - accumHCount;
	hCountBase += diff;

	DEBUG_LOG(SCEDISPLAY, "sceDisplayAdjustAccumulatedHcount(%d)", value);
	return 0;
}

int sceDisplayGetAccumulatedHcount() {
	u32 accumHCount = __DisplayGetAccumulatedHcount();
	DEBUG_LOG(SCEDISPLAY, "%d=sceDisplayGetAccumulatedHcount()", accumHCount);
	hleEatCycles(235);
	return accumHCount;
}

float sceDisplayGetFramePerSec() {
	const static float framePerSec = 59.9400599f;
	DEBUG_LOG(SCEDISPLAY,"%f=sceDisplayGetFramePerSec()", framePerSec);
	return framePerSec;	// (9MHz * 1)/(525 * 286)
}

u32 sceDisplayIsForeground() {
  	DEBUG_LOG(SCEDISPLAY,"IMPL sceDisplayIsForeground()");	
	if (!hasSetMode || framebuf.topaddr == 0)
		return 0;
	else
  		return 1;   // return value according to JPCSP comment
}

u32 sceDisplayGetMode(u32 modeAddr, u32 widthAddr, u32 heightAddr) {
	DEBUG_LOG(SCEDISPLAY,"sceDisplayGetMode(%08x, %08x, %08x)", modeAddr, widthAddr, heightAddr);
	if (Memory::IsValidAddress(modeAddr))
		Memory::Write_U32(mode, modeAddr);
	if (Memory::IsValidAddress(widthAddr))
		Memory::Write_U32(width, widthAddr);
	if (Memory::IsValidAddress(heightAddr))
		Memory::Write_U32(height, heightAddr);
	return 0;
}

u32 sceDisplayIsVsync() {
	ERROR_LOG(SCEDISPLAY,"UNIMPL sceDisplayIsVsync()");
	return 0;
}

u32 sceDisplayGetResumeMode(u32 resumeModeAddr) {
	ERROR_LOG(SCEDISPLAY,"sceDisplayGetResumeMode(%08x)", resumeModeAddr);
	if (Memory::IsValidAddress(resumeModeAddr))
		Memory::Write_U32(resumeMode, resumeModeAddr);
	return 0;
}

u32 sceDisplaySetResumeMode(u32 rMode) {
	ERROR_LOG(SCEDISPLAY,"sceDisplaySetResumeMode(%08x)", rMode);
	resumeMode = rMode;
	return 0;
}

u32 sceDisplayGetBrightness(u32 levelAddr) {
	ERROR_LOG(SCEDISPLAY,"UNIMPL sceDisplayGetBrightness(%08x)", levelAddr);
	return 0;
}

u32 sceDisplaySetHoldMode(u32 hMode) {
	ERROR_LOG(SCEDISPLAY,"sceDisplaySetHoldMode(%08x)", hMode);
	holdMode = hMode;
	return 0;
}

const HLEFunction sceDisplay[] = {
	{0x0E20F177,WrapU_III<sceDisplaySetMode>, "sceDisplaySetMode"},
	{0x289D82FE,WrapU_UIII<sceDisplaySetFramebuf>, "sceDisplaySetFrameBuf"},
	{0xEEDA2E54,WrapU_UUUI<sceDisplayGetFramebuf>,"sceDisplayGetFrameBuf"},
	{0x36CDFADE,WrapU_V<sceDisplayWaitVblank>, "sceDisplayWaitVblank", HLE_NOT_DISPATCH_SUSPENDED},
	{0x984C27E7,WrapU_V<sceDisplayWaitVblankStart>, "sceDisplayWaitVblankStart", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x40f1469c,WrapU_I<sceDisplayWaitVblankStartMulti>, "sceDisplayWaitVblankStartMulti"},
	{0x8EB9EC49,WrapU_V<sceDisplayWaitVblankCB>, "sceDisplayWaitVblankCB", HLE_NOT_DISPATCH_SUSPENDED},
	{0x46F186C3,WrapU_V<sceDisplayWaitVblankStartCB>, "sceDisplayWaitVblankStartCB", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x77ed8b3a,WrapU_I<sceDisplayWaitVblankStartMultiCB>,"sceDisplayWaitVblankStartMultiCB"},
	{0xdba6c4c4,WrapF_V<sceDisplayGetFramePerSec>,"sceDisplayGetFramePerSec"},
	{0x773dd3a3,WrapU_V<sceDisplayGetCurrentHcount>,"sceDisplayGetCurrentHcount"},
	{0x210eab3a,WrapI_V<sceDisplayGetAccumulatedHcount>,"sceDisplayGetAccumulatedHcount"},
	{0xA83EF139,WrapI_I<sceDisplayAdjustAccumulatedHcount>,"sceDisplayAdjustAccumulatedHcount"},
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
