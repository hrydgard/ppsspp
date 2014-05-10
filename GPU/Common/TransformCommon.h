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

#include <cstring>
#include "base/basictypes.h"
#include "Common/Log.h"
#include "Common/CommonTypes.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/Math3D.h"

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

// Convenient way to do precomputation to save the parts of the lighting calculation
// that's common between the many vertices of a draw call.
class Lighter {
public:
	Lighter(int vertType);
	void Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], const Vec3f &pos, const Vec3f &normal);

private:
	Color4 globalAmbient;
	Color4 materialEmissive;
	Color4 materialAmbient;
	Color4 materialDiffuse;
	Color4 materialSpecular;
	float specCoef_;
	// Vec3f viewer_;
	bool doShadeMapping_;
	int materialUpdate_;

	// Converted light parameters
public:
	float lpos[12];  // Used by shade UV mapping
private:
	float ldir[12];
	float latt[12];
	float lcutoff[4];
	float lconv[4];
	float lcolor[3][4][3];
};

