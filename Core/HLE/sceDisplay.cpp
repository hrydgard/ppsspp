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

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

// TODO: Move this somewhere else, cleanup.
#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h>
#endif

#include "Common/Data/Text/I18n.h"
#include "Common/Profiler/Profiler.h"
#include "Common/System/System.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HW/Display.h"
#include "Core/Util/PPGeDraw.h"

#include "GPU/GPU.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PostShader.h"
#include "GPU/Debugger/Record.h"

struct FrameBufferState {
	u32 topaddr;
	GEBufferFormat fmt;
	int stride;
};

struct WaitVBlankInfo {
	WaitVBlankInfo(u32 tid) : threadID(tid), vcountUnblock(1) {}
	WaitVBlankInfo(u32 tid, int vcount) : threadID(tid), vcountUnblock(vcount) {}
	SceUID threadID;
	// Number of vcounts to block for.
	int vcountUnblock;

	void DoState(PointerWrap &p) {
		auto s = p.Section("WaitVBlankInfo", 1);
		if (!s)
			return;

		Do(p, threadID);
		Do(p, vcountUnblock);
	}
};

// STATE BEGIN
static FrameBufferState framebuf;
static FrameBufferState latchedFramebuf;
static bool framebufIsLatched;

static int enterVblankEvent = -1;
static int leaveVblankEvent = -1;
static int afterFlipEvent = -1;
static int lagSyncEvent = -1;

static double lastLagSync = 0.0;
static bool lagSyncScheduled = false;

static int numSkippedFrames;
static bool hasSetMode;
static int resumeMode;
static int holdMode;
static int brightnessLevel;
static int mode;
static int width;
static int height;
static bool wasPaused;
static bool flippedThisFrame;

// 1.001f to compensate for the classic 59.94 NTSC framerate that the PSP seems to have.
static const double timePerVblank = 1.001f / 60.0f;

// Don't include this in the state, time increases regardless of state.
static double curFrameTime;
static double lastFrameTime;
static double nextFrameTime;
static int numVBlanksSinceFlip;

const int PSP_DISPLAY_MODE_LCD = 0;

std::vector<WaitVBlankInfo> vblankWaitingThreads;
// Key is the callback id it was for, or if no callback, the thread id.
// Value is the goal vcount number (in case the callback takes >= 1 vcount to return.)
std::map<SceUID, int> vblankPausedWaits;

// STATE END

// The vblank period is 731.5 us (0.7315 ms)
const double vblankMs = 0.7315;
// These are guesses based on tests.
const double vsyncStartMs = 0.5925;
const double vsyncEndMs = 0.7265;
const double frameMs = 1001.0 / 60.0;

enum {
	PSP_DISPLAY_SETBUF_IMMEDIATE = 0,
	PSP_DISPLAY_SETBUF_NEXTFRAME = 1
};

// For the "max 60 fps" setting.
static int lastFlipsTooFrequent = 0;
static u64 lastFlipCycles = 0;
static u64 nextFlipCycles = 0;

void hleEnterVblank(u64 userdata, int cyclesLate);
void hleLeaveVblank(u64 userdata, int cyclesLate);
void hleAfterFlip(u64 userdata, int cyclesLate);
void hleLagSync(u64 userdata, int cyclesLate);

void __DisplayVblankBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __DisplayVblankEndCallback(SceUID threadID, SceUID prevCallbackId);

void __DisplayFlip(int cyclesLate);

static bool UseLagSync() {
	return g_Config.bForceLagSync && !g_Config.bAutoFrameSkip;
}

static void ScheduleLagSync(int over = 0) {
	lagSyncScheduled = UseLagSync();
	if (lagSyncScheduled) {
		// Reset over if it became too high, such as after pausing or initial loading.
		// There's no real sense in it being more than 1/60th of a second.
		if (over > 1000000 / 60) {
			over = 0;
		}
		CoreTiming::ScheduleEvent(usToCycles(1000 + over), lagSyncEvent, 0);
		lastLagSync = time_now_d();
	}
}

void __DisplayInit() {
	DisplayHWInit();
	hasSetMode = false;
	mode = 0;
	resumeMode = 0;
	holdMode = 0;
	brightnessLevel = 84;
	width = 480;
	height = 272;
	numSkippedFrames = 0;
	numVBlanksSinceFlip = 0;
	flippedThisFrame = false;
	framebufIsLatched = false;
	framebuf.topaddr = 0x04000000;
	framebuf.fmt = GE_FORMAT_8888;
	framebuf.stride = 512;
	memcpy(&latchedFramebuf, &framebuf, sizeof(latchedFramebuf));
	lastFlipsTooFrequent = 0;
	lastFlipCycles = 0;
	nextFlipCycles = 0;
	wasPaused = false;

	enterVblankEvent = CoreTiming::RegisterEvent("EnterVBlank", &hleEnterVblank);
	leaveVblankEvent = CoreTiming::RegisterEvent("LeaveVBlank", &hleLeaveVblank);
	afterFlipEvent = CoreTiming::RegisterEvent("AfterFlip", &hleAfterFlip);

	lagSyncEvent = CoreTiming::RegisterEvent("LagSync", &hleLagSync);
	ScheduleLagSync();

	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs), enterVblankEvent, 0);
	curFrameTime = 0.0;
	nextFrameTime = 0.0;
	lastFrameTime = 0.0;

	__KernelRegisterWaitTypeFuncs(WAITTYPE_VBLANK, __DisplayVblankBeginCallback, __DisplayVblankEndCallback);
}

