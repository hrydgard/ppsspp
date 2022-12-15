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

#include <functional>
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/TransformUnit.h" // for DrawingCoords

#ifdef _DEBUG
#define SOFTGPU_MEMORY_TAGGING_BASIC
#endif
// #define SOFTGPU_MEMORY_TAGGING_DETAILED

struct GPUDebugBuffer;
struct BinCoords;
class BinManager;

namespace Rasterizer {

enum class RasterizerStateFlags {
	NONE = 0,
	VERTEX_NON_FULL_WHITE = 0x0001,
	VERTEX_ALPHA_NON_ZERO = 0x0002,
	VERTEX_ALPHA_NON_FULL = 0x0004,
	VERTEX_HAS_FOG = 0x0008,

	CLUT_ALPHA_CHECKED = 0x0010,
	CLUT_ALPHA_NON_FULL = 0x0020,
	CLUT_ALPHA_NON_ZERO = 0x0040,

	VERTEX_FLAT_RESET = VERTEX_NON_FULL_WHITE | VERTEX_ALPHA_NON_FULL | VERTEX_ALPHA_NON_ZERO | VERTEX_HAS_FOG,

	OPTIMIZED = 0x0001'0000,
	OPTIMIZED_BLEND_SRC = 0x0002'0000,
	OPTIMIZED_BLEND_DST = 0x0004'0000,
	OPTIMIZED_BLEND_OFF = 0x0008'0000,
	OPTIMIZED_TEXREPLACE = 0x0010'0000,
	OPTIMIZED_FOG_OFF = 0x0020'0000,
	OPTIMIZED_ALPHATEST_OFF_NE = 0x0040'0000,
	OPTIMIZED_ALPHATEST_OFF_GT = 0x0080'0000,
	OPTIMIZED_ALPHATEST_ON = 0x0100'0000,

	// Anything that changes the actual pixel or sampler func.
	OPTIMIZED_PIXELID = OPTIMIZED_BLEND_SRC | OPTIMIZED_BLEND_DST | OPTIMIZED_BLEND_OFF | OPTIMIZED_FOG_OFF | OPTIMIZED_ALPHATEST_OFF_NE | OPTIMIZED_ALPHATEST_OFF_GT | OPTIMIZED_ALPHATEST_ON,
	OPTIMIZED_SAMPLERID = OPTIMIZED_TEXREPLACE,

	INVALID = 0x7FFFFFFF,
};
ENUM_CLASS_BITOPS(RasterizerStateFlags);

struct RasterizerState {
	PixelFuncID pixelID;
	SamplerID samplerID;
	SingleFunc drawPixel;
	Sampler::LinearFunc linear;
	Sampler::NearestFunc nearest;
	uint32_t texaddr[8]{};
	uint16_t texbufw[8]{};
	const u8 *texptr[8]{};
	float textureLodSlope;
	RasterizerStateFlags flags = RasterizerStateFlags::NONE;
	RasterizerStateFlags lastFlags = RasterizerStateFlags::INVALID;

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
		bool antialiasLines : 1;
		bool textureProj : 1;
	};

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	uint32_t listPC;
#endif

	GETexLevelMode TexLevelMode() const {
		return GETexLevelMode(texLevelMode);
	}
};

void ComputeRasterizerState(RasterizerState *state, BinManager *binner);
void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0);
void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0, const VertexData &v1, bool forceFlat);
void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0, const VertexData &v1, const VertexData &v2);
bool OptimizeRasterState(RasterizerState *state);

// Draws a triangle if its vertices are specified in counter-clockwise order
void DrawTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2, const BinCoords &range, const RasterizerState &state);
void DrawRectangle(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state);
void DrawPoint(const VertexData &v0, const BinCoords &range, const RasterizerState &state);
void DrawLine(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state);
void ClearRectangle(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state);

bool GetCurrentTexture(GPUDebugBuffer &buffer, int level);

}  // namespace Rasterizer
