// Copyright (c) 2014- PPSSPP Project.

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

#include <cmath>
#include <cstring>

#include "Common/CommonTypes.h"
#include "GPU/Math3D.h"
#include "GPU/GPU.h"
#include "GPU/GPUState.h"

struct Color4 {
	float r, g, b, a;

	Color4() : r(0), g(0), b(0), a(0) { }
	Color4(float _r, float _g, float _b, float _a = 1.0f)
		: r(_r), g(_g), b(_b), a(_a) {
	}
	Color4(const float in[4]) { r = in[0]; g = in[1]; b = in[2]; a = in[3]; }
	Color4(const float in[3], float alpha) { r = in[0]; g = in[1]; b = in[2]; a = alpha; }

	const float &operator [](int i) const { return *(&r + i); }

	Color4 operator *(float f) const {
		return Color4(f*r, f*g, f*b, f*a);
	}
	Color4 operator *(const Color4 &c) const {
		return Color4(r*c.r, g*c.g, b*c.b, a*c.a);
	}
	Color4 operator +(const Color4 &c) const {
		return Color4(r + c.r, g + c.g, b + c.b, a + c.a);
	}
	void operator +=(const Color4 &c) {
		r += c.r;
		g += c.g;
		b += c.b;
		a += c.a;
	}
	void GetFromRGB(u32 col) {
		b = ((col >> 16) & 0xff) * (1.0f / 255.0f);
		g = ((col >> 8) & 0xff) * (1.0f / 255.0f);
		r = ((col >> 0) & 0xff) * (1.0f / 255.0f);
	}
	void GetFromA(u32 col) {
		a = (col & 0xff) * (1.0f / 255.0f);
	}
};

// The GE's pow() for specular, powered diffuse and the spot exponent: 1 for e <= 0, else 0 for
// x <= 0. Otherwise exp2(e * log2(x)) with log2 and exp2 each a straight line between powers of two
// (Mitchell's approximation), which is what reading a float's bits as an integer gives: exponent
// plus mantissa, scaled by 2^23. Matches hardware within one step of 255 (gpu/lighting/specular).
inline float PSPLightPow(float x, float e) {
	if (!(x > 0.0f)) {
		return e > 0.0f ? 0.0f : 1.0f;
	}
	int32_t ix;
	memcpy(&ix, &x, sizeof(ix));
	float t = (e > 0.0f ? e : 0.0f) * (float)(ix - 0x3F800000) + 1065353216.0f;
	// Also turns NaN into 0, and stays below infinity's bits.
	t = t >= 0.0f ? (t < 2139095039.0f ? t : 2139095039.0f) : 0.0f;
	int32_t iy = (int32_t)t;
	float y;
	memcpy(&y, &iy, sizeof(y));
	return y;
}

// The GE only uses the top 4 bits of the specular coefficient's mantissa.
inline float PSPSpecularCoef(float e) {
	u32 bits;
	memcpy(&bits, &e, sizeof(bits));
	bits &= 0xFFF80000;
	memcpy(&e, &bits, sizeof(bits));
	return e;
}

// The viewer is at infinity along view space +z, so in world space it's the view matrix's third column.
inline Vec3f PSPViewDirection(const float viewMatrix[12]) {
	return Vec3f(viewMatrix[2], viewMatrix[5], viewMatrix[8]).NormalizedOr001(false);
}

inline Vec3f NormalizedOr000(const Vec3f &v) {
	float len2 = v.Length2();
	return len2 > 0.0f ? v * (1.0f / sqrtf(len2)) : Vec3f(0.0f, 0.0f, 0.0f);
}

// Shade mapping (environment map UV gen) coordinate from light l: (N.L + 1) / 2, with L the light's
// direction as lighting sees it (a zero vector stays zero), or the half vector if the light does
// specular. Lighting and light enables don't matter (gpu/lighting/shademap).
inline float PSPShadeMapCoord(int l, const Vec3f &worldpos, const Vec3f &worldnormal, const Vec3f &viewDir) {
	Vec3f L(getFloat24(gstate.lpos[l * 3]), getFloat24(gstate.lpos[l * 3 + 1]), getFloat24(gstate.lpos[l * 3 + 2]));
	if (gstate.getLightType(l) != GE_LIGHTTYPE_DIRECTIONAL) {
		L -= worldpos;
	}
	L = NormalizedOr000(L);
	if (gstate.isUsingSpecularLight(l)) {
		L = NormalizedOr000(L + viewDir);
	}
	return (Dot(L, worldnormal) + 1.0f) * 0.5f;
}

// Convenient way to do precomputation to save the parts of the lighting calculation
// that's common between the many vertices of a draw call.
class Lighter {
public:
	Lighter(int vertType);
	void Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], const Vec3f &pos, const Vec3f &normal);

private:
	inline Vec3f Vec3fFromGE(const u32 *values) const {
		float x = getFloat24(values[0]);
		float y = getFloat24(values[1]);
		float z = getFloat24(values[2]);
		return Vec3f(x, y, z);
	}

	Color4 globalAmbient;
	Color4 materialEmissive;
	Color4 materialAmbient;
	Color4 materialDiffuse;
	Color4 materialSpecular;
	float specCoef_;
	Vec3f viewDir_;
	bool doShadeMapping_;
	int materialUpdate_;

	// Converted light parameters
	Vec3f lpos[4];  // Used by shade UV mapping
	Vec3f ldir[4];
	Vec3f latt[4];
	float lcutoff[4];
	float lconv[4];
	float lcolor[3][4][3];
};

// PSP compatible format so we can use the end of the pipeline in beziers etc
// 8 + 4 + 12 + 12 = 36 bytes
struct SimpleVertex {
	float uv[2];
	union {
		u8 color[4];
		u32_le color_32;
	};
	Vec3Packedf nrm;
	Vec3Packedf pos;
};
