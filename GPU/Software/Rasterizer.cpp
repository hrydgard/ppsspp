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
#include "Core/Reporting.h"
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
static inline __m128 Interpolate(const __m128 &c0, const __m128 &c1, const __m128 &c2, int w0, int w1, int w2, float wsum) {
	__m128 v = _mm_mul_ps(c0, _mm_cvtepi32_ps(_mm_set1_epi32(w0)));
	v = _mm_add_ps(v, _mm_mul_ps(c1, _mm_cvtepi32_ps(_mm_set1_epi32(w1))));
	v = _mm_add_ps(v, _mm_mul_ps(c2, _mm_cvtepi32_ps(_mm_set1_epi32(w2))));
	return _mm_mul_ps(v, _mm_set_ps1(wsum));
}

static inline __m128i Interpolate(const __m128i &c0, const __m128i &c1, const __m128i &c2, int w0, int w1, int w2, float wsum) {
	return _mm_cvtps_epi32(Interpolate(_mm_cvtepi32_ps(c0), _mm_cvtepi32_ps(c1), _mm_cvtepi32_ps(c2), w0, w1, w2, wsum));
}
#endif

// NOTE: When not casting color0 and color1 to float vectors, this code suffers from severe overflow issues.
// Not sure if that should be regarded as a bug or if casting to float is a valid fix.

static inline Vec4<int> Interpolate(const Vec4<int> &c0, const Vec4<int> &c1, const Vec4<int> &c2, int w0, int w1, int w2, float wsum) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return Vec4<int>(Interpolate(c0.ivec, c1.ivec, c2.ivec, w0, w1, w2, wsum));
#else
	return ((c0.Cast<float>() * w0 + c1.Cast<float>() * w1 + c2.Cast<float>() * w2) * wsum).Cast<int>();
#endif
}

static inline Vec3<int> Interpolate(const Vec3<int> &c0, const Vec3<int> &c1, const Vec3<int> &c2, int w0, int w1, int w2, float wsum) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return Vec3<int>(Interpolate(c0.ivec, c1.ivec, c2.ivec, w0, w1, w2, wsum));
#else
	return ((c0.Cast<float>() * w0 + c1.Cast<float>() * w1 + c2.Cast<float>() * w2) * wsum).Cast<int>();
#endif
}

static inline Vec2<float> Interpolate(const Vec2<float> &c0, const Vec2<float> &c1, const Vec2<float> &c2, int w0, int w1, int w2, float wsum) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return Vec2<float>(Interpolate(c0.vec, c1.vec, c2.vec, w0, w1, w2, wsum));
#else
	return (c0 * w0 + c1 * w1 + c2 * w2) * wsum;
#endif
}

static inline Vec4<float> Interpolate(const float &c0, const float &c1, const float &c2, const Vec4<float> &w0, const Vec4<float> &w1, const Vec4<float> &w2, const Vec4<float> &wsum_recip) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128 v = _mm_mul_ps(w0.vec, _mm_set1_ps(c0));
	v = _mm_add_ps(v, _mm_mul_ps(w1.vec, _mm_set1_ps(c1)));
	v = _mm_add_ps(v, _mm_mul_ps(w2.vec, _mm_set1_ps(c2)));
	return _mm_mul_ps(v, wsum_recip.vec);
#else
	return (w0 * c0 + w1 * c1 + w2 * c2) * wsum_recip;
#endif
}

static inline Vec4<float> Interpolate(const float &c0, const float &c1, const float &c2, const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2, const Vec4<float> &wsum_recip) {
	return Interpolate(c0, c1, c2, w0.Cast<float>(), w1.Cast<float>(), w2.Cast<float>(), wsum_recip);
}

