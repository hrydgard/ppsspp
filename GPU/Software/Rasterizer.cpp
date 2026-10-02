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

#include "ppsspp_config.h"
#include <algorithm>
#include <climits>
#include <cmath>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/Util/PPGeDraw.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/GEMath.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"

#include "Common/Math/SIMDHeaders.h"

// For the SSE4 stuff
#if PPSSPP_ARCH(SSE2)
#include <smmintrin.h>
#endif

namespace Rasterizer {

// Only OK on x64 where our stack is aligned
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
static inline __m128 InterpolateF(const __m128 &c0, const __m128 &c1, const __m128 &c2, int w0, int w1, int w2, float wsum) {
	__m128 v = _mm_mul_ps(c0, _mm_cvtepi32_ps(_mm_set1_epi32(w0)));
	v = _mm_add_ps(v, _mm_mul_ps(c1, _mm_cvtepi32_ps(_mm_set1_epi32(w1))));
	v = _mm_add_ps(v, _mm_mul_ps(c2, _mm_cvtepi32_ps(_mm_set1_epi32(w2))));
	return _mm_mul_ps(v, _mm_set_ps1(wsum));
}

static inline __m128i InterpolateI(const __m128i &c0, const __m128i &c1, const __m128i &c2, int w0, int w1, int w2, float wsum) {
	return _mm_cvtps_epi32(InterpolateF(_mm_cvtepi32_ps(c0), _mm_cvtepi32_ps(c1), _mm_cvtepi32_ps(c2), w0, w1, w2, wsum));
}
#elif PPSSPP_ARCH(ARM64_NEON)
static inline float32x4_t InterpolateF(const float32x4_t &c0, const float32x4_t &c1, const float32x4_t &c2, int w0, int w1, int w2, float wsum) {
	float32x4_t v = vmulq_f32(c0, vcvtq_f32_s32(vdupq_n_s32(w0)));
	v = vaddq_f32(v, vmulq_f32(c1, vcvtq_f32_s32(vdupq_n_s32(w1))));
	v = vaddq_f32(v, vmulq_f32(c2, vcvtq_f32_s32(vdupq_n_s32(w2))));
	return vmulq_f32(v, vdupq_n_f32(wsum));
}

static inline int32x4_t InterpolateI(const int32x4_t &c0, const int32x4_t &c1, const int32x4_t &c2, int w0, int w1, int w2, float wsum) {
	return vcvtq_s32_f32(InterpolateF(vcvtq_f32_s32(c0), vcvtq_f32_s32(c1), vcvtq_f32_s32(c2), w0, w1, w2, wsum));
}
#endif

// NOTE: When not casting color0 and color1 to float vectors, this code suffers from severe overflow issues.
// Not sure if that should be regarded as a bug or if casting to float is a valid fix.

static inline Vec4<int> Interpolate(const Vec4<int> &c0, const Vec4<int> &c1, const Vec4<int> &c2, int w0, int w1, int w2, float wsum) {
#if (defined(_M_SSE) || PPSSPP_ARCH(ARM64_NEON)) && !PPSSPP_ARCH(X86)
	return Vec4<int>(InterpolateI(c0.ivec, c1.ivec, c2.ivec, w0, w1, w2, wsum));
#else
	return ((c0.Cast<float>() * w0 + c1.Cast<float>() * w1 + c2.Cast<float>() * w2) * wsum).Cast<int>();
#endif
}

static inline Vec3<int> Interpolate(const Vec3<int> &c0, const Vec3<int> &c1, const Vec3<int> &c2, int w0, int w1, int w2, float wsum) {
#if (defined(_M_SSE) || PPSSPP_ARCH(ARM64_NEON)) && !PPSSPP_ARCH(X86)
	return Vec3<int>(InterpolateI(c0.ivec, c1.ivec, c2.ivec, w0, w1, w2, wsum));
#else
	return ((c0.Cast<float>() * w0 + c1.Cast<float>() * w1 + c2.Cast<float>() * w2) * wsum).Cast<int>();
#endif
}

static inline Vec4<float> Interpolate(const float &c0, const float &c1, const float &c2, const Vec4<float> &w0, const Vec4<float> &w1, const Vec4<float> &w2, const Vec4<float> &wsum_recip) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128 v = _mm_mul_ps(w0.vec, _mm_set1_ps(c0));
	v = _mm_add_ps(v, _mm_mul_ps(w1.vec, _mm_set1_ps(c1)));
	v = _mm_add_ps(v, _mm_mul_ps(w2.vec, _mm_set1_ps(c2)));
	return _mm_mul_ps(v, wsum_recip.vec);
#elif PPSSPP_ARCH(ARM64_NEON)
	float32x4_t v = vmulq_f32(w0.vec, vdupq_n_f32(c0));
	v = vaddq_f32(v, vmulq_f32(w1.vec, vdupq_n_f32(c1)));
	v = vaddq_f32(v, vmulq_f32(w2.vec, vdupq_n_f32(c2)));
	return vmulq_f32(v, wsum_recip.vec);
#else
	return (w0 * c0 + w1 * c1 + w2 * c2) * wsum_recip;
#endif
}

static inline Vec4<float> Interpolate(const float &c0, const float &c1, const float &c2, const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2, const Vec4<float> &wsum_recip) {
	return Interpolate(c0, c1, c2, w0.Cast<float>(), w1.Cast<float>(), w2.Cast<float>(), wsum_recip);
}

void ComputeRasterizerState(RasterizerState *state, BinManager *binner) {
	ComputePixelFuncID(&state->pixelID);
	state->drawPixel = Rasterizer::GetSingleFunc(state->pixelID, binner);

	state->enableTextures = gstate.isTextureMapEnabled() && !state->pixelID.clearMode;
	if (state->enableTextures) {
		ComputeSamplerID(&state->samplerID);
		state->linear = Sampler::GetLinearFunc(state->samplerID, binner);
		state->nearest = Sampler::GetNearestFunc(state->samplerID, binner);

		// Since the definitions are the same, just force this setting using the func pointer.
		if (g_Config.iTexFiltering == TEX_FILTER_FORCE_LINEAR) {
			state->nearest = state->linear;
		} else if (g_Config.iTexFiltering == TEX_FILTER_FORCE_NEAREST) {
			state->linear = state->nearest;
		}

		state->maxTexLevel = state->samplerID.hasAnyMips ? gstate.getTextureMaxLevel() : 0;

		GETextureFormat texfmt = state->samplerID.TexFmt();
		for (uint8_t i = 0; i <= state->maxTexLevel; i++) {
			u32 texaddr = gstate.getTextureAddress(i);
			state->texaddr[i] = texaddr;
			state->texbufw[i] = (uint16_t)GetTextureBufw(i, texaddr, texfmt);
			if (Memory::IsValidTextureAddress(texaddr)) {
				u32 offset;
				if (IsPPGEAtlasFakeAddress(texaddr, &offset)) {
					state->texptr[i] = PPGeAtlasGetData() + offset;
				} else {
					state->texptr[i] = Memory::GetPointerUnchecked(texaddr);
				}
			} else {
				state->texptr[i] = nullptr;
			}
		}

		state->textureLodSlope = gstate.getTextureLodSlope();
		state->texLevelMode = gstate.getTexLevelMode();
		state->texLevelOffset = (int8_t)gstate.getTexLevelOffset16();
		state->mipFilt = gstate.isMipmapFilteringEnabled();
		state->minFilt = gstate.isMinifyFilteringEnabled();
		state->magFilt = gstate.isMagnifyFilteringEnabled();
		state->textureProj = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
		if (state->textureProj) {
			// We may be able to optimize this off.  This is actually kinda common.
			const bool qZeroST = gstate.tgenMatrix[2] == 0.0f && gstate.tgenMatrix[5] == 0.0f;
			const bool qZeroQ = gstate.tgenMatrix[8] == 0.0f;

			// Two common cases: the source q factor is zero, OR source is UV.
			const bool qFactorZero = gstate.getUVProjMode() == GE_PROJMAP_UV;
			if (qZeroST && (qZeroQ || qFactorZero) && gstate.tgenMatrix[11] == 1.0f) {
				state->textureProj = false;
			}
		}
	}

	state->shadeGouraud = !gstate.isModeClear() && gstate.getShadeMode() == GE_SHADE_GOURAUD;
	state->throughMode = gstate.isModeThrough();
	state->antialiasLines = gstate.isAntiAliasEnabled();

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	DisplayList currentList{};
	if (gpu)
		gpu->GetCurrentDisplayList(currentList);
	state->listPC = currentList.pc;
#endif
}

static inline void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0, bool useColor) {
	if (useColor) {
		if ((v0.color0 & 0x00FFFFFF) != 0x00FFFFFF)
			state->flags |= RasterizerStateFlags::VERTEX_NON_FULL_WHITE;
		uint8_t alpha = v0.color0 >> 24;
		if (alpha != 0)
			state->flags |= RasterizerStateFlags::VERTEX_ALPHA_NON_ZERO;
		if (alpha != 0xFF)
			state->flags |= RasterizerStateFlags::VERTEX_ALPHA_NON_FULL;
	}
	if (!(v0.fogdepth >= 255.0f / 256.0f))
		state->flags |= RasterizerStateFlags::VERTEX_HAS_FOG;
}

void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0) {
	CalculateRasterStateFlags(state, v0, true);
}

void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0, const VertexData &v1, bool forceFlat) {
	CalculateRasterStateFlags(state, v0, !forceFlat && state->shadeGouraud);
	CalculateRasterStateFlags(state, v1, true);
	// Antialiased lines replace the alpha with their coverage (DrawLine), anywhere from 0 to 128.
	if (state->antialiasLines && !forceFlat)
		state->flags |= RasterizerStateFlags::VERTEX_ALPHA_NON_FULL;
}

void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0, const VertexData &v1, const VertexData &v2) {
	CalculateRasterStateFlags(state, v0, state->shadeGouraud);
	CalculateRasterStateFlags(state, v1, state->shadeGouraud);
	CalculateRasterStateFlags(state, v2, true);
}

static inline int OptimizePixelIDFlags(const RasterizerStateFlags &flags) {
	return (int)flags & (int)RasterizerStateFlags::OPTIMIZED_PIXELID;
}

static inline int OptimizeSamplerIDFlags(const RasterizerStateFlags &flags) {
	return (int)flags & (int)RasterizerStateFlags::OPTIMIZED_SAMPLERID;
}

static inline int OptimizeAllFlags(const RasterizerStateFlags &flags) {
	return OptimizePixelIDFlags(flags) | OptimizeSamplerIDFlags(flags);
}

static inline RasterizerStateFlags ClearFlags(const RasterizerStateFlags &flags, const RasterizerStateFlags &mask) {
	int clearBits = (int)flags & (int)mask;
	return (RasterizerStateFlags)((int)flags & ~clearBits);
}

static inline RasterizerStateFlags ReplacePixelIDFlags(const RasterizerStateFlags &flags, const RasterizerStateFlags &replace) {
	RasterizerStateFlags updated = ClearFlags(flags, RasterizerStateFlags::OPTIMIZED_PIXELID);
	return updated | (RasterizerStateFlags)OptimizePixelIDFlags(replace);
}

static inline RasterizerStateFlags ReplaceSamplerIDFlags(const RasterizerStateFlags &flags, const RasterizerStateFlags &replace) {
	RasterizerStateFlags updated = ClearFlags(flags, RasterizerStateFlags::OPTIMIZED_SAMPLERID);
	return updated | (RasterizerStateFlags)OptimizeSamplerIDFlags(replace);
}

static bool CheckClutAlphaFull(RasterizerState *state) {
	// We only need to check it once.
	if (state->flags & RasterizerStateFlags::CLUT_ALPHA_CHECKED)
		return !(state->flags & RasterizerStateFlags::CLUT_ALPHA_NON_FULL);
	// For now, let's keep things simple.
	const SamplerID &samplerID = state->samplerID;
	if (samplerID.hasClutOffset || !samplerID.useSharedClut)
		return false;

	uint32_t count = samplerID.TexFmt() == GE_TFMT_CLUT4 ? 16 : 256;
	if (samplerID.hasClutMask)
		count = std::min(count, ((samplerID.cached.clutFormat >> 8) & 0xFF) + 1);

	u32 alphaSum = 0xFFFFFFFF;
	if (samplerID.ClutFmt() == GE_CMODE_32BIT_ABGR8888) {
		CheckMask32((const uint32_t *)samplerID.cached.clut, count, &alphaSum);
	} else {
		CheckMask16((const uint16_t *)samplerID.cached.clut, count, &alphaSum);
	}

	bool onlyFull = true;
	// alphaSum ANDs every entry: alpha bits all entries share mean no entry has zero alpha.
	u32 alphaBits = 1;
	switch (samplerID.ClutFmt()) {
	case GE_CMODE_16BIT_BGR5650:
		break;

	case GE_CMODE_16BIT_ABGR5551:
		alphaBits = alphaSum & 0x8000;
		onlyFull = alphaBits != 0;
		break;

	case GE_CMODE_16BIT_ABGR4444:
		alphaBits = alphaSum & 0xF000;
		onlyFull = alphaBits == 0xF000;
		break;

	case GE_CMODE_32BIT_ABGR8888:
		alphaBits = alphaSum & 0xFF000000;
		onlyFull = alphaBits == 0xFF000000;
		break;
	}

	// Only the alpha bits count: a palette of white with alphas 0, 0x10, ... shares all its RGB bits but
	// still contains zero (Ace Combat's subtitle glyphs).
	if (alphaBits != 0)
		state->flags |= RasterizerStateFlags::CLUT_ALPHA_NON_ZERO;
	if (!onlyFull)
		state->flags |= RasterizerStateFlags::CLUT_ALPHA_NON_FULL;
	state->flags |= RasterizerStateFlags::CLUT_ALPHA_CHECKED;

	return onlyFull;
}

