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
#include <cmath>
#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "GPU/GPUState.h"
#include "GPU/Software/Lighting.h"

namespace Lighting {

static inline Vec3f GetLightVec(const u32 lparams[12], int light) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128i values = _mm_loadu_si128((__m128i *)&lparams[3 * light]);
	__m128i from24 = _mm_slli_epi32(values, 8);
	return _mm_castsi128_ps(from24);
#elif PPSSPP_ARCH(ARM64_NEON)
	uint32x4_t values = vld1q_u32((uint32_t *)&lparams[3 * light]);
	uint32x4_t from24 = vshlq_n_u32(values, 8);
	return vreinterpretq_f32_u32(from24);
#else
	return Vec3<float>(getFloat24(lparams[3 * light]), getFloat24(lparams[3 * light + 1]), getFloat24(lparams[3 * light + 2]));
#endif
}

static inline float pspLightPow(float v, float e) {
	if (e <= 0.0f) {
		return 1.0f;
	}
	if (v > 0.0f) {
		return pow(v, e);
	}
	// Negative stays negative, so let's just return the original.
	return v;
}

static inline Vec4<int> LightColorFactor(const Vec4<int> &expanded, const Vec4<int> &ones) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return _mm_add_epi32(_mm_slli_epi32(expanded.ivec, 1), ones.ivec);
#elif PPSSPP_ARCH(ARM64_NEON)
	return vaddq_s32(vshlq_n_s32(expanded.ivec, 1), ones.ivec);
#else
	return expanded * 2 + ones;
#endif
}

static inline Vec4<int> LightColorFactor(uint32_t c, const Vec4<int> &ones) {
	return LightColorFactor(Vec4<int>::FromRGBA(c), ones);
}

static inline bool IsLargerThanHalf(const Vec4<int> &v) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128i add23 = _mm_add_epi32(v.ivec, _mm_shuffle_epi32(v.ivec, _MM_SHUFFLE(3, 2, 3, 2)));
	__m128i add1 = _mm_add_epi32(add23, _mm_shuffle_epi32(add23, _MM_SHUFFLE(1, 1, 1, 1)));
	return _mm_cvtsi128_si32(add1) > 4;
#elif PPSSPP_ARCH(ARM64_NEON)
	int32x2_t add02 = vpmax_s32(vget_low_s32(v.ivec), vget_high_s32(v.ivec));
	int32x2_t add1 = vpmax_s32(add02, add02);
	return vget_lane_s32(add1, 0) > 4;
#else
	bool larger = false;
	for (int i = 0; i < 3; ++i)
		larger = v[i] > 1;
	return larger;
#endif
}

