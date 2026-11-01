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

#include "Common/Math/CrossSIMD.h"
#include "TransformUnit.h"

namespace Lighting {

struct State {
	struct {
		// Pre-normalized if directional.
		Vec4F32 pos;
		// Point and spot lights: the position minus the world translation, the light vector's constant term.
		Vec4F32 translated;
		Vec4F32 att;
		Vec4F32 spotDir;
		float spotDirRsqrt;
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
	Vec4F32 viewDir;

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

// modelpos is (x, y, z, 1). worldnormal isn't normalized; normalRsqrt is its reciprocal length.
void GenerateLightST(VertexData &vertex, Vec4F32 modelpos, Vec4F32 worldnormal, float normalRsqrt, Vec4F32 viewDir);
void Process(VertexData &vertex, Vec4F32 modelpos, Vec4F32 worldnormal, float normalRsqrt, const State &state);

}