static RasterizerStateFlags DetectStateOptimizations(RasterizerState *state) {
	// Note: all optimizations must be undoable.
	RasterizerStateFlags optimize = RasterizerStateFlags::NONE;
	auto &pixelID = state->pixelID;
	auto &samplerID = state->samplerID;

	bool alphaZero = !(state->flags & RasterizerStateFlags::VERTEX_ALPHA_NON_ZERO);
	bool alphaFull = !(state->flags & RasterizerStateFlags::VERTEX_ALPHA_NON_FULL);
	bool needTextureAlpha = state->enableTextures && samplerID.useTextureAlpha;

	if (!pixelID.clearMode) {
		auto &cached = pixelID.cached;

		bool alphaBlend = pixelID.alphaBlend || (state->flags & RasterizerStateFlags::OPTIMIZED_BLEND_OFF);
		if (needTextureAlpha && alphaBlend && alphaFull) {
			bool usesClut = (samplerID.texfmt & 4) != 0;
			if (usesClut && CheckClutAlphaFull(state))
				needTextureAlpha = false;
		}

		if (alphaBlend && !needTextureAlpha) {
			PixelBlendFactor src = pixelID.AlphaBlendSrc();
			PixelBlendFactor dst = pixelID.AlphaBlendDst();
			if (state->flags & RasterizerStateFlags::OPTIMIZED_BLEND_SRC)
				src = PixelBlendFactor::SRCALPHA;
			if (state->flags & RasterizerStateFlags::OPTIMIZED_BLEND_DST)
				dst = PixelBlendFactor::INVSRCALPHA;

			// Okay, we may be able to convert this to a fixed value.
			if (alphaZero || alphaFull) {
				// If it was already set and we still can, set it again.
				if (src == PixelBlendFactor::SRCALPHA)
					optimize |= RasterizerStateFlags::OPTIMIZED_BLEND_SRC;
				if (dst == PixelBlendFactor::INVSRCALPHA)
					optimize |= RasterizerStateFlags::OPTIMIZED_BLEND_DST;
			}
			if (alphaFull && (src == PixelBlendFactor::SRCALPHA || src == PixelBlendFactor::ONE) && (dst == PixelBlendFactor::INVSRCALPHA || dst == PixelBlendFactor::ZERO)) {
				optimize |= RasterizerStateFlags::OPTIMIZED_BLEND_OFF;
			}
		}

		if (alphaBlend && (needTextureAlpha || !alphaFull)) {
			// Okay, we're blending, and we need to.  Are we alpha testing?
			GEComparison alphaTestFunc = pixelID.AlphaTestFunc();
			if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_NE)
				alphaTestFunc = GE_COMP_NOTEQUAL;
			if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_GT)
				alphaTestFunc = GE_COMP_GREATER;
			if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_ON)
				alphaTestFunc = GE_COMP_ALWAYS;

			PixelBlendFactor src = pixelID.AlphaBlendSrc();
			PixelBlendFactor dst = pixelID.AlphaBlendDst();
			if (state->flags & RasterizerStateFlags::OPTIMIZED_BLEND_SRC)
				src = PixelBlendFactor::SRCALPHA;
			if (state->flags & RasterizerStateFlags::OPTIMIZED_BLEND_DST)
				dst = PixelBlendFactor::INVSRCALPHA;

			if (alphaTestFunc == GE_COMP_ALWAYS && src == PixelBlendFactor::SRCALPHA && dst == PixelBlendFactor::INVSRCALPHA) {
				bool usesClut = (samplerID.texfmt & 4) != 0;
				bool couldHaveZeroTexAlpha = true;
				if (usesClut && CheckClutAlphaFull(state))
					couldHaveZeroTexAlpha = false;
				if (state->flags & RasterizerStateFlags::CLUT_ALPHA_NON_ZERO)
					couldHaveZeroTexAlpha = false;

				// Blending is expensive, since we read the target.  Force alpha testing on.
				// Not with dithering: the GE still adds the dither to a zero alpha pixel (Test Drive).
				if (!pixelID.depthWrite && !pixelID.stencilTest && !pixelID.dithering && couldHaveZeroTexAlpha)
					optimize |= RasterizerStateFlags::OPTIMIZED_ALPHATEST_ON;
			}
		}

		bool applyFog = pixelID.applyFog || (state->flags & RasterizerStateFlags::OPTIMIZED_FOG_OFF);
		if (applyFog) {
			bool hasFog = state->flags & RasterizerStateFlags::VERTEX_HAS_FOG;
			if (!hasFog)
				optimize |= RasterizerStateFlags::OPTIMIZED_FOG_OFF;
		}
	}

	if (state->enableTextures) {
		bool colorFull = !(state->flags & RasterizerStateFlags::VERTEX_NON_FULL_WHITE);
		if (colorFull && (!needTextureAlpha || alphaFull)) {
			// Modulate is common, sometimes even with a fixed color.  Replace is cheaper.
			GETexFunc texFunc = samplerID.TexFunc();
			if (state->flags & RasterizerStateFlags::OPTIMIZED_TEXREPLACE)
				texFunc = GE_TEXFUNC_MODULATE;

			if (texFunc == GE_TEXFUNC_MODULATE)
				optimize |= RasterizerStateFlags::OPTIMIZED_TEXREPLACE;
		}

		bool usesClut = (samplerID.texfmt & 4) != 0;
		if (usesClut && alphaFull && samplerID.useTextureAlpha) {
			GEComparison alphaTestFunc = pixelID.AlphaTestFunc();
			// We optimize > 0 to != 0, so this is especially common.
			if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_NE)
				alphaTestFunc = GE_COMP_NOTEQUAL;
			// > 16, 8, or similar are also very common.
			if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_GT)
				alphaTestFunc = GE_COMP_GREATER;
			if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_ON)
				alphaTestFunc = GE_COMP_ALWAYS;

			bool alphaTest = (alphaTestFunc == GE_COMP_NOTEQUAL || alphaTestFunc == GE_COMP_GREATER) && pixelID.alphaTestRef < 0xFF && !state->pixelID.hasAlphaTestMask;
			if (alphaTest) {
				bool canSkipAlphaTest = CheckClutAlphaFull(state);
				if ((state->flags & RasterizerStateFlags::CLUT_ALPHA_NON_ZERO) && pixelID.alphaTestRef == 0)
					canSkipAlphaTest = true;
				if (canSkipAlphaTest)
					optimize |= alphaTestFunc == GE_COMP_NOTEQUAL ? RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_NE : RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_GT;
			}
		}
	}

	return optimize;
}

static bool ApplyStateOptimizations(RasterizerState *state, const RasterizerStateFlags &optimize) {
	bool changed = false;

	// Check if we can compile the new funcs before replacing.
	if (OptimizePixelIDFlags(state->flags) != OptimizePixelIDFlags(optimize)) {
		bool canFull = !(state->flags & RasterizerStateFlags::VERTEX_ALPHA_NON_FULL);

		PixelFuncID pixelID = state->pixelID;
		if (optimize & RasterizerStateFlags::OPTIMIZED_BLEND_OFF)
			pixelID.alphaBlend = false;
		else if (state->flags & RasterizerStateFlags::OPTIMIZED_BLEND_OFF)
			pixelID.alphaBlend = true;
		if (optimize & RasterizerStateFlags::OPTIMIZED_BLEND_SRC)
			pixelID.alphaBlendSrc = (uint8_t)(canFull ? PixelBlendFactor::ONE : PixelBlendFactor::ZERO);
		else if (state->flags & RasterizerStateFlags::OPTIMIZED_BLEND_SRC)
			pixelID.alphaBlendSrc = (uint8_t)PixelBlendFactor::SRCALPHA;
		if (optimize & RasterizerStateFlags::OPTIMIZED_BLEND_DST)
			pixelID.alphaBlendDst = (uint8_t)(canFull ? PixelBlendFactor::ZERO : PixelBlendFactor::ONE);
		else if (state->flags & RasterizerStateFlags::OPTIMIZED_BLEND_DST)
			pixelID.alphaBlendDst = (uint8_t)PixelBlendFactor::INVSRCALPHA;
		if (optimize & RasterizerStateFlags::OPTIMIZED_FOG_OFF)
			pixelID.applyFog = false;
		else if (state->flags & RasterizerStateFlags::OPTIMIZED_FOG_OFF)
			pixelID.applyFog = true;
		if (optimize & (RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_NE | RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_GT))
			pixelID.alphaTestFunc = GE_COMP_ALWAYS;
		else if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_NE)
			pixelID.alphaTestFunc = GE_COMP_NOTEQUAL;
		else if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_OFF_GT)
			pixelID.alphaTestFunc = GE_COMP_GREATER;
		else if (optimize & RasterizerStateFlags::OPTIMIZED_ALPHATEST_ON) {
			pixelID.alphaTestFunc = GE_COMP_NOTEQUAL;
			pixelID.alphaTestRef = 0;
			pixelID.hasAlphaTestMask = false;
		} else if (state->flags & RasterizerStateFlags::OPTIMIZED_ALPHATEST_ON) {
			pixelID.alphaTestFunc = GE_COMP_ALWAYS;
		}

		SingleFunc drawPixel = Rasterizer::GetSingleFunc(pixelID, nullptr);
		// Can't compile during runtime.  This failing is a bit of a problem when undoing...
		if (drawPixel) {
			state->drawPixel = drawPixel;
			memcpy(&state->pixelID, &pixelID, sizeof(PixelFuncID));
			state->flags = ReplacePixelIDFlags(state->flags, optimize) | RasterizerStateFlags::OPTIMIZED;
			changed = true;
		}
	}

	if (OptimizeSamplerIDFlags(state->flags) != OptimizeSamplerIDFlags(optimize)) {
		SamplerID samplerID = state->samplerID;
		if (optimize & RasterizerStateFlags::OPTIMIZED_TEXREPLACE)
			samplerID.texFunc = (uint8_t)GE_TEXFUNC_REPLACE;
		else if (state->flags & RasterizerStateFlags::OPTIMIZED_TEXREPLACE)
			samplerID.texFunc = (uint8_t)GE_TEXFUNC_MODULATE;

		Sampler::LinearFunc linear = Sampler::GetLinearFunc(samplerID, nullptr);
		Sampler::LinearFunc nearest = Sampler::GetNearestFunc(samplerID, nullptr);
		// Can't compile during runtime.  This failing is a bit of a problem when undoing...
		if (linear && nearest) {
			// Since the definitions are the same, just force this setting using the func pointer.
			if (g_Config.iTexFiltering == TEX_FILTER_FORCE_LINEAR) {
				state->nearest = linear;
				state->linear = linear;
			} else if (g_Config.iTexFiltering == TEX_FILTER_FORCE_NEAREST) {
				state->nearest = nearest;
				state->linear = nearest;
			} else {
				state->nearest = nearest;
				state->linear = linear;
			}
			memcpy(&state->samplerID, &samplerID, sizeof(SamplerID));
			state->flags = ReplaceSamplerIDFlags(state->flags, optimize) | RasterizerStateFlags::OPTIMIZED;
			changed = true;
		}
	}

	state->lastFlags = state->flags;
	return changed;
}

bool OptimizeRasterState(RasterizerState *state) {
	if (state->flags == state->lastFlags)
		return false;

	RasterizerStateFlags optimize = DetectStateOptimizations(state);

	// If it was optimized before, just revert and don't churn.
	if ((state->flags & RasterizerStateFlags::OPTIMIZED) && OptimizeAllFlags(state->flags) != OptimizeAllFlags(optimize)) {
		optimize = RasterizerStateFlags::NONE;
	} else if (optimize == RasterizerStateFlags::NONE && !(state->flags & RasterizerStateFlags::OPTIMIZED)) {
		state->lastFlags = state->flags;
		return false;
	}

	return ApplyStateOptimizations(state, optimize);
}

RasterizerState OptimizeFlatRasterizerState(const RasterizerState &origState, const VertexData &v1) {
	uint8_t alpha = v1.color0 >> 24;
	RasterizerState state = origState;

	// Sometimes, a particular draw can do better than the overall state.
	state.flags = ClearFlags(state.flags, RasterizerStateFlags::VERTEX_FLAT_RESET);
	CalculateRasterStateFlags(&state, v1, true);

	RasterizerStateFlags optimize = DetectStateOptimizations(&state);
	if (OptimizeAllFlags(state.flags) != OptimizeAllFlags(optimize)) {
		ApplyStateOptimizations(&state, optimize);
		return state;
	}

	return origState;
}

static inline u8 ClampFogDepth(float fogdepth) {
	union FloatBits {
		float f;
		u32 u;
	};
	FloatBits f;
	f.f = fogdepth;

	u32 exp = f.u >> 23;
	if ((f.u & 0x80000000) != 0 || exp <= 126 - 8)
		return 0;
	if (exp > 126)
		return 255;

	u32 mantissa = (f.u & 0x007FFFFF) | 0x00800000;
	return mantissa >> (16 + 126 - exp);
}

static inline void GetTextureCoordinates(const VertexData& v0, const VertexData& v1, const float p, float &s, float &t) {
	// Note that for environment mapping, texture coordinates have been calculated during lighting
	float q0 = 1.f / v0.clipw;
	float q1 = 1.f / v1.clipw;
	float wq0 = p * q0;
	float wq1 = (1.0f - p) * q1;

	float q_recip = 1.0f / (wq0 + wq1);
	s = (v0.texturecoords.s() * wq0 + v1.texturecoords.s() * wq1) * q_recip;
	t = (v0.texturecoords.t() * wq0 + v1.texturecoords.t() * wq1) * q_recip;
}

static inline void GetTextureCoordinatesProj(const VertexData& v0, const VertexData& v1, const float p, float &s, float &t) {
	// This is for texture matrix projection.
	float q0 = 1.f / v0.clipw;
	float q1 = 1.f / v1.clipw;
	float wq0 = p * q0;
	float wq1 = (1.0f - p) * q1;

	float q_recip = 1.0f / (v0.texturecoords.q() * wq0 + v1.texturecoords.q() * wq1);

	s = (v0.texturecoords.s() * wq0 + v1.texturecoords.s() * wq1) * q_recip;
	t = (v0.texturecoords.t() * wq0 + v1.texturecoords.t() * wq1) * q_recip;
}

static inline void GetTextureCoordinates(const VertexData &v0, const VertexData &v1, const VertexData &v2, const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2, const Vec4<float> &wsum_recip, Vec4<float> &s, Vec4<float> &t) {
	// Note that for environment mapping, texture coordinates have been calculated during lighting.
	float q0 = 1.f / v0.clipw;
	float q1 = 1.f / v1.clipw;
	float q2 = 1.f / v2.clipw;
	Vec4<float> wq0 = w0.Cast<float>() * q0;
	Vec4<float> wq1 = w1.Cast<float>() * q1;
	Vec4<float> wq2 = w2.Cast<float>() * q2;

	Vec4<float> q_recip = (wq0 + wq1 + wq2).Reciprocal();
	s = Interpolate(v0.texturecoords.s(), v1.texturecoords.s(), v2.texturecoords.s(), wq0, wq1, wq2, q_recip);
	t = Interpolate(v0.texturecoords.t(), v1.texturecoords.t(), v2.texturecoords.t(), wq0, wq1, wq2, q_recip);
}

static inline void GetTextureCoordinatesProj(const VertexData &v0, const VertexData &v1, const VertexData &v2, const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2, const Vec4<float> &wsum_recip, Vec4<float> &s, Vec4<float> &t) {
	// This is for texture matrix projection.
	float q0 = 1.f / v0.clipw;
	float q1 = 1.f / v1.clipw;
	float q2 = 1.f / v2.clipw;
	Vec4<float> wq0 = w0.Cast<float>() * q0;
	Vec4<float> wq1 = w1.Cast<float>() * q1;
	Vec4<float> wq2 = w2.Cast<float>() * q2;

	// Here, Interpolate() is a bit suboptimal, since
	// there's no need to multiply by 1.0f.
	Vec4<float> q_recip = Interpolate(v0.texturecoords.q(), v1.texturecoords.q(), v2.texturecoords.q(), wq0, wq1, wq2, Vec4<float>::AssignToAll(1.0f)).Reciprocal();

	s = Interpolate(v0.texturecoords.s(), v1.texturecoords.s(), v2.texturecoords.s(), wq0, wq1, wq2, q_recip);
	t = Interpolate(v0.texturecoords.t(), v1.texturecoords.t(), v2.texturecoords.t(), wq0, wq1, wq2, q_recip);
}

static inline void SetPixelDepth(int x, int y, int stride, u16 value) {
	depthbuf.Set16(x, y, stride, value);
}

