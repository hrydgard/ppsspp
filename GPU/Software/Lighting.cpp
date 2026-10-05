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
#include "Common/Math/SIMDHeaders.h"
#include "GPU/GPUState.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Software/GEMath.h"
#include "GPU/Software/Lighting.h"
#include "GPU/Software/TransformUnit.h"

#if PPSSPP_ARCH(SSE2)
// For the SSE4 stuff.
#include <smmintrin.h>
#endif

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

// Whether a color factor (2c + 1 per channel, see LightColorFactor) has any channel above zero. The alpha
// lane is 1 (light colors have no alpha), so the sum is above 4 exactly then, and so is the maximum above 1.
static inline bool IsLargerThanHalf(const Vec4<int> &v) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128i add23 = _mm_add_epi32(v.ivec, _mm_shuffle_epi32(v.ivec, _MM_SHUFFLE(3, 2, 3, 2)));
	__m128i add1 = _mm_add_epi32(add23, _mm_shuffle_epi32(add23, _MM_SHUFFLE(1, 1, 1, 1)));
	return _mm_cvtsi128_si32(add1) > 4;
#elif PPSSPP_ARCH(ARM64_NEON)
	int32x2_t add02 = vpmax_s32(vget_low_s32(v.ivec), vget_high_s32(v.ivec));
	int32x2_t add1 = vpmax_s32(add02, add02);
	return vget_lane_s32(add1, 0) > 1;
#else
	return v[0] > 1 || v[1] > 1 || v[2] > 1;
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
			// A zero direction stays zero: no diffuse, and the half vector is just the eye's (gpu/probe exp164).
			GENormalize(lstate.pos);
		} else {
			lstate.att = GetLightVec(gstate.latt, light);
			anyNonDirectional = true;
		}

		lstate.spot = gstate.isSpotLight(light);
		if (lstate.spot) {
			// The direction isn't normalized: the dot with L is scaled by its rsqrt (gpu/probe exp100).
			lstate.spotDir = GetLightVec(gstate.ldir, light);
			// A component with exponent 255 (inf or NaN) acts as the largest value of its sign, so after the
			// scaling below the finite ones vanish next to it (gpu/commands/light: -NAN and -INFINITY light
			// like (-1, -1, -1), NAN and INFINITY like (1, 1, 1)).
			if (!std::isfinite(lstate.spotDir.x) || !std::isfinite(lstate.spotDir.y) || !std::isfinite(lstate.spotDir.z)) {
				for (int i = 0; i < 3; ++i)
					lstate.spotDir[i] = std::isfinite(lstate.spotDir[i]) ? 0.0f : (std::signbit(lstate.spotDir[i]) ? -1.0f : 1.0f);
			}
			const float dirLen2 = GEDot(lstate.spotDir, lstate.spotDir);
			lstate.spotDirRsqrt = dirLen2 > 0.0f && std::isfinite(dirLen2) ? GERsqrt(dirLen2) : 0.0f;
			lstate.spotCutoff = getFloat24(gstate.lcutoff[light]);
			if (std::isnan(lstate.spotCutoff) && std::signbit(lstate.spotCutoff))
				lstate.spotCutoff = 0.0f;

			lstate.spotExp = PSPLightExponent(getFloat24(gstate.lconv[light]));
			if (lstate.spotExp <= 0.0f)
				lstate.spotExp = 0.0f;
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
		state->specularExp = PSPLightExponent(gstate.getMaterialSpecularCoef());
		if (state->specularExp <= 0.0f)
			state->specularExp = 0.0f;
	}

	state->baseAmbientColorFactor = LightColorFactor(gstate.getAmbientRGBA(), ones);
	state->setColor1 = gstate.isUsingSecondaryColor() && anySpecular;
	state->addColor1 = !gstate.isUsingSecondaryColor() && anySpecular;
	state->usesWorldPos = anyNonDirectional;
	state->usesWorldNormal = gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP || anyDiffuse || anySpecular;
}

// PSPShadeMapCoord in the GE's arithmetic, with L as lighting computes it.
// v . N for the unnormalized normal N, scaled by its reciprocal length (the spot direction works the same).
static inline float GENormalDot(const Vec3f &v, const Vec3f &n, float nRsqrt) {
	return ProductToFloat24((double)GEDot(v, n) * nRsqrt);
}

// The vector from a vertex to a point light as the GE computes it (gpu/probe exp153-158): the light position
// minus the world translation (with the GE's adder), minus the model position times the world matrix, summed
// like a matrix row. No world space position is formed; lighting with one in float24 loses the products' low
// bits to a large translation (Syphon Filter #13568).
static Vec3f GELightVector(const Vec3f &lpos, const Vec3f &modelpos) {
	const float *m = gstate.worldMatrix;
	Vec3f L;
	for (int i = 0; i < 3; ++i) {
		const GERowTerm terms[4] = {
			GEProduct(1.0f, GEAddFloat24(lpos[i], -TruncateToFloat24(m[9 + i]))),
			GEProduct(TruncateToFloat24(modelpos.x), -m[i]),
			GEProduct(TruncateToFloat24(modelpos.y), -m[3 + i]),
			GEProduct(TruncateToFloat24(modelpos.z), -m[6 + i]),
		};
		L[i] = GERowSum(terms, 4);
	}
	return L;
}