void ComputeRasterizerState(RasterizerState *state) {
	ComputePixelFuncID(&state->pixelID);
	state->drawPixel = Rasterizer::GetSingleFunc(state->pixelID);

	state->enableTextures = gstate.isTextureMapEnabled() && !state->pixelID.clearMode;
	if (state->enableTextures) {
		ComputeSamplerID(&state->samplerID);
		state->linear = Sampler::GetLinearFunc(state->samplerID);
		state->nearest = Sampler::GetNearestFunc(state->samplerID);

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
			state->texbufw[i] = GetTextureBufw(i, texaddr, texfmt);
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
	}

	state->shadeGouraud = gstate.getShadeMode() == GE_SHADE_GOURAUD;
	state->throughMode = gstate.isModeThrough();
	state->antialiasLines = gstate.isAntiAliasEnabled();

	state->screenOffsetX = gstate.getOffsetX16();
	state->screenOffsetY = gstate.getOffsetY16();

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	DisplayList currentList{};
	if (gpuDebug)
		gpuDebug->GetCurrentDisplayList(currentList);
	state->listPC = currentList.pc;
#endif
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
	// All UV gen modes, by the time they get here, behave the same.

	// TODO: What happens if vertex has no texture coordinates?
	// Note that for environment mapping, texture coordinates have been calculated during lighting
	float q0 = 1.f / v0.clippos.w;
	float q1 = 1.f / v1.clippos.w;
	float wq0 = p * q0;
	float wq1 = (1.0f - p) * q1;

	float q_recip = 1.0f / (wq0 + wq1);
	s = (v0.texturecoords.s() * wq0 + v1.texturecoords.s() * wq1) * q_recip;
	t = (v0.texturecoords.t() * wq0 + v1.texturecoords.t() * wq1) * q_recip;
}

static inline void GetTextureCoordinates(const VertexData &v0, const VertexData &v1, const VertexData &v2, const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2, const Vec4<float> &wsum_recip, Vec4<float> &s, Vec4<float> &t) {
	// All UV gen modes, by the time they get here, behave the same.

	// TODO: What happens if vertex has no texture coordinates?
	// Note that for environment mapping, texture coordinates have been calculated during lighting.
	float q0 = 1.f / v0.clippos.w;
	float q1 = 1.f / v1.clippos.w;
	float q2 = 1.f / v2.clippos.w;
	Vec4<float> wq0 = w0.Cast<float>() * q0;
	Vec4<float> wq1 = w1.Cast<float>() * q1;
	Vec4<float> wq2 = w2.Cast<float>() * q2;

	Vec4<float> q_recip = (wq0 + wq1 + wq2).Reciprocal();
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

static inline Vec3<int> GetSourceFactor(PixelBlendFactor factor, const Vec4<int> &source, const Vec4<int> &dst, uint32_t fix) {
	switch (factor) {
	case PixelBlendFactor::OTHERCOLOR:
		return dst.rgb();

	case PixelBlendFactor::INVOTHERCOLOR:
		return Vec3<int>::AssignToAll(255) - dst.rgb();

	case PixelBlendFactor::SRCALPHA:
#if defined(_M_SSE)
		return Vec3<int>(_mm_shuffle_epi32(source.ivec, _MM_SHUFFLE(3, 3, 3, 3)));
#else
		return Vec3<int>::AssignToAll(source.a());
#endif

	case PixelBlendFactor::INVSRCALPHA:
#if defined(_M_SSE)
		return Vec3<int>(_mm_sub_epi32(_mm_set1_epi32(255), _mm_shuffle_epi32(source.ivec, _MM_SHUFFLE(3, 3, 3, 3))));
#else
		return Vec3<int>::AssignToAll(255 - source.a());
#endif

	case PixelBlendFactor::DSTALPHA:
		return Vec3<int>::AssignToAll(dst.a());

	case PixelBlendFactor::INVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - dst.a());

	case PixelBlendFactor::DOUBLESRCALPHA:
		return Vec3<int>::AssignToAll(2 * source.a());

	case PixelBlendFactor::DOUBLEINVSRCALPHA:
		return Vec3<int>::AssignToAll(255 - std::min(2 * source.a(), 255));

	case PixelBlendFactor::DOUBLEDSTALPHA:
		return Vec3<int>::AssignToAll(2 * dst.a());

	case PixelBlendFactor::DOUBLEINVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - std::min(2 * dst.a(), 255));

	case PixelBlendFactor::FIX:
	default:
		// All other dest factors (> 10) are treated as FIXA.
		return Vec3<int>::FromRGB(fix);

	case PixelBlendFactor::ZERO:
		return Vec3<int>::AssignToAll(0);

	case PixelBlendFactor::ONE:
		return Vec3<int>::AssignToAll(255);
	}
}