static inline bool IsRightSideOrFlatBottomLine(const Vec2<int>& vertex, const Vec2<int>& line1, const Vec2<int>& line2)
{
	if (line1.y == line2.y) {
		// just check if vertex is above us => bottom line parallel to x-axis
		return vertex.y < line1.y;
	} else {
		// check if vertex is on our left => right side
		// Exactly: a truncating divide put a vertex 0.2 subpixels left of the line on it, so a pixel center
		// on such a thin triangle's right edge counted as inside (Peace Walker ULUS10509).
		const int64_t dy = line2.y - line1.y;
		const int64_t lhs = (int64_t)(vertex.x - line1.x) * dy;
		const int64_t rhs = (int64_t)(line2.x - line1.x) * (vertex.y - line1.y);
		return dy > 0 ? lhs < rhs : lhs > rhs;
	}
}


// Color doubling applies to the specular (secondary) color too (gpu/probe exp86).
static inline bool DoubleSecondaryColor(const RasterizerState &state) {
	return state.enableTextures && state.samplerID.useColorDoubling;
}

static inline Vec4IntResult SOFTRAST_CALL ApplyTexturing(float s, float t, Vec4IntArg prim_color, int texlevel, int frac_texlevel, bool bilinear, const RasterizerState &state) {
	const u8 **tptr0 = const_cast<const u8 **>(&state.texptr[texlevel]);
	const uint16_t *bufw0 = &state.texbufw[texlevel];

	if (!bilinear) {
		return state.nearest(s, t, prim_color, tptr0, bufw0, texlevel, frac_texlevel, state.samplerID);
	}
	return state.linear(s, t, prim_color, tptr0, bufw0, texlevel, frac_texlevel, state.samplerID);
}

static inline Vec4IntResult SOFTRAST_CALL ApplyTexturingSingle(float s, float t, Vec4IntArg prim_color, int texlevel, int frac_texlevel, bool bilinear, const RasterizerState &state) {
	return ApplyTexturing(s, t, prim_color, texlevel, frac_texlevel, bilinear, state);
}


// q is 1 / w at the pixel, as the GE interpolates it (UVPlanes).
// autoGrad: the largest UV plane gradient in texels per pixel, when there are planes (or negative).
static inline void CalculateSamplingParams(const float ds, const float dt, float q, const RasterizerState &state, int &level, int &levelFrac, bool &filt, float autoGrad = -1.0f) {
	const int width = 1 << state.samplerID.width0Shift;
	const int height = 1 << state.samplerID.height0Shift;

	// With 8 bits of fraction (because texslope can be fairly precise.)
	int detail;
	switch (state.TexLevelMode()) {
	case GE_TEXLEVEL_MODE_AUTO:
		if (autoGrad >= 0.0f) {
			// The largest gradient of the s and t planes over the pixel's q, both through the float-bits
			// log2, like slope mode (gpu/probe exp92-93).
			detail = GELog16(autoGrad) - GELog16(q);
		} else {
			detail = GELog16(std::max(std::abs(ds * width), std::abs(dt * height)));
		}
		break;
	case GE_TEXLEVEL_MODE_SLOPE:
		// The GE takes the same float-bits log2 of q and of the slope, and adds a level (gpu/probe
		// exp58-60, bit exact).
		detail = 16 + GELog16(state.textureLodSlope) - GELog16(q);
		break;
	case GE_TEXLEVEL_MODE_CONST:
	default:
		// Unused value 3 operates the same as CONST.
		detail = 0;
		break;
	}

	// Add in the bias (used in all modes), with 4 bits of fraction.
	detail += state.texLevelOffset;

	if (detail > 0 && state.maxTexLevel > 0) {
		bool mipFilt = state.mipFilt;

		int level8 = std::min(detail, state.maxTexLevel * 16);
		if (!mipFilt) {
			// Round up at 1.5.
			level8 += 8;
		}
		level = level8 >> 4;
		levelFrac = mipFilt ? level8 & 0xF : 0;
	} else {
		level = 0;
		levelFrac = 0;
	}

	if (detail > 0)
		filt = state.minFilt;
	else
		filt = state.magFilt;
}

static inline void ApplyTexturing(const RasterizerState &state, Vec4<int> *prim_color, const Vec4<int> &mask, const Vec4<float> &s, const Vec4<float> &t, const Vec4<float> &q, float autoGrad = -1.0f) {
	// Auto LOD takes the largest of all four UV derivatives (gpu/probe exp92, exact for affine mappings).
	float ds = std::max(std::abs(s[1] - s[0]), std::abs(s[2] - s[0]));
	float dt = std::max(std::abs(t[1] - t[0]), std::abs(t[2] - t[0]));

	int level;
	int levelFrac;
	bool bilinear;
	const bool perPixel = state.TexLevelMode() == GE_TEXLEVEL_MODE_SLOPE || (state.TexLevelMode() == GE_TEXLEVEL_MODE_AUTO && autoGrad >= 0.0f);
	if (!perPixel)
		CalculateSamplingParams(ds, dt, 0.0f, state, level, levelFrac, bilinear);

	PROFILE_THIS_SCOPE("sampler");
	for (int i = 0; i < 4; ++i) {
		if (mask[i] >= 0) {
			if (perPixel)
				CalculateSamplingParams(ds, dt, q[i], state, level, levelFrac, bilinear, autoGrad);
			prim_color[i] = ApplyTexturing(s[i], t[i], ToVec4IntArg(prim_color[i]), level, levelFrac, bilinear, state);
		}
	}
}

static inline Vec4<int> SOFTRAST_CALL CheckDepthTestPassed4(const Vec4<int> &mask, GEComparison func, int x, int y, int stride, Vec4<int> z) {
	// Skip the depth buffer read if we're masked already.
#if defined(_M_SSE)
	__m128i result = SAFE_M128I(mask.ivec);
	int maskbits = _mm_movemask_epi8(result);
	if (maskbits >= 0xFFFF)
		return mask;
#else
	Vec4<int> result = mask;
	if (mask.x < 0 && mask.y < 0 && mask.z < 0 && mask.w < 0)
		return result;
#endif

	// Read in the existing depth values.
#if defined(_M_SSE)
	// Tried using flags from maskbits to skip dwords... seemed neutral.
	__m128i refz = _mm_cvtsi32_si128(*(u32 *)depthbuf.Get16Ptr(x, y, stride));
	refz = _mm_unpacklo_epi32(refz, _mm_cvtsi32_si128(*(u32 *)depthbuf.Get16Ptr(x, y + 1, stride)));
	refz = _mm_unpacklo_epi16(refz, _mm_setzero_si128());
#else
	Vec4<int> refz(depthbuf.Get16(x, y, stride), depthbuf.Get16(x + 1, y, stride), depthbuf.Get16(x, y + 1, stride), depthbuf.Get16(x + 1, y + 1, stride));
#endif

	switch (func) {
	case GE_COMP_NEVER:
#if defined(_M_SSE)
		result = _mm_set1_epi32(-1);
#else
		result = Vec4<int>::AssignToAll(-1);
#endif
		break;

	case GE_COMP_ALWAYS:
		break;

	case GE_COMP_EQUAL:
#if defined(_M_SSE)
		result = _mm_or_si128(result, _mm_xor_si128(_mm_cmpeq_epi32(z.ivec, refz), _mm_set1_epi32(-1)));
#else
		for (int i = 0; i < 4; ++i)
			result[i] |= z[i] != refz[i] ? -1 : 0;
#endif
		break;

	case GE_COMP_NOTEQUAL:
#if defined(_M_SSE)
		result = _mm_or_si128(result, _mm_cmpeq_epi32(z.ivec, refz));
#else
		for (int i = 0; i < 4; ++i)
			result[i] |= z[i] == refz[i] ? -1 : 0;
#endif
		break;

	case GE_COMP_LESS:
#if defined(_M_SSE)
		result = _mm_or_si128(result, _mm_cmpgt_epi32(z.ivec, refz));
		result = _mm_or_si128(result, _mm_cmpeq_epi32(z.ivec, refz));
#else
		for (int i = 0; i < 4; ++i)
			result[i] |= z[i] >= refz[i] ? -1 : 0;
#endif
		break;

	case GE_COMP_LEQUAL:
#if defined(_M_SSE)
		result = _mm_or_si128(result, _mm_cmpgt_epi32(z.ivec, refz));
#else
		for (int i = 0; i < 4; ++i)
			result[i] |= z[i] > refz[i] ? -1 : 0;
#endif
		break;

	case GE_COMP_GREATER:
#if defined(_M_SSE)
		result = _mm_or_si128(result, _mm_cmplt_epi32(z.ivec, refz));
		result = _mm_or_si128(result, _mm_cmpeq_epi32(z.ivec, refz));
#else
		for (int i = 0; i < 4; ++i)
			result[i] |= z[i] <= refz[i] ? -1 : 0;
#endif
		break;

	case GE_COMP_GEQUAL:
#if defined(_M_SSE)
		result = _mm_or_si128(result, _mm_cmplt_epi32(z.ivec, refz));
#else
		for (int i = 0; i < 4; ++i)
			result[i] |= z[i] < refz[i] ? -1 : 0;
#endif
		break;
	}

	return result;
}

template <bool useSSE4>
struct TriangleEdge {
	Vec4<int> Start(const ScreenCoords &v0, const ScreenCoords &v1, const ScreenCoords &origin);
	inline Vec4<int> StepX(const Vec4<int> &w);
	inline Vec4<int> StepY(const Vec4<int> &w);

	inline void NarrowMinMaxX(const Vec4<int> &w, int64_t minX, int64_t &rowMinX, int64_t &rowMaxX);
	inline Vec4<int> StepXTimes(const Vec4<int> &w, int c);

	Vec4<int> stepX;
	Vec4<int> stepY;
};

#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline __m128i SOFTRAST_CALL TriangleEdgeStartSSE4(__m128i initX, __m128i initY, int xf, int yf, int c) {
	initX = _mm_mullo_epi32(initX, _mm_set1_epi32(xf));
	initY = _mm_mullo_epi32(initY, _mm_set1_epi32(yf));
	return _mm_add_epi32(_mm_add_epi32(initX, initY), _mm_set1_epi32(c));
}
#endif

template <bool useSSE4>
Vec4<int> TriangleEdge<useSSE4>::Start(const ScreenCoords &v0, const ScreenCoords &v1, const ScreenCoords &origin) {
	// Start at pixel centers. The GE samples exactly there, with left and top edges inclusive (gpu/probe).
	static constexpr int centerOff = SCREEN_SCALE_FACTOR / 2;
	static constexpr int centerPlus1 = SCREEN_SCALE_FACTOR + centerOff;
	Vec4<int> initX = Vec4<int>::AssignToAll(origin.x) + Vec4<int>(centerOff, centerPlus1, centerOff, centerPlus1);
	Vec4<int> initY = Vec4<int>::AssignToAll(origin.y) + Vec4<int>(centerOff, centerOff, centerPlus1, centerPlus1);

	// orient2d refactored.
	int xf = v0.y - v1.y;
	int yf = v1.x - v0.x;
	int c = v1.y * v0.x - v1.x * v0.y;

	stepX = Vec4<int>::AssignToAll(xf * SCREEN_SCALE_FACTOR * 2);
	stepY = Vec4<int>::AssignToAll(yf * SCREEN_SCALE_FACTOR * 2);

#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	if constexpr (useSSE4)
		return TriangleEdgeStartSSE4(initX.ivec, initY.ivec, xf, yf, c);
#endif
	return Vec4<int>::AssignToAll(xf) * initX + Vec4<int>::AssignToAll(yf) * initY + Vec4<int>::AssignToAll(c);
}

template <bool useSSE4>
inline Vec4<int> TriangleEdge<useSSE4>::StepX(const Vec4<int> &w) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return _mm_add_epi32(w.ivec, stepX.ivec);
#elif PPSSPP_ARCH(ARM64_NEON)
	return vaddq_s32(w.ivec, stepX.ivec);
#else
	return w + stepX;
#endif
}

template <bool useSSE4>
inline Vec4<int> TriangleEdge<useSSE4>::StepY(const Vec4<int> &w) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return _mm_add_epi32(w.ivec, stepY.ivec);
#elif PPSSPP_ARCH(ARM64_NEON)
	return vaddq_s32(w.ivec, stepY.ivec);
#else
	return w + stepY;
#endif
}

#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline int SOFTRAST_CALL MaxWeightSSE4(__m128i w) {
	__m128i max2 = _mm_max_epi32(w, _mm_shuffle_epi32(w, _MM_SHUFFLE(3, 2, 3, 2)));
	__m128i max1 = _mm_max_epi32(max2, _mm_shuffle_epi32(max2, _MM_SHUFFLE(1, 1, 1, 1)));
	return _mm_cvtsi128_si32(max1);
}
#endif

template <bool useSSE4>
void TriangleEdge<useSSE4>::NarrowMinMaxX(const Vec4<int> &w, int64_t minX, int64_t &rowMinX, int64_t &rowMaxX) {
	int wmax;
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	if constexpr (useSSE4) {
		wmax = MaxWeightSSE4(w.ivec);
	} else {
		wmax = std::max(std::max(w.x, w.y), std::max(w.z, w.w));
	}
#elif PPSSPP_ARCH(ARM64_NEON)
	int32x2_t wmax_temp = vpmax_s32(vget_low_s32(w.ivec), vget_high_s32(w.ivec));
	wmax = vget_lane_s32(vpmax_s32(wmax_temp, wmax_temp), 0);
#else
	wmax = std::max(std::max(w.x, w.y), std::max(w.z, w.w));
#endif
	if (wmax < 0) {
		if (stepX.x > 0) {
			int steps = -wmax / stepX.x;
			rowMinX = std::max(rowMinX, minX + steps * SCREEN_SCALE_FACTOR * 2);
		} else if (stepX.x <= 0) {
			rowMinX = rowMaxX + 1;
		}
	}

	if (wmax >= 0 && stepX.x < 0) {
		int steps = (-wmax / stepX.x) + 1;
		rowMaxX = std::min(rowMaxX, minX + steps * SCREEN_SCALE_FACTOR * 2);
	}
}

#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline __m128i SOFTRAST_CALL StepTimesSSE4(__m128i w, __m128i step, int c) {
	return _mm_add_epi32(w, _mm_mullo_epi32(_mm_set1_epi32(c), step));
}
#endif

template <bool useSSE4>
inline Vec4<int> TriangleEdge<useSSE4>::StepXTimes(const Vec4<int> &w, int c) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	if constexpr (useSSE4)
		return StepTimesSSE4(w.ivec, stepX.ivec, c);
#elif PPSSPP_ARCH(ARM64_NEON)
	return vaddq_s32(w.ivec, vmulq_s32(vdupq_n_s32(c), stepX.ivec));
#endif
	return w + stepX * c;
}

static inline Vec4<int> MakeMask(const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2, const Vec4<int> &bias0, const Vec4<int> &bias1, const Vec4<int> &bias2, const Vec4<int> &scissor) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128i biased0 = _mm_add_epi32(w0.ivec, bias0.ivec);
	__m128i biased1 = _mm_add_epi32(w1.ivec, bias1.ivec);
	__m128i biased2 = _mm_add_epi32(w2.ivec, bias2.ivec);

	return _mm_or_si128(_mm_or_si128(biased0, _mm_or_si128(biased1, biased2)), scissor.ivec);
#elif PPSSPP_ARCH(ARM64_NEON)
	int32x4_t biased0 = vaddq_s32(w0.ivec, bias0.ivec);
	int32x4_t biased1 = vaddq_s32(w1.ivec, bias1.ivec);
	int32x4_t biased2 = vaddq_s32(w2.ivec, bias2.ivec);

	return vorrq_s32(vorrq_s32(biased0, vorrq_s32(biased1, biased2)), scissor.ivec);