static float GEShadeMapCoord(int l, const Vec3f &modelpos, const WorldCoords &worldnormal, float normalRsqrt, const Vec3f &viewDir) {
	Vec3f L(getFloat24(gstate.lpos[l * 3]), getFloat24(gstate.lpos[l * 3 + 1]), getFloat24(gstate.lpos[l * 3 + 2]));
	if (gstate.getLightType(l) != GE_LIGHTTYPE_DIRECTIONAL)
		L = GELightVector(L, modelpos);
	GENormalize(L);
	if (gstate.isUsingSpecularLight(l)) {
		for (int i = 0; i < 3; ++i)
			L[i] = GEAddFloat24(L[i], viewDir[i]);
		GENormalize(L);
	}
	return GEAddFloat24(GENormalDot(L, worldnormal, normalRsqrt), 1.0f) * 0.5f;
}

void GenerateLightST(VertexData &vertex, const Vec3f &modelpos, const WorldCoords &worldnormal, float normalRsqrt, const Vec3f &viewDir) {
	// Always calculate texture coords from lighting results if environment mapping is active
	// This should be done even if lighting is disabled altogether.
	vertex.texturecoords.s() = GEShadeMapCoord(gstate.getUVLS0(), modelpos, worldnormal, normalRsqrt, viewDir);
	vertex.texturecoords.t() = GEShadeMapCoord(gstate.getUVLS1(), modelpos, worldnormal, normalRsqrt, viewDir);
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
static void ProcessSIMD(VertexData &vertex, const Vec3f &modelpos, const WorldCoords &worldnormal, float normalRsqrt, const State &state) {
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
		// Attenuation and spot each scale the light's colors as their own 8-bit factor (gpu/probe exp100).
		float att = 1.0f;
		float spot = 1.0f;
		if (!lstate.directional) {
			L = GELightVector(L, modelpos);
			// TODO: Should this normalize (0, 0, 0) to (0, 0, 1)?
			// The quadratic term takes the squared length from the normalization, not d * d (gpu/probe exp63).
			const float d2 = GEDot(L, L);
			float d = GENormalize(L);
			if (d == 0.0f)
				L = Vec3f(0.0f, 0.0f, 1.0f);

			const float den = GEDot(lstate.att, Vec3f(1.0f, d, d2));
			att = den > 0.0f ? GERecip(den) : 0.0f;
			if (!(att > 0.0f))
				att = 0.0f;
			else if (att > 1.0f)
				att = 1.0f;
		}

		if (lstate.spot) {
			float rawSpot = ProductToFloat24((double)GEDot(lstate.spotDir, L) * lstate.spotDirRsqrt);
			if (std::isnan(rawSpot))
				rawSpot = std::signbit(rawSpot) ? 0.0f : 1.0f;

			if (rawSpot >= lstate.spotCutoff) {
				spot = GELightPow(rawSpot, lstate.spotExp);
				if (std::isnan(spot))
					spot = 0.0f;
			} else {
				spot = 0.0f;
			}

		}
		auto scaleAttSpot = [&](Vec4<int> c) {
			if (att < 1.0f)
				c = GELightColorScale(c, att);
			if (spot < 1.0f)
				c = GELightColorScale(c, spot);
			return c;
		};

		// ambient lighting
		if (lstate.ambient) {
			Vec4<int> lambient = GELightColorProduct(lstate.ambientColorFactor, mac);
			lambient = scaleAttSpot(lambient);
			LightColorSum(final_color, lambient);
		}

		// diffuse lighting
		float diffuse_factor;
		if (lstate.diffuse || lstate.specular) {
			diffuse_factor = GENormalDot(L, worldnormal, normalRsqrt);
			if (lstate.poweredDiffuse) {
				diffuse_factor = GELightPow(diffuse_factor, state.specularExp);
			}
		}

		if (lstate.diffuse && diffuse_factor > 0.0f) {
			Vec4<int> mdc = state.colorForDiffuse ? colorFactor : state.material.diffuseColorFactor;
			Vec4<int> ldiffuse = GELightColorScale(GELightColorProduct(lstate.diffuseColorFactor, mdc), diffuse_factor);
			ldiffuse = scaleAttSpot(ldiffuse);
			LightColorSum(final_color, ldiffuse);
		}

		if (lstate.specular && diffuse_factor >= 0.0f) {
			Vec3<float> H;
			for (int i = 0; i < 3; ++i)
				H[i] = GEAddFloat24(L[i], state.viewDir[i]);
			if (GENormalize(H) == 0.0f)
				H = Vec3f(0.0f, 0.0f, 1.0f);

			float specular_factor = GENormalDot(H, worldnormal, normalRsqrt);
			specular_factor = GELightPow(specular_factor, state.specularExp);

			if (specular_factor > 0.0f) {
				Vec4<int> msc = state.colorForSpecular ? colorFactor : state.material.specularColorFactor;
				Vec4<int> lspecular = GELightColorScale(GELightColorProduct(lstate.specularColorFactor, msc), specular_factor);
				lspecular = scaleAttSpot(lspecular);
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

void Process(VertexData &vertex, const Vec3f &modelpos, const WorldCoords &worldnormal, float normalRsqrt, const State &state) {
#ifdef _M_SSE
	if (cpu_info.bSSE4_1) {
		ProcessSIMD<true>(vertex, modelpos, worldnormal, normalRsqrt, state);
		return;
	}
#endif
	ProcessSIMD<false>(vertex, modelpos, worldnormal, normalRsqrt, state);
}

} // namespace