void ComputeState(State *state, bool hasColor0) {
	const Vec4<int> ones = Vec4<int>::AssignToAll(1);

	bool anyAmbient = false;
	bool anyDiffuse = false;
	bool anySpecular = false;
	bool anyNonDirectional = false;
	for (int light = 0; light < 4; ++light) {
		auto &lstate = state->lights[light];
		lstate.enabled = gstate.isLightChanEnabled(light);
		if (!lstate.enabled)
			continue;

		lstate.poweredDiffuse = gstate.isUsingPoweredDiffuseLight(light);
		lstate.specular = gstate.isUsingSpecularLight(light);

		lstate.ambientColorFactor = LightColorFactor(gstate.getLightAmbientColor(light), ones);
		lstate.ambient = IsLargerThanHalf(lstate.ambientColorFactor);
		anyAmbient = anyAmbient || lstate.ambient;

		lstate.diffuseColorFactor = LightColorFactor(gstate.getDiffuseColor(light), ones);
		lstate.diffuse = IsLargerThanHalf(lstate.diffuseColorFactor);
		anyDiffuse = anyDiffuse || lstate.diffuse;

		if (lstate.specular) {
			lstate.specularColorFactor = LightColorFactor(gstate.getSpecularColor(light), ones);
			lstate.specular = IsLargerThanHalf(lstate.specularColorFactor);
			anySpecular = anySpecular || lstate.specular;
		}

		// Doesn't actually need to be on if nothing will affect it.
		if (!lstate.specular && !lstate.ambient && !lstate.diffuse) {
			lstate.enabled = false;
			continue;
		}

		lstate.pos = GetLightVec(gstate.lpos, light);
		lstate.directional = gstate.isDirectionalLight(light);
		if (lstate.directional) {
			lstate.pos.NormalizeOr001();
		} else {
			lstate.att = GetLightVec(gstate.latt, light);
			anyNonDirectional = true;
		}

		lstate.spot = gstate.isSpotLight(light);
		if (lstate.spot) {
			lstate.spotDir = GetLightVec(gstate.ldir, light);
			lstate.spotDir.Normalize();
			lstate.spotCutoff = getFloat24(gstate.lcutoff[light]);
			if (std::isnan(lstate.spotCutoff) && std::signbit(lstate.spotCutoff))
				lstate.spotCutoff = 0.0f;

			lstate.spotExp = getFloat24(gstate.lconv[light]);
			if (lstate.spotExp <= 0.0f)
				lstate.spotExp = 0.0f;
			else if (std::isnan(lstate.spotExp))
				lstate.spotExp = std::signbit(lstate.spotExp) ? 0.0f : INFINITY;
		}
	}

	const int materialupdate = gstate.materialupdate & (hasColor0 ? 7 : 0);
	state->colorForAmbient = (materialupdate & 1) != 0;
	state->colorForDiffuse = (materialupdate & 2) != 0;
	state->colorForSpecular = (materialupdate & 4) != 0;

	if (!state->colorForAmbient) {
		state->material.ambientColorFactor = LightColorFactor(gstate.getMaterialAmbientRGBA(), ones);
		if (!IsLargerThanHalf(state->material.ambientColorFactor) && anyAmbient) {
			for (int i = 0; i < 4; ++i)
				state->lights[i].ambient = false;
		}
	}

	if (anyDiffuse && !state->colorForDiffuse) {
		state->material.diffuseColorFactor = LightColorFactor(gstate.getMaterialDiffuse(), ones);
		if (!IsLargerThanHalf(state->material.diffuseColorFactor)) {
			anyDiffuse = false;
			for (int i = 0; i < 4; ++i)
				state->lights[i].diffuse = false;
		}
	}

	if (anySpecular && !state->colorForSpecular) {
		state->material.specularColorFactor = LightColorFactor(gstate.getMaterialSpecular(), ones);
		if (!IsLargerThanHalf(state->material.specularColorFactor)) {
			anySpecular = false;
			for (int i = 0; i < 4; ++i)
				state->lights[i].specular = false;
		}
	}

	if (anyDiffuse || anySpecular) {
		state->specularExp = gstate.getMaterialSpecularCoef();
		if (state->specularExp <= 0.0f)
			state->specularExp = 0.0f;
		else if (std::isnan(state->specularExp))
			state->specularExp = std::signbit(state->specularExp) ? 0.0f : INFINITY;
	}

	state->baseAmbientColorFactor = LightColorFactor(gstate.getAmbientRGBA(), ones);
	state->setColor1 = gstate.isUsingSecondaryColor() && anySpecular;
	state->addColor1 = !gstate.isUsingSecondaryColor() && anySpecular;
	state->usesWorldPos = anyNonDirectional;
	state->usesWorldNormal = gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP || anyDiffuse || anySpecular;
}

static inline float GenerateLightCoord(VertexData &vertex, const WorldCoords &worldnormal, int light) {
	// TODO: Should specular lighting should affect this, too?  Doesn't in GLES.
	Vec3<float> L = GetLightVec(gstate.lpos, light);
	// In other words, L.Length2() == 0.0f means Dot({0, 0, 1}, worldnormal).
	float diffuse_factor = Dot(L.NormalizedOr001(cpu_info.bSSE4_1), worldnormal);

	return (diffuse_factor + 1.0f) / 2.0f;
}

void GenerateLightST(VertexData &vertex, const WorldCoords &worldnormal) {
	// Always calculate texture coords from lighting results if environment mapping is active
	// This should be done even if lighting is disabled altogether.
	vertex.texturecoords.s() = GenerateLightCoord(vertex, worldnormal, gstate.getUVLS0());
	vertex.texturecoords.t() = GenerateLightCoord(vertex, worldnormal, gstate.getUVLS1());
}

