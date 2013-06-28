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

void Process(VertexData& vertex)
{
	if (!gstate.isLightingEnabled())
		return;

	Vec3<int> mec = Vec3<int>(gstate.getMaterialEmissiveR(), gstate.getMaterialEmissiveG(), gstate.getMaterialEmissiveB());

	Vec3<int> mac = (gstate.materialupdate&1)
						? Vec3<int>(gstate.getMaterialAmbientR(), gstate.getMaterialAmbientG(), gstate.getMaterialAmbientB())
						: vertex.color0.rgb();
	vertex.color0.r() = mec.r() + mac.r() * gstate.getAmbientR()/255;
	vertex.color0.g() = mec.g() + mac.g() * gstate.getAmbientG()/255;
	vertex.color0.b() = mec.b() + mac.b() * gstate.getAmbientB()/255;

	int maa = (gstate.materialupdate&1) ? gstate.getMaterialAmbientA() : vertex.color0.a();
	vertex.color0.a() = gstate.getAmbientA() * maa / 255;

	for (unsigned int light = 0; light < 4; ++light) {
		if (!gstate.isLightChanEnabled(light))
			continue;

		Vec3<float> L = Vec3<float>(getFloat24(gstate.lpos[3*light]&0xFFFFFF), getFloat24(gstate.lpos[3*light+1]&0xFFFFFF),getFloat24(gstate.lpos[3*light+2]&0xFFFFFF));
		L -= vertex.worldpos;
		float d = L.Length();

		float lka = getFloat24(gstate.latt[3*light]&0xFFFFFF);
		float lkb = getFloat24(gstate.latt[3*light+1]&0xFFFFFF);
		float lkc = getFloat24(gstate.latt[3*light+2]&0xFFFFFF);
		float att = 1.f;
		if (!gstate.isDirectionalLight(light)) {
			att = 1.f / (lka + lkb * d + lkc * d * d);
			if (att > 1.f) att = 1.f;
			if (att < 0.f) att = 0.f;
		}

		float spot = 1.f;
		if (gstate.isSpotLight(light)) {
			Vec3<float> dir = Vec3<float>(getFloat24(gstate.ldir[3*light]&0xFFFFFF), getFloat24(gstate.ldir[3*light+1]&0xFFFFFF),getFloat24(gstate.ldir[3*light+2]&0xFFFFFF));
			float _spot = Dot(-L,dir) / d / dir.Length();
			float cutoff = getFloat24(gstate.lcutoff[light]&0xFFFFFF);
			if (_spot > cutoff) {
				spot = _spot;
				float conv = getFloat24(gstate.lconv[light]&0xFFFFFF);
				spot = pow(_spot, conv);
			} else {
				spot = 0.f;
			}
		}

		// diffuse lighting
		Vec3<int> ldc = Vec3<int>(gstate.getDiffuseColorR(light), gstate.getDiffuseColorG(light), gstate.getDiffuseColorB(light));
		Vec3<int> mdc = (gstate.materialupdate&2)
							? Vec3<int>(gstate.getMaterialDiffuseR(), gstate.getMaterialDiffuseG(), gstate.getMaterialDiffuseB())
							: vertex.color0.rgb();

		float diffuse_factor = Dot(L,vertex.normal) / d / vertex.worldpos.Length();

		vertex.color0.r() += att * spot * ldc.r() * mdc.r() * diffuse_factor / 255;
		vertex.color0.g() += att * spot * ldc.g() * mdc.g() * diffuse_factor / 255;
		vertex.color0.b() += att * spot * ldc.b() * mdc.b() * diffuse_factor / 255;
	}

	// Currently only implementing ambient+diffuse lighting, so secondary color is always zero anyway
	//if (!gstate.isUsingSecondaryColor())
	{
		vertex.color1 = Vec3<int>(0, 0, 0);
	}
}

} // namespace
