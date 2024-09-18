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
#include <cmath>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"

#if defined(_M_SSE)
#include <emmintrin.h>
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
			if (Memory::IsValidAddress(texaddr))
				state->texptr[i] = Memory::GetPointerUnchecked(texaddr);
			else
				state->texptr[i] = nullptr;
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
	if (gpuDebug)
		gpuDebug->GetCurrentDisplayList(currentList);
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
	if (!(v0.fogdepth >= 1.0f))
		state->flags |= RasterizerStateFlags::VERTEX_HAS_FOG;
}

void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0) {
	CalculateRasterStateFlags(state, v0, true);
}

void CalculateRasterStateFlags(RasterizerState *state, const VertexData &v0, const VertexData &v1, bool forceFlat) {
	CalculateRasterStateFlags(state, v0, !forceFlat && state->shadeGouraud);
	CalculateRasterStateFlags(state, v1, true);
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
	switch (samplerID.ClutFmt()) {
	case GE_CMODE_16BIT_BGR5650:
		break;

	case GE_CMODE_16BIT_ABGR5551:
		onlyFull = (alphaSum & 0x8000) != 0;
		break;

	case GE_CMODE_16BIT_ABGR4444:
		onlyFull = (alphaSum & 0xF000) == 0xF000;
		break;

	case GE_CMODE_32BIT_ABGR8888:
		onlyFull = (alphaSum & 0xFF000000) == 0xFF000000;
		break;
	}

	// Might just be different patterns, but if alphaSum != 0, it can't contain zero.
	if (alphaSum != 0)
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
				if (!pixelID.depthWrite && !pixelID.stencilTest && couldHaveZeroTexAlpha)
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
		return vertex.x < line1.x + (line2.x - line1.x) * (vertex.y - line1.y) / (line2.y - line1.y);
	}
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

// Produces a signed 1.27.4 value.
static int TexLog2(float delta) {
	union FloatBits {
		float f;
		u32 u;
	};
	FloatBits f;
	f.f = delta;
	// Use the exponent as the tex level, and the top mantissa bits for a frac.
	// We can't support more than 4 bits of frac, so truncate.
	int useful = (f.u >> 19) & 0x0FFF;
	// Now offset so the exponent aligns with log2f (exp=127 is 0.)
	return useful - 127 * 16;
}

