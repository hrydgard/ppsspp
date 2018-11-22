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

#include "../GPUState.h"

#include "Lighting.h"

namespace Lighting {

static inline Vec3f GetLightVec(u32 lparams[12], int light) {
	return Vec3<float>(getFloat24(lparams[3 * light]), getFloat24(lparams[3 * light + 1]), getFloat24(lparams[3 * light + 2]));
}

static inline float pspLightPow(float v, float e) {
	if (e == 0.0f) {
		return 1.0f;
	}
	if (v > 0.0f) {
		return pow(v, e);
	}
	// Negative stays negative, so let's just return the original.
	return v;
}

void Process(VertexData& vertex, bool hasColor) {
	const int materialupdate = gstate.materialupdate & (hasColor ? 7 : 0);

	Vec3<float> vcol0 = vertex.color0.rgb().Cast<float>() * Vec3<float>::AssignToAll(1.0f / 255.0f);
	Vec3<float> mec = Vec3<float>::FromRGB(gstate.getMaterialEmissive());

	Vec3<float> mac = (materialupdate & 1) ? vcol0 : Vec3<float>::FromRGB(gstate.getMaterialAmbientRGBA());
	Vec3<float> final_color = mec + mac * Vec3<float>::FromRGB(gstate.getAmbientRGBA());
	Vec3<float> specular_color(0.0f, 0.0f, 0.0f);

	for (unsigned int light = 0; light < 4; ++light) {
		// Always calculate texture coords from lighting results if environment mapping is active
		// TODO: Should specular lighting should affect this, too?  Doesn't in GLES.
		// This should be done even if lighting is disabled altogether.
		if (gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP) {
			Vec3<float> L = GetLightVec(gstate.lpos, light);
			// In other words, L.Length2() == 0.0f means Dot({0, 0, 1}, worldnormal).
			float diffuse_factor = L.Length2() == 0.0f ? vertex.worldnormal.z : Dot(L.Normalized(), vertex.worldnormal);

			if (gstate.getUVLS0() == (int)light)
				vertex.texturecoords.s() = (diffuse_factor + 1.f) / 2.f;

			if (gstate.getUVLS1() == (int)light)
				vertex.texturecoords.t() = (diffuse_factor + 1.f) / 2.f;
		}
	}

	if (!gstate.isLightingEnabled())
		return;

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
		float d = L.Normalize();

		float att = 1.f;
		if (!gstate.isDirectionalLight(light)) {
			att = 1.f / Dot(GetLightVec(gstate.latt, light), Vec3f(1.0f, d, d * d));
			if (att > 1.f) att = 1.f;
			if (att < 0.f) att = 0.f;
		}

		float spot = 1.f;
		if (gstate.isSpotLight(light)) {
			Vec3<float> dir = GetLightVec(gstate.ldir, light);
			float rawSpot = dir.Length2() == 0.0f ? 0.0f : Dot(dir.Normalized(), L);
			float cutoff = getFloat24(gstate.lcutoff[light]);
			if (rawSpot >= cutoff) {
				float conv = getFloat24(gstate.lconv[light]);
				spot = pspLightPow(rawSpot, conv);
			} else {
				spot = 0.f;
			}
		}

		// ambient lighting
		Vec3<float> lac = Vec3<float>::FromRGB(gstate.getLightAmbientColor(light));
		final_color += lac * mac * att * spot;

		// diffuse lighting
		Vec3<float> ldc = Vec3<float>::FromRGB(gstate.getDiffuseColor(light));
		Vec3<float> mdc = (materialupdate & 2) ? vcol0 : Vec3<float>::FromRGB(gstate.getMaterialDiffuse());

		float diffuse_factor = Dot(L, vertex.worldnormal);
		if (gstate.isUsingPoweredDiffuseLight(light)) {
			float k = gstate.getMaterialSpecularCoef();
			diffuse_factor = pspLightPow(diffuse_factor, k);
		}

		if (diffuse_factor > 0.f) {
			final_color += ldc * mdc * diffuse_factor * att * spot;
		}

		if (gstate.isUsingSpecularLight(light) && diffuse_factor >= 0.0f) {
			Vec3<float> H = L + Vec3<float>(0.f, 0.f, 1.f);

			Vec3<float> lsc = Vec3<float>::FromRGB(gstate.getSpecularColor(light));
			Vec3<float> msc = (materialupdate & 4) ? vcol0 : Vec3<float>::FromRGB(gstate.getMaterialSpecular());

			float specular_factor = Dot(H.Normalized(), vertex.worldnormal);
			float k = gstate.getMaterialSpecularCoef();
			specular_factor = pspLightPow(specular_factor, k);

			if (specular_factor > 0.f) {
				specular_color += lsc * msc * specular_factor * att * spot;
			}
		}
	}

	int maa = (materialupdate & 1) ? vertex.color0.a() : gstate.getMaterialAmbientA();
	int final_alpha = (gstate.getAmbientA() * maa) / 255;

	if (gstate.isUsingSecondaryColor()) {
		Vec3<int> final_color_int = (final_color.Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
		vertex.color0 = Vec4<int>(final_color_int, final_alpha);
		vertex.color1 = (specular_color.Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
	} else {
		Vec3<int> final_color_int = ((final_color + specular_color).Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
		vertex.color0 = Vec4<int>(final_color_int, final_alpha);
	}
}

} // namespace
