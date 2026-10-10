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

inline Vec4F32 TruncateToFloat24x4(Vec4F32 v) {
	return Vec4F32FromBits(Vec4S32FromBits(v) & Vec4S32::Splat((int)0xFFFFFF00));
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

// The GE's reciprocal: the top 7 bits of w's 15-bit float24 mantissa pick a segment, which the low
// 8 bits interpolate linearly. b is the segment's start in units of 2^-17, m its slope in units of
// 2^-23 per step, and 63 a rounding bias below half. Measured for every mantissa on hardware.
struct GERecipSegment {
	int32_t b;
	int32_t m;
};
extern const GERecipSegment geRecipSegments[128];

// f * 2^n with the given sign bit, by adding n to f's exponent: f must be positive, and the result normal.
inline float ScaleByPow2(float f, int n, uint32_t sign) {
	uint32_t bits;
	memcpy(&bits, &f, sizeof(bits));
	bits = (uint32_t)((int32_t)bits + n * (1 << 23)) | sign;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

// The GE's reciprocal (TransformUnit, depth, UVs, lighting): inline, since the rasterizer takes one per pixel.
// w must be a normal float24. Returns a float24 (q has 16 significant bits, or is 2^16).
inline float GERecip(float w) {
	uint32_t bits;
	memcpy(&bits, &w, sizeof(bits));
	const uint32_t i = (bits >> 8) & 0x7FFF;
	const int e = (int)((bits >> 23) & 0xFF) - 127;  // |w| = 1.i * 2^e
	const GERecipSegment &seg = geRecipSegments[i >> 8];
	const int32_t q = (64 * seg.b + 63 + seg.m * (int32_t)(i & 255)) >> 7;  // 1 / 1.i in units of 2^-16
	if (e >= 126) {
		// The result can be a denormal (q is at most 2^16).
		return copysign(ldexpf((float)q, -16 - e), w);
	}
	return ScaleByPow2((float)q, -16 - e, bits & 0x80000000);
}

// The GE's reciprocal square root.
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
		if (m == 0) {
			// Its lsbExp may be INT_MIN.
			continue;
		}
		const int shift = lsbExp - terms[i].lsbExp;
		if (shift < 32) {
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

// One right operand of GERowSum4 taken apart: its exponent fields and fraction bits, its sign as a mask,
// and where it's zero or denormal.
struct GERowSumOperand {
	Vec4S32 eb;
	Vec4S32 bf;
	Vec4S32 sign;
	Vec4S32 none;

	static GERowSumOperand From(Vec4F32 b) {
		const Vec4S32 bb = Vec4S32FromBits(b);
		const Vec4S32 eb = (bb & Vec4S32::Splat(0x7F800000)).Shr<23>();
		return { eb, bb.Shr<8>() & Vec4S32::Splat(0x7FFF), bb.Shr<31>(), eb.CompareEq(Vec4S32::Zero()) };
	}
};

// GERowSum4's right operands taken apart once, for ones that stay the same over many sums (a matrix's rows).
struct GERowSumRows {
	Vec4F32 rows[4];
	GERowSumOperand parts[4];

	void Set(const Vec4F32 b[4]) {
		for (int k = 0; k < 4; ++k) {
			rows[k] = b[k];
			parts[k] = GERowSumOperand::From(b[k]);
		}
	}
};

// Four row sums at once: lane i is GERowSum of the products a[k] * b[k][i], k < count, so four rows
// sharing their left operands (the lanes of a), like the four rows of a vertex transform. a is truncated
// to float24s here; every b must be one already (its low 8 mantissa bits zero), as matrix entries are.
// That makes each exact product a 32-bit integer: (1.a' * 1.b') >> 15 = 32768 + a' + b' + (a' b' >> 15),
// a' and b' being the 15 fraction bits, and a' b' fits a 16-bit multiply. The terms are aligned to the
// largest lsb by scaling with a power of two and truncating, which drops low bits toward zero as
// GERowSum's shifts do.
template <int count, typename Operand>
inline Vec4F32 GERowSum4Core(Vec4F32 a, const Vec4F32 b[4], Operand operand) {
	static_assert(count >= 1 && count <= 4, "GERowSum4 sums up to four terms");
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	const Vec4S32 fracMask = Vec4S32::Splat(0x7FFF);
	// Below any real lsb, without overflowing the differences below.
	const Vec4S32 noTerm = Vec4S32::Splat(-100000);

	// The left operands' parts, for all terms at once; each term takes its lane.
	const Vec4S32 aBits = Vec4S32FromBits(a) & Vec4S32::Splat((int)0xFFFFFF00);
	const Vec4S32 ea = (aBits & expMask).Shr<23>();
	const Vec4S32 af = aBits.Shr<8>() & fracMask;
	const Vec4S32 aSign = aBits.Shr<31>();
	const Vec4S32 aNone = ea.CompareEq(Vec4S32::Zero());
	const Vec4S32 aLsb = ea - Vec4S32::Splat(254 + 15);

	Vec4S32 m[4], lsb[4];
	Vec4S32 lsbMax = noTerm;
	auto term = [&](auto lane) {
		constexpr int k = decltype(lane)::value;
		const GERowSumOperand bk = operand(k);
		const Vec4S32 afk = af.template SplatLane<k>();
		Vec4S32 prod = afk + Vec4S32::Splat(32768) + bk.bf + bk.bf.Mul16(afk).template Shr<15>();
		const Vec4S32 sign = bk.sign ^ aSign.template SplatLane<k>();
		prod = (prod ^ sign) - sign;
		// Zero and denormals give no term.
		const Vec4S32 none = aNone.template SplatLane<k>() | bk.none;
		m[k] = prod.AndNot(none);
		lsb[k] = (bk.eb + aLsb.template SplatLane<k>()).AndNot(none) | (noTerm & none);
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

template <int count>
inline Vec4F32 GERowSum4(Vec4F32 a, const Vec4F32 b[4]) {
	return GERowSum4Core<count>(a, b, [&](int k) { return GERowSumOperand::From(b[k]); });
}

// With the right operands taken apart beforehand.
template <int count>
inline Vec4F32 GERowSum4(Vec4F32 a, const GERowSumRows &b) {
	return GERowSum4Core<count>(a, b.rows, [&](int k) { return b.parts[k]; });
}

float GEAddFloat24(float a, float b);

// GEAdd4's lanes that it can't do itself: the larger exponent field from 1 to 15 (values under 2^-111) or
// 255 (inf and NaN).
Vec4F32 GEAdd4Fallback(Vec4F32 a, Vec4F32 b, Vec4S32 lanes, Vec4F32 result);

// Four of GEAdd at once: each term goes to 16-bit fixed point at the larger exponent by scaling with a power
// of two and truncating toward zero, which drops what GEAdd's masks clear, then the sum is exact.
// GEAdd4 without its fallback: the lanes that need it are added to bad, and their results are wrong.
inline Vec4F32 GEAdd4Unchecked(Vec4F32 a, Vec4F32 b, Vec4S32 &bad) {
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
	bad = bad | e.CompareLt(Vec4S32::Splat(16 << 23)).AndNot(none) | e.CompareEq(expMask);
	return result;
}

inline Vec4F32 GEAdd4(Vec4F32 a, Vec4F32 b) {
	Vec4S32 outside = Vec4S32::Zero();
	const Vec4F32 result = GEAdd4Unchecked(a, b, outside);
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
// GEMulFloat24x4 without its fallback, as GEAdd4Unchecked.
inline Vec4F32 GEMulFloat24x4Unchecked(Vec4F32 a, Vec4F32 b, Vec4S32 &bad) {
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
	bad = bad | (special | resultExp.CompareLt(Vec4S32::Splat(1)) | resultExp.CompareGt(Vec4S32::Splat(254))).AndNot(zero);
	return result;
}

inline Vec4F32 GEMulFloat24x4(Vec4F32 a, Vec4F32 b) {
	Vec4S32 outside = Vec4S32::Zero();
	const Vec4F32 result = GEMulFloat24x4Unchecked(a, b, outside);
	if (AnyCompareBitsSet(outside))
		return GEMulFloat24x4Fallback(a, b, outside, result);
	return result;
}

// Four of GEAddFloat24 at once.
inline Vec4F32 GEAddFloat24x4(Vec4F32 a, Vec4F32 b) {
	return TruncateToFloat24x4(GEAdd4(a, b));
}

inline Vec4F32 GEAddFloat24x4Unchecked(Vec4F32 a, Vec4F32 b, Vec4S32 &bad) {
	return TruncateToFloat24x4(GEAdd4Unchecked(a, b, bad));
}
float GEDot(const Vec3f &a, const Vec3f &b);
float GENormalize(Vec3f &v);

// GEDot of lanes 0-2: GERowSum4's terms, one per lane, summed across the lanes.
inline float GEDot3(Vec4F32 a, Vec4F32 b) {
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	const Vec4S32 fracMask = Vec4S32::Splat(0x7FFF);
	const Vec4S32 noTerm = Vec4S32::Splat(-100000);
	alignas(16) static const int lane3[4] = { 0, 0, 0, -1 };

	const Vec4S32 aBits = Vec4S32FromBits(a) & Vec4S32::Splat((int)0xFFFFFF00);
	const Vec4S32 bBits = Vec4S32FromBits(b) & Vec4S32::Splat((int)0xFFFFFF00);
	const Vec4S32 ea = (aBits & expMask).Shr<23>();
	const Vec4S32 eb = (bBits & expMask).Shr<23>();
	const Vec4S32 af = aBits.Shr<8>() & fracMask;
	const Vec4S32 bf = bBits.Shr<8>() & fracMask;
	Vec4S32 prod = af + Vec4S32::Splat(32768) + bf + bf.Mul16(af).Shr<15>();
	const Vec4S32 sign = (aBits ^ bBits).Shr<31>();
	prod = (prod ^ sign) - sign;
	const Vec4S32 none = ea.CompareEq(Vec4S32::Zero()) | eb.CompareEq(Vec4S32::Zero()) | Vec4S32::LoadAligned(lane3);
	const Vec4S32 m = prod.AndNot(none);
	const Vec4S32 lsb = (ea + eb - Vec4S32::Splat(254 + 15)).AndNot(none) | (noTerm & none);
	const Vec4S32 lsbMax = lsb.SplatLane<0>().Max(lsb.SplatLane<1>()).Max(lsb.SplatLane<2>());

	// Aligned to the largest lsb as in GERowSum4.
	Vec4S32 field = lsb - lsbMax + Vec4S32::Splat(127);
	field = field.AndNot(field.Shr<31>());
	const Vec4S32 aligned = Vec4S32FromF32(Vec4F32FromS32(m) * Vec4F32FromBits(field.Shl<23>()));
	const int sum = (aligned + aligned.SplatLane<1>() + aligned.SplatLane<2>()).GetLane<0>();
	if (sum == 0)
		return 0.0f;
	return TruncateToFloat24(ldexpf((float)sum, lsbMax.GetLane<0>()));
}

// GENormalize of lanes 0-2, which zeroes lane 3. Denormal components (in or out) become zero, as in GEProduct. d2 is the squared length, GEDot3(v, v).
inline float GENormalize4(Vec4F32 &v, float d2) {
	if (!(d2 > 0.0f) || !std::isfinite(d2))
		return 0.0f;
	const float r = GERsqrt(d2);
	// A one-term row sum is the truncated product.
	const Vec4F32 rows[1] = { TruncateToFloat24x4(v.WithLane3Zero()) };
	v = GERowSum4<1>(Vec4F32::Splat(r), rows);
	return ProductToFloat24((double)d2 * r);
}

inline float GENormalize4(Vec4F32 &v) {
	return GENormalize4(v, GEDot3(v, v));
}

// The reciprocal triangle setup uses for its planes: q ~ 2^(e + 16) / absDet, e = floor(log2(absDet)).
int64_t GESetupRecip(uint64_t absDet, int *e);

// Whether a through-mode sprite's u plane has an exact gradient, so a pixel center whose exact u is on a texel
// edge lands on it rather than just below. The gradient comes from the setup reciprocal of the whole area,
// exact only for a power of two, whatever the height (gpu/probe exp87, exp163). x and y in subpixels, s the
// left and right u divided by the texture width.
bool GESpriteUPlaneExact(int64_t left, int64_t top, int64_t right, int64_t bottom, double sLeft, double sRight);

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

// Where q isn't positive, the GE samples the last texel in u and v, whatever s and t are and in both clamp
// and wrap modes (gpu/texmtx/negq): a saturated coordinate. 1 - 2^-16 lands on texel size - 1 with the highest
// subtexel fraction, for any texture size.
constexpr float GE_NONPOSITIVE_Q_UV = 1.0f - 1.0f / 65536.0f;
constexpr int32_t GE_NONPOSITIVE_Q_UV_BITS = 0x3F7FFF00;

// The rasterizer's texture coordinates for four pixels (Rasterizer's GetTextureCoordinatesGE): from the UV
// planes' values at them (each times 2^scaleExp), q as a float24, and s and t as GEUVProduct(float24 *
// GERecip(q)), or GE_NONPOSITIVE_Q_UV where q isn't positive. False, with the outputs untouched, when a lane
// needs more than the vector path does (a value of 2^24 or more, a denormal or out of range result): then do
// it scalar. The core takes the values as lanes, each below 2^24; with check false, the caller knows they're
// inside what the vector path covers (GEUVSpanSafe).
template <bool check>
inline bool GEUVSpanCore(Vec4S32 qv, Vec4S32 sv, Vec4S32 tv, int expQ, int expS, int expT, float *s, float *t, float *q) {
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	Vec4S32 bad = Vec4S32::Zero();
	// value * 2^exp, exactly (24 bits fit a float), truncated to a float24.
	auto toFloat24 = [&](Vec4S32 vi, int exp) {
		const Vec4S32 fb = Vec4S32FromBits(Vec4F32FromS32(vi));
		const Vec4S32 zero = vi.CompareEq(Vec4S32::Zero());
		const Vec4S32 field = (fb & expMask).Shr<23>() + Vec4S32::Splat(exp);
		if constexpr (check)
			bad = bad | (field.CompareLt(Vec4S32::Splat(1)) | field.CompareGt(Vec4S32::Splat(254))).AndNot(zero);
		return ((fb + Vec4S32::Splat(exp).Shl<23>()) & Vec4S32::Splat((int)0xFFFFFF00)).AndNot(zero);
	};
	const Vec4S32 qb = toFloat24(qv, expQ);
	// A bad q matters in any lane (it's also for the mip level), the rest only where q > 0.
	const Vec4S32 badQ = bad;
	bad = Vec4S32::Zero();
	(void)badQ;
	const Vec4S32 sb = toFloat24(sv, expS);
	const Vec4S32 tb = toFloat24(tv, expT);

	// GERecip, where q > 0.
	const Vec4S32 valid = qb.CompareGt(Vec4S32::Zero());
	const Vec4S32 qe = (qb & expMask).Shr<23>() - Vec4S32::Splat(127);
	if constexpr (check)
		bad = bad | qe.CompareGt(Vec4S32::Splat(125));
	alignas(16) int32_t qbits[4];
	Vec4S32(qb).Store(qbits);
	// Gathered in registers: stored to an array one by one and loaded as a vector, they'd stall.
	Vec4S32 segB, segM;
	auto seg = [&](int i) { return &geRecipSegments[(qbits[i] >> 16) & 0x7F].b; };
	Vec4S32::LoadPairs(seg(0), seg(1), seg(2), seg(3), segB, segM);
	const Vec4S32 qi = qb.Shr<8>() & Vec4S32::Splat(0xFF);
	const Vec4S32 recip = (segB.Shl<6>() + Vec4S32::Splat(63) + segM.Mul(qi)).Shr<7>();
	const Vec4S32 rb = Vec4S32FromBits(Vec4F32FromS32(recip)) - (Vec4S32::Splat(16) + qe).Shl<23>();

	// GEUVProduct(x * r): the 16-bit significands' product, cut to 24 bits.
	const Vec4S32 rA = (rb.Shr<8>() & Vec4S32::Splat(0x7FFF)) | Vec4S32::Splat(0x8000);
	const Vec4S32 re = (rb & expMask).Shr<23>();
	auto product = [&](Vec4S32 xb) {
		const Vec4S32 xe = (xb & expMask).Shr<23>();
		const Vec4S32 xZero = xe.CompareEq(Vec4S32::Zero());
		// A denormal (not zero) isn't handled.
		if constexpr (check)
			bad = bad | (xZero & (xb & Vec4S32::Splat(0x7FFFFFFF)).CompareGt(Vec4S32::Zero()));
		const Vec4S32 xA = (xb.Shr<8>() & Vec4S32::Splat(0x7FFF)) | Vec4S32::Splat(0x8000);
		const Vec4S32 p = xA.Mul(rA);
		// The product is 2^30 to 2^32: keep 24 bits from bit 31 or 30.
		const Vec4S32 hi = p.CompareLt(Vec4S32::Zero());
		const Vec4S32 m = (hi & (p.Shr<8>() & Vec4S32::Splat(0x00FFFFFF))) | p.Shr<7>().AndNot(hi);
		const Vec4S32 shift = Vec4S32::Splat(7) - hi;
		// m * 2^(xe + re - 254 - 30 + shift); float(m) has the exponent field 150.
		const Vec4S32 adjust = xe + re + shift - Vec4S32::Splat(254 + 30);
		const Vec4S32 field = Vec4S32::Splat(150) + adjust;
		if constexpr (check)
			bad = bad | (field.CompareLt(Vec4S32::Splat(1)) | field.CompareGt(Vec4S32::Splat(254))).AndNot(xZero);
		const Vec4S32 sign = xb & Vec4S32::Splat((int)0x80000000);
		const Vec4S32 result = (Vec4S32FromBits(Vec4F32FromS32(m)) + adjust.Shl<23>()) | sign;
		// Zero stays as it is, with its sign.
		return ((result.AndNot(xZero) | (xb & xZero)) & valid) | Vec4S32::Splat(GE_NONPOSITIVE_Q_UV_BITS).AndNot(valid);
	};
	const Vec4S32 so = product(sb);
	const Vec4S32 to = product(tb);
	if constexpr (check) {
		if (AnyCompareBitsSet(badQ | (bad & valid)))
			return false;
	}
	Vec4F32FromBits(qb).Store(q);
	Vec4F32FromBits(so).Store(s);
	Vec4F32FromBits(to).Store(t);
	return true;
}

// From 64-bit plane values, checked.
inline bool GEUVSpan(const int64_t qs[4], const int64_t ss[4], const int64_t ts[4], int expQ, int expS, int expT, float *s, float *t, float *q) {
	for (int i = 0; i < 4; ++i) {
		if (qs[i] <= -(1 << 24) || qs[i] >= (1 << 24) || ss[i] <= -(1 << 24) || ss[i] >= (1 << 24) || ts[i] <= -(1 << 24) || ts[i] >= (1 << 24))
			return false;
	}
	// Picked in registers: storing them one by one and loading them as a vector stalls.
	return GEUVSpanCore<true>(Vec4S32::LoadS64Low(qs), Vec4S32::LoadS64Low(ss), Vec4S32::LoadS64Low(ts), expQ, expS, expT, s, t, q);
}

// Whether GEUVSpanCore<false> covers any values below 2^24 with these plane exponents: the float24s, the
// reciprocal and the products all stay normal.
inline bool GEUVSpanSafe(int expQ, int expS, int expT) {
	auto inRange = [](int exp) { return exp >= -126 && exp <= 104; };
	return inRange(expQ) && inRange(expS) && inRange(expT) && expQ <= 102 &&
		expS - expQ >= -102 && expS - expQ <= 103 && expT - expQ >= -102 && expT - expQ <= 103;
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
