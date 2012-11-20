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

//#include "base/timeutil.h"

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
#include "../../GPU/GPUState.h"
#include "../../GPU/GPUInterface.h"
// Internal drawing library
#include "../Util/PPGeDraw.h"

extern ShaderManager shaderManager;

struct FrameBufferState
{
	u32 topaddr;
	PspDisplayPixelFormat pspFramebufFormat;
	int pspFramebufLinesize;
};

// STATE BEGIN
static FrameBufferState framebuf;
static FrameBufferState latchedFramebuf;
static bool framebufIsLatched;

static int enterVblankEvent = -1;
static int leaveVblankEvent = -1;

static int hCount = 0;
static int hCountTotal = 0; //unused
static int vCount = 0;
static int isVblank = 0;
static bool hasSetMode = false;

// STATE END

// The vblank period is 731.5 us (0.7315 ms)
const double vblankMs = 0.7315;
const double frameMs = 1000.0 / 60.0;

enum {
	PSP_DISPLAY_SETBUF_IMMEDIATE = 0,
	PSP_DISPLAY_SETBUF_NEXTFRAME = 1
};

struct WaitVBlankInfo
{
	u32 threadID;
	int vcountUnblock;
};

std::vector<WaitVBlankInfo> vblankWaitingThreads;

void hleEnterVblank(u64 userdata, int cyclesLate);
void hleLeaveVblank(u64 userdata, int cyclesLate);

void __DisplayInit()
{
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

	InitGfxState();
}

void __DisplayShutdown()
{
	ShutdownGfxState();
}

void hleEnterVblank(u64 userdata, int cyclesLate)
{
	int vbCount = userdata;

	DEBUG_LOG(HLE, "Enter VBlank %i", vbCount);

	isVblank = 1;

	// Wake up threads waiting for VBlank
	__KernelTriggerWait(WAITTYPE_VBLANK, 0, true);

	// Trigger VBlank interrupt handlers.
	__TriggerInterrupt(PSP_VBLANK_INTR);

	CoreTiming::ScheduleEvent(msToCycles(vblankMs) - cyclesLate, leaveVblankEvent, vbCount+1);

	// TODO: Should this be done here or in hleLeaveVblank?
	if (framebufIsLatched)
	{
		DEBUG_LOG(HLE, "Setting latched framebuffer %08x (prev: %08x)", latchedFramebuf.topaddr, framebuf.topaddr);
		framebuf = latchedFramebuf;
		framebufIsLatched = false;
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
	}

	// Draw screen overlays before blitting. Saves and restores the Ge context.

	/*
	if (g_Config.bShowGPUStats)
	{
		char stats[512];
		sprintf(stats, "Draw calls")
	}*/

	// Yeah, this has to be the right moment to end the frame. Give the graphics backend opportunity
	// to blit the framebuffer, in order to support half-framerate games that otherwise wouldn't have
	// anything to draw here.
	gpu->CopyDisplayToOutput();

	{
		host->EndFrame();
		host->BeginFrame();
		gpu->BeginFrame();

		shaderManager.DirtyShader();
		shaderManager.DirtyUniform(DIRTY_ALL);
	}

	// Tell the emu core that it's time to stop emulating
	// Win32 doesn't need this.
#ifndef _WIN32
	coreState = CORE_NEXTFRAME;
#endif
}


void hleLeaveVblank(u64 userdata, int cyclesLate)
{
	isVblank = 0;
	DEBUG_LOG(HLE,"Leave VBlank %i", (int)userdata - 1);
	vCount++;
	hCount = 0;
	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs) - cyclesLate, enterVblankEvent, userdata);
}

void sceDisplayIsVblank()
{
	DEBUG_LOG(HLE,"%i=sceDisplayIsVblank()",isVblank);
	RETURN(isVblank);
}

u32 sceDisplaySetMode(u32 unknown, u32 xres, u32 yres)
{
	DEBUG_LOG(HLE,"sceDisplaySetMode(%d,%d,%d)",unknown,xres,yres);
	host->BeginFrame();

	if (!hasSetMode)
	{
		gpu->InitClear();
		hasSetMode = true;
	}

	return 0;
}

void sceDisplaySetFramebuf()
{
	//host->EndFrame();
	u32 topaddr = PARAM(0);
	int linesize = PARAM(1);
	int pixelformat = PARAM(2);
	int sync = PARAM(3);

	FrameBufferState fbstate;
	DEBUG_LOG(HLE,"sceDisplaySetFramebuf(topaddr=%08x,linesize=%d,pixelsize=%d,sync=%d)",topaddr,linesize,pixelformat,sync);
	if (topaddr == 0)
	{
		DEBUG_LOG(HLE,"- screen off");
	}
	else
	{
		fbstate.topaddr = topaddr;
		fbstate.pspFramebufFormat = (PspDisplayPixelFormat)pixelformat;
		fbstate.pspFramebufLinesize = linesize;
	}

	if (sync == PSP_DISPLAY_SETBUF_IMMEDIATE)
	{
		// Write immediately to the current framebuffer parameters
		framebuf = fbstate;
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.pspFramebufLinesize, framebuf.pspFramebufFormat);
	}
	else if (topaddr != 0)
	{
		// Delay the write until vblank
		latchedFramebuf = fbstate;
		framebufIsLatched = true;
	}

	RETURN(0);
}

