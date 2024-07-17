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
#include <mutex>
#include <vector>
#include "Common/CommonTypes.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HW/Display.h"
#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"

// Called when vblank happens (like an internal interrupt.)  Not part of state, should be static.
static std::mutex listenersLock;
static std::vector<VblankCallback> vblankListeners;
typedef std::pair<FlipCallback, void *> FlipListener;
static std::vector<FlipListener> flipListeners;

static uint64_t frameStartTicks;
static int numVBlanks;
// hCount is computed now.
static int vCount;
// The "AccumulatedHcount" can be adjusted, this is the base.
static uint32_t hCountBase;
static int isVblank;
static constexpr int hCountPerVblank = 286;

// FPS stats for frameskip, FPS display, etc.
static int lastFpsFrame = 0;
static double lastFpsTime = 0.0;
static double fps = 0.0;
static int lastNumFlips = 0;
static float flips = 0.0f;
static int actualFlips = 0;  // taking frameskip into account
static int lastActualFlips = 0;
static float actualFps = 0;

// FPS stats for averaging.
static double fpsHistory[120];
static constexpr int fpsHistorySize = (int)ARRAY_SIZE(fpsHistory);
static int fpsHistoryPos = 0;
static int fpsHistoryValid = 0;

// Frame time stats.
static double frameTimeHistory[600];
static double frameSleepHistory[600];
static constexpr int frameTimeHistorySize = (int)ARRAY_SIZE(frameTimeHistory);
static int frameTimeHistoryPos = 0;
static int frameTimeHistoryValid = 0;
static double lastFrameTimeHistory = 0.0;

static void CalculateFPS() {
	double now = time_now_d();

	if (now >= lastFpsTime + 1.0) {
		double frames = (numVBlanks - lastFpsFrame);
		actualFps = (float)(actualFlips - lastActualFlips);

		fps = frames / (now - lastFpsTime);
		flips = (float)(g_Config.iDisplayRefreshRate * (double)(gpuStats.numFlips - lastNumFlips) / frames);

		lastFpsFrame = numVBlanks;
		lastNumFlips = gpuStats.numFlips;
		lastActualFlips = actualFlips;
		lastFpsTime = now;

		fpsHistory[fpsHistoryPos++] = fps;
		fpsHistoryPos = fpsHistoryPos % fpsHistorySize;
		if (fpsHistoryValid < fpsHistorySize) {
			++fpsHistoryValid;
		}
	}

	if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::FRAME_GRAPH || coreCollectDebugStats) {
		frameTimeHistory[frameTimeHistoryPos++] = now - lastFrameTimeHistory;
		lastFrameTimeHistory = now;
		frameTimeHistoryPos = frameTimeHistoryPos % frameTimeHistorySize;
		if (frameTimeHistoryValid < frameTimeHistorySize) {
			++frameTimeHistoryValid;
		}
		frameSleepHistory[frameTimeHistoryPos] = 0.0;
	}
}

// TODO: Also average actualFps
void __DisplayGetFPS(float *out_vps, float *out_fps, float *out_actual_fps) {
	*out_vps = (float)fps;
	*out_fps = flips;
	*out_actual_fps = actualFps;
}

void __DisplayGetVPS(float *out_vps) {
	*out_vps = (float)fps;
}

void __DisplayGetAveragedFPS(float *out_vps, float *out_fps) {
	double avg = 0.0;
	if (fpsHistoryValid > 0) {
		for (int i = 0; i < fpsHistoryValid; ++i) {
			avg += fpsHistory[i];
		}
		avg /= (double)fpsHistoryValid;
	}

	*out_vps = *out_fps = (float)avg;
}

int __DisplayGetFlipCount() {
	return actualFlips;
}

int __DisplayGetNumVblanks() {
	return numVBlanks;
}

int __DisplayGetVCount() {
	return vCount;
}

bool DisplayIsVblank() {
	return isVblank != 0;
}

uint64_t DisplayFrameStartTicks() {
	return frameStartTicks;
}

uint32_t __DisplayGetCurrentHcount() {
	const int ticksIntoFrame = (int)(CoreTiming::GetTicks() - frameStartTicks);
	const int ticksPerVblank = CoreTiming::GetClockFrequencyHz() / 60 / hCountPerVblank;
	// Can't seem to produce a 0 on real hardware, offsetting by 1 makes things look right.
	return 1 + (ticksIntoFrame / ticksPerVblank);
}

uint32_t __DisplayGetAccumulatedHcount() {
	// The hCount is always a positive int, and wraps from 0x7FFFFFFF -> 0.
	int value = hCountBase + __DisplayGetCurrentHcount();
	return value & 0x7FFFFFFF;
}

void DisplayAdjustAccumulatedHcount(uint32_t diff) {
	hCountBase += diff;
}

double *__DisplayGetFrameTimes(int *out_valid, int *out_pos, double **out_sleep) {
	*out_valid = frameTimeHistoryValid;
	*out_pos = frameTimeHistoryPos;
	*out_sleep = frameSleepHistory;
	return frameTimeHistory;
}

int DisplayGetSleepPos() {
	return frameTimeHistoryPos;
}

void DisplayNotifySleep(double t, int pos) {
	if (pos < 0)
		pos = frameTimeHistoryPos;
	frameSleepHistory[pos] += t;
}