static inline Vec3<int> GetDestFactor(PixelBlendFactor factor, const Vec4<int> &source, const Vec4<int> &dst, uint32_t fix) {
	switch (factor) {
	case PixelBlendFactor::OTHERCOLOR:
		return source.rgb();

	case PixelBlendFactor::INVOTHERCOLOR:
		return Vec3<int>::AssignToAll(255) - source.rgb();

	case PixelBlendFactor::SRCALPHA:
#if defined(_M_SSE)
		return Vec3<int>(_mm_shuffle_epi32(source.ivec, _MM_SHUFFLE(3, 3, 3, 3)));
#else
		return Vec3<int>::AssignToAll(source.a());
#endif

	case PixelBlendFactor::INVSRCALPHA:
#if defined(_M_SSE)
		return Vec3<int>(_mm_sub_epi32(_mm_set1_epi32(255), _mm_shuffle_epi32(source.ivec, _MM_SHUFFLE(3, 3, 3, 3))));
#else
		return Vec3<int>::AssignToAll(255 - source.a());
#endif

	case PixelBlendFactor::DSTALPHA:
		return Vec3<int>::AssignToAll(dst.a());

	case PixelBlendFactor::INVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - dst.a());

	case PixelBlendFactor::DOUBLESRCALPHA:
		return Vec3<int>::AssignToAll(2 * source.a());

	case PixelBlendFactor::DOUBLEINVSRCALPHA:
		return Vec3<int>::AssignToAll(255 - std::min(2 * source.a(), 255));

	case PixelBlendFactor::DOUBLEDSTALPHA:
		return Vec3<int>::AssignToAll(2 * dst.a());

	case PixelBlendFactor::DOUBLEINVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - std::min(2 * dst.a(), 255));

	case PixelBlendFactor::FIX:
	default:
		// All other dest factors (> 10) are treated as FIXB.
		return Vec3<int>::FromRGB(fix);

	case PixelBlendFactor::ZERO:
		return Vec3<int>::AssignToAll(0);

	case PixelBlendFactor::ONE:
		return Vec3<int>::AssignToAll(255);
	}
}

// Removed inline here - it was never chosen to be inlined by the compiler anyway, too complex.
Vec3<int> AlphaBlendingResult(const PixelFuncID &pixelID, const Vec4<int> &source, const Vec4<int> &dst) {
	// Note: These factors cannot go below 0, but they can go above 255 when doubling.
	Vec3<int> srcfactor = GetSourceFactor(pixelID.AlphaBlendSrc(), source, dst, pixelID.cached.alphaBlendSrc);
	Vec3<int> dstfactor = GetDestFactor(pixelID.AlphaBlendDst(), source, dst, pixelID.cached.alphaBlendDst);

	switch (pixelID.AlphaBlendEq()) {
	case GE_BLENDMODE_MUL_AND_ADD:
	{
#if defined(_M_SSE)
		// We switch to 16 bit to use mulhi, and we use 4 bits of decimal to make the 16 bit shift free.
		const __m128i half = _mm_set1_epi16(1 << 3);

		const __m128i srgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(source.ivec, source.ivec), 4), half);
		const __m128i sf = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(srcfactor.ivec, srcfactor.ivec), 4), half);
		const __m128i s = _mm_mulhi_epi16(srgb, sf);

		const __m128i drgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dst.ivec, dst.ivec), 4), half);
		const __m128i df = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dstfactor.ivec, dstfactor.ivec), 4), half);
		const __m128i d = _mm_mulhi_epi16(drgb, df);

		return Vec3<int>(_mm_unpacklo_epi16(_mm_adds_epi16(s, d), _mm_setzero_si128()));
#else
		Vec3<int> half = Vec3<int>::AssignToAll(1);
		Vec3<int> lhs = ((source.rgb() * 2 + half) * (srcfactor * 2 + half)) / 1024;
		Vec3<int> rhs = ((dst.rgb() * 2 + half) * (dstfactor * 2 + half)) / 1024;
		return lhs + rhs;
