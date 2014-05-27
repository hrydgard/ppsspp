
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

#include <stdio.h>

#include "GPU/GPUState.h"
#include "GPU/Common/TransformCommon.h"

// Check for max first as clamping to max is more common than min when lighting.
inline float clamp(float in, float min, float max) {
	return in > max ? max : (in < min ? min : in);
}

Lighter::Lighter(int vertType) {
	if (!gstate.isLightingEnabled())
		return;

	doShadeMapping_ = gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP;
	materialEmissive.GetFromRGB(gstate.materialemissive);
	materialEmissive.a = 0.0f;
	globalAmbient.GetFromRGB(gstate.ambientcolor);
	globalAmbient.GetFromA(gstate.ambientalpha);
	materialAmbient.GetFromRGB(gstate.materialambient);
	materialAmbient.GetFromA(gstate.materialalpha);
	materialDiffuse.GetFromRGB(gstate.materialdiffuse);
	materialDiffuse.a = 1.0f;
	materialSpecular.GetFromRGB(gstate.materialspecular);
	materialSpecular.a = 1.0f;
	specCoef_ = getFloat24(gstate.materialspecularcoef);
	// viewer_ = Vec3f(-gstate.viewMatrix[9], -gstate.viewMatrix[10], -gstate.viewMatrix[11]);
	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	materialUpdate_ = hasColor ? (gstate.materialupdate & 7) : 0;

	for (int l = 0; l < 4; l++) {
		lcutoff[l] = getFloat24(gstate.lcutoff[l]);
		lconv[l] = getFloat24(gstate.lconv[l]);
		int i = l * 3;
		if (gstate.isLightChanEnabled(l)) {
			lpos[i] = getFloat24(gstate.lpos[i]);
			lpos[i + 1] = getFloat24(gstate.lpos[i + 1]);
			lpos[i + 2] = getFloat24(gstate.lpos[i + 2]);
			ldir[i] = getFloat24(gstate.ldir[i]);
			ldir[i + 1] = getFloat24(gstate.ldir[i + 1]);
			ldir[i + 2] = getFloat24(gstate.ldir[i + 2]);
			latt[i] = getFloat24(gstate.latt[i]);
			latt[i + 1] = getFloat24(gstate.latt[i + 1]);
			latt[i + 2] = getFloat24(gstate.latt[i + 2]);
			for (int t = 0; t < 3; t++) {
				u32 data = gstate.lcolor[l * 3 + t] & 0xFFFFFF;
				float r = (float)(data & 0xff) * (1.0f / 255.0f);
				float g = (float)((data >> 8) & 0xff) * (1.0f / 255.0f);
				float b = (float)(data >> 16) * (1.0f / 255.0f);
				lcolor[t][l][0] = r;
				lcolor[t][l][1] = g;
				lcolor[t][l][2] = b;
			}
		}
	}
}

void Lighter::Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], const Vec3f &pos, const Vec3f &norm) {
	Color4 in(colorIn);

	const Color4 *ambient;
	if (materialUpdate_ & 1)
		ambient = &in;
	else
		ambient = &materialAmbient;

	const Color4 *diffuse;
	if (materialUpdate_ & 2)
		diffuse = &in;
	else
		diffuse = &materialDiffuse;

	const Color4 *specular;
	if (materialUpdate_ & 4)
		specular = &in;
	else
		specular = &materialSpecular;

	Color4 lightSum0 = globalAmbient * *ambient + materialEmissive;
	Color4 lightSum1(0, 0, 0, 0);

	for (int l = 0; l < 4; l++) {
		// can we skip this light?
		if (!gstate.isLightChanEnabled(l))
			continue;

		GELightType type = gstate.getLightType(l);

		Vec3f toLight(0, 0, 0);
		Vec3f lightDir(0, 0, 0);

		if (type == GE_LIGHTTYPE_DIRECTIONAL)
			toLight = Vec3f(&lpos[l * 3]);  // lightdir is for spotlights
		else
			toLight = Vec3f(&lpos[l * 3]) - pos;

		bool doSpecular = gstate.isUsingSpecularLight(l);
		bool poweredDiffuse = gstate.isUsingPoweredDiffuseLight(l);

		float distanceToLight = toLight.Length();
		float dot = 0.0f;
		float angle = 0.0f;
		float lightScale = 0.0f;

		if (distanceToLight > 0.0f) {
			toLight /= distanceToLight;
			dot = Dot(toLight, norm);
		}
		// Clamp dot to zero.
		if (dot < 0.0f) dot = 0.0f;

		if (poweredDiffuse)
			dot = powf(dot, specCoef_);

		// Attenuation
		switch (type) {
		case GE_LIGHTTYPE_DIRECTIONAL:
			lightScale = 1.0f;
			break;
		case GE_LIGHTTYPE_POINT:
			lightScale = clamp(1.0f / (latt[l * 3] + latt[l * 3 + 1] * distanceToLight + latt[l * 3 + 2] * distanceToLight*distanceToLight), 0.0f, 1.0f);
			break;
		case GE_LIGHTTYPE_SPOT:
		case GE_LIGHTTYPE_UNKNOWN:
			lightDir = Vec3f(&ldir[l * 3]);
			angle = Dot(toLight.Normalized(), lightDir.Normalized());
			if (angle >= lcutoff[l])
				lightScale = clamp(1.0f / (latt[l * 3] + latt[l * 3 + 1] * distanceToLight + latt[l * 3 + 2] * distanceToLight*distanceToLight), 0.0f, 1.0f) * powf(angle, lconv[l]);
			break;
		default:
			// ILLEGAL
			break;
		}

		Color4 lightDiff(lcolor[1][l], 0.0f);
		Color4 diff = (lightDiff * *diffuse) * dot;

		// Real PSP specular
		Vec3f toViewer(0, 0, 1);
		// Better specular
		// Vec3f toViewer = (viewer - pos).Normalized();

		if (doSpecular) {
			Vec3f halfVec = (toLight + toViewer);
			halfVec.Normalize();

			dot = Dot(halfVec, norm);
			if (dot > 0.0f) {
				Color4 lightSpec(lcolor[2][l], 0.0f);
				lightSum1 += (lightSpec * *specular * (powf(dot, specCoef_) * lightScale));
			}
		}

		if (gstate.isLightChanEnabled(l)) {
			Color4 lightAmbient(lcolor[0][l], 0.0f);
			lightSum0 += (lightAmbient * *ambient + diff) * lightScale;
		}
	}

	// 4?
	for (int i = 0; i < 4; i++) {
		colorOut0[i] = lightSum0[i] > 1.0f ? 1.0f : lightSum0[i];
		colorOut1[i] = lightSum1[i] > 1.0f ? 1.0f : lightSum1[i];
	}
}
