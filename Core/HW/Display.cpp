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

#include <atomic>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>
#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Text/StringWriter.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceIo.h"
#include "Core/HW/Display.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

// Called when vblank happens (like an internal interrupt.)  Not part of state, should be static.
static std::mutex listenersLock;
typedef std::pair<FlipCallback, void *> FlipListener;
static std::vector<FlipListener> flipListeners;

static uint64_t frameStartTicks;
// Atomic because the WebSocket debugger reads it from its own thread (input.buttons.press uses
// it to count down frames) while the CPU thread bumps it.
static std::atomic<int> numVBlanks;
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
static float frameTimeHistory[600];
static float frameSleepHistory[600];
static float frameJitHistory[600];
static constexpr int frameTimeHistorySize = (int)ARRAY_SIZE(frameTimeHistory);
static int frameTimeHistoryPos = 0;
static int frameTimeHistoryValid = 0;
static double lastFrameTimeHistory = 0.0;

// JIT compile time in the frame since the last flip, from the running totals.
static MIPSComp::JitCompileTimes lastFlipJitTimes;
static double lastFlipTime = 0.0;
// Frames that spend this long compiling are logged.
static constexpr double JIT_STALL_LOG_SECONDS = 0.002;

static double CheckJitStall(double now) {
	const MIPSComp::JitCompileTimes &cur = MIPSComp::g_jitCompileTimes;
	const MIPSComp::JitCompileTimes &last = lastFlipJitTimes;
	const double jitSeconds = (cur.TotalNanos() - last.TotalNanos()) * 1e-9;
	if (jitSeconds >= JIT_STALL_LOG_SECONDS && lastFlipTime != 0.0) {
		char phases[160] = "";
		if (cur.phaseBlocks != last.phaseBlocks) {
			snprintf(phases, sizeof(phases), "; frontend %0.2f, passes %0.2f, backend %0.2f, finalize %0.2f",
				(cur.frontendNanos - last.frontendNanos) * 1e-6, (cur.passNanos - last.passNanos) * 1e-6,
				(cur.backendNanos - last.backendNanos) * 1e-6, (cur.finalizeNanos - last.finalizeNanos) * 1e-6);
		}
		INFO_LOG(Log::JIT, "JIT stall: %0.2f ms in a %0.2f ms frame (%d blocks, %d MIPS instructions, compiling %0.2f ms%s; invalidate %0.2f, %d clears %0.2f ms)",
			jitSeconds * 1000.0, (now - lastFlipTime) * 1000.0,
			(int)(cur.blocks - last.blocks), (int)(cur.mipsInstructions - last.mipsInstructions),
			(cur.CompileNanos() - last.CompileNanos()) * 1e-6, phases,
			(cur.invalidateNanos - last.invalidateNanos) * 1e-6,
			cur.clears - last.clears, (cur.clearNanos - last.clearNanos) * 1e-6);
	}
	lastFlipJitTimes = cur;
	lastFlipTime = now;
	return jitSeconds;
}

static void CalculateFPS() {
	double now = time_now_d();
	const double jitSeconds = CheckJitStall(now);
	if (g_Config.iJitBenchFlip > 0 && actualFlips == g_Config.iJitBenchFlip) {
		mipsr4k.BenchmarkJitDeferred();
	}

	if (now >= lastFpsTime + 1.0) {
		double frames = (numVBlanks - lastFpsFrame);
		actualFps = (float)(actualFlips - lastActualFlips);

		fps = frames / (now - lastFpsTime);
		flips = (float)(g_Config.iDisplayRefreshRate * (double)(gpuStats.totals.numFlips - lastNumFlips) / frames);

		lastFpsFrame = numVBlanks;
		lastNumFlips = gpuStats.totals.numFlips;
		lastActualFlips = actualFlips;
		lastFpsTime = now;

		fpsHistory[fpsHistoryPos++] = fps;
		fpsHistoryPos = fpsHistoryPos % fpsHistorySize;
		if (fpsHistoryValid < fpsHistorySize) {
			++fpsHistoryValid;
		}
	}

	if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::FRAME_GRAPH || g_coreCollectDebugStats) {
		frameJitHistory[frameTimeHistoryPos] = (float)jitSeconds;
		frameTimeHistory[frameTimeHistoryPos++] = (float)(now - lastFrameTimeHistory);
		lastFrameTimeHistory = now;
		frameTimeHistoryPos = frameTimeHistoryPos % frameTimeHistorySize;
		if (frameTimeHistoryValid < frameTimeHistorySize) {
			++frameTimeHistoryValid;
		}
		frameSleepHistory[frameTimeHistoryPos] = 0.0f;
	}
}