#else
	return (w0 + bias0) | (w1 + bias1) | (w2 + bias2) | scissor;
#endif
}

#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline bool SOFTRAST_CALL AnyMaskSSE4(__m128i mask) {
	__m128i sig = _mm_srai_epi32(mask, 31);
	return _mm_test_all_ones(sig) == 0;
}
#endif

template <bool useSSE4>
static inline bool AnyMask(const Vec4<int> &mask) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	if constexpr (useSSE4) {
		return AnyMaskSSE4(mask.ivec);
	}

	// Source: https://fgiesen.wordpress.com/2013/02/10/optimizing-the-basic-rasterizer/#comment-6676
	return _mm_movemask_ps(_mm_castsi128_ps(mask.ivec)) != 15;
#elif PPSSPP_ARCH(ARM64_NEON)
	int64x2_t sig = vreinterpretq_s64_s32(vshrq_n_s32(mask.ivec, 31));
	return vgetq_lane_s64(sig, 0) != -1 || vgetq_lane_s64(sig, 1) != -1;
#else
	return mask.x >= 0 || mask.y >= 0 || mask.z >= 0 || mask.w >= 0;
#endif
}

static inline Vec4<float> EdgeRecip(const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128i wsum = _mm_add_epi32(w0.ivec, _mm_add_epi32(w1.ivec, w2.ivec));
	// _mm_rcp_ps loses too much precision.
	return _mm_div_ps(_mm_set1_ps(1.0f), _mm_cvtepi32_ps(wsum));
#elif PPSSPP_ARCH(ARM64_NEON)
	int32x4_t wsum = vaddq_s32(w0.ivec, vaddq_s32(w1.ivec, w2.ivec));
	return vdivq_f32(vdupq_n_f32(1.0f), vcvtq_f32_s32(wsum));
#else
	return (w0 + w1 + w2).Cast<float>().Reciprocal();
#endif
}



// The plane the GE interpolates depth and Gouraud color with (gpu/probe exp36-41 for depth, exp54
// for color, bit exact; color is screen-linear in transform mode too): the gradients are fixed
// point with 14 fractional bits per subpixel, from the exact edge cross products and GESetupRecip,
// and the plane is anchored at one vertex: the leftmost, unless the long edge (top to bottom)
// is strictly the right side, then the rightmost. A pixel's value is the plane at its center, floored.
struct DepthPlane {
	int64_t base;  // value << 14 at screen (0, 0)
	int64_t kx;    // per subpixel, << 14
	int64_t ky;
	bool rightAnchored = false;  // the long edge is the right side: the GE walks rows right to left

	int64_t At(int64_t x, int64_t y) const {
		return (base + kx * x + ky * y) >> 14;
	}
};

static DepthPlane ComputePlane(const int64_t X[3], const int64_t Y[3], const int64_t Z[3]) {
	DepthPlane plane{};
	const int64_t det = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
	if (det == 0) {
		plane.base = Z[0] << 14;
		return plane;
	}
	const int64_t nx = (Z[1] - Z[0]) * (Y[2] - Y[0]) - (Z[2] - Z[0]) * (Y[1] - Y[0]);
	const int64_t ny = (Z[2] - Z[0]) * (X[1] - X[0]) - (Z[1] - Z[0]) * (X[2] - X[0]);
	const uint64_t absDet = (uint64_t)(det < 0 ? -det : det);
	int e;
	const int64_t q = GESetupRecip(absDet, &e);
	const int64_t sign = det < 0 ? -1 : 1;
	// n / det * 2^14, as floor(n * q / 2^(e + 2)).
	plane.kx = (sign * nx * q) >> (e + 2);
	plane.ky = (sign * ny * q) >> (e + 2);

	int top = 0, mid = 1, bot = 2;
	auto above = [&](int a, int b) { return Y[a] < Y[b] || (Y[a] == Y[b] && X[a] < X[b]); };
	if (above(mid, top)) std::swap(mid, top);
	if (above(bot, mid)) std::swap(bot, mid);
	if (above(mid, top)) std::swap(mid, top);
	const int64_t cross = (X[bot] - X[top]) * (Y[mid] - Y[top]) - (Y[bot] - Y[top]) * (X[mid] - X[top]);
	const bool flat = Y[top] == Y[mid] || Y[mid] == Y[bot];
	int anchor = 0;
	if (cross > 0 && !flat) {
		plane.rightAnchored = true;
		for (int i = 1; i < 3; ++i)
			if (X[i] > X[anchor] || (X[i] == X[anchor] && Y[i] < Y[anchor]))
				anchor = i;
	} else {
		for (int i = 1; i < 3; ++i)
			if (X[i] < X[anchor] || (X[i] == X[anchor] && Y[i] < Y[anchor]))
				anchor = i;
	}
	plane.base = (Z[anchor] << 14) - plane.kx * X[anchor] - plane.ky * Y[anchor];
	return plane;
}

static DepthPlane ComputeDepthPlane(const VertexData &v0, const VertexData &v1, const VertexData &v2) {
	const int64_t X[3] = { v0.screenpos.x, v1.screenpos.x, v2.screenpos.x };
	const int64_t Y[3] = { v0.screenpos.y, v1.screenpos.y, v2.screenpos.y };
	const int64_t Z[3] = { v0.screenpos.z, v1.screenpos.z, v2.screenpos.z };
	return ComputePlane(X, Y, Z);
}

// Planes for each channel of a color (8 bits per channel, packed as in VertexData).
template <int channels>
static void ComputeColorPlanes(const VertexData &v0, const VertexData &v1, const VertexData &v2, u32 c0, u32 c1, u32 c2, DepthPlane *planes) {
	const int64_t X[3] = { v0.screenpos.x, v1.screenpos.x, v2.screenpos.x };
	const int64_t Y[3] = { v0.screenpos.y, v1.screenpos.y, v2.screenpos.y };
	for (int i = 0; i < channels; ++i) {
		const int64_t C[3] = { (c0 >> (i * 8)) & 0xFF, (c1 >> (i * 8)) & 0xFF, (c2 >> (i * 8)) & 0xFF };
		planes[i] = ComputePlane(X, Y, C);
	}
}

template <int channels>
static Vec4<int> ColorFromPlanes(const DepthPlane *planes, int64_t x, int64_t y) {
	Vec4<int> c(0, 0, 0, 0);
	for (int i = 0; i < channels; ++i)
		c[i] = std::clamp((int)planes[i].At(x, y), 0, 255);
	return c;
}

// Perspective texture coordinates as the GE interpolates them (gpu/probe exp55-56, bit exact at 1/16
// texel): per vertex q = 1/w with the GE's reciprocal and s = u * q, as float24s. s, t and q each become
// 15-bit integers at the largest exponent of the three vertices, go through the same plane as depth,
// and a pixel's u is s * 1/q, again with the GE's reciprocal (UVProduct).
struct UVPlanes {
	DepthPlane s, t, q;
	int shiftS, shiftT, shiftQ;
	bool valid;
};

static int SharedShift(const double v[3]) {
	int e = INT_MIN;
	for (int i = 0; i < 3; ++i) {
		if (v[i] != 0.0)
			e = std::max(e, std::ilogb(v[i]));
	}
	return e == INT_MIN ? 0 : 14 - e;
}

static DepthPlane FixedPlane(const int64_t X[3], const int64_t Y[3], const double v[3], int shift) {
	const int64_t V[3] = { (int64_t)std::ldexp(v[0], shift), (int64_t)std::ldexp(v[1], shift), (int64_t)std::ldexp(v[2], shift) };
	return ComputePlane(X, Y, V);
}

// u, v and w at three screen points. A w of 1 (through mode) leaves u and v as they are. With texture
// projection, uq is the texture matrix's q, and u and v come out divided by it (gpu/probe exp64).
static UVPlanes ComputeUVPlanesSTQ(const int64_t X[3], const int64_t Y[3], const double s[3], const double t[3], const double q[3]);

static UVPlanes ComputeUVPlanes(const int64_t X[3], const int64_t Y[3], const float u[3], const float v[3], const float w[3], const float *uq = nullptr) {
	UVPlanes planes{};
	double s[3], t[3], q[3];
	for (int i = 0; i < 3; ++i) {
		const float w24 = TruncateToFloat24(w[i]);
		if (!(w24 > 0.0f) || !std::isfinite(w24))
			return planes;
		const double r = GERecip(w24);
		q[i] = uq ? ProductToFloat24((double)TruncateToFloat24(uq[i]) * r) : r;
		s[i] = ProductToFloat24((double)TruncateToFloat24(u[i]) * r);
		t[i] = ProductToFloat24((double)TruncateToFloat24(v[i]) * r);
	}
	return ComputeUVPlanesSTQ(X, Y, s, t, q);
}

static UVPlanes ComputeUVPlanesSTQ(const int64_t X[3], const int64_t Y[3], const double s[3], const double t[3], const double q[3]) {
	UVPlanes planes{};
	planes.shiftS = SharedShift(s);
	planes.shiftT = SharedShift(t);
	planes.shiftQ = SharedShift(q);
	planes.s = FixedPlane(X, Y, s, planes.shiftS);
	planes.t = FixedPlane(X, Y, t, planes.shiftT);
	planes.q = FixedPlane(X, Y, q, planes.shiftQ);
	planes.valid = true;
	return planes;
}

static UVPlanes ComputeUVPlanes(const VertexData &v0, const VertexData &v1, const VertexData &v2, bool textureProj) {
	const int64_t X[3] = { v0.screenpos.x, v1.screenpos.x, v2.screenpos.x };
	const int64_t Y[3] = { v0.screenpos.y, v1.screenpos.y, v2.screenpos.y };
	const float u[3] = { v0.texturecoords.s(), v1.texturecoords.s(), v2.texturecoords.s() };
	const float v[3] = { v0.texturecoords.t(), v1.texturecoords.t(), v2.texturecoords.t() };
	const float w[3] = { v0.clipw, v1.clipw, v2.clipw };
	const float q[3] = { v0.texturecoords.q(), v1.texturecoords.q(), v2.texturecoords.q() };
	return ComputeUVPlanes(X, Y, u, v, w, textureProj ? q : nullptr);
}


// The largest of the s and t planes' gradients, in texels per pixel (auto mip level selection).
// inTexels: the planes hold texel coordinates (through-mode triangles) rather than normalized ones.
static float UVPlaneGradient(const UVPlanes &planes, const RasterizerState &state, bool inTexels) {
	if (!planes.valid)
		return -1.0f;
	const double perPixel = (double)SCREEN_SCALE_FACTOR / 16384.0;
	double gs = std::ldexp((double)std::max(std::abs(planes.s.kx), std::abs(planes.s.ky)) * perPixel, -planes.shiftS);
	double gt = std::ldexp((double)std::max(std::abs(planes.t.kx), std::abs(planes.t.ky)) * perPixel, -planes.shiftT);
	if (!inTexels) {
		gs *= 1 << state.samplerID.width0Shift;
		gt *= 1 << state.samplerID.height0Shift;
	}
	return (float)std::max(gs, gt);
}

// The q the mip level comes from: the GE picks it once per span of four pixels in a row, at the span's
// second pixel in the direction it walks the row, or when that one is outside the triangle, at the
// span's first pixel inside. Left to right that's x = 4k + 1; right to left (when the long edge is the
// right side, as for the plane anchor) 4k + 2 (gpu/probe exp93, exp103-106). quadX is the quad's left
// column in drawing coordinates, centerX/Y its first pixel's center in screen subpixels, and
// covered(x, y) the triangle's coverage there.
template <typename Covered>
static inline Vec4<float> LodQFromPlanes(const UVPlanes &planes, int64_t centerX, int64_t centerY, int quadX, const Covered &covered) {
	const bool rtl = planes.q.rightAnchored;
	Vec4<float> q;
	for (int i = 0; i < 4; ++i) {
		const int px = quadX + (i & 1);
		const int64_t y = centerY + (i >> 1) * SCREEN_SCALE_FACTOR;
		const int spanX = px & ~3;
		int pick = spanX + (rtl ? 2 : 1);
		if (!covered(centerX + (pick - quadX) * SCREEN_SCALE_FACTOR, y)) {
			for (int j = 0; j < 4; ++j) {
				const int c = rtl ? spanX + 3 - j : spanX + j;
				if (covered(centerX + (c - quadX) * SCREEN_SCALE_FACTOR, y)) {
					pick = c;
					break;
				}
			}
		}
		const int64_t x = centerX + (pick - quadX) * SCREEN_SCALE_FACTOR;
		q[i] = TruncateToFloat24((float)std::ldexp((double)planes.q.At(x, y), -planes.shiftQ));
	}
	return q;
}

static inline void GetTextureCoordinatesGE(const UVPlanes &planes, int64_t centerX, int64_t centerY, Vec4<float> &s, Vec4<float> &t, Vec4<float> &qOut) {
	for (int i = 0; i < 4; ++i) {
		const int64_t x = centerX + (i & 1) * SCREEN_SCALE_FACTOR, y = centerY + (i >> 1) * SCREEN_SCALE_FACTOR;
		const float q = TruncateToFloat24((float)std::ldexp((double)planes.q.At(x, y), -planes.shiftQ));
		qOut[i] = q;
		if (!(q > 0.0f)) {
			s[i] = 0.0f;
			t[i] = 0.0f;
			continue;
		}
		const double r = GERecip(q);
		s[i] = GEUVProduct((double)TruncateToFloat24((float)std::ldexp((double)planes.s.At(x, y), -planes.shiftS)) * r);
		t[i] = GEUVProduct((double)TruncateToFloat24((float)std::ldexp((double)planes.t.At(x, y), -planes.shiftT)) * r);
	}
}

