// Copyright (c) 2012- PPSSPP Project.

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

// The GE's own arithmetic, as measured on a PSP (gpu/probe experiments; see unittest/TestGEMath.cpp).
// float24s are floats with the low 8 mantissa bits clear.

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "Common/CommonTypes.h"
#include "GPU/Math3D.h"

using namespace Math3D;

inline float TruncateToFloat24(float f) {
	uint32_t bits;
	memcpy(&bits, &f, sizeof(bits));
	bits &= 0xFFFFFF00;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

// A product of two float24s has up to 32 significant bits: form it in a double, and truncate it
// there, since rounding it to a float first could round up across the truncation.
inline float ProductToFloat24(double d) {
	uint64_t bits;
	memcpy(&bits, &d, sizeof(bits));
	if (((bits >> 52) & 0x7FF) != 0x7FF) {
		bits &= ~((1ULL << (52 - 15)) - 1);
	}
	memcpy(&d, &bits, sizeof(d));
	return (float)d;
}

// The GE's reciprocal and reciprocal square root (TransformUnit, depth, UVs, lighting).
float GERecip(float w);
float GERsqrt(float d);

// How the GE adds two float24s, such as a product and a matrix translation or the viewport center:
// the adder has no guard bits, so the smaller term is truncated to the precision of the larger one.
// Clearing the low 8 + (exponent difference) bits of each does that, and the float sum is then exact.
inline float GEAdd(float a, float b) {
	uint32_t ba, bb;
	memcpy(&ba, &a, sizeof(ba));
	memcpy(&bb, &b, sizeof(bb));
	const int ea = (ba >> 23) & 0xFF;
	const int eb = (bb >> 23) & 0xFF;
	if (ea == 0 || eb == 0) {
		// Zero (or denormal, which the GE treats as zero) leaves the other term alone.
		return ea == 0 ? (eb == 0 ? 0.0f : TruncateToFloat24(b)) : TruncateToFloat24(a);
	}
	const int emax = std::max(ea, eb);
	const int na = 8 + emax - ea;
	const int nb = 8 + emax - eb;
	ba = na >= 24 ? (ba & 0x80000000) : (ba & ~((1U << na) - 1));
	bb = nb >= 24 ? (bb & 0x80000000) : (bb & ~((1U << nb) - 1));
	memcpy(&a, &ba, sizeof(a));
	memcpy(&b, &bb, sizeof(b));
	return a + b;
}

// One term of a matrix row as the GE keeps it: the exact product, truncated at a fixed bit weight,
// 2^-15 below the inputs' exponents combined. A significand product of 2 or more keeps a 17th bit.
struct GERowTerm {
	int32_t mantissa;  // The term is mantissa * 2^lsbExp.
	int lsbExp;
	float Value() const { return ldexpf((float)mantissa, lsbExp); }
};

// Zero and denormals (which the GE treats as zero) give a zero term.
inline GERowTerm GEProduct(float a, float b) {
	uint32_t ba, bb;
	memcpy(&ba, &a, sizeof(ba));
	memcpy(&bb, &b, sizeof(bb));
	const int ea = (ba >> 23) & 0xFF;
	const int eb = (bb >> 23) & 0xFF;
	if (ea == 0 || eb == 0) {
		return { 0, INT_MIN };
	}
	// The 24-bit significands make a 48-bit product with 46 fraction bits, of which we keep 15.
	const uint64_t ma = (ba & 0x007FFFFF) | 0x00800000;
	const uint64_t mb = (bb & 0x007FFFFF) | 0x00800000;
	const int32_t m = (int32_t)((ma * mb) >> 31);
	return { ((ba ^ bb) & 0x80000000) ? -m : m, (ea - 127) + (eb - 127) - 15 };
}

// The GE sums a matrix row in one go, with no order: every term is truncated to the bit weight of the
// term with the largest one, then they're added exactly, and the result becomes a float24.
inline float GERowSum(const GERowTerm *terms, int count) {
	int lsbExp = INT_MIN;
	for (int i = 0; i < count; ++i) {
		lsbExp = std::max(lsbExp, terms[i].lsbExp);
	}
	int32_t sum = 0;
	for (int i = 0; i < count; ++i) {
		const int32_t m = terms[i].mantissa;
		const int shift = lsbExp - terms[i].lsbExp;
		if (m != 0 && shift < 32) {
			sum += m < 0 ? -(-m >> shift) : (m >> shift);
		}
	}
	if (sum == 0) {
		return 0.0f;
	}
	return TruncateToFloat24(ldexpf((float)sum, lsbExp));
}

float GEAddFloat24(float a, float b);
float GEDot(const Vec3f &a, const Vec3f &b);
float GENormalize(Vec3f &v);

// The reciprocal triangle setup uses for its planes: q ~ 2^(e + 16) / absDet, e = floor(log2(absDet)).
int64_t GESetupRecip(uint64_t absDet, int *e);

// The float-bits log2 the mip level selection uses, in 1/16 (a signed 1.27.4 value): the exponent and
// the top 4 mantissa bits, a piecewise linear log2 floored to 1/16 (gpu/probe exp58-60).
inline int GELog16(float delta) {
	union FloatBits {
		float f;
		u32 u;
	};
	FloatBits f;
	f.f = delta;
	int useful = (f.u >> 19) & 0x0FFF;
	return useful - 127 * 16;
}

// 15 significant bits, truncated: what a UV plane keeps of a lone value (gpu/probe exp83).
inline float GETruncateTexCoord(float f) {
	u32 bits;
	memcpy(&bits, &f, 4);
	bits &= 0xFFFFFE00;
	memcpy(&f, &bits, 4);
	return f;
}

// u = s * R(q) keeps 24 significant bits, truncated: a float32 product without rounding, not a float24
// (gpu/probe exp82: 23 or 24 bits match, 22 and 25 don't).
inline float GEUVProduct(double d) {
	uint64_t bits;
	memcpy(&bits, &d, sizeof(bits));
	if (((bits >> 52) & 0x7FF) != 0x7FF) {
		bits &= ~((1ULL << (52 - 23)) - 1);
	}
	memcpy(&d, &bits, sizeof(d));
	return (float)d;
}

// The lighting pow (gpu/probe exp98, exp102): Mitchell's approximation on the float's bits, with the
// product exact and truncated toward zero to units of 16 in the bits. e <= 0 gives 1, and a non-positive v is returned as is.
float GELightPow(float v, float e);

// How the GE scales light by a factor (gpu/probe exp61-63): the light and material colors make an
// 8-bit product x = ((2l + 1) * (2m + 1)) >> 10, and each factor (N.L or the specular power, then the
// attenuation and spot) becomes an 8-bit s = floor(256 * f), 0 for a negative f, that is expanded like a color:
// ((2x + 1) * (2s + 1)) >> 10. A factor of 1 (s = 256) leaves x as it is.
inline Vec4<int> GELightColorProduct(const Vec4<int> &lightFactor, const Vec4<int> &materialFactor) {
	return (lightFactor * materialFactor) >> 10;
}

inline Vec4<int> GELightColorScale(const Vec4<int> &x, float f) {
	const int s = std::clamp((int)(256.0f * f), 0, 256);
	return ((x * 2 + Vec4<int>::AssignToAll(1)) * (2 * s + 1)) >> 10;
}

// The alpha of pixel (px, py) of an antialiased line between two points in screen subpixels (gpu/probe
// exp112, 1735 of 1744 pixels). Antialiased lines light the same pixels as plain ones, and this replaces the
// vertex alpha: 128 - |v|, v the floor of 16 times the pixel center's offset from the line along the minor
// axis in subpixels, with the line's minor position from the triangle setup's reciprocal of its length.
int GELineCoverageAlpha(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int px, int py);
