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
inline void lerpColor(u8 a[4], u8 b[4], float x, u8 out[4]) {
	for (int i = 0; i < 4; i++) {
		out[i] = (u8)((float)a[i] + x * ((float)b[i] - (float)a[i]));
	}
}

// We decode all vertices into a common format for easy interpolation and stuff.
// Not fast but can be optimized later.
struct BezierPatch {
	SimpleVertex *points[16];

	// These are used to generate UVs.
	int u_index, v_index;

	int index;

	// Interpolate colors between control points (bilinear, should be good enough).
	void sampleColor(float u, float v, u8 color[4]) const {
		u *= 3.0f;
		v *= 3.0f;
		int iu = (int)floorf(u);
		int iv = (int)floorf(v);
		int iu2 = iu + 1;
		int iv2 = iv + 1;
		float fracU = u - iu;
		float fracV = v - iv;
		if (iu2 > 3) iu2 = 3;
		if (iv2 > 3) iv2 = 3;

		int tl = iu + 4 * iv;
		int tr = iu2 + 4 * iv;
		int bl = iu + 4 * iv2;
		int br = iu2 + 4 * iv2;

		u8 upperColor[4], lowerColor[4];
		lerpColor(points[tl]->color, points[tr]->color, fracU, upperColor);
		lerpColor(points[bl]->color, points[br]->color, fracU, lowerColor);
		lerpColor(upperColor, lowerColor, fracV, color);
	}

	void sampleTexUV(float u, float v, float &tu, float &tv) const {
		u *= 3.0f;
		v *= 3.0f;
		int iu = (int)floorf(u);
		int iv = (int)floorf(v);
		int iu2 = iu + 1;
		int iv2 = iv + 1;
		float fracU = u - iu;
		float fracV = v - iv;
		if (iu2 > 3) iu2 = 3;
		if (iv2 > 3) iv2 = 3;

		int tl = iu + 4 * iv;
		int tr = iu2 + 4 * iv;
		int bl = iu + 4 * iv2;
		int br = iu2 + 4 * iv2;

		float upperTU = lerp(points[tl]->uv[0], points[tr]->uv[0], fracU);
		float upperTV = lerp(points[tl]->uv[1], points[tr]->uv[1], fracU);
		float lowerTU = lerp(points[bl]->uv[0], points[br]->uv[0], fracU);
		float lowerTV = lerp(points[bl]->uv[1], points[br]->uv[1], fracU);
		tu = lerp(upperTU, lowerTU, fracV);
		tv = lerp(upperTV, lowerTV, fracV);
	}
};

struct SplinePatchLocal {
	SimpleVertex **points;
	int count_u;
	int count_v;
	int type_u;
	int type_v;

	/*
	// Interpolate colors between control points (bilinear, should be good enough).
	void sampleColor(float u, float v, u8 color[4]) const {
		u *= 3.0f;
		v *= 3.0f;
		int iu = (int)floorf(u);
		int iv = (int)floorf(v);
		int iu2 = iu + 1;
		int iv2 = iv + 1;
		float fracU = u - iu;
		float fracV = v - iv;
		if (iu2 >= count_u) iu2 = count_u - 1;
		if (iv2 >= count_v) iv2 = count_v - 1;

		int tl = iu + count_u * iv;
		int tr = iu2 + count_u * iv;
		int bl = iu + count_u * iv2;
		int br = iu2 + count_u * iv2;

		u8 upperColor[4], lowerColor[4];
		lerpColor(points[tl]->color, points[tr]->color, fracU, upperColor);
		lerpColor(points[bl]->color, points[br]->color, fracU, lowerColor);
		lerpColor(upperColor, lowerColor, fracV, color);
	}

	void sampleTexUV(float u, float v, float &tu, float &tv) const {
		u *= 3.0f;
		v *= 3.0f;
		int iu = (int)floorf(u);
		int iv = (int)floorf(v);
		int iu2 = iu + 1;
		int iv2 = iv + 1;
		float fracU = u - iu;
		float fracV = v - iv;
		if (iu2 >= count_u) iu2 = count_u - 1;
		if (iv2 >= count_v) iv2 = count_v - 1;

		int tl = iu + count_u * iv;
		int tr = iu2 + count_u * iv;
		int bl = iu + count_u * iv2;
		int br = iu2 + count_u * iv2;

		float upperTU = lerp(points[tl]->uv[0], points[tr]->uv[0], fracU);
		float upperTV = lerp(points[tl]->uv[1], points[tr]->uv[1], fracU);
		float lowerTU = lerp(points[bl]->uv[0], points[br]->uv[0], fracU);
		float lowerTV = lerp(points[bl]->uv[1], points[br]->uv[1], fracU);
		tu = lerp(upperTU, lowerTU, fracV);
		tv = lerp(upperTV, lowerTV, fracV);
	}*/
};

enum quality {
	LOW_QUALITY = 0,
	MEDIUM_QUALITY = 1,
	HIGH_QUALITY = 2,
};

void TesselateSplinePatch(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int maxVertices);
void TesselateBezierPatch(u8 *&dest, u16 *&indices, int &count, int tess_u, int tess_v, const BezierPatch &patch, u32 origVertType, int maxVertices);