template <bool clearMode, bool useSSE4>
void DrawTriangleSlice(
	const VertexData& v0, const VertexData& v1, const VertexData& v2,
	int x1, int y1, int x2, int y2,
	const RasterizerState &state)
{
	Vec4<int> bias0 = Vec4<int>::AssignToAll(IsRightSideOrFlatBottomLine(v0.screenpos.xy(), v1.screenpos.xy(), v2.screenpos.xy()) ? -1 : 0);
	Vec4<int> bias1 = Vec4<int>::AssignToAll(IsRightSideOrFlatBottomLine(v1.screenpos.xy(), v2.screenpos.xy(), v0.screenpos.xy()) ? -1 : 0);
	Vec4<int> bias2 = Vec4<int>::AssignToAll(IsRightSideOrFlatBottomLine(v2.screenpos.xy(), v0.screenpos.xy(), v1.screenpos.xy()) ? -1 : 0);

	const PixelFuncID &pixelID = state.pixelID;

	TriangleEdge<useSSE4> e0;
	TriangleEdge<useSSE4> e1;
	TriangleEdge<useSSE4> e2;

	int64_t minX = x1, maxX = x2, minY = y1, maxY = y2;

	ScreenCoords pprime(minX, minY, 0);
	// Coverage of any pixel center, for picking the mip level's q (LodQFromPlanes), when it matters.
	const bool lodUsesQ = state.TexLevelMode() != GE_TEXLEVEL_MODE_CONST && (state.maxTexLevel > 0 || state.minFilt != state.magFilt);
	auto edgeAt = [](const ScreenCoords &a, const ScreenCoords &b, int64_t x, int64_t y) {
		return (int64_t)(a.y - b.y) * x + (int64_t)(b.x - a.x) * y + ((int64_t)b.y * a.x - (int64_t)b.x * a.y);
	};
	auto coveredAt = [&](int64_t x, int64_t y) {
		return edgeAt(v1.screenpos, v2.screenpos, x, y) + bias0[0] >= 0 && edgeAt(v2.screenpos, v0.screenpos, x, y) + bias1[0] >= 0 && edgeAt(v0.screenpos, v1.screenpos, x, y) + bias2[0] >= 0;
	};
	// A very tall triangle's long edge (top to bottom vertex): when 3 dy > 2^17 (in subpixels), the first pixel
	// of each 4-pixel span (for a left edge, the last for a right edge) is inside it when any of the span is
	// (gpu/probe exp131-135).
	int snapEdge = -1;
	bool snapLeft = false;
	{
		const VertexData *vs[3] = { &v0, &v1, &v2 };
		int top = 0, bot = 0;
		for (int i = 1; i < 3; ++i) {
			if (vs[i]->screenpos.y < vs[top]->screenpos.y)
				top = i;
			// On a tie for the bottom, the long edge ends at the left one (gpu/probe exp131).
			if (vs[i]->screenpos.y > vs[bot]->screenpos.y || (vs[i]->screenpos.y == vs[bot]->screenpos.y && vs[i]->screenpos.x < vs[bot]->screenpos.x))
				bot = i;
		}
		const int64_t dy = (int64_t)vs[bot]->screenpos.y - vs[top]->screenpos.y;
		if (top != bot && 3 * dy > (1 << 17)) {
			const int third = 3 - top - bot;
			snapEdge = third;  // e_k is the edge opposite vertex k
			// Left edge when the third vertex is to the right of the long edge at its height.
			const int64_t ex = (int64_t)(vs[bot]->screenpos.x - vs[top]->screenpos.x) * (vs[third]->screenpos.y - vs[top]->screenpos.y);
			snapLeft = (int64_t)(vs[third]->screenpos.x - vs[top]->screenpos.x) * dy > ex;
		}
	}
	auto edgeK = [&](int k, int64_t x, int64_t y) {
		switch (k) {
		case 0: return edgeAt(v1.screenpos, v2.screenpos, x, y) + bias0[0];
		case 1: return edgeAt(v2.screenpos, v0.screenpos, x, y) + bias1[0];
		default: return edgeAt(v0.screenpos, v1.screenpos, x, y) + bias2[0];
		}
	};

	Vec4<int> w0_base = e0.Start(v1.screenpos, v2.screenpos, pprime);
	Vec4<int> w1_base = e1.Start(v2.screenpos, v0.screenpos, pprime);
	Vec4<int> w2_base = e2.Start(v0.screenpos, v1.screenpos, pprime);

	// The sum of weights should remain constant as we move toward/away from the edges.
	const Vec4<float> wsum_recip = EdgeRecip(w0_base, w1_base, w2_base);

	// All the z values are the same, no interpolation required.
	// This is common, and when we interpolate, we lose accuracy.
	const bool flatZ = v0.screenpos.z == v1.screenpos.z && v0.screenpos.z == v2.screenpos.z;
	const bool flatColorAll = !state.shadeGouraud;
	const bool flatColor0 = flatColorAll || (v0.color0 == v1.color0 && v0.color0 == v2.color0);
	const bool flatColor1 = flatColorAll || (v0.color1 == v1.color1 && v0.color1 == v2.color1);
	const bool noFog = clearMode || !pixelID.applyFog || (v0.fogdepth >= 255.0f / 256.0f && v1.fogdepth >= 255.0f / 256.0f && v2.fogdepth >= 255.0f / 256.0f);

	if (pixelID.applyDepthRange && flatZ) {
		if (v0.screenpos.z < pixelID.cached.minz || v0.screenpos.z > pixelID.cached.maxz)
			return;
	}

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	uint32_t bpp = pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	std::string tag = StringFromFormat("DisplayListT_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListTZ_%08x", state.listPC);
#endif

	const Vec4<int> v2_c0 = Vec4<int>::FromRGBA(v2.color0);
	const Vec3<int> v2_c1 = Vec3<int>::FromRGB(v2.color1);

	const DepthPlane depthPlane = flatZ ? DepthPlane{} : ComputeDepthPlane(v0, v1, v2);
	// Through mode too, where w is 1 (gpu/probe exp73).
	const UVPlanes uvPlanes = state.enableTextures ? ComputeUVPlanes(v0, v1, v2, state.textureProj && !state.throughMode) : UVPlanes{};
	const float autoGrad = UVPlaneGradient(uvPlanes, state, state.throughMode);
	DepthPlane color0Planes[4], color1Planes[3];
	DepthPlane fogPlane{};
	if (!noFog) {
		const int64_t X[3] = { v0.screenpos.x, v1.screenpos.x, v2.screenpos.x };
		const int64_t Y[3] = { v0.screenpos.y, v1.screenpos.y, v2.screenpos.y };
		const int64_t F[3] = { ClampFogDepth(v0.fogdepth), ClampFogDepth(v1.fogdepth), ClampFogDepth(v2.fogdepth) };
		fogPlane = ComputePlane(X, Y, F);
	}
	if (!flatColor0)
		ComputeColorPlanes<4>(v0, v1, v2, v0.color0, v1.color0, v2.color0, color0Planes);
	if (!flatColor1)
		ComputeColorPlanes<3>(v0, v1, v2, v0.color1, v1.color1, v2.color1, color1Planes);
	const Vec4<int> minz = Vec4<int>::AssignToAll(pixelID.cached.minz);
	const Vec4<int> maxz = Vec4<int>::AssignToAll(pixelID.cached.maxz);

	for (int64_t curY = minY; curY <= maxY; curY += SCREEN_SCALE_FACTOR * 2,
										w0_base = e0.StepY(w0_base),
										w1_base = e1.StepY(w1_base),
										w2_base = e2.StepY(w2_base)) {
		Vec4<int> w0 = w0_base;
		Vec4<int> w1 = w1_base;
		Vec4<int> w2 = w2_base;

		DrawingCoords p = TransformUnit::ScreenToDrawing(minX, curY);

		int64_t rowMinX = minX, rowMaxX = maxX;
		// A snapping edge can light pixels up to three past it.
		if (snapEdge != 0)
			e0.NarrowMinMaxX(w0, minX, rowMinX, rowMaxX);
		if (snapEdge != 1)
			e1.NarrowMinMaxX(w1, minX, rowMinX, rowMaxX);
		if (snapEdge != 2)
			e2.NarrowMinMaxX(w2, minX, rowMinX, rowMaxX);

		int skipX = (rowMinX - minX) / (SCREEN_SCALE_FACTOR * 2);
		w0 = e0.StepXTimes(w0, skipX);
		w1 = e1.StepXTimes(w1, skipX);
		w2 = e2.StepXTimes(w2, skipX);
		p.x = (p.x + 2 * skipX) & 0x3FF;

		// TODO: Maybe we can clip the edges instead?
		int scissorYPlus1 = curY + SCREEN_SCALE_FACTOR > maxY ? -1 : 0;
		Vec4<int> scissor_mask = Vec4<int>(0, rowMaxX - rowMinX - SCREEN_SCALE_FACTOR, scissorYPlus1, (rowMaxX - rowMinX - SCREEN_SCALE_FACTOR) | scissorYPlus1);
		Vec4<int> scissor_step = Vec4<int>(0, -(SCREEN_SCALE_FACTOR * 2), 0, -(SCREEN_SCALE_FACTOR * 2));

		for (int64_t curX = rowMinX; curX <= rowMaxX; curX += SCREEN_SCALE_FACTOR * 2,
			w0 = e0.StepX(w0),
			w1 = e1.StepX(w1),
			w2 = e2.StepX(w2),
			scissor_mask = scissor_mask + scissor_step,
			p.x = (p.x + 2) & 0x3FF) {

			// If p is on or inside all edges, render pixel
			Vec4<int> mask = MakeMask(w0, w1, w2, bias0, bias1, bias2, scissor_mask);
			if (snapEdge >= 0) {
				for (int i = 0; i < 4; ++i) {
					const int64_t x = curX + SCREEN_SCALE_FACTOR / 2 + (i & 1) * SCREEN_SCALE_FACTOR;
					const int64_t y = curY + SCREEN_SCALE_FACTOR / 2 + (i >> 1) * SCREEN_SCALE_FACTOR;
					const int px = p.x + (i & 1);
					// Only the span's first pixel (left edge) or last (right edge) takes the edge at the other end.
					const int spanX = snapLeft ? ((px & 3) == 0 ? (px | 3) : px) : ((px & 3) == 3 ? (px & ~3) : px);
					const int64_t xs = x + (int64_t)(spanX - px) * SCREEN_SCALE_FACTOR;
					bool inside = true;
					for (int k = 0; k < 3; ++k)
						inside = inside && edgeK(k, k == snapEdge ? xs : x, y) >= 0;
					mask[i] = (inside ? 0 : -1) | scissor_mask[i];
				}
			}
			if (AnyMask<useSSE4>(mask)) {
				Vec4<int> z;
				if (flatZ) {
					z = Vec4<int>::AssignToAll(v2.screenpos.z);
				} else {
					// The GE's fixed point depth plane at the four pixel centers.
					const int64_t z00 = depthPlane.base + depthPlane.kx * (curX + SCREEN_SCALE_FACTOR / 2) + depthPlane.ky * (curY + SCREEN_SCALE_FACTOR / 2);
					const int64_t dx = depthPlane.kx * SCREEN_SCALE_FACTOR, dy = depthPlane.ky * SCREEN_SCALE_FACTOR;
					z = Vec4<int>((int)(z00 >> 14), (int)((z00 + dx) >> 14), (int)((z00 + dy) >> 14), (int)((z00 + dx + dy) >> 14));
					// A value floored below 0 (next to an edge of z = 0 vertices) is 0 (gpu/probe exp148).
					for (int i = 0; i < 4; ++i)
						z[i] = std::max(z[i], 0);
				}

				if (pixelID.earlyZChecks) {
					if (pixelID.applyDepthRange) {
#if defined(_M_SSE)
						mask.ivec = _mm_or_si128(mask.ivec, _mm_or_si128(_mm_cmplt_epi32(z.ivec, minz.ivec), _mm_cmpgt_epi32(z.ivec, maxz.ivec)));
#else
						for (int i = 0; i < 4; ++i) {
							if (z[i] < minz[i] || z[i] > maxz[i])
								mask[i] = -1;
						}
#endif
					}
					mask = CheckDepthTestPassed4(mask, pixelID.DepthTestFunc(), p.x, p.y, pixelID.cached.depthbufStride, z);
					if (!AnyMask<useSSE4>(mask))
						continue;
				}

				// Color interpolation is not perspective corrected on the PSP.
				const int64_t centerX = curX + SCREEN_SCALE_FACTOR / 2, centerY = curY + SCREEN_SCALE_FACTOR / 2;
				Vec4<int> prim_color[4];
				if (!flatColor0) {
					for (int i = 0; i < 4; ++i) {
						if (mask[i] >= 0)
							prim_color[i] = ColorFromPlanes<4>(color0Planes, centerX + (i & 1) * SCREEN_SCALE_FACTOR, centerY + (i >> 1) * SCREEN_SCALE_FACTOR);
					}
				} else {
					for (int i = 0; i < 4; ++i) {
						prim_color[i] = v2_c0;
					}
				}
				Vec3<int> sec_color[4];
				if (!flatColor1) {
					for (int i = 0; i < 4; ++i) {
						if (mask[i] >= 0)
							sec_color[i] = ColorFromPlanes<3>(color1Planes, centerX + (i & 1) * SCREEN_SCALE_FACTOR, centerY + (i >> 1) * SCREEN_SCALE_FACTOR).rgb();
					}
				} else {
					for (int i = 0; i < 4; ++i) {
						sec_color[i] = v2_c1;
					}
				}
				if (DoubleSecondaryColor(state)) {
					for (int i = 0; i < 4; ++i) {
						sec_color[i] = sec_color[i] + sec_color[i];
					}
				}

				if (state.enableTextures) {
					if constexpr (!clearMode) {
						Vec4<float> s, t;
						Vec4<float> q = Vec4<float>::AssignToAll(1.0f);
						if (state.throughMode) {
							if (uvPlanes.valid) {
								GetTextureCoordinatesGE(uvPlanes, centerX, centerY, s, t, q);
							} else {
								s = Interpolate(v0.texturecoords.s(), v1.texturecoords.s(), v2.texturecoords.s(), w0, w1,
												w2, wsum_recip);
								t = Interpolate(v0.texturecoords.t(), v1.texturecoords.t(), v2.texturecoords.t(), w0, w1,
												w2, wsum_recip);
							}

							// For levels > 0, mipmapping is always based on level 0.  Simpler to scale first.
							s *= 1.0f / (float) (1 << state.samplerID.width0Shift);
							t *= 1.0f / (float) (1 << state.samplerID.height0Shift);
						} else if (uvPlanes.valid) {
							GetTextureCoordinatesGE(uvPlanes, centerX, centerY, s, t, q);
							if (lodUsesQ)
								q = LodQFromPlanes(uvPlanes, centerX, centerY, p.x, coveredAt);
						} else if (state.textureProj) {
							// Texture coordinate interpolation must definitely be perspective-correct.
							GetTextureCoordinatesProj(v0, v1, v2, w0, w1, w2, wsum_recip, s, t);
						} else {
							// Texture coordinate interpolation must definitely be perspective-correct.
							GetTextureCoordinates(v0, v1, v2, w0, w1, w2, wsum_recip, s, t);
						}

						if (state.TexLevelMode() == GE_TEXLEVEL_MODE_SLOPE && !uvPlanes.valid) {
							const float clipw = (v0.clipw * w0.x + v1.clipw * w1.x + v2.clipw * w2.x) * wsum_recip.x;
							q = Vec4<float>::AssignToAll(1.0f / clipw);
						}
						ApplyTexturing(state, prim_color, mask, s, t, q, autoGrad);
					}
				}

				if constexpr (!clearMode) {
					for (int i = 0; i < 4; ++i) {
#if defined(_M_SSE)
						// TODO: Tried making Vec4 do this, but things got slower.
						const __m128i sec = _mm_and_si128(sec_color[i].ivec, _mm_set_epi32(0, -1, -1, -1));
						prim_color[i].ivec = _mm_add_epi32(prim_color[i].ivec, sec);
#elif PPSSPP_ARCH(ARM64_NEON)
						int32x4_t sec = vsetq_lane_s32(0, sec_color[i].ivec, 3);
						prim_color[i].ivec = vaddq_s32(prim_color[i].ivec, sec);
#else
						prim_color[i] += Vec4<int>(sec_color[i], 0);
#endif
					}
				}

				Vec4<int> fog = Vec4<int>::AssignToAll(255);
				if (!noFog) {
					// The 8-bit fog of each vertex through the depth plane, like Gouraud color (gpu/probe exp21).
					for (int i = 0; i < 4; ++i)
						fog[i] = std::clamp((int)fogPlane.At(centerX + (i & 1) * SCREEN_SCALE_FACTOR, centerY + (i >> 1) * SCREEN_SCALE_FACTOR), 0, 255);
				}

				PROFILE_THIS_SCOPE("draw_tri_px");
				DrawingCoords subp = p;
				for (int i = 0; i < 4; ++i) {
					if (mask[i] < 0) {
						continue;
					}
					subp.x = p.x + (i & 1);
					subp.y = p.y + (i / 2);

					state.drawPixel(subp.x, subp.y, z[i], fog[i], ToVec4IntArg(prim_color[i]), pixelID);

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED)
					uint32_t row = gstate.getFrameBufAddress() + subp.y * pixelID.cached.framebufStride * bpp;
					NotifyMemInfo(MemBlockFlags::WRITE, row + subp.x * bpp, bpp, tag.c_str(), tag.size());
					if (pixelID.depthWrite) {
						row = gstate.getDepthBufAddress() + subp.y * pixelID.cached.depthbufStride * 2;
						NotifyMemInfo(MemBlockFlags::WRITE, row + subp.x * 2, 2, ztag.c_str(), ztag.size());
					}
#endif
				}
			}
		}
	}