struct GPUStatistics_v0 {
	int firstInts[11];
	double msProcessingDisplayLists;
	int moreInts[15];
};

void __DisplayDoState(PointerWrap &p) {
	auto s = p.Section("sceDisplay", 1, 7);
	if (!s)
		return;

	Do(p, framebuf);
	Do(p, latchedFramebuf);
	Do(p, framebufIsLatched);
	DisplayHWDoState(p, s <= 2);
	Do(p, hasSetMode);
	Do(p, mode);
	Do(p, resumeMode);
	Do(p, holdMode);
	if (s >= 4) {
		Do(p, brightnessLevel);
	}
	Do(p, width);
	Do(p, height);
	WaitVBlankInfo wvi(0);
	Do(p, vblankWaitingThreads, wvi);
	Do(p, vblankPausedWaits);

	Do(p, enterVblankEvent);
	CoreTiming::RestoreRegisterEvent(enterVblankEvent, "EnterVBlank", &hleEnterVblank);
	Do(p, leaveVblankEvent);
	CoreTiming::RestoreRegisterEvent(leaveVblankEvent, "LeaveVBlank", &hleLeaveVblank);
	Do(p, afterFlipEvent);
	CoreTiming::RestoreRegisterEvent(afterFlipEvent, "AfterFlip", &hleAfterFlip);

	if (s >= 5) {
		Do(p, lagSyncEvent);
		Do(p, lagSyncScheduled);
		CoreTiming::RestoreRegisterEvent(lagSyncEvent, "LagSync", &hleLagSync);
		lastLagSync = time_now_d();
		if (lagSyncScheduled != UseLagSync()) {
			ScheduleLagSync();
		}
	} else {
		lagSyncEvent = -1;
		CoreTiming::RestoreRegisterEvent(lagSyncEvent, "LagSync", &hleLagSync);
		ScheduleLagSync();
	}

	Do(p, gstate);

	// TODO: GPU stuff is really not the responsibility of sceDisplay.
	// Display just displays the buffers the GPU has drawn, they are really completely distinct.
	// Maybe a bit tricky to move at this point, though...

	gstate_c.DoState(p);
	if (s < 2) {
		// This shouldn't have been savestated anyway, but it was.
		// It's unlikely to overlap with the first value in gpuStats.
		int gpuVendorTemp = 0;
		p.ExpectVoid(&gpuVendorTemp, sizeof(gpuVendorTemp));
	}
	if (s < 6) {
		GPUStatistics_v0 oldStats;
		Do(p, oldStats);
	}

	if (s < 7) {
		u64 now = CoreTiming::GetTicks();
		lastFlipCycles = now;
		nextFlipCycles = now;
	} else {
		Do(p, lastFlipCycles);
		Do(p, nextFlipCycles);
	}

	gpu->DoState(p);

	if (p.mode == p.MODE_READ) {
		gpu->ReapplyGfxState();

		if (hasSetMode) {
			gpu->InitClear();
		}
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.stride, framebuf.fmt);
	}
}

void __DisplayShutdown() {
	DisplayHWShutdown();
	vblankWaitingThreads.clear();
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
		if (t->threadID == threadID) {
			waitData = *t;
			vblankWaitingThreads.erase(vblankWaitingThreads.begin() + i);
			break;
		}
	}

	if (waitData.threadID != threadID) {
		WARN_LOG_REPORT(SCEDISPLAY, "sceDisplayWaitVblankCB: could not find waiting thread info.");
		return;
	}

	vblankPausedWaits[pauseKey] = __DisplayGetVCount() + waitData.vcountUnblock;
	DEBUG_LOG(SCEDISPLAY, "sceDisplayWaitVblankCB: Suspending vblank wait for callback");
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
	if (vcountUnblock <= __DisplayGetVCount()) {
		__KernelResumeThreadFromWait(threadID, 0);
		return;
	}

	// Still have to wait a bit longer.
	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread(), vcountUnblock - __DisplayGetVCount()));
	DEBUG_LOG(SCEDISPLAY, "sceDisplayWaitVblankCB: Resuming vblank wait from callback");
}

void __DisplaySetWasPaused() {
	wasPaused = true;
}

static int FrameTimingLimit() {
	if (PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM1)
		return g_Config.iFpsLimit1;
	if (PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM2)
		return g_Config.iFpsLimit2;
	if (PSP_CoreParameter().fastForward)
		return 0;
	return 60;
}

static bool FrameTimingThrottled() {
	return FrameTimingLimit() != 0;
}

static void DoFrameDropLogging(float scaledTimestep) {
	if (lastFrameTime != 0.0 && !wasPaused && lastFrameTime + scaledTimestep < curFrameTime) {
		const double actualTimestep = curFrameTime - lastFrameTime;

		char stats[4096];
		__DisplayGetDebugStats(stats, sizeof(stats));
		NOTICE_LOG(SCEDISPLAY, "Dropping frames - budget = %.2fms / %.1ffps, actual = %.2fms (+%.2fms) / %.1ffps\n%s", scaledTimestep * 1000.0, 1.0 / scaledTimestep, actualTimestep * 1000.0, (actualTimestep - scaledTimestep) * 1000.0, 1.0 / actualTimestep, stats);
	}
}