void __DisplayGetDebugStats(char *stats, size_t bufsize) {
	char statbuf[4096];
	if (!gpu) {
		snprintf(stats, bufsize, "N/A");
		return;
	}
	gpu->GetStats(statbuf, sizeof(statbuf));

	snprintf(stats, bufsize,
		"Kernel processing time: %0.2f ms\n"
		"Slowest syscall: %s : %0.2f ms\n"
		"Most active syscall: %s : %0.2f ms\n%s",
		kernelStats.msInSyscalls * 1000.0f,
		kernelStats.slowestSyscallName ? kernelStats.slowestSyscallName : "(none)",
		kernelStats.slowestSyscallTime * 1000.0f,
		kernelStats.summedSlowestSyscallName ? kernelStats.summedSlowestSyscallName : "(none)",
		kernelStats.summedSlowestSyscallTime * 1000.0f,
		statbuf);
}

// On like 90hz, 144hz, etc, we return 60.0f as the framerate target. We only target other
// framerates if they're close to 60.
static float FramerateTarget() {
	float target = System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE);
	if (target < 57.0 || target > 63.0f) {
		return 60.0f;
	} else {
		return target;
	}
}

bool DisplayIsRunningSlow() {
	// Allow for some startup turbulence for 8 seconds before assuming things are bad.
	if (fpsHistoryValid >= 8) {
		// Look at only the last 15 samples (starting at the 14th sample behind current.)
		int rangeStart = fpsHistoryPos - std::min(fpsHistoryValid, 14);

		double best = 0.0;
		for (int i = rangeStart; i <= fpsHistoryPos; ++i) {
			// rangeStart may have been negative if near a wrap around.
			int index = (fpsHistorySize + i) % fpsHistorySize;
			best = std::max(fpsHistory[index], best);
		}

		return best < FramerateTarget() * 0.97;
	}

	return false;
}

void DisplayFireVblankStart() {
	frameStartTicks = CoreTiming::GetTicks();
	numVBlanks++;

	isVblank = 1;
	vCount++; // vCount increases at each VBLANK.
	hCountBase += hCountPerVblank; // This is the "accumulated" hcount base.
	if (hCountBase > 0x7FFFFFFF) {
		hCountBase -= 0x80000000;
	}
}

void DisplayFireVblankEnd() {
	isVblank = 0;
	std::vector<VblankCallback> toCall;
	{
		std::lock_guard<std::mutex> guard(listenersLock);
		toCall = vblankListeners;
	}

	for (VblankCallback cb : toCall) {
		cb();
	}
}

void DisplayFireFlip() {
	std::vector<FlipListener> toCall = [] {
		std::lock_guard<std::mutex> guard(listenersLock);
		return flipListeners;
	}();

	// This is also the right time to calculate FPS.
	CalculateFPS();

	for (FlipListener cb : toCall) {
		cb.first(cb.second);
	}
}

void DisplayFireActualFlip() {
	actualFlips++;
}

void __DisplayListenVblank(VblankCallback callback) {
	std::lock_guard<std::mutex> guard(listenersLock);
	vblankListeners.push_back(callback);
}

void __DisplayListenFlip(FlipCallback callback, void *userdata) {
	std::lock_guard<std::mutex> guard(listenersLock);
	flipListeners.emplace_back(callback, userdata);
}

void __DisplayForgetFlip(FlipCallback callback, void *userdata) {
	std::lock_guard<std::mutex> guard(listenersLock);
	flipListeners.erase(std::remove_if(flipListeners.begin(), flipListeners.end(), [&](FlipListener item) {
		return item.first == callback && item.second == userdata;
	}), flipListeners.end());
}

int DisplayCalculateFrameSkip() {
	int frameSkipNum;
	if (g_Config.iFrameSkipType == 1) {
		// Calculate the frames to skip dynamically using the set percentage of the current fps
		frameSkipNum = (int)ceil(flips * (static_cast<double>(g_Config.iFrameSkip) / 100.00));
	} else {
		// Use the set number of frames to skip
		frameSkipNum = g_Config.iFrameSkip;
	}
	return frameSkipNum;
}

void DisplayHWInit() {
	frameStartTicks = 0;
	numVBlanks = 0;
	isVblank = 0;
	vCount = 0;
	hCountBase = 0;

	flips = 0;
	fps = 0.0;
	actualFlips = 0;
	lastActualFlips = 0;
	lastNumFlips = 0;

	fpsHistoryValid = 0;
	fpsHistoryPos = 0;

	frameTimeHistoryValid = 0;
	frameTimeHistoryPos = 0;
	lastFrameTimeHistory = 0.0;
}

void DisplayHWShutdown() {
	std::lock_guard<std::mutex> guard(listenersLock);
	vblankListeners.clear();
	flipListeners.clear();
}

void DisplayHWDoState(PointerWrap &p, int hleCompatV2) {
	Do(p, frameStartTicks);
	Do(p, vCount);
	if (hleCompatV2) {
		double oldHCountBase;
		Do(p, oldHCountBase);
		hCountBase = (int)oldHCountBase;
	} else {
		Do(p, hCountBase);
	}
	Do(p, isVblank);
}
