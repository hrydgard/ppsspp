// Copyright (c) 2017- PPSSPP Project.

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

#include "ppsspp_config.h"

#include "GPU/Math3D.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/RasterizerTypes.h"

namespace Sampler {

// Our std::unordered_map argument will ignore the alignment attribute, but that doesn't matter.
// We'll still have and want it for the actual function call, to keep the args in vector registers.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

typedef Rasterizer::Vec4IntResult(SOFTRAST_CALL *FetchFunc)(int u, int v, const u8 *tptr, int bufw, int level, const SamplerID &samplerID);
FetchFunc GetFetchFunc(const SamplerID &id);

typedef Rasterizer::Vec4IntResult (SOFTRAST_CALL *NearestFunc)(float s, float t, Rasterizer::Vec4IntArg prim_color, const u8 *const *tptr, const uint16_t *bufw, int level, int levelFrac, const SamplerID &samplerID);
NearestFunc GetNearestFunc(const SamplerID &id);

typedef Rasterizer::Vec4IntResult (SOFTRAST_CALL *LinearFunc)(float s, float t, Rasterizer::Vec4IntArg prim_color, const u8 *const *tptr, const uint16_t *bufw, int level, int levelFrac, const SamplerID &samplerID);
LinearFunc GetLinearFunc(const SamplerID &id);

// Bilinear samples for the pixels of a quad with bit i of active set, each at its own level, through the
// texture function: colors goes in as the primitive's colors and comes out textured. A channel at a time,
// as for SpanFunc. level and levelFrac nullptr: all at level 0.
typedef void (SOFTRAST_CALL *LinearQuadFunc)(const float *s, const float *t, const int *level, const int *levelFrac, int active, const u8 *const *texptr, const uint16_t *texbufw, int *colors, int colorStride, const SamplerID &samplerID);
// Only when linear is what GetLinearFunc returned (not with forced nearest filtering), nullptr otherwise.
LinearQuadFunc GetLinearQuadFunc(const SamplerID &id, LinearFunc linear);

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

};