#if !defined(SOFTGPU_MEMORY_TAGGING_DETAILED) && defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	for (int y = minY; y <= maxY; y += SCREEN_SCALE_FACTOR) {
		DrawingCoords p = TransformUnit::ScreenToDrawing(minX, y);
		DrawingCoords pend = TransformUnit::ScreenToDrawing(maxX, y);
		uint32_t row = gstate.getFrameBufAddress() + p.y * pixelID.cached.framebufStride * bpp;
		NotifyMemInfo(MemBlockFlags::WRITE, row + p.x * bpp, (pend.x - p.x) * bpp, tag.c_str(), tag.size());

		if (pixelID.depthWrite) {
			row = gstate.getDepthBufAddress() + p.y * pixelID.cached.depthbufStride * 2;
			NotifyMemInfo(MemBlockFlags::WRITE, row + p.x * 2, (pend.x - p.x) * 2, ztag.c_str(), ztag.size());
		}
	}
#endif
}

// Draws triangle, vertices specified in counter-clockwise direction
void DrawTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2, const BinCoords &range, const RasterizerState &state) {
	PROFILE_THIS_SCOPE("draw_tri");

	auto drawSlice = cpu_info.bSSE4_1 ?
		(state.pixelID.clearMode ? &DrawTriangleSlice<true, true> : &DrawTriangleSlice<false, true>) :
		(state.pixelID.clearMode ? &DrawTriangleSlice<true, false> : &DrawTriangleSlice<false, false>);

	drawSlice(v0, v1, v2, range.x1, range.y1, range.x2, range.y2, state);
}

void DrawRectangle(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &rastState) {
	int entireX1 = std::min(v0.screenpos.x, v1.screenpos.x);
	int entireY1 = std::min(v0.screenpos.y, v1.screenpos.y);
	int entireX2 = std::max(v0.screenpos.x, v1.screenpos.x) - 1;
	int entireY2 = std::max(v0.screenpos.y, v1.screenpos.y) - 1;
	// Pixel centers are at 16k + 7 here. The GE's sprite edges (gpu/probe): the first column is
	// (x1 + 6) >> 4, the first row (y1 + 7) >> 4, and both end at (x2 + 7) >> 4, exclusive.
	int minX = std::max(entireX1 & ~(SCREEN_SCALE_FACTOR - 1), range.x1) | (SCREEN_SCALE_FACTOR / 2 - 1);
	int minY = std::max(entireY1 & ~(SCREEN_SCALE_FACTOR - 1), range.y1) | (SCREEN_SCALE_FACTOR / 2 - 1);
	int maxX = std::min(entireX2 - 1, range.x2);
	int maxY = std::min(entireY2 - 1, range.y2);

	if (minX < entireX1 - 2)
		minX += SCREEN_SCALE_FACTOR;
	// A sprite drawn bottom-left to top-right is rotated, and its first row rounds like a column
	// (gpu/probe exp122).
	const bool rotated = v0.screenpos.x < v1.screenpos.x && v0.screenpos.y > v1.screenpos.y;
	if (minY < entireY1 - (rotated ? 2 : 1))
		minY += SCREEN_SCALE_FACTOR;

	RasterizerState state = OptimizeFlatRasterizerState(rastState, v1);

	UVPlanes uvPlanes{};
	Vec2f rowST(0.0f, 0.0f);
	// Note: this is double the x or y movement.
	Vec2f stx(0.0f, 0.0f);
	Vec2f sty(0.0f, 0.0f);
	if (state.enableTextures) {
		// Note: texture projection is not handled here, those always turn into triangles.
		Vec2f tc0 = v0.texturecoords.uv();
		Vec2f tc1 = v1.texturecoords.uv();
		if (state.throughMode) {
			// For levels > 0, mipmapping is always based on level 0.  Simpler to scale first.
			tc0.s() *= 1.0f / (float)(1 << state.samplerID.width0Shift);
			tc1.s() *= 1.0f / (float)(1 << state.samplerID.width0Shift);
			tc0.t() *= 1.0f / (float)(1 << state.samplerID.height0Shift);
			tc1.t() *= 1.0f / (float)(1 << state.samplerID.height0Shift);
		}

		float diffX = (entireX2 - entireX1 + 1) / (float)SCREEN_SCALE_FACTOR;
		float diffY = (entireY2 - entireY1 + 1) / (float)SCREEN_SCALE_FACTOR;
		float diffS = tc1.s() - tc0.s();
		float diffT = tc1.t() - tc0.t();

		if (v0.screenpos.x < v1.screenpos.x) {
			if (v0.screenpos.y < v1.screenpos.y) {
				// Okay, simple, TL -> BR.  S and T move toward v1 with X and Y.
				rowST = tc0;
				stx = Vec2f(2.0f * diffS / diffX, 0.0f);
				sty = Vec2f(0.0f, 2.0f * diffT / diffY);
			} else {
				// BL to TR, rotated.  We start at TL still.
				// X moves T (not S) toward v1, and Y moves S away from v1.
				rowST = Vec2f(tc1.s(), tc0.t());
				stx = Vec2f(0.0f, 2.0f * diffT / diffX);
				sty = Vec2f(2.0f * -diffS / diffY, 0.0f);
			}
		} else {
			if (v0.screenpos.y < v1.screenpos.y) {
				// TR to BL.  Like BL to TR, rotated.
				// X moves T (not s) away from v1, and Y moves S toward v1.
				rowST = Vec2f(tc0.s(), tc1.t());
				stx = Vec2f(0.0f, 2.0f * -diffT / diffX);
				sty = Vec2f(2.0f * diffS / diffY, 0.0f);
			} else {
				// BR to TL, just inverse of TL to BR.
				rowST = Vec2f(tc1.s(), tc1.t());
				stx = Vec2f(2.0f * -diffS / diffX, 0.0f);
				sty = Vec2f(0.0f, 2.0f * -diffT / diffY);
			}
		}

		// Okay, now move ST to the minX, minY position.
		rowST += (stx / (float)(SCREEN_SCALE_FACTOR * 2)) * (minX - entireX1 + 1);
		rowST += (sty / (float)(SCREEN_SCALE_FACTOR * 2)) * (minY - entireY1 + 1);

		// The GE interpolates sprite UVs with the same planes as triangles, from three corners whose s, t and q
		// it takes component by component from the two vertices: s from the vertex that supplies the corner's
		// x, t from the one that supplies its y (swapped for a rotated sprite, drawn bottom left to top right
		// or top right to bottom left), and q from the one that supplies its x. So different w at the two
		// vertices bend the mapping (gpu/probe exp57, exp122, exp129). The corners are top left, top right
		// and bottom left, or for bottom left to top right, bottom left, top right and bottom right, which
		// anchors that plane at the bottom left.
		const bool right = v0.screenpos.x < v1.screenpos.x, down = v0.screenpos.y < v1.screenpos.y;
		const bool swapST = right != down;
		const VertexData *vs[2] = { &v0, &v1 };
		double S[2], T[2], Q[2];
		for (int k = 0; k < 2; ++k) {
			const double r = state.throughMode ? 1.0 : GERecip(TruncateToFloat24(vs[k]->clipw));
			Q[k] = r;
			S[k] = ProductToFloat24((double)TruncateToFloat24(k == 0 ? tc0.s() : tc1.s()) * r);
			T[k] = ProductToFloat24((double)TruncateToFloat24(k == 0 ? tc0.t() : tc1.t()) * r);
		}
		const int64_t left = std::min(v0.screenpos.x, v1.screenpos.x), top = std::min(v0.screenpos.y, v1.screenpos.y);
		const int64_t rightX = std::max(v0.screenpos.x, v1.screenpos.x), bottom = std::max(v0.screenpos.y, v1.screenpos.y);
		const bool bottomLeftFirst = right && !down;
		const int64_t CX[3] = { left, rightX, bottomLeftFirst ? rightX : left };
		const int64_t CY[3] = { bottomLeftFirst ? bottom : top, top, bottom };
		double cs[3], ct[3], cq[3];
		for (int k = 0; k < 3; ++k) {
			const int xs = CX[k] == v0.screenpos.x ? 0 : 1, ys = CY[k] == v0.screenpos.y ? 0 : 1;
			cs[k] = S[swapST ? ys : xs];
			ct[k] = T[swapST ? xs : ys];
			cq[k] = Q[xs];
		}
		uvPlanes = ComputeUVPlanesSTQ(CX, CY, cs, ct, cq);
	}
	// Sprite planes are built from normalized coordinates, also in through mode.
	const float autoGrad = UVPlaneGradient(uvPlanes, state, false);

	// And now what we add to spread out to 4 values.
	const Vec4f sto4(0.0f, 0.5f * stx.s(), 0.5f * sty.s(), 0.5f * stx.s() + 0.5f * sty.s());
	const Vec4f tto4(0.0f, 0.5f * stx.t(), 0.5f * sty.t(), 0.5f * stx.t() + 0.5f * sty.t());

	ScreenCoords pprime(minX, minY, 0);
	const Vec4<int> fog = Vec4<int>::AssignToAll(ClampFogDepth(v1.fogdepth));
	const Vec4<int> z = Vec4<int>::AssignToAll(v1.screenpos.z);
	const Vec4<int> c0 = Vec4<int>::FromRGBA(v1.color0);
	const Vec3<int> sec_color = Vec3<int>::FromRGB(v1.color1) * (DoubleSecondaryColor(state) ? 2 : 1);

	if (state.pixelID.applyDepthRange) {
		// We can bail early since the Z is flat.
		if (v1.screenpos.z < state.pixelID.cached.minz || v1.screenpos.z > state.pixelID.cached.maxz)
			return;
	}

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	uint32_t bpp = state.pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	std::string tag = StringFromFormat("DisplayListR_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListRZ_%08x", state.listPC);
#endif

	for (int64_t curY = minY; curY <= maxY; curY += SCREEN_SCALE_FACTOR * 2, rowST += sty) {
		DrawingCoords p = TransformUnit::ScreenToDrawing(minX, curY);

		int scissorY2 = curY + SCREEN_SCALE_FACTOR > maxY ? -1 : 0;
		Vec4<int> scissor_mask = Vec4<int>(0, maxX - minX - SCREEN_SCALE_FACTOR, scissorY2, (maxX - minX - SCREEN_SCALE_FACTOR) | scissorY2);
		Vec4<int> scissor_step = Vec4<int>(0, -(SCREEN_SCALE_FACTOR * 2), 0, -(SCREEN_SCALE_FACTOR * 2));
		Vec2f st = rowST;

		for (int64_t curX = minX; curX <= maxX; curX += SCREEN_SCALE_FACTOR * 2,
			st += stx,
			scissor_mask += scissor_step,
			p.x = (p.x + 2) & 0x3FF) {
			Vec4<int> mask = scissor_mask;

			Vec4<int> prim_color[4];
			for (int i = 0; i < 4; ++i) {
				prim_color[i] = c0;
			}

			if (state.pixelID.earlyZChecks) {
				for (int i = 0; i < 4; ++i) {
					if (mask[i] < 0)
						continue;

					int x = p.x + (i & 1);
					int y = p.y + (i / 2);
					if (!CheckDepthTestPassed(state.pixelID.DepthTestFunc(), x, y, state.pixelID.cached.depthbufStride, z[i])) {
						mask[i] = -1;
					}
				}
			}

			if (state.enableTextures) {
				Vec4<float> s, t;
				Vec4<float> q = Vec4<float>::AssignToAll(1.0f / v1.clipw);
				if (uvPlanes.valid) {
					// Pixel centers are at 16k + 7 here, the GE's at 16k + 8.
					GetTextureCoordinatesGE(uvPlanes, curX + 1, curY + 1, s, t, q);
				} else {
					s = Vec4<float>::AssignToAll(st.s()) + sto4;
					t = Vec4<float>::AssignToAll(st.t()) + tto4;
				}

				ApplyTexturing(state, prim_color, mask, s, t, q, autoGrad);
			}

			if (!state.pixelID.clearMode) {
				for (int i = 0; i < 4; ++i) {
#if defined(_M_SSE)
					// TODO: Tried making Vec4 do this, but things got slower.
					const __m128i sec = _mm_and_si128(sec_color.ivec, _mm_set_epi32(0, -1, -1, -1));
					prim_color[i].ivec = _mm_add_epi32(prim_color[i].ivec, sec);
#elif PPSSPP_ARCH(ARM64_NEON)
					int32x4_t sec = vsetq_lane_s32(0, sec_color.ivec, 3);
					prim_color[i].ivec = vaddq_s32(prim_color[i].ivec, sec);
#else
					prim_color[i] += Vec4<int>(sec_color, 0);
#endif
				}
			}

			PROFILE_THIS_SCOPE("draw_rect_px");
			DrawingCoords subp = p;
			for (int i = 0; i < 4; ++i) {
				if (mask[i] < 0) {
					continue;
				}
				subp.x = p.x + (i & 1);
				subp.y = p.y + (i / 2);

				state.drawPixel(subp.x, subp.y, z[i], fog[i], ToVec4IntArg(prim_color[i]), state.pixelID);

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED)
				uint32_t row = gstate.getFrameBufAddress() + subp.y * state.pixelID.cached.framebufStride * bpp;
				NotifyMemInfo(MemBlockFlags::WRITE, row + subp.x * bpp, bpp, tag.c_str(), tag.size());
				if (state.pixelID.depthWrite) {
					row = gstate.getDepthBufAddress() + subp.y * state.pixelID.cached.depthbufStride * 2;
					NotifyMemInfo(MemBlockFlags::WRITE, row + subp.x * 2, 2, ztag.c_str(), ztag.size());
				}
#endif
			}
		}
	}

#if !defined(SOFTGPU_MEMORY_TAGGING_DETAILED) && defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	for (int y = minY; y <= maxY; y += SCREEN_SCALE_FACTOR) {
		DrawingCoords p = TransformUnit::ScreenToDrawing(minX, y);
		DrawingCoords pend = TransformUnit::ScreenToDrawing(maxX, y);
		uint32_t row = gstate.getFrameBufAddress() + p.y * state.pixelID.cached.framebufStride * bpp;
		NotifyMemInfo(MemBlockFlags::WRITE, row + p.x * bpp, (pend.x - p.x) * bpp, tag.c_str(), tag.size());

		if (state.pixelID.depthWrite) {
			row = gstate.getDepthBufAddress() + p.y * state.pixelID.cached.depthbufStride * 2;
			NotifyMemInfo(MemBlockFlags::WRITE, row + p.x * 2, (pend.x - p.x) * 2, ztag.c_str(), ztag.size());
		}
	}