#if defined(_M_SSE)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline int LightCeilSSE4(float f) {
	__m128 v = _mm_set_ss(f);
	// This isn't terribly fast, but seems to be better than calling ceilf().
	return _mm_cvt_ss2si(_mm_ceil_ss(v, v));
}

#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline __m128i LightColorScaleBy512SSE4(__m128i factor, __m128i color, __m128i scale) {
	// We can use 16-bit multiply here (faster than 32-bit multiply) since our top bits are zero.
	__m128i result18 = _mm_madd_epi16(factor, color);
	// But now with 18 bits, we need a full multiply.
	__m128i multiplied = _mm_mullo_epi32(result18, scale);
	return _mm_srai_epi32(multiplied, 10 + 9);
}
#endif

template <bool useSSE4>
static inline int LightCeil(float f) {
#if defined(_M_SSE)
	if (useSSE4)
		return LightCeilSSE4(f);
#elif PPSSPP_ARCH(ARM64_NEON)
	return vcvtps_s32_f32(f);
#endif
	return (int)ceilf(f);
}

template <bool useSSE4>
static Vec4<int> LightColorScaleBy512(const Vec4<int> &factor, const Vec4<int> &color, int scale) {
	// We multiply s9 * s9 * s9, resulting in s27, then shift off 19 to get 8-bit.
	// The reason all factors are s9 is to account for rounding.
	// Also note that all values are positive, so can be treated as unsigned.
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	if (useSSE4)
		return LightColorScaleBy512SSE4(factor.ivec, color.ivec, _mm_set1_epi32(scale));
#elif PPSSPP_ARCH(ARM64_NEON)
	int32x4_t multiplied = vmulq_n_s32(vmulq_s32(factor.ivec, color.ivec), scale);
	return vshrq_n_s32(multiplied, 10 + 9);
#endif
	return (factor * color * scale) >> (10 + 9);
}

static inline void LightColorSum(Vec4<int> &sum, const Vec4<int> &src) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	sum.ivec = _mm_add_epi32(sum.ivec, src.ivec);
#elif PPSSPP_ARCH(ARM64_NEON)
	sum.ivec = vaddq_s32(sum.ivec, src.ivec);
#else
	sum += src;
#endif
}

static inline float Dot33(const Vec3f &a, const Vec3f &b) {
#if defined(_M_SSE)
	__m128 v = _mm_mul_ps(SAFE_M128(a.vec), SAFE_M128(b.vec)); // [X, Y, Z, W]
	__m128 shuf = _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 2, 0, 1)); // [Y, X, Z, W]
	__m128 sums = _mm_add_ps(v, shuf); // [X + Y, X + Y, Z + Z, W + W]
	shuf = _mm_movehl_ps(shuf, shuf); // [Z, W, Z, W]
	return _mm_cvtss_f32(_mm_add_ss(sums, shuf)); // X + Y + Z
#elif PPSSPP_ARCH(ARM64_NEON)
	float32x4_t multipled = vsetq_lane_f32(0.0f, vmulq_f32(a.vec, b.vec), 3);
	float32x2_t add1 = vget_low_f32(vpaddq_f32(multipled, multipled));
	float32x2_t add2 = vpadd_f32(add1, add1);
	return vget_lane_f32(add2, 0);
#else
	return Dot(a, b);
#endif
}

