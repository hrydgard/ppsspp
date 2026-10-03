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
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "Common/CommonTypes.h"
#include "Common/Math/CrossSIMD.h"
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

// A clip space component from the combined matrix (gpu/probe exp32, exp34, exp42). The position is a
// float24; the translation is a term of its own.
inline float GEClipComponent(const Vec3f &v, const float m[16], int c) {
	GERowTerm terms[4] = {
		GEProduct(TruncateToFloat24(v.x), m[c]),
		GEProduct(TruncateToFloat24(v.y), m[4 + c]),
		GEProduct(TruncateToFloat24(v.z), m[8 + c]),
		GEProduct(1.0f, m[12 + c]),
	};
	return GERowSum(terms, 4);
}

// Multiplies two matrices the way the GE combines world, view and projection (gpu/probe exp35, exp42):
// in the order (world * view) * projection, each entry summed like a row in GEClipComponent.
inline void GECombineMatrices(float out[16], const float a[16], const float b[16]) {
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			GERowTerm terms[4];
			for (int k = 0; k < 4; ++k) {
				terms[k] = GEProduct(a[r * 4 + k], b[k * 4 + c]);
			}
			out[r * 4 + c] = GERowSum(terms, 4);
		}
	}
}

// A screen coordinate as the GE computes it (gpu/depth/transformprecision for Z, gpu/probe for X and Y):
// the component divided by w is it times the reciprocal above, truncated to a float24, then scaled and
// offset with GEAdd. Around a center of 2048, that lands X and Y on the 1/16 subpixel grid.
inline float GEViewport(float clipC, float clipW, float scale, float center) {
	const float w = TruncateToFloat24(clipW);
	if (!std::isfinite(w) || !std::isfinite(clipC) || fabsf(w) < FLT_MIN) {
		return clipC * scale / clipW + center;
	}
	const float ndc = ProductToFloat24((double)TruncateToFloat24(clipC) * GERecip(w));
	return GEAdd(ProductToFloat24((double)ndc * scale), center);
}

// Screen Z is floored.
inline float GEScreenZ(float clipZ, float clipW, float zScale, float zCenter) {
	return floorf(GEViewport(clipZ, clipW, zScale, zCenter));
}

// GERowSum4's lanes that it can't do itself: a result that isn't a normal float.
Vec4F32 GERowSum4Fallback(Vec4F32 a, const Vec4F32 b[4], int count, Vec4S32 lanes, Vec4F32 result);