// Let's collect all the throttling and frameskipping logic here.
static void DoFrameTiming(bool &throttle, bool &skipFrame, float timestep) {
	PROFILE_THIS_SCOPE("timing");
	int fpsLimit = FrameTimingLimit();
	throttle = FrameTimingThrottled();
	skipFrame = false;

	// Check if the frameskipping code should be enabled. If neither throttling or frameskipping is on,
	// we have nothing to do here.
	bool doFrameSkip = g_Config.iFrameSkip != 0;
	if (!throttle && !doFrameSkip)
		return;

	float scaledTimestep = timestep;
	if (fpsLimit > 0 && fpsLimit != 60) {
		scaledTimestep *= 60.0f / fpsLimit;
	}

	if (lastFrameTime == 0.0 || wasPaused) {
		nextFrameTime = time_now_d() + scaledTimestep;
	} else {
		// Advance lastFrameTime by a constant amount each frame,
		// but don't let it get too far behind as things can get very jumpy.
		const double maxFallBehindFrames = 5.5;

		nextFrameTime = std::max(lastFrameTime + scaledTimestep, time_now_d() - maxFallBehindFrames * scaledTimestep);
	}
	curFrameTime = time_now_d();

	if (g_Config.bLogFrameDrops) {
		DoFrameDropLogging(scaledTimestep);
	}

	// Auto-frameskip automatically if speed limit is set differently than the default.
	int frameSkipNum = DisplayCalculateFrameSkip();
	if (g_Config.bAutoFrameSkip) {
		// autoframeskip
		// Argh, we are falling behind! Let's skip a frame and see if we catch up.
		if (curFrameTime > nextFrameTime && doFrameSkip) {
			skipFrame = true;
		}
	} else if (frameSkipNum >= 1) {
		// fixed frameskip
		if (numSkippedFrames >= frameSkipNum)
			skipFrame = false;
		else
			skipFrame = true;
	}

	// TODO: This is NOT where we should wait, really! We should mark each outgoing frame with the desired
	// timestamp to push it to display, and sleep in the render thread to achieve that.

	if (curFrameTime < nextFrameTime && throttle) {
		// If time gap is huge just jump (somebody fast-forwarded)
		if (nextFrameTime - curFrameTime > 2*scaledTimestep) {
			nextFrameTime = curFrameTime;
		} else {
			// Wait until we've caught up.
			while (time_now_d() < nextFrameTime) {
#ifdef _WIN32
				sleep_ms(1); // Sleep for 1ms on this thread
#else
				const double left = nextFrameTime - curFrameTime;
				usleep((long)(left * 1000000));
#endif
			}
		}
		curFrameTime = time_now_d();
	}

	lastFrameTime = nextFrameTime;
	wasPaused = false;
}

static void DoFrameIdleTiming() {
	PROFILE_THIS_SCOPE("timing");
	if (!FrameTimingThrottled() || !g_Config.bEnableSound || wasPaused) {
		return;
	}

	double before = time_now_d();
	double dist = before - lastFrameTime;
	// Ignore if the distance is just crazy.  May mean wrap or pause.
	if (dist < 0.0 || dist >= 15 * timePerVblank) {
		return;
	}

	float scaledVblank = timePerVblank;
	int fpsLimit = FrameTimingLimit();
	if (fpsLimit != 0 && fpsLimit != 60) {
		// 0 is handled in FrameTimingThrottled().
		scaledVblank *= 60.0f / fpsLimit;
	}

	// If we have over at least a vblank of spare time, maintain at least 30fps in delay.
	// This prevents fast forward during loading screens.
	// Give a little extra wiggle room in case the next vblank does more work.
	const double goal = lastFrameTime + (numVBlanksSinceFlip - 1) * scaledVblank - 0.001;
	if (numVBlanksSinceFlip >= 2 && before < goal) {
		double cur_time;
		while ((cur_time = time_now_d()) < goal) {
#ifdef _WIN32
			sleep_ms(1);
#else
			const double left = goal - cur_time;
			usleep((long)(left * 1000000));
#endif
		}

		if (g_Config.bDrawFrameGraph || coreCollectDebugStats) {
			DisplayNotifySleep(time_now_d() - before);
		}
	}
}


