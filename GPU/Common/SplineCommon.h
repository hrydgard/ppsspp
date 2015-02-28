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

#include "Common/CommonTypes.h"
#include "GPU/Math3D.h"

// PSP compatible format so we can use the end of the pipeline in beziers etc
struct SimpleVertex {
	float uv[2];
	union {
		u8 color[4];
		u32_le color_32;
	};
	Vec3Packedf nrm;
	Vec3Packedf pos;
};

inline float lerp(float a, float b, float x) {
	return a + x * (b - a);
}

// SLOW!
inline void lerpColor(const Vec4f &a, const Vec4f &b, float x, Vec4f &out) {
	for (int i = 0; i < 4; i++) {
		out[i] = a[i] + x * (b[i] - a[i]);
	}
}

inline void lerpColor(const u8 *a, const u8 *b, float x, Vec4f &out) {
	lerpColor(Vec4f::FromRGBA(a), Vec4f::FromRGBA(b), x, out);
}

// We decode all vertices into a common format for easy interpolation and stuff.
// Not fast but can be optimized later.
struct BezierPatch {
	SimpleVertex *points[16];

	// These are used to generate UVs.
	int u_index, v_index;

	int index;

	struct SamplingParams {
		float fracU;
		float fracV;
		int tl;
		int tr;
		int bl;
		int br;

		SamplingParams(float u, float v) {
			u *= 3.0f;
			v *= 3.0f;
			int iu = (int)floorf(u);
			int iv = (int)floorf(v);
			int iu2 = iu + 1;
			int iv2 = iv + 1;
			fracU = u - iu;
			fracV = v - iv;

			if (iu2 > 3)
				iu2 = 3;
			if (iv2 > 3)
				iv2 = 3;

			tl = iu + 4 * iv;
			tr = iu2 + 4 * iv;
			bl = iu + 4 * iv2;
			br = iu2 + 4 * iv2;
		}
	};

	// Interpolate colors between control points (bilinear, should be good enough).
	void sampleColor(float u, float v, u8 color[4]) const {
		const SamplingParams params(u, v);
		Vec4f upperColor, lowerColor, resultColor;
		lerpColor(points[params.tl]->color, points[params.tr]->color, params.fracU, upperColor);
		lerpColor(points[params.bl]->color, points[params.br]->color, params.fracU, lowerColor);
		lerpColor(upperColor, lowerColor, params.fracV, resultColor);
		resultColor.ToRGBA(color);
	}

	void sampleTexUV(float u, float v, float &tu, float &tv) const {
		const SamplingParams params(u, v);
		float upperTU = lerp(points[params.tl]->uv[0], points[params.tr]->uv[0], params.fracU);
		float upperTV = lerp(points[params.tl]->uv[1], points[params.tr]->uv[1], params.fracU);
		float lowerTU = lerp(points[params.bl]->uv[0], points[params.br]->uv[0], params.fracU);
		float lowerTV = lerp(points[params.bl]->uv[1], points[params.br]->uv[1], params.fracU);
		tu = lerp(upperTU, lowerTU, params.fracV);
		tv = lerp(upperTV, lowerTV, params.fracV);
	}
};

struct SplinePatchLocal {
	SimpleVertex **points;
	int count_u;
	int count_v;
	int type_u;
	int type_v;
};

enum quality {
	LOW_QUALITY = 0,
	MEDIUM_QUALITY = 1,
	HIGH_QUALITY = 2,
};

void TesselateSplinePatch(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int maxVertices);
void TesselateBezierPatch(u8 *&dest, u16 *&indices, int &count, int tess_u, int tess_v, const BezierPatch &patch, u32 origVertType, int maxVertices);
