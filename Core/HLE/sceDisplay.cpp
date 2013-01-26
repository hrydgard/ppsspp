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
// Internal drawing library
#include "../Util/PPGeDraw.h"

struct FrameBufferState {
	u32 topaddr;
	PspDisplayPixelFormat pspFramebufFormat;
	int pspFramebufLinesize;
};

struct WaitVBlankInfo
{
	WaitVBlankInfo(u32 tid) : threadID(tid), vcountUnblock(0) {}
	u32 threadID;
	int vcountUnblock; // what was this for again?
};

// STATE BEGIN
static FrameBufferState framebuf;
static FrameBufferState latchedFramebuf;
static bool framebufIsLatched;

static int enterVblankEvent = -1;
static int leaveVblankEvent = -1;

static int hCount;
static int hCountTotal; //unused
static int vCount;
static int isVblank;
static bool hasSetMode;
double lastFrameTime;

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

void hleEnterVblank(u64 userdata, int cyclesLate);
void hleLeaveVblank(u64 userdata, int cyclesLate);

void __DisplayInit() {
	gpuStats.reset();
	hasSetMode = false;
	framebufIsLatched = false;
	framebuf.topaddr = 0x04000000;
	framebuf.pspFramebufFormat = PSP_DISPLAY_PIXEL_FORMAT_8888;
	framebuf.pspFramebufLinesize = 480; // ??

	enterVblankEvent = CoreTiming::RegisterEvent("EnterVBlank", &hleEnterVblank);
	leaveVblankEvent = CoreTiming::RegisterEvent("LeaveVBlank", &hleLeaveVblank);

	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs), enterVblankEvent, 0);
	isVblank = 0;
	vCount = 0;
	hCount = 0;
	hCountTotal = 0;
	lastFrameTime = 0;

	InitGfxState();
}

void __DisplayDoState(PointerWrap &p) {
	p.Do(framebuf);
	p.Do(latchedFramebuf);
	p.Do(framebufIsLatched);
	p.Do(hCount);
	p.Do(hCountTotal);
	p.Do(vCount);
	p.Do(isVblank);
	p.Do(hasSetMode);
	p.Do(lastFrameTime);
	WaitVBlankInfo wvi(0);
	p.Do(vblankWaitingThreads, wvi);

	p.Do(enterVblankEvent);
	CoreTiming::RestoreRegisterEvent(enterVblankEvent, "EnterVBlank", &hleEnterVblank);
	p.Do(leaveVblankEvent);
	CoreTiming::RestoreRegisterEvent(leaveVblankEvent, "LeaveVBlank", &hleLeaveVblank);

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

void hleEnterVblank(u64 userdata, int cyclesLate) {
	int vbCount = userdata;

	DEBUG_LOG(HLE, "Enter VBlank %i", vbCount);

	isVblank = 1;

	// Fire the vblank listeners before we wake threads.
	__DisplayFireVblank();

	// Wake up threads waiting for VBlank
	for (size_t i = 0; i < vblankWaitingThreads.size(); i++) {
		__KernelResumeThreadFromWait(vblankWaitingThreads[i].threadID, 0);
	}
	vblankWaitingThreads.clear();

	// Trigger VBlank interrupt handlers.
	__TriggerInterrupt(PSP_INTR_IMMEDIATE | PSP_INTR_ONLY_IF_ENABLED, PSP_VBLANK_INTR);

	CoreTiming::ScheduleEvent(msToCycles(vblankMs) - cyclesLate, leaveVblankEvent, vbCount+1);

	// TODO: Should this be done here or in hleLeaveVblank?
	if (framebufIsLatched) {
		DEBUG_LOG(HLE, "Setting latched framebuffer %08x (prev: %08x)", latchedFramebuf.topaddr, framebuf.topaddr);
		framebuf = latchedFramebuf;
		framebufIsLatched = false;
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
	}

	// Draw screen overlays before blitting. Saves and restores the Ge context.
	gpuStats.numFrames++;

	// Now we can subvert the Ge engine in order to draw custom overlays like stat counters etc.
	if (g_Config.bShowDebugStats && gpuStats.numDrawCalls) {
		gpu->UpdateStats();
		char stats[2048];
		sprintf(stats,
			"Frames: %i\n"
			"DL processing time: %0.2f ms\n"
			"Kernel processing time: %0.2f ms\n"
			"Slowest syscall: %s : %0.2f ms\n"
			"Most active syscall: %s : %0.2f ms\n"
			"Draw calls: %i, flushes %i\n"
			"Cached Draw calls: %i\n"
			"Num Tracked Vertex Arrays: %i\n"
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

		float zoom = 0.3f; /// g_Config.iWindowZoom;
		float soff = 0.3f;
		PPGeBegin();
		PPGeDrawText(stats, soff, soff, 0, zoom, 0xCC000000);
		PPGeDrawText(stats, -soff, -soff, 0, zoom, 0xCC000000);
		PPGeDrawText(stats, 0, 0, 0, zoom, 0xFFFFFFFF);
		PPGeEnd();

		gpuStats.resetFrame();
		kernelStats.ResetFrame();
	}

	// Yeah, this has to be the right moment to end the frame. Give the graphics backend opportunity
	// to blit the framebuffer, in order to support half-framerate games that otherwise wouldn't have
	// anything to draw here.
	gpu->CopyDisplayToOutput();

	host->EndFrame();

#ifdef _WIN32
	static double lastFrameTime = 0.0;
	// Best place to throttle the frame rate on non vsynced platforms is probably here. Let's try it.
	time_update();
	if (lastFrameTime == 0.0)
		lastFrameTime = time_now_d();
	if (!GetAsyncKeyState(VK_TAB)) {
		while (time_now_d() < lastFrameTime + 1.0 / 60.0) {
			Common::SleepCurrentThread(1);
			time_update();
		}
		// Advance lastFrameTime by a constant amount each frame,
		// but don't let it get too far behind.
		lastFrameTime = std::max(lastFrameTime + 1.0 / 60.0, time_now_d() - 1.5 / 60.0);
	}

	// We are going to have to do something about audio timing for platforms that
	// are vsynced to something that's not exactly 60fps..

#endif

	host->BeginFrame();
	gpu->BeginFrame();

	// Tell the emu core that it's time to stop emulating
	// Win32 doesn't need this.
#ifndef _WIN32
	coreState = CORE_NEXTFRAME;
#endif
}

void hleLeaveVblank(u64 userdata, int cyclesLate) {
	isVblank = 0;
	DEBUG_LOG(HLE,"Leave VBlank %i", (int)userdata - 1);
	vCount++;
	hCount = 0;
	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs) - cyclesLate, enterVblankEvent, userdata);
}