void hleEnterVblank(u64 userdata, int cyclesLate) {
	int vbCount = userdata;

	VERBOSE_LOG(SCEDISPLAY, "Enter VBlank %i", vbCount);

	DisplayFireVblankStart();

	CoreTiming::ScheduleEvent(msToCycles(vblankMs) - cyclesLate, leaveVblankEvent, vbCount + 1);

	// Trigger VBlank interrupt handlers.
	__TriggerInterrupt(PSP_INTR_IMMEDIATE | PSP_INTR_ONLY_IF_ENABLED | PSP_INTR_ALWAYS_RESCHED, PSP_VBLANK_INTR, PSP_INTR_SUB_ALL);

	// Wake up threads waiting for VBlank
	u32 error;
	bool wokeThreads = false;
	for (size_t i = 0; i < vblankWaitingThreads.size(); i++) {
		if (--vblankWaitingThreads[i].vcountUnblock == 0) {
			// Only wake it if it wasn't already released by someone else.
			SceUID waitID = __KernelGetWaitID(vblankWaitingThreads[i].threadID, WAITTYPE_VBLANK, error);
			if (waitID == 1) {
				__KernelResumeThreadFromWait(vblankWaitingThreads[i].threadID, 0);
				wokeThreads = true;
			}
			vblankWaitingThreads.erase(vblankWaitingThreads.begin() + i--);
		}
	}
	if (wokeThreads) {
		__KernelReSchedule("entered vblank");
	}

	numVBlanksSinceFlip++;

	// TODO: Should this be done here or in hleLeaveVblank?
	if (framebufIsLatched) {
		DEBUG_LOG(SCEDISPLAY, "Setting latched framebuffer %08x (prev: %08x)", latchedFramebuf.topaddr, framebuf.topaddr);
		framebuf = latchedFramebuf;
		framebufIsLatched = false;
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.stride, framebuf.fmt);
		__DisplayFlip(cyclesLate);
	} else if (!flippedThisFrame) {
		// Gotta flip even if sceDisplaySetFramebuf was not called.
		__DisplayFlip(cyclesLate);
	}
}

void __DisplayFlip(int cyclesLate) {
	flippedThisFrame = true;
	// We flip only if the framebuffer was dirty. This eliminates flicker when using
	// non-buffered rendering. The interaction with frame skipping seems to need
	// some work.
	// But, let's flip at least once every 10 vblanks, to update fps, etc.
	const bool noRecentFlip = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE && numVBlanksSinceFlip >= 10;
	// Also let's always flip for animated shaders.
	bool postEffectRequiresFlip = false;

	bool duplicateFrames = g_Config.bRenderDuplicateFrames && g_Config.iFrameSkip == 0;

	bool fastForwardSkipFlip = g_Config.iFastForwardMode != (int)FastForwardMode::CONTINUOUS;
	if (g_Config.bVSync && GetGPUBackend() == GPUBackend::VULKAN) {
		// Vulkan doesn't support the interval setting, so we force skipping the flip.
		fastForwardSkipFlip = true;
	}

	if (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) {
		postEffectRequiresFlip = duplicateFrames || g_Config.bShaderChainRequires60FPS;
	}

	const bool fbDirty = gpu->FramebufferDirty();

	if (fbDirty || noRecentFlip || postEffectRequiresFlip) {
		int frameSleepPos = DisplayGetSleepPos();
		double frameSleepStart = time_now_d();
		DisplayFireFlip();

		// Let the user know if we're running slow, so they know to adjust settings.
		// Sometimes users just think the sound emulation is broken.
		static bool hasNotifiedSlow = false;
		if (!g_Config.bHideSlowWarnings &&
			!hasNotifiedSlow &&
			PSP_CoreParameter().fpsLimit == FPSLimit::NORMAL &&
			DisplayIsRunningSlow()) {
#ifndef _DEBUG
			auto err = GetI18NCategory("Error");
			if (g_Config.bSoftwareRendering) {
				host->NotifyUserMessage(err->T("Running slow: Try turning off Software Rendering"), 6.0f, 0xFF30D0D0);
			} else {
				host->NotifyUserMessage(err->T("Running slow: try frameskip, sound is choppy when slow"), 6.0f, 0xFF30D0D0);
			}
#endif
			hasNotifiedSlow = true;
		}

		bool forceNoFlip = false;
		float refreshRate = System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE);
		// Avoid skipping on devices that have 58 or 59 FPS, except when alternate speed is set.
		bool refreshRateNeedsSkip = FrameTimingLimit() != 60 && FrameTimingLimit() > refreshRate;
		// Alternative to frameskip fast-forward, where we draw everything.
		// Useful if skipping a frame breaks graphics or for checking drawing speed.
		if (fastForwardSkipFlip && (!FrameTimingThrottled() || refreshRateNeedsSkip)) {
			static double lastFlip = 0;
			double now = time_now_d();
			if ((now - lastFlip) < 1.0f / refreshRate) {
				forceNoFlip = true;
			} else {
				lastFlip = now;
			}
		}

		// Setting CORE_NEXTFRAME causes a swap.
		const bool fbReallyDirty = gpu->FramebufferReallyDirty();
		if (fbReallyDirty || noRecentFlip || postEffectRequiresFlip) {
			// Check first though, might've just quit / been paused.
			if (!forceNoFlip && Core_NextFrame()) {
				gpu->CopyDisplayToOutput(fbReallyDirty);
				if (fbReallyDirty) {
					DisplayFireActualFlip();
				}
			}
		}

		if (fbDirty) {
			gpuStats.numFlips++;
		}

		bool throttle, skipFrame;
		DoFrameTiming(throttle, skipFrame, (float)numVBlanksSinceFlip * timePerVblank);

		int maxFrameskip = 8;
		int frameSkipNum = DisplayCalculateFrameSkip();
		if (throttle) {
			// 4 here means 1 drawn, 4 skipped - so 12 fps minimum.
			maxFrameskip = frameSkipNum;
		}
		if (numSkippedFrames >= maxFrameskip || GPURecord::IsActivePending()) {
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

		if (g_Config.bDrawFrameGraph || coreCollectDebugStats) {
			// Track how long we sleep (whether vsync or sleep_ms.)
			DisplayNotifySleep(time_now_d() - frameSleepStart, frameSleepPos);
		}
	} else {
		// Okay, there's no new frame to draw.  But audio may be playing, so we need to time still.
		DoFrameIdleTiming();
	}
}

