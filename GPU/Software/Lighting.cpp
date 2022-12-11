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
#else
	return Vec3<float>(getFloat24(lparams[3 * light]), getFloat24(lparams[3 * light + 1]), getFloat24(lparams[3 * light + 2]));
#endif
}

static inline float pspLightPow(float v, float e) {
	if (v > 0.0f) {
		return pow(v, e);
	}
	// Negative stays negative, so let's just return the original.
	return v;
}

static inline Vec4<int> LightColorFactor(const Vec4<int> &expanded, const Vec4<int> &ones) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	return _mm_add_epi32(_mm_slli_epi32(expanded.ivec, 1), ones.ivec);
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
	for (int light = 0; light < 4; ++light) {
		auto &lstate = state->lights[light];
		lstate.enabled = gstate.isLightChanEnabled(light);
		if (!lstate.enabled)
			continue;

		lstate.spot = gstate.isSpotLight(light);
		lstate.directional = gstate.isDirectionalLight(light);
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

		lstate.pos = GetLightVec(gstate.lpos, light);
		if (lstate.directional)
			lstate.pos.NormalizeOr001();
		else
			lstate.att = GetLightVec(gstate.latt, light);

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

void Process(VertexData &vertex, const WorldCoords &worldpos, const WorldCoords &worldnormal, const State &state) {
	// Lighting blending rounds using the half offset method (like alpha blend.)
	const Vec4<int> ones = Vec4<int>::AssignToAll(1);
	Vec4<int> colorFactor;
	if (state.colorForAmbient || state.colorForDiffuse || state.colorForSpecular)
		colorFactor = LightColorFactor(vertex.color0, ones);

	Vec4<int> mec = Vec4<int>::FromRGBA(gstate.getMaterialEmissive());

	Vec4<int> mac = state.colorForAmbient ? colorFactor : state.material.ambientColorFactor;
	Vec4<int> ambient = (mac * state.baseAmbientColorFactor) / 1024;

	Vec4<int> final_color = mec + ambient;
	Vec4<int> specular_color = Vec4<int>::AssignToAll(0);

	for (unsigned int light = 0; light < 4; ++light) {
		const auto &lstate = state.lights[light];
		if (!lstate.enabled)
			continue;

		// L =  vector from vertex to light source
		// TODO: Should transfer the light positions to world/view space for these calculations?
		Vec3<float> L = lstate.pos;
		float att = 1.0f;
		if (!lstate.directional) {
			L -= worldpos;
			// TODO: Should this normalize (0, 0, 0) to (0, 0, 1)?
			float d = L.NormalizeOr001();

			att = 1.0f / Dot(lstate.att, Vec3f(1.0f, d, d * d));
			if (!(att > 0.0f))
				att = 0.0f;
			else if (att > 1.0f)
				att = 1.0f;
		}

		float spot = 1.0f;
		if (lstate.spot) {
			float rawSpot = Dot(lstate.spotDir, L);
			if (std::isnan(rawSpot))
				rawSpot = std::signbit(rawSpot) ? 0.0f : 1.0f;

			if (rawSpot >= lstate.spotCutoff) {
				spot = pspLightPow(rawSpot, lstate.spotExp);
				if (std::isnan(spot))
					spot = 0.0f;
			} else {
				spot = 0.0f;
			}
		}

		// ambient lighting
		if (lstate.ambient) {
			int attspot = (int)ceilf(256 * 2 * att * spot + 1);
			if (attspot > 512)
				attspot = 512;
			Vec4<int> lambient = (mac * lstate.ambientColorFactor * attspot) / (1024 * 512);
			final_color += lambient;
		}

		// diffuse lighting
		float diffuse_factor;
		if (lstate.diffuse || lstate.specular) {
			diffuse_factor = Dot(L, worldnormal);
			if (lstate.poweredDiffuse) {
				diffuse_factor = pspLightPow(diffuse_factor, state.specularExp);
			}
		}

		if (lstate.diffuse && diffuse_factor > 0.0f) {
			int diffuse_attspot = (int)ceilf(256 * 2 * att * spot * diffuse_factor + 1);
			if (diffuse_attspot > 512)
				diffuse_attspot = 512;
			Vec4<int> mdc = state.colorForDiffuse ? colorFactor : state.material.diffuseColorFactor;
			Vec4<int> ldiffuse = (lstate.diffuseColorFactor * mdc * diffuse_attspot) / (1024 * 512);
			final_color += ldiffuse;
		}

		if (lstate.specular && diffuse_factor >= 0.0f) {
			Vec3<float> H = L + Vec3<float>(0.f, 0.f, 1.f);

			float specular_factor = Dot(H.NormalizedOr001(cpu_info.bSSE4_1), worldnormal);
			specular_factor = pspLightPow(specular_factor, state.specularExp);

			if (specular_factor > 0.0f) {
				int specular_attspot = (int)ceilf(256 * 2 * att * spot * specular_factor + 1);
				if (specular_attspot > 512)
					specular_attspot = 512;

				Vec4<int> msc = state.colorForSpecular ? colorFactor : state.material.specularColorFactor;
				Vec4<int> lspecular = (lstate.specularColorFactor * msc * specular_attspot) / (1024 * 512);
				specular_color += lspecular;
			}
		}
	}

	if (state.setColor1) {
		vertex.color0 = final_color.Clamp(0, 255).ToRGBA();
		vertex.color1 = specular_color.Clamp(0, 255).rgb().ToRGB();
	} else if (state.addColor1) {
		vertex.color0 = (final_color + specular_color).Clamp(0, 255).ToRGBA();
	} else {
		vertex.color0 = final_color.Clamp(0, 255).ToRGBA();
	}
}

} // namespace
