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

#if defined(ANDROID) || defined(BLACKBERRY)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include <vector>

//#include "base/timeutil.h"

#include "../Core/CoreTiming.h"
#include "../MIPS/MIPS.h"
#include "../HLE/HLE.h"
#include "sceAudio.h"
#include "../Host.h"
#include "../Config.h"
#include "sceDisplay.h"
#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelInterrupt.h"

// TODO: This file should not depend directly on GLES code.
#include "../../GPU/GLES/Framebuffer.h"
#include "../../GPU/GLES/ShaderManager.h"
#include "../../GPU/GPUState.h"

extern ShaderManager shaderManager;

struct FrameBufferState
{
	u32 topaddr;
	u8 *pspframebuf;
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
	framebufIsLatched = false;
	framebuf.topaddr = 0x04000000;
	framebuf.pspframebuf = Memory::GetPointer(0x04000000);
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
	}

	// Yeah, this has to be the right moment to end the frame. Should possibly blit the right buffer
	// depending on what's set in sceDisplaySetFramebuf, in order to support half-framerate games -
	// an initial hack could be to NOT end the frame if the buffer didn't change? that should work okay.
	{
		host->EndFrame();

		host->BeginFrame();
		if (g_Config.bDisplayFramebuffer)
		{
			INFO_LOG(HLE, "Drawing the framebuffer");
			DisplayDrawer_DrawFramebuffer(framebuf.pspframebuf, framebuf.pspFramebufFormat, framebuf.pspFramebufLinesize);
		}

		shaderManager.DirtyShader();
		shaderManager.DirtyUniform(DIRTY_ALL);
	}

	// TODO: Find a way to tell the CPU core to stop emulating here, when running on Android.
}


void hleLeaveVblank(u64 userdata, int cyclesLate)
{
	isVblank = 0;
	DEBUG_LOG(HLE,"Leave VBlank");
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

	glClearColor(0,0,0,1);
//	glClearColor(1,0,1,1);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	return 0;
}

void sceDisplaySetFramebuf()
{
	//host->EndFrame();
	u32 topaddr = PARAM(0);
	int linesize = PARAM(1);
	int pixelformat = PARAM(2);
	int sync = PARAM(3);

	FrameBufferState *fbstate = 0;
	if (sync == PSP_DISPLAY_SETBUF_IMMEDIATE)
	{
		// Write immediately to the current framebuffer parameters
		fbstate = &framebuf;
	}
	else if (topaddr != 0)
	{
		// Delay the write until vblank
		fbstate = &latchedFramebuf;
		framebufIsLatched = true;
	}

	DEBUG_LOG(HLE,"sceDisplaySetFramebuf(topaddr=%08x,linesize=%d,pixelsize=%d,sync=%d)",topaddr,linesize,pixelformat,sync);
	if (topaddr == 0)
	{
		DEBUG_LOG(HLE,"- screen off");
	}
	else
	{
		fbstate->topaddr = topaddr;
		fbstate->pspframebuf = Memory::GetPointer((0x44000000)|(topaddr & 0x1FFFFF));	// TODO - check
		fbstate->pspFramebufFormat = (PspDisplayPixelFormat)pixelformat;
		fbstate->pspFramebufLinesize = linesize;
	}

	RETURN(0);
}

void sceDisplayGetFramebuf()
{
	DEBUG_LOG(HLE,"sceDisplayGetFramebuf()");	
	RETURN(framebuf.topaddr);
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
	{0x36CDFADE,sceDisplayWaitVblank, "sceDisplayWaitVblank"},
	{0x984C27E7,sceDisplayWaitVblankStart, "sceDisplayWaitVblankStart"},
	{0x8EB9EC49,sceDisplayWaitVblankCB, "sceDisplayWaitVblankCB"},
	{0x46F186C3,sceDisplayWaitVblankStartCB, "sceDisplayWaitVblankStartCB"},
	{0x77ed8b3a,0,"sceDisplayWaitVblankStartMultiCB"},

	{0xdba6c4c4,&WrapF_V<sceDisplayGetFramePerSec>,"sceDisplayGetFramePerSec"},
	{0x773dd3a3,sceDisplayGetCurrentHcount,"sceDisplayGetCurrentHcount"},
	{0x210eab3a,sceDisplayGetAccumulatedHcount,"sceDisplayGetAccumulatedHcount"},
	{0x9C6EAAD7,sceDisplayGetVcount,"sceDisplayGetVcount"},
	{0x984C27E7,0,"sceDisplayWaitVblankStart"},
	{0xDEA197D4,0,"sceDisplayGetMode"},
	{0x7ED59BC4,0,"sceDisplaySetHoldMode"},
	{0xA544C486,0,"sceDisplaySetResumeMode"},
	{0x289D82FE,0,"sceDisplaySetFrameBuf"},
	{0xEEDA2E54,sceDisplayGetFramebuf,"sceDisplayGetFrameBuf"},
	{0xB4F378FA,0,"sceDisplayIsForeground"},
	{0x31C4BAA8,0,"sceDisplayGetBrightness"},
	{0x4D4E10EC,sceDisplayIsVblank,"sceDisplayIsVblank"},
};

void Register_sceDisplay()
{
	RegisterModule("sceDisplay", ARRAY_SIZE(sceDisplay), sceDisplay);
}