void hleAfterFlip(u64 userdata, int cyclesLate) {
	gpu->BeginFrame();  // doesn't really matter if begin or end of frame.
	PPGeNotifyFrame();

	// This seems like as good a time as any to check if the config changed.
	if (lagSyncScheduled != UseLagSync()) {
		ScheduleLagSync();
	}
}

void hleLeaveVblank(u64 userdata, int cyclesLate) {
	flippedThisFrame = false;
	VERBOSE_LOG(SCEDISPLAY,"Leave VBlank %i", (int)userdata - 1);
	CoreTiming::ScheduleEvent(msToCycles(frameMs - vblankMs) - cyclesLate, enterVblankEvent, userdata);

	// Fire the vblank listeners after the vblank completes.
	DisplayFireVblankEnd();
}

void hleLagSync(u64 userdata, int cyclesLate) {
	// The goal here is to prevent network, audio, and input lag from the real world.
	// Our normal timing is very "stop and go".  This is efficient, but causes real world lag.
	// This event (optionally) runs every 1ms to sync with the real world.
	PROFILE_THIS_SCOPE("timing");

	if (!FrameTimingThrottled()) {
		lagSyncScheduled = false;
		return;
	}

	float scale = 1.0f;
	int fpsLimit = FrameTimingLimit();
	if (fpsLimit != 0 && fpsLimit != 60) {
		// 0 is handled in FrameTimingThrottled().
		scale = 60.0f / fpsLimit;
	}

	const double goal = lastLagSync + (scale / 1000.0f);
	double before = time_now_d();
	// Don't lag too long ever, if they leave it paused.
	double now = before;
	while (now < goal && goal < now + 0.01) {
		// Tight loop on win32 - intentionally, as timing is otherwise not precise enough.
#ifndef _WIN32
		const double left = goal - now;
		usleep((long)(left * 1000000.0));
#endif
		now = time_now_d();
	}

	const int emuOver = (int)cyclesToUs(cyclesLate);
	const int over = (int)((now - goal) * 1000000);
	ScheduleLagSync(over - emuOver);

	if (g_Config.bDrawFrameGraph || coreCollectDebugStats) {
		DisplayNotifySleep(now - before);
	}
}

static u32 sceDisplayIsVblank() {
	return hleLogSuccessI(SCEDISPLAY, DisplayIsVblank());
}

static int DisplayWaitForVblanks(const char *reason, int vblanks, bool callbacks = false) {
	const s64 ticksIntoFrame = CoreTiming::GetTicks() - DisplayFrameStartTicks();
	const s64 cyclesToNextVblank = msToCycles(frameMs) - ticksIntoFrame;

	// These syscalls take about 115 us, so if the next vblank is before then, we're waiting extra.
	// At least, on real firmware a wait >= 16500 into the frame will wait two.
	if (cyclesToNextVblank <= usToCycles(115)) {
		++vblanks;
	}

	vblankWaitingThreads.push_back(WaitVBlankInfo(__KernelGetCurThread(), vblanks));
	__KernelWaitCurThread(WAITTYPE_VBLANK, 1, 0, 0, callbacks, reason);

	return hleLogSuccessVerboseI(SCEDISPLAY, 0, "waiting for %d vblanks", vblanks);
}

