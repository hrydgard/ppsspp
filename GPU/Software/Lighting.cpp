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

	// Currently only implementing ambient lighting, so secondary color is always zero anyway
	//if (!gstate.isUsingSecondaryColor())
	{
		vertex.color1 = Vec3<float>(0.f, 0.f, 0.f);
	}
}

} // namespace
