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

#pragma once

#include "TransformUnit.h"

namespace Lighting {

struct State {
	struct {
		// Pre-normalized if directional.
		Vec3f pos;
		Vec3f att;
		Vec3f spotDir;
		float spotCutoff;
		float spotExp;

		Vec4<int> ambientColorFactor;
		Vec4<int> diffuseColorFactor;
		Vec4<int> specularColorFactor;

		struct {
			bool enabled : 1;
			bool spot : 1;
			bool directional : 1;
			bool poweredDiffuse : 1;
			bool ambient : 1;
			bool diffuse : 1;
			bool specular : 1;
		};
	} lights[4];

	struct {
		Vec4<int> ambientColorFactor;
		Vec4<int> diffuseColorFactor;
		Vec4<int> specularColorFactor;
	} material;

	Vec4<int> baseAmbientColorFactor;
	float specularExp;

	struct {
		bool colorForAmbient : 1;
		bool colorForDiffuse : 1;
		bool colorForSpecular : 1;
		bool setColor1 : 1;
		bool addColor1 : 1;
		bool usesWorldPos : 1;
		bool usesWorldNormal : 1;
	};
};

void ComputeState(State *state, bool hasColor0);

void GenerateLightST(VertexData &vertex, const WorldCoords &worldnormal);
void Process(VertexData &vertex, const WorldCoords &worldpos, const WorldCoords &worldnormal, const State &state);

}
