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
#include "Common/CPUDetect.h"
#include "GPU/GPUState.h"
#include "GPU/Software/Lighting.h"

namespace Lighting {

static inline Vec3f GetLightVec(u32 lparams[12], int light) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	__m128i values = _mm_loadu_si128((__m128i *)&lparams[3 * light]);
	__m128i from24 = _mm_slli_epi32(values, 8);
	return _mm_castsi128_ps(from24);
#else
	return Vec3<float>(getFloat24(lparams[3 * light]), getFloat24(lparams[3 * light + 1]), getFloat24(lparams[3 * light + 2]));
#endif
}

static inline float pspLightPow(float v, float e) {
	if (e <= 0.0f || (std::isnan(e) && std::signbit(e))) {
		return 1.0f;
	}
	if (v > 0.0f) {
		return pow(v, e);
	}
	// Negative stays negative, so let's just return the original.
	return v;
}

static inline float GenerateLightCoord(VertexData &vertex, int light) {
	// TODO: Should specular lighting should affect this, too?  Doesn't in GLES.
	Vec3<float> L = GetLightVec(gstate.lpos, light);
	// In other words, L.Length2() == 0.0f means Dot({0, 0, 1}, worldnormal).
	float diffuse_factor = Dot(L.NormalizedOr001(cpu_info.bSSE4_1), vertex.worldnormal);

	return (diffuse_factor + 1.0f) / 2.0f;
}

void GenerateLightST(VertexData &vertex) {
	// Always calculate texture coords from lighting results if environment mapping is active
	// This should be done even if lighting is disabled altogether.
	vertex.texturecoords.s() = GenerateLightCoord(vertex, gstate.getUVLS0());
	vertex.texturecoords.t() = GenerateLightCoord(vertex, gstate.getUVLS1());
}

void Process(VertexData& vertex, bool hasColor) {
	const int materialupdate = gstate.materialupdate & (hasColor ? 7 : 0);

	Vec4<int> mec = Vec4<int>::FromRGBA(gstate.getMaterialEmissive());

	Vec4<int> mac = (materialupdate & 1) ? vertex.color0 : Vec4<int>::FromRGBA(gstate.getMaterialAmbientRGBA());
	Vec4<int> ac = Vec4<int>::FromRGBA(gstate.getAmbientRGBA());
	// Ambient (whether vertex or material) rounds using the half offset method (like alpha blend.)
	const Vec4<int> ones = Vec4<int>::AssignToAll(1);
	Vec4<int> ambient = ((mac * 2 + ones) * (ac * 2 + ones)) / 1024;

	Vec4<int> final_color = mec + ambient;
	Vec4<int> specular_color = Vec4<int>::AssignToAll(0);

	for (unsigned int light = 0; light < 4; ++light) {
		if (!gstate.isLightChanEnabled(light))
			continue;

		// L =  vector from vertex to light source
		// TODO: Should transfer the light positions to world/view space for these calculations?
		Vec3<float> L = GetLightVec(gstate.lpos, light);
		if (!gstate.isDirectionalLight(light)) {
			L -= vertex.worldpos;
		}
		// TODO: Should this normalize (0, 0, 0) to (0, 0, 1)?
		float d = L.NormalizeOr001();

		float att = 1.0f;
		if (!gstate.isDirectionalLight(light)) {
			att = 1.0f / Dot(GetLightVec(gstate.latt, light), Vec3f(1.0f, d, d * d));
			if (!(att > 0.0f))
				att = 0.0f;
			else if (att > 1.0f)
				att = 1.0f;
		}

		float spot = 1.0f;
		if (gstate.isSpotLight(light)) {
			Vec3<float> dir = GetLightVec(gstate.ldir, light);
			float rawSpot = Dot(dir.Normalized(cpu_info.bSSE4_1), L);
			if (std::isnan(rawSpot))
				rawSpot = std::signbit(rawSpot) ? 0.0f : 1.0f;
			float cutoff = getFloat24(gstate.lcutoff[light]);
			if (std::isnan(cutoff) && std::signbit(cutoff))
				cutoff = 0.0f;
			if (rawSpot >= cutoff) {
				float conv = getFloat24(gstate.lconv[light]);
				spot = pspLightPow(rawSpot, conv);
				if (std::isnan(spot))
					spot = 0.0f;
			} else {
				spot = 0.0f;
			}
		}

		// ambient lighting
		int attspot = (int)ceilf(256 * 2 * att * spot + 1);
		if (attspot > 512)
			attspot = 512;
		Vec4<int> lac = Vec4<int>::FromRGBA(gstate.getLightAmbientColor(light));
		Vec4<int> lambient = ((mac * 2 + ones) * (lac * 2 + ones) * attspot) / (1024 * 512);
		final_color += lambient;

		// diffuse lighting
		float diffuse_factor = Dot(L, vertex.worldnormal);
		if (gstate.isUsingPoweredDiffuseLight(light)) {
			float k = gstate.getMaterialSpecularCoef();
			diffuse_factor = pspLightPow(diffuse_factor, k);
		}

		if (diffuse_factor > 0.0f) {
			int diffuse_attspot = (int)ceilf(attspot * diffuse_factor + 1);
			Vec4<int> ldc = Vec4<int>::FromRGBA(gstate.getDiffuseColor(light));
			Vec4<int> mdc = (materialupdate & 2) ? vertex.color0 : Vec4<int>::FromRGBA(gstate.getMaterialDiffuse());
			Vec4<int> ldiffuse = ((ldc * 2 + ones) * (mdc * 2 + ones) * diffuse_attspot) / (1024 * 512);
			final_color += ldiffuse;
		}

		if (gstate.isUsingSpecularLight(light) && diffuse_factor >= 0.0f) {
			Vec3<float> H = L + Vec3<float>(0.f, 0.f, 1.f);

			float specular_factor = Dot(H.NormalizedOr001(cpu_info.bSSE4_1), vertex.worldnormal);
			float k = gstate.getMaterialSpecularCoef();
			specular_factor = pspLightPow(specular_factor, k);

			if (specular_factor > 0.f) {
				int specular_attspot = (int)ceilf(attspot * specular_factor + 1);
				if (specular_attspot > 512)
					specular_attspot = 512;
				Vec4<int> lsc = Vec4<int>::FromRGBA(gstate.getSpecularColor(light));
				Vec4<int> msc = (materialupdate & 4) ? vertex.color0 : Vec4<int>::FromRGBA(gstate.getMaterialSpecular());
				Vec4<int> lspecular = ((lsc * 2 + ones) * (msc * 2 + ones) * specular_attspot) / (1024 * 512);
				specular_color += lspecular;
			}
		}
	}

	if (gstate.isUsingSecondaryColor()) {
		vertex.color0 = final_color.Clamp(0, 255);
		vertex.color1 = specular_color.Clamp(0, 255).rgb();
	} else {
		vertex.color0 = (final_color + specular_color).Clamp(0, 255);
	}
}

} // namespace