#endif
	}

	case GE_BLENDMODE_MUL_AND_SUBTRACT:
	{
#if defined(_M_SSE)
		const __m128i half = _mm_set1_epi16(1 << 3);

		const __m128i srgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(source.ivec, source.ivec), 4), half);
		const __m128i sf = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(srcfactor.ivec, srcfactor.ivec), 4), half);
		const __m128i s = _mm_mulhi_epi16(srgb, sf);

		const __m128i drgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dst.ivec, dst.ivec), 4), half);
		const __m128i df = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dstfactor.ivec, dstfactor.ivec), 4), half);
		const __m128i d = _mm_mulhi_epi16(drgb, df);

		return Vec3<int>(_mm_unpacklo_epi16(_mm_max_epi16(_mm_subs_epi16(s, d), _mm_setzero_si128()), _mm_setzero_si128()));
#else
		Vec3<int> half = Vec3<int>::AssignToAll(1);
		Vec3<int> lhs = ((source.rgb() * 2 + half) * (srcfactor * 2 + half)) / 1024;
		Vec3<int> rhs = ((dst.rgb() * 2 + half) * (dstfactor * 2 + half)) / 1024;
		return lhs - rhs;
#endif
	}

	case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE:
	{
#if defined(_M_SSE)
		const __m128i half = _mm_set1_epi16(1 << 3);

		const __m128i srgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(source.ivec, source.ivec), 4), half);
		const __m128i sf = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(srcfactor.ivec, srcfactor.ivec), 4), half);
		const __m128i s = _mm_mulhi_epi16(srgb, sf);

		const __m128i drgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dst.ivec, dst.ivec), 4), half);
		const __m128i df = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dstfactor.ivec, dstfactor.ivec), 4), half);
		const __m128i d = _mm_mulhi_epi16(drgb, df);

		return Vec3<int>(_mm_unpacklo_epi16(_mm_max_epi16(_mm_subs_epi16(d, s), _mm_setzero_si128()), _mm_setzero_si128()));
#else
		Vec3<int> half = Vec3<int>::AssignToAll(1);
		Vec3<int> lhs = ((source.rgb() * 2 + half) * (srcfactor * 2 + half)) / 1024;
		Vec3<int> rhs = ((dst.rgb() * 2 + half) * (dstfactor * 2 + half)) / 1024;
		return rhs - lhs;
#endif
	}

	case GE_BLENDMODE_MIN:
		return Vec3<int>(std::min(source.r(), dst.r()),
						std::min(source.g(), dst.g()),
						std::min(source.b(), dst.b()));

	case GE_BLENDMODE_MAX:
		return Vec3<int>(std::max(source.r(), dst.r()),
						std::max(source.g(), dst.g()),
						std::max(source.b(), dst.b()));

	case GE_BLENDMODE_ABSDIFF:
		return Vec3<int>(::abs(source.r() - dst.r()),
						::abs(source.g() - dst.g()),
						::abs(source.b() - dst.b()));

	default:
		return source.rgb();
	}
}

static inline Vec4IntResult SOFTRAST_CALL ApplyTexturing(float s, float t, int x, int y, Vec4IntArg prim_color, int texlevel, int frac_texlevel, bool bilinear, const RasterizerState &state) {
	const u8 **tptr0 = const_cast<const u8 **>(&state.texptr[texlevel]);
	const int *bufw0 = &state.texbufw[texlevel];

	if (!bilinear) {
		return state.nearest(s, t, x, y, prim_color, tptr0, bufw0, texlevel, frac_texlevel, state.samplerID);
	}
	return state.linear(s, t, x, y, prim_color, tptr0, bufw0, texlevel, frac_texlevel, state.samplerID);
}