// Four row sums at once: lane i is GERowSum of the products a[k] * b[k][i], k < count, so four rows
// sharing their left operands (the lanes of a), like the four rows of a vertex transform. a is truncated
// to float24s here; every b must be one already (its low 8 mantissa bits zero), as matrix entries are.
// That makes each exact product a 32-bit integer: (1.a' * 1.b') >> 15 = 32768 + a' + b' + (a' b' >> 15),
// a' and b' being the 15 fraction bits, and a' b' fits a 16-bit multiply. The terms are aligned to the
// largest lsb by scaling with a power of two and truncating, which drops low bits toward zero as
// GERowSum's shifts do.
template <int count>
inline Vec4F32 GERowSum4(Vec4F32 a, const Vec4F32 b[4]) {
	static_assert(count >= 1 && count <= 4, "GERowSum4 sums up to four terms");
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	const Vec4S32 fracMask = Vec4S32::Splat(0x7FFF);
	// Below any real lsb, without overflowing the differences below.
	const Vec4S32 noTerm = Vec4S32::Splat(-100000);

	// The left operands' parts, for all terms at once; each term takes its lane.
	const Vec4S32 aBits = Vec4S32FromBits(a) & Vec4S32::Splat((int)0xFFFFFF00);
	const Vec4S32 ea = (aBits & expMask).Shr<23>();
	const Vec4S32 af = aBits.Shr<8>() & fracMask;
	const Vec4S32 aNone = ea.CompareEq(Vec4S32::Zero());
	const Vec4S32 aLsb = ea - Vec4S32::Splat(254 + 15);

	Vec4S32 m[4], lsb[4];
	Vec4S32 lsbMax = noTerm;
	auto term = [&](auto lane) {
		constexpr int k = decltype(lane)::value;
		const Vec4S32 bb = Vec4S32FromBits(b[k]);
		const Vec4S32 eb = (bb & expMask).Shr<23>();
		const Vec4S32 bf = bb.Shr<8>() & fracMask;
		const Vec4S32 afk = af.template SplatLane<k>();
		Vec4S32 prod = afk + Vec4S32::Splat(32768) + bf + bf.Mul16(afk).template Shr<15>();
		const Vec4S32 sign = (bb ^ aBits.template SplatLane<k>()).template Shr<31>();
		prod = (prod ^ sign) - sign;
		// Zero and denormals give no term.
		const Vec4S32 none = aNone.template SplatLane<k>() | eb.CompareEq(Vec4S32::Zero());
		m[k] = prod.AndNot(none);
		lsb[k] = (eb + aLsb.template SplatLane<k>()).AndNot(none) | (noTerm & none);
		lsbMax = lsbMax.Max(lsb[k]);
	};
	term(std::integral_constant<int, 0>{});
	if constexpr (count > 1)
		term(std::integral_constant<int, 1>{});
	if constexpr (count > 2)
		term(std::integral_constant<int, 2>{});
	if constexpr (count > 3)
		term(std::integral_constant<int, 3>{});

	Vec4S32 sum = Vec4S32::Zero();
	for (int k = 0; k < count; ++k) {
		// 2^(lsb - lsbMax), or zero once that's below the float range (a shift of 127 or more drops it anyway).
		Vec4S32 field = lsb[k] - lsbMax + Vec4S32::Splat(127);
		field = field.AndNot(field.Shr<31>());
		const Vec4F32 scale = Vec4F32FromBits(field.Shl<23>());
		sum += Vec4S32FromF32(Vec4F32FromS32(m[k]) * scale);
	}
	// float(sum) * 2^lsbMax, by adding lsbMax to the exponent, which works while the result stays normal.
	const Vec4S32 zero = sum.CompareEq(Vec4S32::Zero());
	const Vec4S32 fbits = Vec4S32FromBits(Vec4F32FromS32(sum));
	const Vec4S32 resultExp = ((fbits & expMask).Shr<23>()) + lsbMax;
	const Vec4F32 result = Vec4F32FromBits(((fbits + lsbMax.Shl<23>()) & Vec4S32::Splat((int)0xFFFFFF00)).AndNot(zero));
	const Vec4S32 outside = (resultExp.CompareLt(Vec4S32::Splat(1)) | resultExp.CompareGt(Vec4S32::Splat(254))).AndNot(zero);
	if (AnyCompareBitsSet(outside))
		return GERowSum4Fallback(a, b, count, outside, result);
	return result;
}

float GEAddFloat24(float a, float b);

// GEAdd4's lanes that it can't do itself: the larger exponent field from 1 to 15 (values under 2^-111) or
// 255 (inf and NaN).
Vec4F32 GEAdd4Fallback(Vec4F32 a, Vec4F32 b, Vec4S32 lanes, Vec4F32 result);

// Four of GEAdd at once: each term goes to 16-bit fixed point at the larger exponent by scaling with a power
// of two and truncating toward zero, which drops what GEAdd's masks clear, then the sum is exact.
inline Vec4F32 GEAdd4(Vec4F32 a, Vec4F32 b) {
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	const Vec4S32 ba = Vec4S32FromBits(a);
	const Vec4S32 bb = Vec4S32FromBits(b);
	const Vec4S32 ea = ba & expMask;
	const Vec4S32 eb = bb & expMask;
	// Denormals count as zero.
	const Vec4F32 za = Vec4F32FromBits(ba.AndNot(ea.CompareEq(Vec4S32::Zero())));
	const Vec4F32 zb = Vec4F32FromBits(bb.AndNot(eb.CompareEq(Vec4S32::Zero())));
	// The larger exponent field, as integers: a float maximum's NaN handling differs between platforms.
	const Vec4S32 e = ea.Max(eb);
	// 2^(142 - e) and 2^(e - 142), e being the biased exponent.
	const Vec4F32 down = Vec4F32FromBits(Vec4S32::Splat((int)(269u << 23)) - e);
	const Vec4F32 up = Vec4F32FromBits(e - Vec4S32::Splat(15 << 23));
	// Both zero (or denormal) makes zero.
	const Vec4S32 none = e.CompareEq(Vec4S32::Zero());
	const Vec4F32 sum = Vec4F32FromS32(Vec4S32FromF32(za * down) + Vec4S32FromF32(zb * down)) * up;
	const Vec4F32 result = Vec4F32FromBits(Vec4S32FromBits(sum).AndNot(none));
	const Vec4S32 outside = e.CompareLt(Vec4S32::Splat(16 << 23)).AndNot(none) | e.CompareEq(expMask);
	if (AnyCompareBitsSet(outside))
		return GEAdd4Fallback(a, b, outside, result);
	return result;
}

