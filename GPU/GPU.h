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
#include <cstdint>

class GPUInterface;
class GPUDebugInterface;
class GraphicsContext;

// PSP rasterization has two outputs, color and depth. Stencil is packed
// into the alpha channel of color (if exists), so possibly RASTER_COLOR
// should be named RASTER_COLOR_STENCIL but it gets kinda hard to read.
enum RasterChannel : uint8_t {
	RASTER_COLOR = 0,
	RASTER_DEPTH = 1,
};

enum SkipDrawReasonFlags {
	SKIPDRAW_SKIPFRAME = 1,
	SKIPDRAW_NON_DISPLAYED_FB = 2,   // Skip drawing to FBO:s that have not been displayed.
	SKIPDRAW_BAD_FB_TEXTURE = 4,
	SKIPDRAW_WINDOW_MINIMIZED = 8, // Don't draw when the host window is minimized.
};

enum class ShaderDepalMode {
	OFF = 0,
	NORMAL = 1,
	SMOOTHED = 2,
	CLUT8_8888 = 3,  // Read 8888 framebuffer as 8-bit CLUT.
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

// The ToString function lives in GPUCommonHW.cpp.
struct GPUStatistics {
	void Reset() {
		ResetFrame();
		numFlips = 0;
	}

	void ResetFrame() {
		numDrawCalls = 0;
		numVertexDecodes = 0;
		numCulledDraws = 0;
		numDrawSyncs = 0;
		numListSyncs = 0;
		numVertsSubmitted = 0;
		numVertsDecoded = 0;
		numUncachedVertsDrawn = 0;
		numTextureInvalidations = 0;
		numTextureInvalidationsByFramebuffer = 0;
		numTexturesHashed = 0;
		numTextureDataBytesHashed = 0;
		numFlushes = 0;
		numBBOXJumps = 0;
		numPlaneUpdates = 0;
		numTexturesDecoded = 0;
		numFramebufferEvaluations = 0;
		numBlockingReadbacks = 0;
		numReadbacks = 0;
		numUploads = 0;
		numCachedUploads = 0;
		numDepal = 0;
		numClears = 0;
		numDepthCopies = 0;
		numReinterpretCopies = 0;
		numColorCopies = 0;
		numCopiesForShaderBlend = 0;
		numCopiesForSelfTex = 0;
		numBlockTransfers = 0;
		numReplacerTrackedTex = 0;
		numCachedReplacedTextures = 0;
		msProcessingDisplayLists = 0;
		vertexGPUCycles = 0;
		otherGPUCycles = 0;
	}

	// Per frame statistics
	int numDrawCalls;
	int numVertexDecodes;
	int numCulledDraws;
	int numDrawSyncs;
	int numListSyncs;
	int numFlushes;
	int numBBOXJumps;
	int numPlaneUpdates;
	int numVertsSubmitted;
	int numVertsDecoded;
	int numUncachedVertsDrawn;
	int numTextureInvalidations;
	int numTextureInvalidationsByFramebuffer;
	int numTexturesHashed;
	int numTextureDataBytesHashed;
	int numTexturesDecoded;
	int numFramebufferEvaluations;
	int numBlockingReadbacks;
	int numReadbacks;
	int numUploads;
	int numCachedUploads;
	int numDepal;
	int numClears;
	int numDepthCopies;
	int numReinterpretCopies;
	int numColorCopies;
	int numCopiesForShaderBlend;
	int numCopiesForSelfTex;
	int numBlockTransfers;
	int numReplacerTrackedTex;
	int numCachedReplacedTextures;
	double msProcessingDisplayLists;
	int vertexGPUCycles;
	int otherGPUCycles;

	// Flip count. Doesn't really belong here.
	int numFlips;
};

extern GPUStatistics gpuStats;
extern GPUInterface *gpu;
extern GPUDebugInterface *gpuDebug;

namespace Draw {
	class DrawContext;
}

bool GPU_Init(GraphicsContext *ctx, Draw::DrawContext *draw);
bool GPU_IsReady();
bool GPU_IsStarted();
void GPU_Shutdown();

const char *RasterChannelToString(RasterChannel channel);