#endif
}

void DrawPoint(const VertexData &v0, const BinCoords &range, const RasterizerState &state) {
	ScreenCoords pos = v0.screenpos;
	Vec4<int> prim_color = Vec4<int>::FromRGBA(v0.color0);

	auto &pixelID = state.pixelID;
	auto &samplerID = state.samplerID;

	DrawingCoords p = TransformUnit::ScreenToDrawing(pos);
	u16 z = pos.z;

	if (pixelID.earlyZChecks) {
		if (pixelID.applyDepthRange) {
			if (z < pixelID.cached.minz || z > pixelID.cached.maxz)
				return;
		}

		if (!CheckDepthTestPassed(pixelID.DepthTestFunc(), p.x, p.y, pixelID.cached.depthbufStride, z)) {
			return;
		}
	}

	if (state.enableTextures) {
		float s = v0.texturecoords.s();
		float t = v0.texturecoords.t();
		if (state.throughMode) {
			s = GETruncateTexCoord(s) * (1.0f / (float)(1 << state.samplerID.width0Shift));
			t = GETruncateTexCoord(t) * (1.0f / (float)(1 << state.samplerID.height0Shift));
		} else {
			// The same planes as triangles, flat (gpu/probe exp64).
			const UVPlanes planes = ComputeUVPlanes(v0, v0, v0, state.textureProj);
			if (planes.valid) {
				Vec4<float> s4, t4, q4;
				GetTextureCoordinatesGE(planes, v0.screenpos.x, v0.screenpos.y, s4, t4, q4);
				s = s4[0];
				t = t4[0];
			} else if (state.textureProj) {
				GetTextureCoordinatesProj(v0, v0, 0.0f, s, t);
			} else {
				GetTextureCoordinates(v0, v0, 0.0f, s, t);
			}
		}

		int texLevel;
		int texLevelFrac;
		bool bilinear;
		CalculateSamplingParams(0.0f, 0.0f, 1.0f / v0.clipw, state, texLevel, texLevelFrac, bilinear);
		PROFILE_THIS_SCOPE("sampler");
		prim_color = ApplyTexturingSingle(s, t, ToVec4IntArg(prim_color), texLevel, texLevelFrac, bilinear, state);
	}

	if (!pixelID.clearMode) {
		Vec3<int> sec_color = Vec3<int>::FromRGB(v0.color1) * (DoubleSecondaryColor(state) ? 2 : 1);
		prim_color += Vec4<int>(sec_color, 0);
	}

	u8 fog = 255;
	if (pixelID.applyFog) {
		fog = ClampFogDepth(v0.fogdepth);
	}

	PROFILE_THIS_SCOPE("draw_px");
	state.drawPixel(p.x, p.y, z, fog, ToVec4IntArg(prim_color), pixelID);

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	uint32_t bpp = pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	std::string tag = StringFromFormat("DisplayListP_%08x", state.listPC);

	uint32_t row = gstate.getFrameBufAddress() + p.y * pixelID.cached.framebufStride * bpp;
	NotifyMemInfo(MemBlockFlags::WRITE, row + p.x * bpp, bpp, tag.c_str(), tag.size());

	if (pixelID.depthWrite) {
		std::string ztag = StringFromFormat("DisplayListPZ_%08x", state.listPC);
		row = gstate.getDepthBufAddress() + p.y * pixelID.cached.depthbufStride * 2;
		NotifyMemInfo(MemBlockFlags::WRITE, row + p.x * 2, 2, ztag.c_str(), ztag.size());
	}
#endif
}

void ClearRectangle(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state) {
	int entireX1 = std::min(v0.screenpos.x, v1.screenpos.x);
	int entireY1 = std::min(v0.screenpos.y, v1.screenpos.y);
	int entireX2 = std::max(v0.screenpos.x, v1.screenpos.x) - 1;
	int entireY2 = std::max(v0.screenpos.y, v1.screenpos.y) - 1;
	int minX = std::max(entireX1 & ~(SCREEN_SCALE_FACTOR - 1), range.x1) | (SCREEN_SCALE_FACTOR / 2 - 1);
	int minY = std::max(entireY1 & ~(SCREEN_SCALE_FACTOR - 1), range.y1) | (SCREEN_SCALE_FACTOR / 2 - 1);
	int maxX = std::min(entireX2, range.x2);
	int maxY = std::min(entireY2, range.y2);

	// If TL x or y was after the half, we don't draw the pixel.
	if (minX < entireX1 - 1)
		minX += SCREEN_SCALE_FACTOR;
	if (minY < entireY1 - 1)
		minY += SCREEN_SCALE_FACTOR;

	const DrawingCoords pprime = TransformUnit::ScreenToDrawing(minX, minY);
	// Only include the end pixel when it's >= 0.5.
	const DrawingCoords pend = TransformUnit::ScreenToDrawing(maxX - SCREEN_SCALE_FACTOR / 2, maxY - SCREEN_SCALE_FACTOR / 2);
	auto &pixelID = state.pixelID;
	auto &samplerID = state.samplerID;

	const int w = pend.x - pprime.x + 1;
	if (w <= 0)
		return;

	if (pixelID.DepthClear()) {
		const u16 z = v1.screenpos.z;
		const int stride = pixelID.cached.depthbufStride;

		// If both bytes of Z equal, we can just use memset directly which is faster.
		if ((z & 0xFF) == (z >> 8)) {
			DrawingCoords p = pprime;
			for (p.y = pprime.y; p.y <= pend.y; ++p.y) {
				u16 *row = depthbuf.Get16Ptr(p.x, p.y, stride);
				memset(row, z, w * 2);
			}
		} else {
			DrawingCoords p = pprime;
			for (p.y = pprime.y; p.y <= pend.y; ++p.y) {
				for (int x = 0; x < w; ++x) {
					SetPixelDepth(p.x + x, p.y, pixelID.cached.depthbufStride, z);
				}
			}
		}

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
		std::string tag = StringFromFormat("DisplayListXZ_%08x", state.listPC);
		for (int y = pprime.y; y <= pend.y; ++y) {
			uint32_t row = gstate.getDepthBufAddress() + y * pixelID.cached.depthbufStride * 2;
			NotifyMemInfo(MemBlockFlags::WRITE, row + pprime.x * 2, w * 2, tag.c_str(), tag.size());
		}
#endif
	}

	// Note: this stays 0xFFFFFFFF if keeping color and alpha, even for 16-bit.
	u32 keepOldMask = 0xFFFFFFFF;
	if (pixelID.ColorClear() && pixelID.StencilClear()) {
		keepOldMask = 0;
	} else {
		switch (pixelID.FBFormat()) {
		case GE_FORMAT_565:
			if (pixelID.ColorClear())
				keepOldMask = 0;
			break;

		case GE_FORMAT_5551:
			if (pixelID.ColorClear())
				keepOldMask = 0xFFFF8000;
			else if (pixelID.StencilClear())
				keepOldMask = 0xFFFF7FFF;
			break;

		case GE_FORMAT_4444:
			if (pixelID.ColorClear())
				keepOldMask = 0xFFFFF000;
			else if (pixelID.StencilClear())
				keepOldMask = 0xFFFF0FFF;
			break;

		case GE_FORMAT_8888:
		default:
			if (pixelID.ColorClear())
				keepOldMask = 0xFF000000;
			else if (pixelID.StencilClear())
				keepOldMask = 0x00FFFFFF;
			break;
		}
	}

	// The pixel write masks are respected in clear mode.
	if (pixelID.applyColorWriteMask) {
		keepOldMask |= pixelID.cached.colorWriteMask;
	}

	const u32 new_color = v1.color0;
	u16 new_color16;
	switch (pixelID.FBFormat()) {
	case GE_FORMAT_565:
		new_color16 = RGBA8888ToRGB565(new_color);
		break;

	case GE_FORMAT_5551:
		new_color16 = RGBA8888ToRGBA5551(new_color);
		break;

	case GE_FORMAT_4444:
		new_color16 = RGBA8888ToRGBA4444(new_color);
		break;

	case GE_FORMAT_8888:
		break;

	case GE_FORMAT_INVALID:
	case GE_FORMAT_DEPTH16:
	case GE_FORMAT_CLUT8:
		_dbg_assert_msg_(false, "Software: invalid framebuf format.");
		break;
	}

	if (keepOldMask == 0) {
		const int stride = pixelID.cached.framebufStride;

		if (pixelID.FBFormat() == GE_FORMAT_8888) {
			const bool canMemsetColor = (new_color & 0xFF) == (new_color >> 8) && (new_color & 0xFFFF) == (new_color >> 16);
			if (canMemsetColor) {
				DrawingCoords p = pprime;
				for (p.y = pprime.y; p.y <= pend.y; ++p.y) {
					u32 *row = fb.Get32Ptr(p.x, p.y, stride);
					memset(row, new_color, w * 4);
				}
			} else {
				DrawingCoords p = pprime;
				for (p.y = pprime.y; p.y <= pend.y; ++p.y) {
					for (int x = 0; x < w; ++x) {
						fb.Set32(p.x + x, p.y, stride, new_color);
					}
				}
			}
		} else {
			const bool canMemsetColor = (new_color16 & 0xFF) == (new_color16 >> 8);
			if (canMemsetColor) {
				DrawingCoords p = pprime;
				for (p.y = pprime.y; p.y <= pend.y; ++p.y) {
					u16 *row = fb.Get16Ptr(p.x, p.y, stride);
					memset(row, new_color16, w * 2);
				}
			} else {
				DrawingCoords p = pprime;
				for (p.y = pprime.y; p.y <= pend.y; ++p.y) {
					for (int x = 0; x < w; ++x) {
						fb.Set16(p.x + x, p.y, stride, new_color16);
					}
				}
			}
		}
	} else if (keepOldMask != 0xFFFFFFFF) {
		const int stride = pixelID.cached.framebufStride;

		if (pixelID.FBFormat() == GE_FORMAT_8888) {
			DrawingCoords p = pprime;
			for (p.y = pprime.y; p.y <= pend.y; ++p.y) {
				for (int x = 0; x < w; ++x) {
					const u32 old_color = fb.Get32(p.x + x, p.y, stride);
					const u32 c = (old_color & keepOldMask) | (new_color & ~keepOldMask);
					fb.Set32(p.x + x, p.y, stride, c);
				}
			}
		} else {
			DrawingCoords p = pprime;
			for (p.y = pprime.y; p.y <= pend.y; ++p.y) {
				for (int x = 0; x < w; ++x) {
					const u16 old_color = fb.Get16(p.x + x, p.y, stride);
					const u16 c = (old_color & keepOldMask) | (new_color16 & ~keepOldMask);
					fb.Set16(p.x + x, p.y, stride, c);
				}
			}
		}
	}

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	if (keepOldMask != 0xFFFFFFFF) {
		uint32_t bpp = pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
		std::string tag = StringFromFormat("DisplayListX_%08x", state.listPC);
		for (int y = pprime.y; y < pend.y; ++y) {
			uint32_t row = gstate.getFrameBufAddress() + y * pixelID.cached.framebufStride * bpp;
			NotifyMemInfo(MemBlockFlags::WRITE, row + pprime.x * bpp, w * bpp, tag.c_str(), tag.size());
		}
	}
#endif
}

// Which pixels a line lights, as the GE does it (gpu/probe exp72: one pixel of 320 random lines differs,
// a y-major line starting on a top corner): diamond exit. A pixel is lit when the line
// passes through the inside of its diamond |x - cx| + |y - cy| < 1/2 and doesn't end inside it (see
// InLineDiamond for points exactly on the edge). In 1/16 pixel units, exact.
struct LinePixel {
	int x, y;
	float t;  // where the pixel's center falls along the line, 0 to 1
};

// A point on a diamond's edge counts as inside on the top corner and the edges either side of it, for
// x-major lines; not on the left or right corner (gpu/probe exp149). Y-major lines swap x and y: the left
// corner and its edges.
static bool LineDiamondEdgeInside(int64_t dx, int64_t dy, bool yMajor) {
	if (yMajor)
		std::swap(dx, dy);
	return dy < 0;
}

static bool InLineDiamond(int64_t cx, int64_t cy, int64_t x, int64_t y, bool yMajor) {
	const int64_t dx = x - cx, dy = y - cy;
	const int64_t d = std::abs(dx) + std::abs(dy);
	if (d != SCREEN_SCALE_FACTOR / 2)
		return d < SCREEN_SCALE_FACTOR / 2;
	return LineDiamondEdgeInside(dx, dy, yMajor);
}

