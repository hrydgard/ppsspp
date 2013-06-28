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

	Vec3<float> mec = Vec3<float>(gstate.getMaterialEmissiveR(), gstate.getMaterialEmissiveG(), gstate.getMaterialEmissiveB())/255.f;

	Vec3<float> mac = (gstate.materialupdate&1)
						? Vec3<float>(gstate.getMaterialAmbientR(), gstate.getMaterialAmbientG(), gstate.getMaterialAmbientB())/255.f
						: vertex.color0.rgb();
	vertex.color0.r() = mec.r() + mac.r() * gstate.getAmbientR()/255.f;
	vertex.color0.g() = mec.g() + mac.g() * gstate.getAmbientG()/255.f;
	vertex.color0.b() = mec.b() + mac.b() * gstate.getAmbientB()/255.f;

	float maa = (gstate.materialupdate&1) ? gstate.getMaterialAmbientA()/255.f : vertex.color0.a();
	vertex.color0.a() = gstate.getAmbientA()/255.f * maa;

	for (unsigned int light = 0; light < 4; ++light) {
		if (!gstate.isLightChanEnabled(light))
			continue;

		Vec3<float> ldc = Vec3<float>(gstate.getDiffuseColorR(light), gstate.getDiffuseColorG(light), gstate.getDiffuseColorB(light))/255.f;
		Vec3<float> mdc = (gstate.materialupdate&2)
							? Vec3<float>(gstate.getMaterialDiffuseR(), gstate.getMaterialDiffuseG(), gstate.getMaterialDiffuseB())/255.f
							: vertex.color0.rgb();
		Vec3<float> L = Vec3<float>(getFloat24(gstate.lpos[3*light]&0xFFFFFF), getFloat24(gstate.lpos[3*light+1]&0xFFFFFF),getFloat24(gstate.lpos[3*light+2]&0xFFFFFF));
		L -= vertex.worldpos;

		float factor = Dot(L,vertex.normal) / L.Length() / vertex.worldpos.Length();

		vertex.color0.r() += ldc.r() * mdc.r() * factor;
		vertex.color0.g() += ldc.g() * mdc.g() * factor;
		vertex.color0.b() += ldc.b() * mdc.b() * factor;
	}

	// Currently only implementing ambient+diffuse lighting, so secondary color is always zero anyway
	//if (!gstate.isUsingSecondaryColor())
	{
		vertex.color1 = Vec3<float>(0.f, 0.f, 0.f);
	}
}

} // namespace
