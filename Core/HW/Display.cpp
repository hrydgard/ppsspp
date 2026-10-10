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
#include <retro_atomic.h>
#include <vector>
#include "Common/CommonTypes.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Data/Text/StringWriter.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceIo.h"
#include "Core/HW/Display.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

// Called when vblank happens (like an internal interrupt.)  Not part of state, should be static.
// Emulation thread only.
typedef std::pair<FlipCallback, void *> FlipListener;
static std::vector<FlipListener> flipListeners;

static uint64_t frameStartTicks;
// Only the emulation thread writes this; debugger readers use the counter.
static retro_atomic_int_t numVBlanks{ 0 };
// hCount is computed now.
static int vCount;
// The "AccumulatedHcount" can be adjusted, this is the base.
static uint32_t hCountBase;
static int isVblank;
static constexpr int hCountPerVblank = 286;

static int actualFlips = 0;

int __DisplayGetFlipCount() {
	return actualFlips;
}

int __DisplayGetNumVblanks() {
	return retro_atomic_load_relaxed_int(&numVBlanks);
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

void __DisplayGetDebugStats(StringWriter &w) {
	if (!gpu) {
		w.C("No GPU stats available").endl();
		return;
	}
	gpu->GetStats(w);
}

void DisplayFireVblankStart() {
	frameStartTicks = CoreTiming::GetTicks(currentMIPS);
	retro_atomic_fetch_add_int(&numVBlanks, 1);

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
	// A copy, since a listener may unregister itself.
	std::vector<FlipListener> toCall = flipListeners;

	for (FlipListener cb : toCall) {
		cb.first(cb.second);
	}
}

void DisplayFireActualFlip() {
	actualFlips++;
}

void __DisplayListenFlip(FlipCallback callback, void *userdata) {
	flipListeners.emplace_back(callback, userdata);
}

void __DisplayForgetFlip(FlipCallback callback, void *userdata) {
	flipListeners.erase(std::remove_if(flipListeners.begin(), flipListeners.end(), [&](FlipListener item) {
		return item.first == callback && item.second == userdata;
	}), flipListeners.end());
}

// This is called on game bootup.
void DisplayHWInit() {
	flipListeners.clear();
	frameStartTicks = 0;
	retro_atomic_store_relaxed_int(&numVBlanks, 0);
	isVblank = 0;
	vCount = 0;
	hCountBase = 0;

	actualFlips = 0;
}

void DisplayHWShutdown() {
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
