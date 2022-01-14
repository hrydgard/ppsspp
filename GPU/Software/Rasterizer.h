// Copyright (c) 2013- PPSSPP Project.

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

#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/RasterizerRegCache.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/TransformUnit.h" // for DrawingCoords

#ifdef _DEBUG
#define SOFTGPU_MEMORY_TAGGING_BASIC
#endif
// #define SOFTGPU_MEMORY_TAGGING_DETAILED

struct GPUDebugBuffer;

namespace Rasterizer {

struct RasterizerState {
	PixelFuncID pixelID;
	SamplerID samplerID;
	SingleFunc drawPixel;
	Sampler::LinearFunc linear;
	Sampler::NearestFunc nearest;
	int texbufw[8]{};
	u8 *texptr[8]{};

	struct {
		uint8_t maxTexLevel : 3;
		bool enableTextures : 1;
		uint8_t texLevelMode : 2;
		bool shadeGouraud : 1;
		bool throughMode : 1;
		int8_t texLevelOffset : 8;
		bool mipFilt : 1;
		bool minFilt : 1;
		bool magFilt : 1;
	};

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	uint32_t listPC;
#endif

	GETexLevelMode TexLevelMode() const {
		return GETexLevelMode(texLevelMode);
	}
};

void ComputeRasterizerState(RasterizerState *state);

// Draws a triangle if its vertices are specified in counter-clockwise order
void DrawTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2, const RasterizerState &state);
void DrawPoint(const VertexData &v0, const RasterizerState &state);
void DrawLine(const VertexData &v0, const VertexData &v1, const RasterizerState &state);
void ClearRectangle(const VertexData &v0, const VertexData &v1, const RasterizerState &state);

bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);
bool GetCurrentTexture(GPUDebugBuffer &buffer, int level);

// Shared functions with RasterizerRectangle.cpp
Vec3<int> AlphaBlendingResult(const PixelFuncID &pixelID, const Vec4<int> &source, const Vec4<int> &dst);
Vec4IntResult SOFTRAST_CALL GetTextureFunctionOutput(Vec4IntArg prim_color, Vec4IntArg texcolor);

}  // namespace Rasterizer