// GEMulFloat24x4's lanes that it can't do itself: a denormal, inf or NaN operand, or a result outside the
// normal range.
Vec4F32 GEMulFloat24x4Fallback(Vec4F32 a, Vec4F32 b, Vec4S32 lanes, Vec4F32 result);

// Per lane, the product of a and b truncated to float24, as ProductToFloat24((double)a * b) with both
// truncated to float24 first, except that a zero operand gives +0. With 15 fraction bits each, the
// significand product truncated to 17 bits is 32768 + a' + b' + (a' b' >> 15), as in GERowSum4.
inline Vec4F32 GEMulFloat24x4(Vec4F32 a, Vec4F32 b) {
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	const Vec4S32 fracMask = Vec4S32::Splat(0x7FFF);
	const Vec4S32 aBits = Vec4S32FromBits(a) & Vec4S32::Splat((int)0xFFFFFF00);
	const Vec4S32 bBits = Vec4S32FromBits(b) & Vec4S32::Splat((int)0xFFFFFF00);
	const Vec4S32 ea = (aBits & expMask).Shr<23>();
	const Vec4S32 eb = (bBits & expMask).Shr<23>();
	const Vec4S32 af = aBits.Shr<8>() & fracMask;
	const Vec4S32 bf = bBits.Shr<8>() & fracMask;
	const Vec4S32 prod = af + Vec4S32::Splat(32768) + bf + bf.Mul16(af).Shr<15>();
	// prod * 2^(ea + eb - 254 - 15), by adding to the exponent of the exact float(prod), then truncated.
	const Vec4S32 lsb = ea + eb - Vec4S32::Splat(254 + 15);
	const Vec4S32 fbits = Vec4S32FromBits(Vec4F32FromS32(prod));
	const Vec4S32 resultExp = (fbits & expMask).Shr<23>() + lsb;
	const Vec4S32 sign = (aBits ^ bBits) & Vec4S32::Splat((int)0x80000000);
	const Vec4S32 zero = (aBits & Vec4S32::Splat(0x7FFFFFFF)).CompareEq(Vec4S32::Zero()) | (bBits & Vec4S32::Splat(0x7FFFFFFF)).CompareEq(Vec4S32::Zero());
	const Vec4F32 result = Vec4F32FromBits((((fbits + lsb.Shl<23>()) & Vec4S32::Splat((int)0xFFFFFF00)) | sign).AndNot(zero));
	const Vec4S32 special = ea.CompareEq(Vec4S32::Zero()) | eb.CompareEq(Vec4S32::Zero()) | ea.CompareEq(Vec4S32::Splat(255)) | eb.CompareEq(Vec4S32::Splat(255));
	const Vec4S32 outside = (special | resultExp.CompareLt(Vec4S32::Splat(1)) | resultExp.CompareGt(Vec4S32::Splat(254))).AndNot(zero);
	if (AnyCompareBitsSet(outside))
		return GEMulFloat24x4Fallback(a, b, outside, result);
	return result;
}

// Four of GEAddFloat24 at once.
inline Vec4F32 GEAddFloat24x4(Vec4F32 a, Vec4F32 b) {
	return Vec4F32FromBits(Vec4S32FromBits(GEAdd4(a, b)) & Vec4S32::Splat((int)0xFFFFFF00));
}
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