template <bool useSSE4>
static void ProcessSIMD(VertexData &vertex, const WorldCoords &worldpos, const WorldCoords &worldnormal, const State &state) {
	// Lighting blending rounds using the half offset method (like alpha blend.)
	Vec4<int> colorFactor;
	if (state.colorForAmbient || state.colorForDiffuse || state.colorForSpecular) {
		const Vec4<int> ones = Vec4<int>::AssignToAll(1);
		colorFactor = LightColorFactor(vertex.color0, ones);
	}

	Vec4<int> mec = Vec4<int>::FromRGBA(gstate.getMaterialEmissive());

	Vec4<int> mac = state.colorForAmbient ? colorFactor : state.material.ambientColorFactor;
	Vec4<int> ambient = (mac * state.baseAmbientColorFactor) >> 10;

	Vec4<int> final_color = mec + ambient;
	Vec4<int> specular_color = Vec4<int>::AssignToAll(0);

	for (unsigned int light = 0; light < 4; ++light) {
		const auto &lstate = state.lights[light];
		if (!lstate.enabled)
			continue;

		// L =  vector from vertex to light source
		// TODO: Should transfer the light positions to world/view space for these calculations?
		Vec3<float> L = lstate.pos;
		float attspot = 1.0f;
		if (!lstate.directional) {
			L -= worldpos;
			// TODO: Should this normalize (0, 0, 0) to (0, 0, 1)?
			float d = L.NormalizeOr001();

			float att = 1.0f / Dot33(lstate.att, Vec3f(1.0f, d, d * d));
			if (!(att > 0.0f))
				att = 0.0f;
			else if (att > 1.0f)
				att = 1.0f;
			attspot = att;
		}

		if (lstate.spot) {
			float rawSpot = Dot33(lstate.spotDir, L);
			if (std::isnan(rawSpot))
				rawSpot = std::signbit(rawSpot) ? 0.0f : 1.0f;

			float spot = 1.0f;
			if (rawSpot >= lstate.spotCutoff) {
				spot = pspLightPow(rawSpot, lstate.spotExp);
				if (std::isnan(spot))
					spot = 0.0f;
			} else {
				spot = 0.0f;
			}

			attspot *= spot;
		}

		// ambient lighting
		if (lstate.ambient) {
			int attspot512 = (int)LightCeil<useSSE4>(256 * 2 * attspot + 1);
			if (attspot512 > 512)
				attspot512 = 512;
			Vec4<int> lambient = LightColorScaleBy512<useSSE4>(lstate.ambientColorFactor, mac, attspot512);
			LightColorSum(final_color, lambient);
		}

		// diffuse lighting
		float diffuse_factor;
		if (lstate.diffuse || lstate.specular) {
			diffuse_factor = Dot33(L, worldnormal);
			if (lstate.poweredDiffuse) {
				diffuse_factor = pspLightPow(diffuse_factor, state.specularExp);
			}
		}

		if (lstate.diffuse && diffuse_factor > 0.0f) {
			int diffuse_attspot = (int)LightCeil<useSSE4>(256 * 2 * attspot * diffuse_factor + 1);
			if (diffuse_attspot > 512)
				diffuse_attspot = 512;
			Vec4<int> mdc = state.colorForDiffuse ? colorFactor : state.material.diffuseColorFactor;
			Vec4<int> ldiffuse = LightColorScaleBy512<useSSE4>(lstate.diffuseColorFactor, mdc, diffuse_attspot);
			LightColorSum(final_color, ldiffuse);
		}

		if (lstate.specular && diffuse_factor >= 0.0f) {
			Vec3<float> H = L + Vec3<float>(0.f, 0.f, 1.f);

			float specular_factor = Dot33(H.NormalizedOr001(useSSE4), worldnormal);
			specular_factor = pspLightPow(specular_factor, state.specularExp);

			if (specular_factor > 0.0f) {
				int specular_attspot = (int)LightCeil<useSSE4>(256 * 2 * attspot * specular_factor + 1);
				if (specular_attspot > 512)
					specular_attspot = 512;

				Vec4<int> msc = state.colorForSpecular ? colorFactor : state.material.specularColorFactor;
				Vec4<int> lspecular = LightColorScaleBy512<useSSE4>(lstate.specularColorFactor, msc, specular_attspot);
				LightColorSum(specular_color, lspecular);
			}
		}
	}

	// Note: these are all naturally clamped by ToRGBA/toRGB.
	if (state.setColor1) {
		vertex.color0 = final_color.ToRGBA();
		vertex.color1 = specular_color.rgb().ToRGB();
	} else if (state.addColor1) {
		vertex.color0 = (final_color + specular_color).ToRGBA();
	} else {
		vertex.color0 = final_color.ToRGBA();
	}
}

void Process(VertexData &vertex, const WorldCoords &worldpos, const WorldCoords &worldnormal, const State &state) {
#ifdef _M_SSE
	if (cpu_info.bSSE4_1) {
		ProcessSIMD<true>(vertex, worldpos, worldnormal, state);
		return;
	}
#endif
	ProcessSIMD<false>(vertex, worldpos, worldnormal, state);
}

} // namespace