void sceDisplayIsVblank() {
	DEBUG_LOG(HLE,"%i=sceDisplayIsVblank()",isVblank);
	RETURN(isVblank);
}

u32 sceDisplaySetMode(u32 unknown, u32 xres, u32 yres) {
	DEBUG_LOG(HLE,"sceDisplaySetMode(%d,%d,%d)",unknown,xres,yres);
	host->BeginFrame();

	if (!hasSetMode) {
		gpu->InitClear();
		hasSetMode = true;
	}

	return 0;
}

u32 sceDisplaySetFramebuf() {
	u32 topaddr = PARAM(0);
	int linesize = PARAM(1);
	int pixelformat = PARAM(2);
	int sync = PARAM(3);

	FrameBufferState fbstate;
	DEBUG_LOG(HLE,"sceDisplaySetFramebuf(topaddr=%08x,linesize=%d,pixelsize=%d,sync=%d)",topaddr,linesize,pixelformat,sync);
	if (topaddr == 0) {
		DEBUG_LOG(HLE,"- screen off");
	} else {
		fbstate.topaddr = topaddr;
		fbstate.pspFramebufFormat = (PspDisplayPixelFormat)pixelformat;
		fbstate.pspFramebufLinesize = linesize;
	}

	if (sync == PSP_DISPLAY_SETBUF_IMMEDIATE) {
		// Write immediately to the current framebuffer parameters
		if (topaddr != 0)
		{
			framebuf = fbstate;
			gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
		}
		else
			WARN_LOG(HLE, "%s: PSP_DISPLAY_SETBUF_IMMEDIATE without topaddr?", __FUNCTION__);
	} else if (topaddr != 0) {
		// Delay the write until vblank
		latchedFramebuf = fbstate;
		framebufIsLatched = true;
	}
	return 0;
}

u32 sceDisplayGetFramebuf(u32 topaddrPtr, u32 linesizePtr, u32 pixelFormatPtr, int mode) {
	const FrameBufferState &fbState = mode == 1 ? latchedFramebuf : framebuf;
	DEBUG_LOG(HLE,"sceDisplayGetFramebuf(*%08x = %08x, *%08x = %08x, *%08x = %08x, %i)",
		topaddrPtr, fbState.topaddr, linesizePtr, fbState.pspFramebufLinesize, pixelFormatPtr, fbState.pspFramebufFormat, mode);

	if (Memory::IsValidAddress(topaddrPtr))
		Memory::Write_U32(fbState.topaddr, topaddrPtr);
	if (Memory::IsValidAddress(linesizePtr))
		Memory::Write_U32(fbState.pspFramebufLinesize, linesizePtr);
	if (Memory::IsValidAddress(pixelFormatPtr))
		Memory::Write_U32(fbState.pspFramebufFormat, pixelFormatPtr);

	return 0;
}

u32 sceDisplayWaitVblankStart() {
	DEBUG_LOG(HLE,"sceDisplayWaitVblankStart()");
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, false, "vblank start waited");
	return 0;
}