// TODO: Also average actualFps
void __DisplayGetFPS(float *out_vps, float *out_fps, float *out_actual_fps) {
	if (out_vps) {
		*out_vps = (float)fps;
	}
	if (out_fps) {
		*out_fps = flips;
	}
	if (out_actual_fps) {
		*out_actual_fps = actualFps;
	}
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
	const int ticksIntoFrame = (int)(CoreTiming::GetTicks(currentMIPS) - frameStartTicks);
	// A frame is 1001/60 ms, and lines 0-285 fill it. Line 0 starts at the vblank, and is rarely
	// seen outside a vblank handler: the interrupt takes most of it (pspautotests display/vblankphase).
	const s64 ticksPerFrame = (s64)CoreTiming::GetClockFrequencyHz() * 1001 / 60000;
	return (u32)((s64)ticksIntoFrame * hCountPerVblank / ticksPerFrame);
}

uint32_t __DisplayGetAccumulatedHcount() {
	// The hCount is always a positive int, and wraps from 0x7FFFFFFF -> 0.
	int value = hCountBase + __DisplayGetCurrentHcount();
	return value & 0x7FFFFFFF;
}

void DisplayAdjustAccumulatedHcount(uint32_t diff) {
	hCountBase += diff;
}

float *__DisplayGetFrameTimes(int *out_valid, int *out_pos, float **out_sleep) {
	*out_valid = frameTimeHistoryValid;
	*out_pos = frameTimeHistoryPos;
	*out_sleep = frameSleepHistory;
	return frameTimeHistory;
}

const float *__DisplayGetFrameJitTimes() {
	return frameJitHistory;
}

int DisplayGetSleepPos() {
	return frameTimeHistoryPos;
}

void DisplayNotifySleep(double t, int pos) {
	if (pos < 0)
		pos = frameTimeHistoryPos;
	frameSleepHistory[pos] += t;
}

void __DisplayGetDebugStats(StringWriter &w) {
	if (!gpu) {
		w.C("No GPU stats available").endl();
		return;
	}
	gpu->GetStats(w);
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
	frameStartTicks = CoreTiming::GetTicks(currentMIPS);
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

	__IoVblank();
	__CtrlVblank();
}

void DisplayFireFlip() {
	// This is also the right time to calculate FPS.
	CalculateFPS();

	// Held across the calls, so that once __DisplayForgetFlip() returns (on a debugger connection's
	// thread, say) its listener isn't running and won't be called again. A listener must not add or
	// remove listeners.
	std::lock_guard<std::mutex> guard(listenersLock);
	for (const FlipListener &cb : flipListeners) {
		cb.first(cb.second);
	}
}

void DisplayFireActualFlip() {
	actualFlips++;
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

// This is called on game bootup.
void DisplayHWInit() {
	{
		std::lock_guard<std::mutex> guard(listenersLock);
		flipListeners.clear();
	}
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
	lastFpsTime = 0.0;
	lastFpsFrame = 0;

	frameTimeHistoryValid = 0;
	frameTimeHistoryPos = 0;
	lastFrameTimeHistory = 0.0;
	lastFlipJitTimes = MIPSComp::g_jitCompileTimes;
	lastFlipTime = 0.0;
}

void DisplayHWShutdown() {
	std::lock_guard<std::mutex> guard(listenersLock);
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