static u32 sceDisplaySetMode(int displayMode, int displayWidth, int displayHeight) {
	if (displayMode != PSP_DISPLAY_MODE_LCD || displayWidth != 480 || displayHeight != 272) {
		WARN_LOG_REPORT(SCEDISPLAY, "Video out requested, not supported: mode=%d size=%d,%d", displayMode, displayWidth, displayHeight);
	}
	if (displayMode != PSP_DISPLAY_MODE_LCD) {
		return hleLogWarning(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_MODE, "invalid mode");
	}
	if (displayWidth != 480 || displayHeight != 272) {
		return hleLogWarning(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid size");
	}

	if (!hasSetMode) {
		gpu->InitClear();
		hasSetMode = true;
	}
	mode = displayMode;
	width = displayWidth;
	height = displayHeight;

	hleLogSuccessI(SCEDISPLAY, 0);
	// On success, this implicitly waits for a vblank start.
	return DisplayWaitForVblanks("display mode", 1);
}

void __DisplaySetFramebuf(u32 topaddr, int linesize, int pixelFormat, int sync) {
	FrameBufferState fbstate = {0};
	fbstate.topaddr = topaddr;
	fbstate.fmt = (GEBufferFormat)pixelFormat;
	fbstate.stride = linesize;

	if (sync == PSP_DISPLAY_SETBUF_IMMEDIATE) {
		// Write immediately to the current framebuffer parameters.
		framebuf = fbstate;
		// Also update latchedFramebuf for any sceDisplayGetFramebuf() after this.
		latchedFramebuf = fbstate;
		gpu->SetDisplayFramebuffer(framebuf.topaddr, framebuf.stride, framebuf.fmt);
		// IMMEDIATE means that the buffer is fine. We can just flip immediately.
		// Doing it in non-buffered though creates problems (black screen) on occasion though
		// so let's not.
		if (!flippedThisFrame && g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) {
			double before_flip = time_now_d();
			__DisplayFlip(0);
			double after_flip = time_now_d();
			// Ignore for debug stats.
			hleSetFlipTime(after_flip - before_flip);
		}
	} else {
		// Delay the write until vblank
		latchedFramebuf = fbstate;
		framebufIsLatched = true;

		// If we update the format or stride, this affects the current framebuf immediately.
		framebuf.fmt = latchedFramebuf.fmt;
		framebuf.stride = latchedFramebuf.stride;
	}
}

// Some games (GTA) never call this during gameplay, so bad place to put a framerate counter.
u32 sceDisplaySetFramebuf(u32 topaddr, int linesize, int pixelformat, int sync) {
	if (sync != PSP_DISPLAY_SETBUF_IMMEDIATE && sync != PSP_DISPLAY_SETBUF_NEXTFRAME) {
		return hleLogError(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_MODE, "invalid sync mode");
	}
	if (topaddr != 0 && !Memory::IsRAMAddress(topaddr) && !Memory::IsVRAMAddress(topaddr)) {
		return hleLogError(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_POINTER, "invalid address");
	}
	if ((topaddr & 0xF) != 0) {
		return hleLogError(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_POINTER, "misaligned address");
	}
	if ((linesize & 0x3F) != 0 || (linesize == 0 && topaddr != 0)) {
		return hleLogError(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid stride");
	}
	if (pixelformat < 0 || pixelformat > GE_FORMAT_8888) {
		return hleLogError(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_FORMAT, "invalid format");
	}

	if (sync == PSP_DISPLAY_SETBUF_IMMEDIATE) {
		if ((GEBufferFormat)pixelformat != latchedFramebuf.fmt || linesize != latchedFramebuf.stride) {
			return hleReportError(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_MODE, "must change latched framebuf first");
		}
	}

	hleEatCycles(290);

	s64 delayCycles = 0;
	// Don't count transitions between display off and display on.
	if (topaddr != 0 && topaddr != framebuf.topaddr && framebuf.topaddr != 0 && PSP_CoreParameter().compat.flags().ForceMax60FPS) {
		// sceDisplaySetFramebuf() isn't supposed to delay threads at all.  This is a hack.
		// So let's only delay when it's more than 1ms.
		const s64 FLIP_DELAY_CYCLES_MIN = usToCycles(1000);
		// Some games (like Final Fantasy 4) only call this too much in spurts.
		// The goal is to fix games where this would result in a consistent overhead.
		const int FLIP_DELAY_MIN_FLIPS = 30;
		// Since we move nextFlipCycles forward a whole frame each time, we allow it to be a little ahead.
		// Otherwise it'll always be ahead if the game messes up even once.
		const s64 LEEWAY_CYCLES_PER_FLIP = usToCycles(10);

		u64 now = CoreTiming::GetTicks();
		s64 cyclesAhead = nextFlipCycles - now;
		if (cyclesAhead > FLIP_DELAY_CYCLES_MIN) {
			if (lastFlipsTooFrequent >= FLIP_DELAY_MIN_FLIPS) {
				delayCycles = cyclesAhead;
			} else {
				++lastFlipsTooFrequent;
			}
		} else if (-lastFlipsTooFrequent < FLIP_DELAY_MIN_FLIPS) {
			--lastFlipsTooFrequent;
		}

		// 1001 to account for NTSC timing (59.94 fps.)
		u64 expected = msToCycles(1001) / 60 - LEEWAY_CYCLES_PER_FLIP;
		lastFlipCycles = now;
		nextFlipCycles = std::max(lastFlipCycles, nextFlipCycles) + expected;
	}

	__DisplaySetFramebuf(topaddr, linesize, pixelformat, sync);

	// No delaying while inside an interrupt.  It'll cause idle threads to starve.
	if (delayCycles > 0 && !__IsInInterrupt()) {
		// Okay, the game is going at too high a frame rate.  God of War and Fat Princess both do this.
		// Simply eating the cycles works and is fast, but breaks other games (like Jeanne d'Arc.)
		// So, instead, we delay this HLE thread only (a small deviation from correct behavior.)
		return hleDelayResult(hleLogSuccessI(SCEDISPLAY, 0, "delaying frame thread"), "set framebuf", cyclesToUs(delayCycles));
	} else {
		if (topaddr == 0) {
			return hleLogSuccessI(SCEDISPLAY, 0, "disabling display");
		} else {
			return hleLogSuccessI(SCEDISPLAY, 0);
		}
	}
}

bool __DisplayGetFramebuf(PSPPointer<u8> *topaddr, u32 *linesize, u32 *pixelFormat, int latchedMode) {
	const FrameBufferState &fbState = latchedMode == PSP_DISPLAY_SETBUF_NEXTFRAME ? latchedFramebuf : framebuf;
	if (topaddr != nullptr)
		(*topaddr).ptr = fbState.topaddr;
	if (linesize != nullptr)
		*linesize = fbState.stride;
	if (pixelFormat != nullptr)
		*pixelFormat = fbState.fmt;

	return true;
}

static u32 sceDisplayGetFramebuf(u32 topaddrPtr, u32 linesizePtr, u32 pixelFormatPtr, int latchedMode) {
	const FrameBufferState &fbState = latchedMode == PSP_DISPLAY_SETBUF_NEXTFRAME ? latchedFramebuf : framebuf;

	if (Memory::IsValidAddress(topaddrPtr))
		Memory::Write_U32(fbState.topaddr, topaddrPtr);
	if (Memory::IsValidAddress(linesizePtr))
		Memory::Write_U32(fbState.stride, linesizePtr);
	if (Memory::IsValidAddress(pixelFormatPtr))
		Memory::Write_U32(fbState.fmt, pixelFormatPtr);

	return hleLogSuccessI(SCEDISPLAY, 0);
}

static int DisplayWaitForVblanksCB(const char *reason, int vblanks) {
	return DisplayWaitForVblanks(reason, vblanks, true);
}

static u32 sceDisplayWaitVblankStart() {
	return DisplayWaitForVblanks("vblank start waited", 1);
}

static u32 sceDisplayWaitVblank() {
	if (!DisplayIsVblank()) {
		return DisplayWaitForVblanks("vblank waited", 1);
	} else {
		hleEatCycles(1110);
		hleReSchedule("vblank wait skipped");
		return hleLogSuccessI(SCEDISPLAY, 1, "not waiting since in vblank");
	}
}

static u32 sceDisplayWaitVblankStartMulti(int vblanks) {
	if (vblanks <= 0) {
		return hleLogWarning(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_VALUE, "invalid number of vblanks");
	}
	if (!__KernelIsDispatchEnabled())
		return hleLogWarning(SCEDISPLAY, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
	if (__IsInInterrupt())
		return hleLogWarning(SCEDISPLAY, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "in interrupt");

	return DisplayWaitForVblanks("vblank start multi waited", vblanks);
}

static u32 sceDisplayWaitVblankCB() {
	if (!DisplayIsVblank()) {
		return DisplayWaitForVblanksCB("vblank waited", 1);
	} else {
		hleEatCycles(1110);
		hleReSchedule("vblank wait skipped");
		return hleLogSuccessI(SCEDISPLAY, 1, "not waiting since in vblank");
	}
}

static u32 sceDisplayWaitVblankStartCB() {
	return DisplayWaitForVblanksCB("vblank start waited", 1);
}

static u32 sceDisplayWaitVblankStartMultiCB(int vblanks) {
	if (vblanks <= 0) {
		return hleLogWarning(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_VALUE, "invalid number of vblanks");
	}
	if (!__KernelIsDispatchEnabled())
		return hleLogWarning(SCEDISPLAY, SCE_KERNEL_ERROR_CAN_NOT_WAIT, "dispatch disabled");
	if (__IsInInterrupt())
		return hleLogWarning(SCEDISPLAY, SCE_KERNEL_ERROR_ILLEGAL_CONTEXT, "in interrupt");

	return DisplayWaitForVblanksCB("vblank start multi waited", vblanks);
}

static u32 sceDisplayGetVcount() {
	hleEatCycles(150);
	hleReSchedule("get vcount");
	return hleLogSuccessVerboseI(SCEDISPLAY, __DisplayGetVCount());
}

static u32 sceDisplayGetCurrentHcount() {
	hleEatCycles(275);
	return hleLogSuccessI(SCEDISPLAY, __DisplayGetCurrentHcount());
}

static int sceDisplayAdjustAccumulatedHcount(int value) {
	if (value < 0) {
		return hleLogError(SCEDISPLAY, SCE_KERNEL_ERROR_INVALID_VALUE, "invalid value");
	}

	// Since it includes the current hCount, find the difference to apply to the base.
	u32 accumHCount = __DisplayGetAccumulatedHcount();
	int diff = value - accumHCount;
	DisplayAdjustAccumulatedHcount(diff);

	return hleLogSuccessI(SCEDISPLAY, 0);
}

static int sceDisplayGetAccumulatedHcount() {
	u32 accumHCount = __DisplayGetAccumulatedHcount();
	hleEatCycles(235);
	return hleLogSuccessI(SCEDISPLAY, accumHCount);
}

static float sceDisplayGetFramePerSec() {
	const static float framePerSec = 59.9400599f;
	VERBOSE_LOG(SCEDISPLAY,"%f=sceDisplayGetFramePerSec()", framePerSec);
	return framePerSec;	// (9MHz * 1)/(525 * 286)
}

static u32 sceDisplayIsForeground() {
	int result = hasSetMode && framebuf.topaddr != 0 ? 1 : 0;
	return hleLogSuccessI(SCEDISPLAY, result);
}

static u32 sceDisplayGetMode(u32 modeAddr, u32 widthAddr, u32 heightAddr) {
	if (Memory::IsValidAddress(modeAddr))
		Memory::Write_U32(mode, modeAddr);
	if (Memory::IsValidAddress(widthAddr))
		Memory::Write_U32(width, widthAddr);
	if (Memory::IsValidAddress(heightAddr))
		Memory::Write_U32(height, heightAddr);
	return hleLogSuccessI(SCEDISPLAY, 0);
}

static u32 sceDisplayIsVsync() {
	u64 now = CoreTiming::GetTicks();
	u64 start = DisplayFrameStartTicks() + msToCycles(vsyncStartMs);
	u64 end = DisplayFrameStartTicks() + msToCycles(vsyncEndMs);

	return hleLogSuccessI(SCEDISPLAY, now >= start && now <= end ? 1 : 0);
}

static u32 sceDisplayGetResumeMode(u32 resumeModeAddr) {
	if (Memory::IsValidAddress(resumeModeAddr))
		Memory::Write_U32(resumeMode, resumeModeAddr);
	return hleLogSuccessI(SCEDISPLAY, 0);
}

static u32 sceDisplaySetResumeMode(u32 rMode) {
	// Not sure what this does, seems to do nothing in tests and accept all values.
	resumeMode = rMode;
	return hleReportError(SCEDISPLAY, 0, "unsupported");
}

static u32 sceDisplayGetBrightness(u32 levelAddr, u32 otherAddr) {
	// Standard levels on a PSP: 44, 60, 72, 84 (AC only)

	if (Memory::IsValidAddress(levelAddr)) {
		Memory::Write_U32(brightnessLevel, levelAddr);
	}
	// Always seems to write zero?
	if (Memory::IsValidAddress(otherAddr)) {
		Memory::Write_U32(0, otherAddr);
	}
	return hleLogWarning(SCEDISPLAY, 0);
}

static u32 sceDisplaySetBrightness(int level, int other) {
	// Note: Only usable in kernel mode.
	brightnessLevel = level;
	return hleLogWarning(SCEDISPLAY, 0);
}

static u32 sceDisplaySetHoldMode(u32 hMode) {
	// Not sure what this does, seems to do nothing in tests and accept all values.
	holdMode = hMode;
	return hleReportError(SCEDISPLAY, 0, "unsupported");
}

const HLEFunction sceDisplay[] = {
	{0X0E20F177, &WrapU_III<sceDisplaySetMode>,               "sceDisplaySetMode",                 'x', "iii" },
	{0X289D82FE, &WrapU_UIII<sceDisplaySetFramebuf>,          "sceDisplaySetFrameBuf",             'x', "xiii"},
	{0XEEDA2E54, &WrapU_UUUI<sceDisplayGetFramebuf>,          "sceDisplayGetFrameBuf",             'x', "pppi"},
	{0X36CDFADE, &WrapU_V<sceDisplayWaitVblank>,              "sceDisplayWaitVblank",              'x', "",   HLE_NOT_DISPATCH_SUSPENDED },
	{0X984C27E7, &WrapU_V<sceDisplayWaitVblankStart>,         "sceDisplayWaitVblankStart",         'x', "",   HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X40F1469C, &WrapU_I<sceDisplayWaitVblankStartMulti>,    "sceDisplayWaitVblankStartMulti",    'x', "i"   },
	{0X8EB9EC49, &WrapU_V<sceDisplayWaitVblankCB>,            "sceDisplayWaitVblankCB",            'x', "",   HLE_NOT_DISPATCH_SUSPENDED },
	{0X46F186C3, &WrapU_V<sceDisplayWaitVblankStartCB>,       "sceDisplayWaitVblankStartCB",       'x', "",   HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X77ED8B3A, &WrapU_I<sceDisplayWaitVblankStartMultiCB>,  "sceDisplayWaitVblankStartMultiCB",  'x', "i"   },
	{0XDBA6C4C4, &WrapF_V<sceDisplayGetFramePerSec>,          "sceDisplayGetFramePerSec",          'f', ""    },
	{0X773DD3A3, &WrapU_V<sceDisplayGetCurrentHcount>,        "sceDisplayGetCurrentHcount",        'x', ""    },
	{0X210EAB3A, &WrapI_V<sceDisplayGetAccumulatedHcount>,    "sceDisplayGetAccumulatedHcount",    'i', ""    },
	{0XA83EF139, &WrapI_I<sceDisplayAdjustAccumulatedHcount>, "sceDisplayAdjustAccumulatedHcount", 'i', "i"   },
	{0X9C6EAAD7, &WrapU_V<sceDisplayGetVcount>,               "sceDisplayGetVcount",               'x', ""    },
	{0XDEA197D4, &WrapU_UUU<sceDisplayGetMode>,               "sceDisplayGetMode",                 'x', "ppp" },
	{0X7ED59BC4, &WrapU_U<sceDisplaySetHoldMode>,             "sceDisplaySetHoldMode",             'x', "x"   },
	{0XA544C486, &WrapU_U<sceDisplaySetResumeMode>,           "sceDisplaySetResumeMode",           'x', "x"   },
	{0XBF79F646, &WrapU_U<sceDisplayGetResumeMode>,           "sceDisplayGetResumeMode",           'x', "p"   },
	{0XB4F378FA, &WrapU_V<sceDisplayIsForeground>,            "sceDisplayIsForeground",            'x', ""    },
	{0X31C4BAA8, &WrapU_UU<sceDisplayGetBrightness>,          "sceDisplayGetBrightness",           'x', "pp"  },
	{0X9E3C6DC6, &WrapU_II<sceDisplaySetBrightness>,          "sceDisplaySetBrightness",           'x', "ii"  },
	{0X4D4E10EC, &WrapU_V<sceDisplayIsVblank>,                "sceDisplayIsVblank",                'x', ""    },
	{0X21038913, &WrapU_V<sceDisplayIsVsync>,                 "sceDisplayIsVsync",                 'x', ""    },
};

void Register_sceDisplay() {
	RegisterModule("sceDisplay", ARRAY_SIZE(sceDisplay), sceDisplay);
}

void Register_sceDisplay_driver() {
	RegisterModule("sceDisplay_driver", ARRAY_SIZE(sceDisplay), sceDisplay);
}