// Whether the line reaches the diamond's inside, or touches its boundary where InLineDiamond counts it
// as inside (a horizontal line along the top corners of a row, for example).
static bool LineCrossesDiamond(int64_t cx, int64_t cy, int64_t x0, int64_t y0, int64_t x1, int64_t y1, bool yMajor) {
	// Clip t in [0, 1] against the four half-planes sx (x - cx) + sy (y - cy) <= 8, as fractions lo..hi,
	// noting whether any of them is only met with equality.
	int64_t loN = 0, loD = 1, hiN = 1, hiD = 1;
	static const int signs[4][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
	for (const auto &s : signs) {
		const int64_t a = s[0] * (x1 - x0) + s[1] * (y1 - y0);
		const int64_t b = SCREEN_SCALE_FACTOR / 2 - (s[0] * (x0 - cx) + s[1] * (y0 - cy));
		if (a == 0) {
			if (b < 0)
				return false;
		} else if (a > 0) {
			if (b * hiD < hiN * a) {
				hiN = b;
				hiD = a;
			}
		} else if ((-b) * loD > loN * (-a)) {
			loN = -b;
			loD = -a;
		}
	}
	if (loN * hiD > hiN * loD)
		return false;
	// Classify the middle of the overlap: inside, or a boundary point that counts.
	const int64_t tn = loN * hiD + hiN * loD, td = 2 * loD * hiD;
	const int64_t dxN = (x0 - cx) * td + (x1 - x0) * tn;
	const int64_t dyN = (y0 - cy) * td + (y1 - y0) * tn;
	const int64_t dN = std::abs(dxN) + std::abs(dyN);
	if (dN < (SCREEN_SCALE_FACTOR / 2) * td)
		return true;
	return LineDiamondEdgeInside(dxN, dyN, yMajor);
}

static void LinePixels(int64_t x0, int64_t y0, int64_t x1, int64_t y1, std::vector<LinePixel> &out) {
	out.clear();
	if (x0 == x1 && y0 == y1)
		return;
	// A diagonal line (|dx| == |dy|) is y-major (gpu/probe exp137).
	const bool xMajor = std::abs(x1 - x0) > std::abs(y1 - y0);
	const int64_t a0 = xMajor ? x0 : y0, a1 = xMajor ? x1 : y1;
	const int64_t b0 = xMajor ? y0 : x0, b1 = xMajor ? y1 : x1;
	const int dir = a1 >= a0 ? 1 : -1;
	const int bdir = b1 >= b0 ? 1 : -1;
	const int64_t first = (a0 / SCREEN_SCALE_FACTOR) - dir, last = (a1 / SCREEN_SCALE_FACTOR) + dir;
	for (int64_t c = first; c != last + dir; c += dir) {
		const int64_t ac = c * SCREEN_SCALE_FACTOR + SCREEN_SCALE_FACTOR / 2;
		// The minor coordinate at this column's center, floored to a pixel.
		const int64_t num = b0 * (a1 - a0) + (ac - a0) * (b1 - b0);
		const int64_t den = a1 - a0;
		int64_t bc = num / den;
		if ((num % den != 0) && ((num < 0) != (den < 0)))
			bc--;
		const int64_t r = bc >= 0 ? bc / SCREEN_SCALE_FACTOR : -((-bc + SCREEN_SCALE_FACTOR - 1) / SCREEN_SCALE_FACTOR);
		for (int k = -1; k <= 1; ++k) {
			const int64_t rr = r + k * bdir;
			const int64_t px = xMajor ? c : rr, py = xMajor ? rr : c;
			const int64_t cx = px * SCREEN_SCALE_FACTOR + SCREEN_SCALE_FACTOR / 2;
			const int64_t cy = py * SCREEN_SCALE_FACTOR + SCREEN_SCALE_FACTOR / 2;
			if (InLineDiamond(cx, cy, x1, y1, !xMajor))
				continue;
			if (InLineDiamond(cx, cy, x0, y0, !xMajor) || LineCrossesDiamond(cx, cy, x0, y0, x1, y1, !xMajor)) {
				const float t = std::clamp((float)(ac - a0) / (float)(a1 - a0), 0.0f, 1.0f);
				out.push_back({ (int)px, (int)py, t });
			}
		}
	}
}

// Gouraud color along a line (gpu/probe exp137, exact): the gradient along the major axis comes from the
// setup reciprocal like a triangle plane's, with 14 fraction bits per subpixel and floored, and a pixel's value
// is the start color plus the gradient times the signed distance of its center from v0 along the line's
// direction, floored.
static int64_t LineFixedAt(int64_t c0, int64_t c1, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int px, int py) {
	const bool xMajor = std::abs(x1 - x0) > std::abs(y1 - y0);
	const int64_t a0 = xMajor ? x0 : y0, a1 = xMajor ? x1 : y1;
	const int64_t ac = (int64_t)(xMajor ? px : py) * SCREEN_SCALE_FACTOR + SCREEN_SCALE_FACTOR / 2;
	if (a1 == a0)
		return c1;
	int e;
	const int64_t q = GESetupRecip((uint64_t)std::abs(a1 - a0), &e);
	const int64_t k = ((c1 - c0) * q) >> (e + 2);
	const int64_t walk = a1 > a0 ? ac - a0 : a0 - ac;
	return ((c0 << 14) + k * walk) >> 14;
}

static int LineValueAt(int c0, int c1, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int px, int py, int maxValue) {
	return (int)std::clamp<int64_t>(LineFixedAt(c0, c1, x0, y0, x1, y1, px, py), 0, maxValue);
}

// Texture coordinates along a transform-mode line: s, t and q as for a triangle's planes (15-bit at the
// two vertices' largest exponent), along the line like its color, then u = s / q with the GE's reciprocal
// (gpu/texmtx/prims: a pixel just inside a line's end samples v just below 1, not the end vertex's 1.0).
struct LineUV {
	double s[2], t[2], q[2];
	int shiftS, shiftT, shiftQ;
	bool valid;
};

static int SharedShift2(const double v[2]) {
	const double v3[3] = { v[0], v[1], 0.0 };
	return SharedShift(v3);
}

static LineUV ComputeLineUV(const VertexData &v0, const VertexData &v1, bool textureProj) {
	LineUV uv{};
	const VertexData *v[2] = { &v0, &v1 };
	for (int i = 0; i < 2; ++i) {
		const float w24 = TruncateToFloat24(v[i]->clipw);
		if (!(w24 > 0.0f) || !std::isfinite(w24))
			return uv;
		const double r = GERecip(w24);
		uv.q[i] = textureProj ? ProductToFloat24((double)TruncateToFloat24(v[i]->texturecoords.q()) * r) : r;
		uv.s[i] = ProductToFloat24((double)TruncateToFloat24(v[i]->texturecoords.s()) * r);
		uv.t[i] = ProductToFloat24((double)TruncateToFloat24(v[i]->texturecoords.t()) * r);
	}
	uv.shiftS = SharedShift2(uv.s);
	uv.shiftT = SharedShift2(uv.t);
	uv.shiftQ = SharedShift2(uv.q);
	uv.valid = true;
	return uv;
}

static float LineUVComponentAt(const double c[2], int shift, const VertexData &v0, const VertexData &v1, int px, int py) {
	const int64_t c0 = (int64_t)std::ldexp(c[0], shift), c1 = (int64_t)std::ldexp(c[1], shift);
	const int64_t value = LineFixedAt(c0, c1, v0.screenpos.x, v0.screenpos.y, v1.screenpos.x, v1.screenpos.y, px, py);
	return TruncateToFloat24((float)std::ldexp((double)value, -shift));
}

static void LineTextureCoordinatesAt(const LineUV &uv, const VertexData &v0, const VertexData &v1, int px, int py, float &s, float &t, float &q) {
	q = LineUVComponentAt(uv.q, uv.shiftQ, v0, v1, px, py);
	if (!(q > 0.0f)) {
		s = 0.0f;
		t = 0.0f;
		return;
	}
	const double r = GERecip(q);
	s = GEUVProduct((double)LineUVComponentAt(uv.s, uv.shiftS, v0, v1, px, py) * r);
	t = GEUVProduct((double)LineUVComponentAt(uv.t, uv.shiftT, v0, v1, px, py) * r);
}

static int LineColorAt(int c0, int c1, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int px, int py) {
	return LineValueAt(c0, c1, x0, y0, x1, y1, px, py, 255);
}

void DrawLine(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state) {
	// TODO: Use a proper line drawing algorithm that handles fractional endpoints correctly.
	Vec3<int> a(v0.screenpos.x, v0.screenpos.y, v0.screenpos.z);
	Vec3<int> b(v1.screenpos.x, v1.screenpos.y, v1.screenpos.z);

	int dx = b.x - a.x;
	int dy = b.y - a.y;
	int dz = b.z - a.z;

	int steps;
	if (abs(dx) < abs(dy))
		steps = abs(dy) / SCREEN_SCALE_FACTOR;
	else
		steps = abs(dx) / SCREEN_SCALE_FACTOR;

	// Avoid going too far since we typically don't start at the pixel center.
	if (dx < 0 && dx >= -SCREEN_SCALE_FACTOR)
		dx++;
	if (dy < 0 && dy >= -SCREEN_SCALE_FACTOR)
		dy++;

	double xinc = (double)dx / steps;
	double yinc = (double)dy / steps;

	auto &pixelID = state.pixelID;
	auto &samplerID = state.samplerID;

	const bool interpolateColor = state.shadeGouraud && !(v0.color0 == v1.color0 && v0.color1 == v1.color1);
	const Vec4<int> v0_c0 = Vec4<int>::FromRGBA(v0.color0);
	const Vec4<int> v1_c0 = Vec4<int>::FromRGBA(v1.color0);
	const Vec3<int> v0_c1 = Vec3<int>::FromRGB(v0.color1);
	const Vec3<int> v1_c1 = Vec3<int>::FromRGB(v1.color1);

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	std::string tag = StringFromFormat("DisplayListL_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListLZ_%08x", state.listPC);
#endif

	const int steps1 = steps == 0 ? 1 : steps;
	const LineUV lineUV = state.enableTextures && !state.throughMode ? ComputeLineUV(v0, v1, state.textureProj) : LineUV{};
	static thread_local std::vector<LinePixel> pixels;
	LinePixels(a.x, a.y, b.x, b.y, pixels);
	for (const LinePixel &lp : pixels) {
		const int i = std::clamp((int)lroundf(lp.t * steps), 0, steps);
		const double x = lp.x * SCREEN_SCALE_FACTOR + SCREEN_SCALE_FACTOR / 2;
		const double y = lp.y * SCREEN_SCALE_FACTOR + SCREEN_SCALE_FACTOR / 2;
		// Depth the same way as Gouraud color, so an endpoint's pixel extrapolates to its center (Coded Arms'
		// short lines passed a GEQUAL test there).
		const int z = LineValueAt(a.z, b.z, a.x, a.y, b.x, b.y, lp.x, lp.y, 65535);
		DrawingCoords p = TransformUnit::ScreenToDrawing(x, y);

		bool maskOK = x >= range.x1 && y >= range.y1 && x <= range.x2 && y <= range.y2;
		if (maskOK) {
			if (pixelID.earlyZChecks) {
				if (pixelID.applyDepthRange) {
					if (z < pixelID.cached.minz || z > pixelID.cached.maxz)
						maskOK = false;
				}

				if (!CheckDepthTestPassed(pixelID.DepthTestFunc(), p.x, p.y, pixelID.cached.depthbufStride, z)) {
					maskOK = false;
				}
			}
		}

		if (maskOK) {
			// Interpolate between the two points.
			Vec4<int> prim_color;
			Vec3<int> sec_color;
			if (interpolateColor) {
				for (int c = 0; c < 4; ++c) {
					prim_color[c] = LineColorAt(v0_c0[c], v1_c0[c], a.x, a.y, b.x, b.y, lp.x, lp.y);
					if (c < 3)
						sec_color[c] = LineColorAt(v0_c1[c], v1_c1[c], a.x, a.y, b.x, b.y, lp.x, lp.y);
				}
			} else {
				prim_color = v1_c0;
				sec_color = v1_c1;
			}

			u8 fog = 255;
			if (pixelID.applyFog) {
				// steps1, so a line shorter than a pixel keeps v0's fog rather than none (SOCOM's radar).
				fog = ClampFogDepth((v0.fogdepth * (float)(steps1 - i) + v1.fogdepth * (float)i) / steps1);
			}

			if (state.antialiasLines) {
				// TODO: Clearmode?
				prim_color.a() = GELineCoverageAlpha(a.x, a.y, b.x, b.y, lp.x, lp.y);
			}

			if (state.enableTextures) {
				float s, s1;
				float t, t1;
				if (state.throughMode) {
					Vec2<float> tc = (v0.texturecoords.uv() * (float)(steps1 - i) + v1.texturecoords.uv() * (float)i) / steps1;
					Vec2<float> tc1 = (v0.texturecoords.uv() * (float)(steps1 - i - 1) + v1.texturecoords.uv() * (float)(i + 1)) / steps1;

					s = tc.s() * (1.0f / (float)(1 << state.samplerID.width0Shift));
					s1 = tc1.s() * (1.0f / (float)(1 << state.samplerID.width0Shift));
					t = tc.t() * (1.0f / (float)(1 << state.samplerID.height0Shift));
					t1 = tc1.t() * (1.0f / (float)(1 << state.samplerID.height0Shift));
				} else if (lineUV.valid) {
					float q, q1;
					LineTextureCoordinatesAt(lineUV, v0, v1, lp.x, lp.y, s, t, q);
					// The next pixel along the major axis, for the derivatives below.
					const bool xMajor = std::abs(dx) > std::abs(dy);
					const int stepX = xMajor ? (dx >= 0 ? 1 : -1) : 0, stepY = xMajor ? 0 : (dy >= 0 ? 1 : -1);
					LineTextureCoordinatesAt(lineUV, v0, v1, lp.x + stepX, lp.y + stepY, s1, t1, q1);
				} else if (state.textureProj) {
					GetTextureCoordinatesProj(v0, v1, (float)(steps1 - i) / steps1, s, t);
					GetTextureCoordinatesProj(v0, v1, (float)(steps1 - i - 1) / steps1, s1, t1);
				} else {
					// Texture coordinate interpolation must definitely be perspective-correct.
					GetTextureCoordinates(v0, v1, (float)(steps1 - i) / steps1, s, t);
					GetTextureCoordinates(v0, v1, (float)(steps1 - i - 1) / steps1, s1, t1);
				}

				// If inc is 0, force the delta to zero.
				float ds = xinc == 0.0 ? 0.0f : (s1 - s) * (float)SCREEN_SCALE_FACTOR * (1.0f / xinc);
				float dt = yinc == 0.0 ? 0.0f : (t1 - t) * (float)SCREEN_SCALE_FACTOR * (1.0f / yinc);
				float w = (v0.clipw * (float)(steps - i) + v1.clipw * (float)i) / steps1;

				int texLevel;
				int texLevelFrac;
				bool texBilinear;
				CalculateSamplingParams(ds, dt, 1.0f / w, state, texLevel, texLevelFrac, texBilinear);

				if (state.antialiasLines) {
					// TODO: This is a naive and wrong implementation.
					DrawingCoords p0 = TransformUnit::ScreenToDrawing(x, y);
					s = ((float)p0.x + xinc / 32.0f) / 512.0f;
					t = ((float)p0.y + yinc / 32.0f) / 512.0f;

					texBilinear = true;
				}

				PROFILE_THIS_SCOPE("sampler");
				prim_color = ApplyTexturingSingle(s, t, ToVec4IntArg(prim_color), texLevel, texLevelFrac, texBilinear, state);
			}

			if (!pixelID.clearMode)
				prim_color += Vec4<int>(sec_color, 0);

			PROFILE_THIS_SCOPE("draw_px");
			state.drawPixel(p.x, p.y, z, fog, ToVec4IntArg(prim_color), pixelID);

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
			uint32_t bpp = pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
			uint32_t row = gstate.getFrameBufAddress() + p.y * pixelID.cached.framebufStride * bpp;
			NotifyMemInfo(MemBlockFlags::WRITE, row + p.x * bpp, bpp, tag.c_str(), tag.size());

			if (pixelID.depthWrite) {
				uint32_t row = gstate.getDepthBufAddress() + y * pixelID.cached.depthbufStride * 2;
				NotifyMemInfo(MemBlockFlags::WRITE, row + p.x * 2, 2, ztag.c_str(), ztag.size());
			}
#endif
		}

	}
}

bool GetCurrentTexture(GPUDebugBuffer &buffer, int level)
{
	if (!gstate.isTextureMapEnabled()) {
		return false;
	}

	GETextureFormat texfmt = gstate.getTextureFormat();
	u32 texaddr = gstate.getTextureAddress(level);
	u32 texbufw = GetTextureBufw(level, texaddr, texfmt);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	u32 sizeInBits = textureBitsPerPixel[texfmt] * (texbufw * (h - 1) + w);
	if (!texaddr || !Memory::IsValidRange(texaddr, sizeInBits / 8))
		return false;
	// We'll break trying to allocate this much.
	if (w >= 0x8000 && h >= 0x8000)
		return false;

	buffer.Allocate(w, h, GE_FORMAT_8888, false);

	SamplerID id;
	ComputeSamplerID(&id);
	id.cached.clut = clut;

	// Slight annoyance, we may have to force a compile.
	Sampler::FetchFunc sampler = Sampler::GetFetchFunc(id, nullptr);
	if (!sampler) {
		Sampler::FlushJit();
		sampler = Sampler::GetFetchFunc(id, nullptr);
		if (!sampler)
			return false;
	}

	u8 *texptr = Memory::GetPointerWriteOrException(texaddr);
	u32 *row = (u32 *)buffer.GetData();
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			row[x] = Vec4<int>(sampler(x, y, texptr, texbufw, level, id)).ToRGBA();
		}
		row += w;
	}
	return true;
}

} // namespace