static inline void CalculateSamplingParams(const float ds, const float dt, float w, const RasterizerState &state, int &level, int &levelFrac, bool &filt) {
	const int width = 1 << state.samplerID.width0Shift;
	const int height = 1 << state.samplerID.height0Shift;

	// With 8 bits of fraction (because texslope can be fairly precise.)
	int detail;
	switch (state.TexLevelMode()) {
	case GE_TEXLEVEL_MODE_AUTO:
		detail = TexLog2(std::max(std::abs(ds * width), std::abs(dt * height)));
		break;
	case GE_TEXLEVEL_MODE_SLOPE:
		// This is always offset by an extra texlevel.
		detail = TexLog2(2.0f * w * state.textureLodSlope);
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

static inline void ApplyTexturing(const RasterizerState &state, Vec4<int> *prim_color, const Vec4<int> &mask, const Vec4<float> &s, const Vec4<float> &t, float w) {
	float ds = s[1] - s[0];
	float dt = t[2] - t[0];

	int level;
	int levelFrac;
	bool bilinear;
	CalculateSamplingParams(ds, dt, w, state, level, levelFrac, bilinear);

	PROFILE_THIS_SCOPE("sampler");
	for (int i = 0; i < 4; ++i) {
		if (mask[i] >= 0)
			prim_color[i] = ApplyTexturing(s[i], t[i], ToVec4IntArg(prim_color[i]), level, levelFrac, bilinear, state);
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
	// Start at pixel centers.
	static constexpr int centerOff = (SCREEN_SCALE_FACTOR / 2) - 1;
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
	const bool noFog = clearMode || !pixelID.applyFog || (v0.fogdepth >= 1.0f && v1.fogdepth >= 1.0f && v2.fogdepth >= 1.0f);

	if (pixelID.applyDepthRange && flatZ) {
		if (v0.screenpos.z < pixelID.cached.minz || v0.screenpos.z > pixelID.cached.maxz)
			return;
	}

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	uint32_t bpp = pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	std::string tag = StringFromFormat("DisplayListT_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListTZ_%08x", state.listPC);
#endif

	const Vec4<int> v0_c0 = Vec4<int>::FromRGBA(v0.color0);
	const Vec4<int> v1_c0 = Vec4<int>::FromRGBA(v1.color0);
	const Vec4<int> v2_c0 = Vec4<int>::FromRGBA(v2.color0);
	const Vec3<int> v0_c1 = Vec3<int>::FromRGB(v0.color1);
	const Vec3<int> v1_c1 = Vec3<int>::FromRGB(v1.color1);
	const Vec3<int> v2_c1 = Vec3<int>::FromRGB(v2.color1);

	const Vec4<float> v0_z4 = Vec4<int>::AssignToAll(v0.screenpos.z).Cast<float>();
	const Vec4<float> v1_z4 = Vec4<int>::AssignToAll(v1.screenpos.z).Cast<float>();
	const Vec4<float> v2_z4 = Vec4<int>::AssignToAll(v2.screenpos.z).Cast<float>();
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
		e0.NarrowMinMaxX(w0, minX, rowMinX, rowMaxX);
		e1.NarrowMinMaxX(w1, minX, rowMinX, rowMaxX);
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
			if (AnyMask<useSSE4>(mask)) {
				Vec4<int> z;
				if (flatZ) {
					z = Vec4<int>::AssignToAll(v2.screenpos.z);
				} else {
					// Z is interpolated pretty much directly.
					Vec4<float> zfloats = w0.Cast<float>() * v0_z4 + w1.Cast<float>() * v1_z4 + w2.Cast<float>() * v2_z4;
					z = (zfloats * wsum_recip).Cast<int>();
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
				Vec4<int> prim_color[4];
				if (!flatColor0) {
					for (int i = 0; i < 4; ++i) {
						if (mask[i] >= 0)
							prim_color[i] = Interpolate(v0_c0, v1_c0, v2_c0, w0[i], w1[i], w2[i], wsum_recip[i]);
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
							sec_color[i] = Interpolate(v0_c1, v1_c1, v2_c1, w0[i], w1[i], w2[i], wsum_recip[i]);
					}
				} else {
					for (int i = 0; i < 4; ++i) {
						sec_color[i] = v2_c1;
					}
				}

				if (state.enableTextures) {
					if constexpr (!clearMode) {
						Vec4<float> s, t;
						if (state.throughMode) {
							s = Interpolate(v0.texturecoords.s(), v1.texturecoords.s(), v2.texturecoords.s(), w0, w1,
											w2, wsum_recip);
							t = Interpolate(v0.texturecoords.t(), v1.texturecoords.t(), v2.texturecoords.t(), w0, w1,
											w2, wsum_recip);

							// For levels > 0, mipmapping is always based on level 0.  Simpler to scale first.
							s *= 1.0f / (float) (1 << state.samplerID.width0Shift);
							t *= 1.0f / (float) (1 << state.samplerID.height0Shift);
						} else if (state.textureProj) {
							// Texture coordinate interpolation must definitely be perspective-correct.
							GetTextureCoordinatesProj(v0, v1, v2, w0, w1, w2, wsum_recip, s, t);
						} else {
							// Texture coordinate interpolation must definitely be perspective-correct.
							GetTextureCoordinates(v0, v1, v2, w0, w1, w2, wsum_recip, s, t);
						}

						if (state.TexLevelMode() == GE_TEXLEVEL_MODE_SLOPE) {
							// Not sure what's right, but we need one value for the slope.
							float clipw = (v0.clipw * w0.x + v1.clipw * w1.x + v2.clipw * w2.x) * wsum_recip.x;
							ApplyTexturing(state, prim_color, mask, s, t, clipw);
						} else {
							ApplyTexturing(state, prim_color, mask, s, t, 0.0f);
						}
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
					Vec4<float> fogdepths = w0.Cast<float>() * v0.fogdepth + w1.Cast<float>() * v1.fogdepth + w2.Cast<float>() * v2.fogdepth;
					fogdepths = fogdepths * wsum_recip;
					for (int i = 0; i < 4; ++i) {
						fog[i] = ClampFogDepth(fogdepths[i]);
					}
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
	int minX = std::max(entireX1 & ~(SCREEN_SCALE_FACTOR - 1), range.x1) | (SCREEN_SCALE_FACTOR / 2 - 1);
	int minY = std::max(entireY1 & ~(SCREEN_SCALE_FACTOR - 1), range.y1) | (SCREEN_SCALE_FACTOR / 2 - 1);
	int maxX = std::min(entireX2, range.x2);
	int maxY = std::min(entireY2, range.y2);

	// If TL x or y was after the half, we don't draw the pixel.
	// TODO: Verify what center is used, allowing slight offset makes gpu/primitives/trianglefan pass.
	if (minX < entireX1 - 1)
		minX += SCREEN_SCALE_FACTOR;
	if (minY < entireY1 - 1)
		minY += SCREEN_SCALE_FACTOR;

	RasterizerState state = OptimizeFlatRasterizerState(rastState, v1);

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
	}

	// And now what we add to spread out to 4 values.
	const Vec4f sto4(0.0f, 0.5f * stx.s(), 0.5f * sty.s(), 0.5f * stx.s() + 0.5f * sty.s());
	const Vec4f tto4(0.0f, 0.5f * stx.t(), 0.5f * sty.t(), 0.5f * stx.t() + 0.5f * sty.t());

	ScreenCoords pprime(minX, minY, 0);
	const Vec4<int> fog = Vec4<int>::AssignToAll(ClampFogDepth(v1.fogdepth));
	const Vec4<int> z = Vec4<int>::AssignToAll(v1.screenpos.z);
	const Vec4<int> c0 = Vec4<int>::FromRGBA(v1.color0);
	const Vec3<int> sec_color = Vec3<int>::FromRGB(v1.color1);

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

	for (int64_t curY = minY; curY < maxY; curY += SCREEN_SCALE_FACTOR * 2, rowST += sty) {
		DrawingCoords p = TransformUnit::ScreenToDrawing(minX, curY);

		int scissorY2 = curY + SCREEN_SCALE_FACTOR > maxY ? -1 : 0;
		Vec4<int> scissor_mask = Vec4<int>(0, maxX - minX - SCREEN_SCALE_FACTOR, scissorY2, (maxX - minX - SCREEN_SCALE_FACTOR) | scissorY2);
		Vec4<int> scissor_step = Vec4<int>(0, -(SCREEN_SCALE_FACTOR * 2), 0, -(SCREEN_SCALE_FACTOR * 2));
		Vec2f st = rowST;

		for (int64_t curX = minX; curX < maxX; curX += SCREEN_SCALE_FACTOR * 2,
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
				s = Vec4<float>::AssignToAll(st.s()) + sto4;
				t = Vec4<float>::AssignToAll(st.t()) + tto4;

				ApplyTexturing(state, prim_color, mask, s, t, v1.clipw);
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
			s *= 1.0f / (float)(1 << state.samplerID.width0Shift);
			t *= 1.0f / (float)(1 << state.samplerID.height0Shift);
		} else if (state.textureProj) {
			GetTextureCoordinatesProj(v0, v0, 0.0f, s, t);
		} else {
			// Texture coordinate interpolation must definitely be perspective-correct.
			GetTextureCoordinates(v0, v0, 0.0f, s, t);
		}

		int texLevel;
		int texLevelFrac;
		bool bilinear;
		CalculateSamplingParams(0.0f, 0.0f, v0.clipw, state, texLevel, texLevelFrac, bilinear);
		PROFILE_THIS_SCOPE("sampler");
		prim_color = ApplyTexturingSingle(s, t, ToVec4IntArg(prim_color), texLevel, texLevelFrac, bilinear, state);
	}

	if (!pixelID.clearMode) {
		Vec3<int> sec_color = Vec3<int>::FromRGB(v0.color1);
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
	double zinc = (double)dz / steps;

	auto &pixelID = state.pixelID;
	auto &samplerID = state.samplerID;

	const bool interpolateColor = !state.shadeGouraud || (v0.color0 == v1.color0 && v0.color1 == v1.color1);
	const Vec4<int> v0_c0 = Vec4<int>::FromRGBA(v0.color0);
	const Vec4<int> v1_c0 = Vec4<int>::FromRGBA(v1.color0);
	const Vec3<int> v0_c1 = Vec3<int>::FromRGB(v0.color1);
	const Vec3<int> v1_c1 = Vec3<int>::FromRGB(v1.color1);

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	std::string tag = StringFromFormat("DisplayListL_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListLZ_%08x", state.listPC);
#endif

	double x = a.x > b.x ? a.x - 1 : a.x;
	double y = a.y > b.y ? a.y - 1 : a.y;
	double z = a.z;
	const int steps1 = steps == 0 ? 1 : steps;
	for (int i = 0; i < steps; i++) {
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
				prim_color = (v0_c0 * (steps - i) + v1_c0 * i) / steps1;
				sec_color = (v0_c1 * (steps - i) + v1_c1 * i) / steps1;
			} else {
				prim_color = v1_c0;
				sec_color = v1_c1;
			}

			u8 fog = 255;
			if (pixelID.applyFog) {
				fog = ClampFogDepth((v0.fogdepth * (float)(steps - i) + v1.fogdepth * (float)i) / steps1);
			}

			if (state.antialiasLines) {
				// TODO: Clearmode?
				// TODO: Calculate.
				prim_color.a() = 0x7F;
			}

			if (state.enableTextures) {
				float s, s1;
				float t, t1;
				if (state.throughMode) {
					Vec2<float> tc = (v0.texturecoords.uv() * (float)(steps - i) + v1.texturecoords.uv() * (float)i) / steps1;
					Vec2<float> tc1 = (v0.texturecoords.uv() * (float)(steps - i - 1) + v1.texturecoords.uv() * (float)(i + 1)) / steps1;

					s = tc.s() * (1.0f / (float)(1 << state.samplerID.width0Shift));
					s1 = tc1.s() * (1.0f / (float)(1 << state.samplerID.width0Shift));
					t = tc.t() * (1.0f / (float)(1 << state.samplerID.height0Shift));
					t1 = tc1.t() * (1.0f / (float)(1 << state.samplerID.height0Shift));
				} else if (state.textureProj) {
					GetTextureCoordinatesProj(v0, v1, (float)(steps - i) / steps1, s, t);
					GetTextureCoordinatesProj(v0, v1, (float)(steps - i - 1) / steps1, s1, t1);
				} else {
					// Texture coordinate interpolation must definitely be perspective-correct.
					GetTextureCoordinates(v0, v1, (float)(steps - i) / steps1, s, t);
					GetTextureCoordinates(v0, v1, (float)(steps - i - 1) / steps1, s1, t1);
				}

				// If inc is 0, force the delta to zero.
				float ds = xinc == 0.0 ? 0.0f : (s1 - s) * (float)SCREEN_SCALE_FACTOR * (1.0f / xinc);
				float dt = yinc == 0.0 ? 0.0f : (t1 - t) * (float)SCREEN_SCALE_FACTOR * (1.0f / yinc);
				float w = (v0.clipw * (float)(steps - i) + v1.clipw * (float)i) / steps1;

				int texLevel;
				int texLevelFrac;
				bool texBilinear;
				CalculateSamplingParams(ds, dt, w, state, texLevel, texLevelFrac, texBilinear);

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

		x += xinc;
		y += yinc;
		z += zinc;
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

	u8 *texptr = Memory::GetPointerWrite(texaddr);
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