u32 sceDisplayWaitVblank() {
	if (!isVblank) {
		DEBUG_LOG(HLE,"sceDisplayWaitVblank()");
		vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
		__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, false, "vblank waited");
		return 0;
	} else {
		DEBUG_LOG(HLE,"sceDisplayWaitVblank() - not waiting since in vBlank");
		return 1;
	}
}

u32 sceDisplayWaitVblankStartMulti() {
	DEBUG_LOG(HLE,"sceDisplayWaitVblankStartMulti()");
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, false, "vblank start multi waited");
	return 0;
}

u32 sceDisplayWaitVblankCB() {
	if (!isVblank) {
		DEBUG_LOG(HLE,"sceDisplayWaitVblankCB()");
		vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
		__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true, "vblank waited");
		return 0;
	} else {
		DEBUG_LOG(HLE,"sceDisplayWaitVblank() - not waiting since in vBlank");
		return 1;
	}
}

u32 sceDisplayWaitVblankStartCB() {
	DEBUG_LOG(HLE,"sceDisplayWaitVblankStartCB()");
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true, "vblank start waited");
	return 0;
}

u32 sceDisplayWaitVblankStartMultiCB() {
	DEBUG_LOG(HLE,"sceDisplayWaitVblankStartMultiCB()");
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread()));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true, "vblank start multi waited");
	return 0;
}

u32 sceDisplayGetVcount() {
	// Too spammy
	// DEBUG_LOG(HLE,"%i=sceDisplayGetVcount()", vCount);

	// Puyo Puyo Fever polls this as a substitute for waiting for vblank.
	// As a result, the game never gets to reschedule so it doesn't mix audio and things break.
	// Need to find a better hack as this breaks games like Project Diva.
	// hleReSchedule("sceDisplayGetVcount hack");  // Puyo puyo hack?

	CoreTiming::Idle(1000000);
	return vCount;
}

void sceDisplayGetCurrentHcount() {
	RETURN(hCount++);
}

void sceDisplayGetAccumulatedHcount() {
	// Just do an estimate
	u32 accumHCount = CoreTiming::GetTicks() / (CoreTiming::GetClockFrequencyMHz() * 1000000 / 60 / 272);
	DEBUG_LOG(HLE,"%i=sceDisplayGetAccumulatedHcount()", accumHCount);
	RETURN(accumHCount);
}

float sceDisplayGetFramePerSec() {
	float fps = 59.9400599f;
	DEBUG_LOG(HLE,"%f=sceDisplayGetFramePerSec()", fps);
	return fps;	// (9MHz * 1)/(525 * 286)
}

const HLEFunction sceDisplay[] = {
	{0x0E20F177,WrapU_UUU<sceDisplaySetMode>, "sceDisplaySetMode"},
	{0x289D82FE,WrapU_V<sceDisplaySetFramebuf>, "sceDisplaySetFramebuf"},
	{0xEEDA2E54,WrapU_UUUI<sceDisplayGetFramebuf>,"sceDisplayGetFrameBuf"},
	{0x36CDFADE,WrapU_V<sceDisplayWaitVblank>, "sceDisplayWaitVblank"},
	{0x984C27E7,WrapU_V<sceDisplayWaitVblankStart>, "sceDisplayWaitVblankStart"},
	{0x40f1469c,WrapU_V<sceDisplayWaitVblankStartMulti>, "sceDisplayWaitVblankStartMulti"},
	{0x8EB9EC49,WrapU_V<sceDisplayWaitVblankCB>, "sceDisplayWaitVblankCB"},
	{0x46F186C3,WrapU_V<sceDisplayWaitVblankStartCB>, "sceDisplayWaitVblankStartCB"},
	{0x77ed8b3a,WrapU_V<sceDisplayWaitVblankStartMultiCB>,"sceDisplayWaitVblankStartMultiCB"},
	{0xdba6c4c4,WrapF_V<sceDisplayGetFramePerSec>,"sceDisplayGetFramePerSec"},
	{0x773dd3a3,sceDisplayGetCurrentHcount,"sceDisplayGetCurrentHcount"},
	{0x210eab3a,sceDisplayGetAccumulatedHcount,"sceDisplayGetAccumulatedHcount"},
	{0x9C6EAAD7,WrapU_V<sceDisplayGetVcount>,"sceDisplayGetVcount"},
	{0xDEA197D4,0,"sceDisplayGetMode"},
	{0x7ED59BC4,0,"sceDisplaySetHoldMode"},
	{0xA544C486,0,"sceDisplaySetResumeMode"},
	{0xB4F378FA,0,"sceDisplayIsForeground"},
	{0x31C4BAA8,0,"sceDisplayGetBrightness"},
	{0x4D4E10EC,sceDisplayIsVblank,"sceDisplayIsVblank"},
};

void Register_sceDisplay() {
	RegisterModule("sceDisplay", ARRAY_SIZE(sceDisplay), sceDisplay);
}