static inline Vec4IntResult SOFTRAST_CALL ApplyTexturingSingle(float s, float t, int x, int y, Vec4IntArg prim_color, int texlevel, int frac_texlevel, bool bilinear, const RasterizerState &state) {
	return ApplyTexturing(s, t, ((x & 15) + 1) / 2, ((y & 15) + 1) / 2, prim_color, texlevel, frac_texlevel, bilinear, state);
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

static inline void CalculateSamplingParams(const float ds, const float dt, const RasterizerState &state, int &level, int &levelFrac, bool &filt) {
	const int width = 1 << state.samplerID.width0Shift;
	const int height = 1 << state.samplerID.height0Shift;

	// With 8 bits of fraction (because texslope can be fairly precise.)
	int detail;
	switch (state.TexLevelMode()) {
	case GE_TEXLEVEL_MODE_AUTO:
		detail = TexLog2(std::max(ds * width, dt * height));
		break;
	case GE_TEXLEVEL_MODE_SLOPE:
		// This is always offset by an extra texlevel.
		detail = 1 * 16 + TexLog2(state.textureLodSlope);
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

static inline void ApplyTexturing(const RasterizerState &state, Vec4<int> *prim_color, const Vec4<int> &mask, const Vec4<float> &s, const Vec4<float> &t, int x, int y) {
	float ds = s[1] - s[0];
	float dt = t[2] - t[0];

	int level;
	int levelFrac;
	bool bilinear;
	CalculateSamplingParams(ds, dt, state, level, levelFrac, bilinear);

	PROFILE_THIS_SCOPE("sampler");
	for (int i = 0; i < 4; ++i) {
		if (mask[i] >= 0)
			prim_color[i] = ApplyTexturing(s[i], t[i], ((x & 15) + 1) / 2, ((y & 15) + 1) / 2, ToVec4IntArg(prim_color[i]), level, levelFrac, bilinear, state);
	}
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
	if (useSSE4)
		return TriangleEdgeStartSSE4(initX.ivec, initY.ivec, xf, yf, c);
#endif
	return Vec4<int>::AssignToAll(xf) * initX + Vec4<int>::AssignToAll(yf) * initY + Vec4<int>::AssignToAll(c);
}

template <bool useSSE4>
inline Vec4<int> TriangleEdge<useSSE4>::StepX(const Vec4<int> &w) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return _mm_add_epi32(w.ivec, stepX.ivec);
#else
	return w + stepX;
#endif
}

template <bool useSSE4>
inline Vec4<int> TriangleEdge<useSSE4>::StepY(const Vec4<int> &w) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return _mm_add_epi32(w.ivec, stepY.ivec);
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
	if (useSSE4) {
		wmax = MaxWeightSSE4(w.ivec);
	} else {
		wmax = std::max(std::max(w.x, w.y), std::max(w.z, w.w));
	}
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
	if (useSSE4)
		return StepTimesSSE4(w.ivec, stepX.ivec, c);
#endif
	return w + stepX * c;
}

static inline Vec4<int> MakeMask(const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2, const Vec4<int> &bias0, const Vec4<int> &bias1, const Vec4<int> &bias2, const Vec4<int> &scissor) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128i biased0 = _mm_add_epi32(w0.ivec, bias0.ivec);
	__m128i biased1 = _mm_add_epi32(w1.ivec, bias1.ivec);
	__m128i biased2 = _mm_add_epi32(w2.ivec, bias2.ivec);

	return _mm_or_si128(_mm_or_si128(biased0, _mm_or_si128(biased1, biased2)), scissor.ivec);
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
	if (useSSE4) {
		return AnyMaskSSE4(mask.ivec);
	}

	// In other words: !(mask.x < 0 && mask.y < 0 && mask.z < 0 && mask.w < 0)
	__m128i low2 = _mm_and_si128(mask.ivec, _mm_shuffle_epi32(mask.ivec, _MM_SHUFFLE(3, 2, 3, 2)));
	__m128i low1 = _mm_and_si128(low2, _mm_shuffle_epi32(low2, _MM_SHUFFLE(1, 1, 1, 1)));
	// Now we only need to check one sign bit.
	return _mm_cvtsi128_si32(low1) >= 0;
#else
	return mask.x >= 0 || mask.y >= 0 || mask.z >= 0 || mask.w >= 0;
#endif
}