u32 sceDisplayGetFramebuf(u32 topaddrPtr, u32 linesizePtr, u32 pixelFormatPtr, int mode)
{
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

void sceDisplayWaitVblankStart()
{
	DEBUG_LOG(HLE,"sceDisplayWaitVblankStart()");
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, false);
}

void sceDisplayWaitVblank()
{
	DEBUG_LOG(HLE,"sceDisplayWaitVblank()");
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, false);
}

void sceDisplayWaitVblankCB()
{
	DEBUG_LOG(HLE,"sceDisplayWaitVblankCB()");	
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true);
	__KernelCheckCallbacks();
}

void sceDisplayWaitVblankStartCB()
{
	DEBUG_LOG(HLE,"sceDisplayWaitVblankStartCB()");	
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true);
	__KernelCheckCallbacks();
}

void sceDisplayWaitVblankStartMultiCB()
{
	DEBUG_LOG(HLE,"sceDisplayWaitVblankStartMultiCB()");	
	__KernelWaitCurThread(WAITTYPE_VBLANK, 0, 0, 0, true);
	__KernelCheckCallbacks();
}

void sceDisplayGetVcount()
{
	// Too spammy
	// DEBUG_LOG(HLE,"%i=sceDisplayGetVcount()", vCount);	
	// Games like Puyo Puyo call this in a tight loop at end-of-frame. We could have it consume some time from CoreTiming?
	CoreTiming::Idle(1000000);
	RETURN(vCount);
}

void sceDisplayGetCurrentHcount()
{
	RETURN(hCount++);
}


void sceDisplayGetAccumulatedHcount()
{
	// Just do an estimate
	u32 accumHCount = CoreTiming::GetTicks() / (222000000 / 60 / 272);
	DEBUG_LOG(HLE,"%i=sceDisplayGetAccumulatedHcount()", accumHCount);	
	RETURN(accumHCount);
}

float sceDisplayGetFramePerSec()
{
	float fps = 59.9400599f;
	DEBUG_LOG(HLE,"%f=sceDisplayGetFramePerSec()", fps);
	return fps;	// (9MHz * 1)/(525 * 286) 
}

const HLEFunction sceDisplay[] = 
{
	{0x0E20F177,&WrapU_UUU<sceDisplaySetMode>, "sceDisplaySetMode"},
	{0x289D82FE,sceDisplaySetFramebuf, "sceDisplaySetFramebuf"},
	{0xEEDA2E54,&WrapU_UUUI<sceDisplayGetFramebuf>,"sceDisplayGetFrameBuf"},
	{0x36CDFADE,sceDisplayWaitVblank, "sceDisplayWaitVblank"},
	{0x984C27E7,sceDisplayWaitVblankStart, "sceDisplayWaitVblankStart"},
	{0x8EB9EC49,sceDisplayWaitVblankCB, "sceDisplayWaitVblankCB"},
	{0x46F186C3,sceDisplayWaitVblankStartCB, "sceDisplayWaitVblankStartCB"},
	{0x77ed8b3a,sceDisplayWaitVblankStartMultiCB,"sceDisplayWaitVblankStartMultiCB"},

	{0xdba6c4c4,&WrapF_V<sceDisplayGetFramePerSec>,"sceDisplayGetFramePerSec"},
	{0x773dd3a3,sceDisplayGetCurrentHcount,"sceDisplayGetCurrentHcount"},
	{0x210eab3a,sceDisplayGetAccumulatedHcount,"sceDisplayGetAccumulatedHcount"},
	{0x9C6EAAD7,sceDisplayGetVcount,"sceDisplayGetVcount"},
	{0xDEA197D4,0,"sceDisplayGetMode"},
	{0x7ED59BC4,0,"sceDisplaySetHoldMode"},
	{0xA544C486,0,"sceDisplaySetResumeMode"},
	{0xB4F378FA,0,"sceDisplayIsForeground"},
	{0x31C4BAA8,0,"sceDisplayGetBrightness"},
	{0x4D4E10EC,sceDisplayIsVblank,"sceDisplayIsVblank"},
};

void Register_sceDisplay()
{
	RegisterModule("sceDisplay", ARRAY_SIZE(sceDisplay), sceDisplay);
}
