// Copyright (c) 2015- PPSSPP Project.

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


#pragma once

#include <cstring>

class GPUInterface;
class GPUDebugInterface;
class GraphicsContext;

enum SkipDrawReasonFlags {
	SKIPDRAW_SKIPFRAME = 1,
	SKIPDRAW_NON_DISPLAYED_FB = 2,   // Skip drawing to FBO:s that have not been displayed.
	SKIPDRAW_BAD_FB_TEXTURE = 4,
	SKIPDRAW_WINDOW_MINIMIZED = 8, // Don't draw when the host window is minimized.
};

// Global GPU-related utility functions. 
// Nothing directly Ge-related in here.

// PSP uses a curious 24-bit float - it's the top 24 bits of a regular IEEE754 32-bit float.
// This is used for light positions, transform matrices, you name it.
inline float getFloat24(unsigned int data) {
	data <<= 8;
	float f;
	memcpy(&f, &data, 4);
	return f;
}

// in case we ever want to generate PSP display lists...
inline unsigned int toFloat24(float f) {
	unsigned int i;
	memcpy(&i, &f, 4);
	return i >> 8;
}

struct GPUStatistics {
	void Reset() {
		// Never add a vtable :)
		memset(this, 0, sizeof(*this));
	}
	void ResetFrame() {
		numDrawCalls = 0;
		numCachedDrawCalls = 0;
		numVertsSubmitted = 0;
		numCachedVertsDrawn = 0;
		numUncachedVertsDrawn = 0;
		numTrackedVertexArrays = 0;
		numTextureInvalidations = 0;
		numTextureSwitches = 0;
		numShaderSwitches = 0;
		numFlushes = 0;
		numTexturesDecoded = 0;
		msProcessingDisplayLists = 0;
		vertexGPUCycles = 0;
		otherGPUCycles = 0;
		memset(gpuCommandsAtCallLevel, 0, sizeof(gpuCommandsAtCallLevel));
	}

	// Per frame statistics
	int numDrawCalls;
	int numCachedDrawCalls;
	int numFlushes;
	int numVertsSubmitted;
	int numCachedVertsDrawn;
	int numUncachedVertsDrawn;
	int numTrackedVertexArrays;
	int numTextureInvalidations;
	int numTextureSwitches;
	int numShaderSwitches;
	int numTexturesDecoded;
	double msProcessingDisplayLists;
	int vertexGPUCycles;
	int otherGPUCycles;
	int gpuCommandsAtCallLevel[4];

	// Flip count. Doesn't really belong here.
	int numFlips;
};

extern GPUStatistics gpuStats;
extern GPUInterface *gpu;
extern GPUDebugInterface *gpuDebug;

class Thin3DContext;

bool GPU_Init(GraphicsContext *ctx, Thin3DContext *thin3d);
void GPU_Shutdown();