static inline Vec4<float> EdgeRecip(const Vec4<int> &w0, const Vec4<int> &w1, const Vec4<int> &w2) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128i wsum = _mm_add_epi32(w0.ivec, _mm_add_epi32(w1.ivec, w2.ivec));
	// _mm_rcp_ps loses too much precision.
	return _mm_div_ps(_mm_set1_ps(1.0f), _mm_cvtepi32_ps(wsum));
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

	// All the z values are the same, no interpolation required.
	// This is common, and when we interpolate, we lose accuracy.
	const bool flatZ = v0.screenpos.z == v1.screenpos.z && v0.screenpos.z == v2.screenpos.z;
	const bool flatColorAll = clearMode || !state.shadeGouraud;
	const bool flatColor0 = flatColorAll || (v0.color0 == v1.color0 && v0.color0 == v2.color0);
	const bool flatColor1 = flatColorAll || (v0.color1 == v1.color1 && v0.color1 == v2.color1);
	const bool noFog = clearMode || !pixelID.applyFog || (v0.fogdepth >= 1.0f && v1.fogdepth >= 1.0f && v2.fogdepth >= 1.0f);

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	uint32_t bpp = pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	std::string tag = StringFromFormat("DisplayListT_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListTZ_%08x", state.listPC);
#endif

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
				Vec4<float> wsum_recip = EdgeRecip(w0, w1, w2);

				// Color interpolation is not perspective corrected on the PSP.
				Vec4<int> prim_color[4];
				if (!flatColor0) {
					for (int i = 0; i < 4; ++i) {
						if (mask[i] >= 0)
							prim_color[i] = Interpolate(v0.color0, v1.color0, v2.color0, w0[i], w1[i], w2[i], wsum_recip[i]);
					}
				} else {
					for (int i = 0; i < 4; ++i) {
						prim_color[i] = v2.color0;
					}
				}
				Vec3<int> sec_color[4];
				if (!flatColor1) {
					for (int i = 0; i < 4; ++i) {
						if (mask[i] >= 0)
							sec_color[i] = Interpolate(v0.color1, v1.color1, v2.color1, w0[i], w1[i], w2[i], wsum_recip[i]);
					}
				} else {
					for (int i = 0; i < 4; ++i) {
						sec_color[i] = v2.color1;
					}
				}

				if (state.enableTextures && !clearMode) {
					Vec4<float> s, t;
					if (state.throughMode) {
						s = Interpolate(v0.texturecoords.s(), v1.texturecoords.s(), v2.texturecoords.s(), w0, w1, w2, wsum_recip);
						t = Interpolate(v0.texturecoords.t(), v1.texturecoords.t(), v2.texturecoords.t(), w0, w1, w2, wsum_recip);

						// For levels > 0, mipmapping is always based on level 0.  Simpler to scale first.
						s *= 1.0f / (float)(1 << state.samplerID.width0Shift);
						t *= 1.0f / (float)(1 << state.samplerID.height0Shift);
					} else {
						// Texture coordinate interpolation must definitely be perspective-correct.
						GetTextureCoordinates(v0, v1, v2, w0, w1, w2, wsum_recip, s, t);
					}

					ApplyTexturing(state, prim_color, mask, s, t, curX, curY);
				}

				if (!clearMode) {
					for (int i = 0; i < 4; ++i) {
#if defined(_M_SSE)
						// TODO: Tried making Vec4 do this, but things got slower.
						const __m128i sec = _mm_and_si128(sec_color[i].ivec, _mm_set_epi32(0, -1, -1, -1));
						prim_color[i].ivec = _mm_add_epi32(prim_color[i].ivec, sec);
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

				Vec4<int> z;
				if (flatZ) {
					z = Vec4<int>::AssignToAll(v2.screenpos.z);
				} else {
					// Z is interpolated pretty much directly.
					Vec4<float> zfloats = w0.Cast<float>() * v0.screenpos.z + w1.Cast<float>() * v1.screenpos.z + w2.Cast<float>() * v2.screenpos.z;
					z = (zfloats * wsum_recip).Cast<int>();
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

void DrawRectangle(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state) {
	int entireX1 = std::min(v0.screenpos.x, v1.screenpos.x);
	int entireY1 = std::min(v0.screenpos.y, v1.screenpos.y);
	int entireX2 = std::max(v0.screenpos.x, v1.screenpos.x) - 1;
	int entireY2 = std::max(v0.screenpos.y, v1.screenpos.y) - 1;
	int minX = std::max(entireX1, range.x1) | (SCREEN_SCALE_FACTOR / 2 - 1);
	int minY = std::max(entireY1, range.y1) | (SCREEN_SCALE_FACTOR / 2 - 1);
	int maxX = std::min(entireX2, range.x2);
	int maxY = std::min(entireY2, range.y2);

	Vec2f rowST(0.0f, 0.0f);
	// Note: this is double the x or y movement.
	Vec2f stx(0.0f, 0.0f);
	Vec2f sty(0.0f, 0.0f);
	if (state.enableTextures) {
		Vec2f tc0 = v0.texturecoords;
		Vec2f tc1 = v1.texturecoords;
		if (state.throughMode) {
			// For levels > 0, mipmapping is always based on level 0.  Simpler to scale first.
			tc0.s() *= 1.0f / (float)(1 << state.samplerID.width0Shift);
			tc1.s() *= 1.0f / (float)(1 << state.samplerID.width0Shift);
			tc0.t() *= 1.0f / (float)(1 << state.samplerID.height0Shift);
			tc1.t() *= 1.0f / (float)(1 << state.samplerID.height0Shift);
		}

		int diffX = (entireX2 - entireX1 + 1) / SCREEN_SCALE_FACTOR;
		int diffY = (entireY2 - entireY1 + 1) / SCREEN_SCALE_FACTOR;
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
		rowST += (stx / (float)(SCREEN_SCALE_FACTOR * 2)) * (minX - entireX1);
		rowST += (sty / (float)(SCREEN_SCALE_FACTOR * 2)) * (minY - entireY1);
	}

	// And now what we add to spread out to 4 values.
	const Vec4f sto4(0.0f, 0.5f * stx.s(), 0.5f * sty.s(), 0.5f * stx.s() + 0.5f * sty.s());
	const Vec4f tto4(0.0f, 0.5f * stx.t(), 0.5f * sty.t(), 0.5f * stx.t() + 0.5f * sty.t());

	ScreenCoords pprime(minX, minY, 0);
	Vec4<int> fog = Vec4<int>::AssignToAll(ClampFogDepth(v1.fogdepth));
	Vec4<int> z = Vec4<int>::AssignToAll(v1.screenpos.z);
	Vec3<int> sec_color = v1.color1;

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	uint32_t bpp = state.pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	std::string tag = StringFromFormat("DisplayListR_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListRZ_%08x", state.listPC);
#endif

	for (int64_t curY = minY; curY <= maxY; curY += SCREEN_SCALE_FACTOR * 2, rowST += sty) {
		DrawingCoords p = TransformUnit::ScreenToDrawing(range.x1, curY);

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
				prim_color[i] = v1.color0;
			}

			if (state.enableTextures) {
				Vec4<float> s, t;
				s = Vec4<float>::AssignToAll(st.s()) + sto4;
				t = Vec4<float>::AssignToAll(st.t()) + tto4;

				ApplyTexturing(state, prim_color, mask, s, t, curX, curY);
			}

			if (!state.pixelID.clearMode) {
				for (int i = 0; i < 4; ++i) {
#if defined(_M_SSE)
					// TODO: Tried making Vec4 do this, but things got slower.
					const __m128i sec = _mm_and_si128(sec_color.ivec, _mm_set_epi32(0, -1, -1, -1));
					prim_color[i].ivec = _mm_add_epi32(prim_color[i].ivec, sec);
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
	Vec4<int> prim_color = v0.color0;
	Vec3<int> sec_color = v0.color1;

	auto &pixelID = state.pixelID;
	auto &samplerID = state.samplerID;

	if (state.enableTextures) {
		float s = v0.texturecoords.s();
		float t = v0.texturecoords.t();
		if (state.throughMode) {
			s *= 1.0f / (float)(1 << state.samplerID.width0Shift);
			t *= 1.0f / (float)(1 << state.samplerID.height0Shift);
		} else {
			// Texture coordinate interpolation must definitely be perspective-correct.
			GetTextureCoordinates(v0, v0, 0.0f, s, t);
		}

		int texLevel;
		int texLevelFrac;
		bool bilinear;
		CalculateSamplingParams(0.0f, 0.0f, state, texLevel, texLevelFrac, bilinear);
		PROFILE_THIS_SCOPE("sampler");
		prim_color = ApplyTexturingSingle(s, t, pos.x, pos.y, ToVec4IntArg(prim_color), texLevel, texLevelFrac, bilinear, state);
	}

	if (!pixelID.clearMode)
		prim_color += Vec4<int>(sec_color, 0);

	DrawingCoords p = TransformUnit::ScreenToDrawing(pos);
	u16 z = pos.z;

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
	DrawingCoords pprime = TransformUnit::ScreenToDrawing(range.x1, range.y1);
	DrawingCoords pend = TransformUnit::ScreenToDrawing(range.x2, range.y2);
	auto &pixelID = state.pixelID;
	auto &samplerID = state.samplerID;

	// Min and max are in PSP fixed point screen coordinates, 16 here is for the 4 subpixel bits.
	const int w = (range.x2 - range.x1 + 1) / SCREEN_SCALE_FACTOR;
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

	const u32 new_color = v1.color0.ToRGBA();
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
	Vec3<int> b(v1.screenpos.x, v1.screenpos.y, v0.screenpos.z);

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

#if defined(SOFTGPU_MEMORY_TAGGING_DETAILED) || defined(SOFTGPU_MEMORY_TAGGING_BASIC)
	std::string tag = StringFromFormat("DisplayListL_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListLZ_%08x", state.listPC);
#endif

	double x = a.x > b.x ? a.x - 1 : a.x;
	double y = a.y > b.y ? a.y - 1 : a.y;
	double z = a.z;
	const int steps1 = steps == 0 ? 1 : steps;
	for (int i = 0; i < steps; i++) {
		if (x >= range.x1 && y >= range.y1 && x <= range.x2 && y <= range.y2) {
			// Interpolate between the two points.
			Vec4<int> prim_color;
			Vec3<int> sec_color;
			if (interpolateColor) {
				prim_color = (v0.color0 * (steps - i) + v1.color0 * i) / steps1;
				sec_color = (v0.color1 * (steps - i) + v1.color1 * i) / steps1;
			} else {
				prim_color = v1.color0;
				sec_color = v1.color1;
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
					Vec2<float> tc = (v0.texturecoords * (float)(steps - i) + v1.texturecoords * (float)i) / steps1;
					Vec2<float> tc1 = (v0.texturecoords * (float)(steps - i - 1) + v1.texturecoords * (float)(i + 1)) / steps1;

					s = tc.s() * (1.0f / (float)(1 << state.samplerID.width0Shift));
					s1 = tc1.s() * (1.0f / (float)(1 << state.samplerID.width0Shift));
					t = tc.t() * (1.0f / (float)(1 << state.samplerID.height0Shift));
					t1 = tc1.t() * (1.0f / (float)(1 << state.samplerID.height0Shift));
				} else {
					// Texture coordinate interpolation must definitely be perspective-correct.
					GetTextureCoordinates(v0, v1, (float)(steps - i) / steps1, s, t);
					GetTextureCoordinates(v0, v1, (float)(steps - i - 1) / steps1, s1, t1);
				}

				// If inc is 0, force the delta to zero.
				float ds = xinc == 0.0 ? 0.0f : (s1 - s) * (float)SCREEN_SCALE_FACTOR * (1.0f / xinc);
				float dt = yinc == 0.0 ? 0.0f : (t1 - t) * (float)SCREEN_SCALE_FACTOR * (1.0f / yinc);

				int texLevel;
				int texLevelFrac;
				bool texBilinear;
				CalculateSamplingParams(ds, dt, state, texLevel, texLevelFrac, texBilinear);

				if (state.antialiasLines) {
					// TODO: This is a naive and wrong implementation.
					DrawingCoords p0 = TransformUnit::ScreenToDrawing(x, y);
					s = ((float)p0.x + xinc / 32.0f) / 512.0f;
					t = ((float)p0.y + yinc / 32.0f) / 512.0f;

					texBilinear = true;
				}

				PROFILE_THIS_SCOPE("sampler");
				prim_color = ApplyTexturingSingle(s, t, x, y, ToVec4IntArg(prim_color), texLevel, texLevelFrac, texBilinear, state);
			}

			if (!pixelID.clearMode)
				prim_color += Vec4<int>(sec_color, 0);

			PROFILE_THIS_SCOPE("draw_px");
			DrawingCoords p = TransformUnit::ScreenToDrawing(x, y);
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
	int texbufw = GetTextureBufw(level, texaddr, texfmt);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	if (!texaddr || !Memory::IsValidRange(texaddr, (textureBitsPerPixel[texfmt] * texbufw * h) / 8))
		return false;

	buffer.Allocate(w, h, GE_FORMAT_8888, false);

	SamplerID id;
	ComputeSamplerID(&id);
	id.cached.clut = (const u8 *)clut;

	Sampler::FetchFunc sampler = Sampler::GetFetchFunc(id);

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
