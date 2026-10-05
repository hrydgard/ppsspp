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

// The GE arithmetic in GPU/Software/GEMath.h against values measured on a PSP. The probes are in
// pspautotests/tests/gpu/probe (expNN.py), with notes in the ppsspp-re repository: reciprocals from
// exp15 and exp38-41, the AA line pixels from exp112. The rest are cases those probes pinned down.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "GPU/Common/TransformCommon.h"
#include "GPU/Software/GEMath.h"

#include "unittest/UnitTest.h"

static float FromBits(uint32_t u) {
	float f;
	memcpy(&f, &u, sizeof(f));
	return f;
}

static uint32_t ToBits(float f) {
	uint32_t u;
	memcpy(&u, &f, sizeof(u));
	return u;
}

static bool TestFloat24() {
	// 15 stored mantissa bits, truncated toward zero.
	EXPECT_EQ_FLOAT(TruncateToFloat24(1.0f + ldexpf(1.0f, -16)), 1.0f);
	EXPECT_EQ_FLOAT(TruncateToFloat24(1.0f + ldexpf(1.0f, -15)), 1.0f + ldexpf(1.0f, -15));
	EXPECT_EQ_FLOAT(TruncateToFloat24(-1.0f - ldexpf(3.0f, -17)), -1.0f);
	EXPECT_EQ_FLOAT(TruncateToFloat24(-1.0f - ldexpf(5.0f, -17)), -1.0f - ldexpf(1.0f, -15));
	// The product is truncated in double, not rounded to a float first.
	EXPECT_EQ_FLOAT(ProductToFloat24(1.0 + ldexp(1.0, -15) + ldexp(1.0, -16) - ldexp(1.0, -40)), 1.0f + ldexpf(1.0f, -15));
	return true;
}

static bool TestGERecip() {
	// 1 / (1 + i / 32768) in units of 2^-16, measured on a PSP for every mantissa (exp15).
	static const int measured[][2] = {
		{ 0, 65536 }, { 1, 65534 }, { 255, 65030 }, { 256, 65027 }, { 909, 63768 }, { 1149, 63316 }, { 2816, 60350 }, { 4727, 57274 },
		{ 8168, 52459 }, { 8423, 52135 }, { 8953, 51472 }, { 10218, 49957 }, { 10809, 49280 }, { 13408, 46507 }, { 13869, 46047 }, { 14837, 45110 },
		{ 15497, 44494 }, { 16762, 43357 }, { 17635, 42606 }, { 18007, 42294 }, { 21093, 39871 }, { 24689, 37376 }, { 24729, 37350 }, { 26789, 36057 },
		{ 26951, 35960 }, { 27112, 35863 }, { 27453, 35660 }, { 28359, 35131 }, { 28463, 35072 }, { 31067, 33641 }, { 31123, 33612 }, { 32767, 32769 },
	};
	for (const auto &m : measured) {
		const float w = FromBits(0x3F800000 | (m[0] << 8));
		const float expected = ldexpf((float)m[1], -16);
		if (GERecip(w) != expected) {
			printf("GERecip(1 + %d / 32768) = %.9f, PSP %.9f\n", m[0], GERecip(w), expected);
			return false;
		}
	}
	// Sign and exponent are carried over.
	EXPECT_EQ_FLOAT(GERecip(-2.0f), -0.5f);
	EXPECT_EQ_FLOAT(GERecip(0.25f), 4.0f);
	return true;
}

static bool TestGERsqrt() {
	// The table for odd exponents stores 1 / sqrt(2 * 1.i) (exp69).
	EXPECT_EQ_FLOAT(GERsqrt(1.0f), 1.0f);
	EXPECT_EQ_FLOAT(GERsqrt(4.0f), 0.5f);
	EXPECT_EQ_FLOAT(GERsqrt(0.25f), 2.0f);
	EXPECT_EQ_FLOAT(GERsqrt(2.0f), ldexpf(46340.0f, -16));
	return true;
}

static bool TestGEAdd() {
	// No guard bits: the smaller term is truncated (toward zero) to the larger one's last bit.
	EXPECT_EQ_FLOAT(GEAddFloat24(1.0f, ldexpf(3.0f, -17)), 1.0f);
	EXPECT_EQ_FLOAT(GEAddFloat24(1.0f, ldexpf(5.0f, -17)), 1.0f + ldexpf(1.0f, -15));
	EXPECT_EQ_FLOAT(GEAddFloat24(1.0f, -ldexpf(5.0f, -17)), 1.0f - ldexpf(1.0f, -15));
	EXPECT_EQ_FLOAT(GEAddFloat24(0.0f, 1.0f + ldexpf(1.0f, -20)), 1.0f);
	return true;
}

static bool TestGEDot() {
	// A row is summed in one go: every product truncated at the largest term's last bit, so two small
	// terms that would carry together in pairwise adds are both dropped.
	EXPECT_EQ_FLOAT(GEDot(Vec3f(1.0f, 1.0f, 1.0f), Vec3f(1.0f, ldexpf(1.0f, -16), ldexpf(1.0f, -16))), 1.0f);
	EXPECT_EQ_FLOAT(GEDot(Vec3f(2.0f, 3.0f, 0.0f), Vec3f(0.5f, 0.25f, 9.0f)), 1.75f);
	// Normalization goes through GERsqrt, and returns the length the same way.
	Vec3f v(3.0f, 4.0f, 0.0f);
	const float len = GENormalize(v);
	EXPECT_EQ_FLOAT(len, ProductToFloat24(25.0 * GERsqrt(25.0f)));
	EXPECT_EQ_FLOAT(v.x, ProductToFloat24(3.0 * GERsqrt(25.0f)));
	EXPECT_EQ_FLOAT(v.y, ProductToFloat24(4.0 * GERsqrt(25.0f)));
	EXPECT_TRUE(fabsf(len - 5.0f) < 0.001f);
	return true;
}

static bool TestGESetupRecip() {
	// The triangle setup's reciprocal for 16-bit significand indices, measured on a PSP (exp38-41).
	static const int measured[][2] = {
		{ 0, 65536 }, { 2, 65534 }, { 254, 65282 }, { 256, 65280 }, { 2594, 63040 }, { 4086, 61689 }, { 7588, 58735 }, { 10520, 56471 },
		{ 18132, 51333 }, { 19190, 50692 }, { 21126, 49560 }, { 23770, 48092 }, { 24120, 47905 }, { 27582, 46123 }, { 27812, 46010 }, { 27998, 45918 },
		{ 29846, 45029 }, { 30100, 44909 }, { 33802, 43235 }, { 34636, 42876 }, { 35440, 42534 }, { 40160, 40635 }, { 40842, 40374 }, { 47082, 38137 },
		{ 47536, 37984 }, { 50630, 36973 }, { 50882, 36892 }, { 57148, 35008 }, { 60100, 34186 }, { 60570, 34058 }, { 63526, 33278 }, { 65534, 32768 },
	};
	for (const auto &m : measured) {
		int e = 0;
		const int64_t q = GESetupRecip(65536 + m[0], &e);
		if (q != m[1] || e != 16) {
			printf("GESetupRecip(index %d) = %lld (e %d), PSP %d\n", m[0], (long long)q, e, m[1]);
			return false;
		}
		// Only the top 16 bits count.
		const int64_t q2 = GESetupRecip((uint64_t)(65536 + m[0]) << 5 | 31, &e);
		EXPECT_EQ_INT(q2, m[1]);
		EXPECT_EQ_INT(e, 21);
	}
	// exp54's Gouraud triangle (det 854491) needs at least 40211 here; exp39 and exp41 bound it from above.
	int e = 0;
	EXPECT_EQ_INT(GESetupRecip(65536 + 41275, &e), 40211);
	// Blade Dancer's large triangle (det 9668772) needs at least 56859 for its depth (frame dump, index 10001).
	EXPECT_EQ_INT(GESetupRecip(9668772, &e), 56859);
	return true;
}

static bool TestGELog16() {
	// The exponent and the top 4 mantissa bits, in 1/16 (exp58-60).
	EXPECT_EQ_INT(GELog16(1.0f), 0);
	EXPECT_EQ_INT(GELog16(2.0f), 16);
	EXPECT_EQ_INT(GELog16(0.5f), -16);
	EXPECT_EQ_INT(GELog16(1.5f), 8);
	EXPECT_EQ_INT(GELog16(0.75f), -8);
	EXPECT_EQ_INT(GELog16(1.0625f), 1);
	EXPECT_EQ_INT(GELog16(1.0624f), 0);
	return true;
}

static bool TestTexCoordPrecision() {
	// A UV plane keeps 15 significant bits of a lone value (exp83).
	EXPECT_EQ_FLOAT(GETruncateTexCoord(300015.75f), 300000.0f);
	EXPECT_EQ_FLOAT(GETruncateTexCoord(-300015.75f), -300000.0f);
	EXPECT_EQ_FLOAT(GETruncateTexCoord(100.75f), 100.75f);
	// u = s * R(q) keeps 24 significant bits, truncated (exp82).
	EXPECT_EQ_FLOAT(GEUVProduct(1.0 + ldexp(1.0, -23) + ldexp(1.0, -30)), 1.0f + ldexpf(1.0f, -23));
	EXPECT_EQ_FLOAT(GEUVProduct(1.0 + ldexp(1.0, -24)), 1.0f);
	return true;
}

static bool TestGELightPow() {
	// Mitchell's approximation: straight lines between powers of two (exp98, exp102).
	EXPECT_EQ_FLOAT(GELightPow(0.5f, 2.0f), 0.25f);
	EXPECT_EQ_FLOAT(GELightPow(0.75f, 2.0f), 0.5f);
	EXPECT_EQ_FLOAT(GELightPow(0.3f, 0.0f), 1.0f);
	EXPECT_EQ_FLOAT(GELightPow(-0.5f, 2.0f), -0.5f);
	// The exponent times the bits is exact, truncated toward zero to units of 16: a rounded float
	// multiply gives 0x3DA64400, and flooring 0x3F69FFFC (one step lower in the 8-bit factor, exp102).
	EXPECT_EQ_HEX(ToBits(GELightPow(FromBits(0x3DC22200), 1.0625f)), 0x3DA64420U);
	EXPECT_EQ_HEX(ToBits(GELightPow(FromBits(0x3F35E500), 0.296875f)), 0x3F6A0000U);
	// The exponent keeps 5 significant bits, truncated, and saturates below 512 (exp221-223).
	EXPECT_EQ_FLOAT(PSPLightExponent(127.0f), 124.0f);
	EXPECT_EQ_FLOAT(PSPLightExponent(150.0f), 144.0f);
	EXPECT_EQ_FLOAT(PSPLightExponent(511.0f), 496.0f);
	EXPECT_EQ_FLOAT(PSPLightExponent(512.0f), 496.0f);
	EXPECT_EQ_FLOAT(PSPLightExponent(INFINITY), 496.0f);
	EXPECT_EQ_FLOAT(PSPLightExponent(NAN), 496.0f);
	EXPECT_EQ_FLOAT(PSPLightExponent(-2.0f), -2.0f);
	return true;
}

static bool TestGELightColor() {
	// 8-bit color products and scaling, ((2a + 1) * (2b + 1)) >> 10 (exp61-63; exp62 all 65536 pairs).
	// GELightColorProduct takes the colors already expanded as 2c + 1.
	const Vec4<int> white = Vec4<int>::AssignToAll(2 * 255 + 1);
	EXPECT_EQ_INT(GELightColorProduct(white, white).x, 255);
	EXPECT_EQ_INT(GELightColorProduct(Vec4<int>::AssignToAll(2 * 128 + 1), Vec4<int>::AssignToAll(2 * 200 + 1)).x, 100);
	EXPECT_EQ_INT(GELightColorProduct(Vec4<int>::AssignToAll(1), Vec4<int>::AssignToAll(1)).x, 0);
	// A factor becomes s = floor(256 f), expanded like a color; 1 leaves the color alone.
	EXPECT_EQ_INT(GELightColorScale(Vec4<int>::AssignToAll(200), 1.0f).x, 200);
	EXPECT_EQ_INT(GELightColorScale(Vec4<int>::AssignToAll(200), 0.5f).x, 100);
	EXPECT_EQ_INT(GELightColorScale(Vec4<int>::AssignToAll(255), 0.999f).x, 255);
	EXPECT_EQ_INT(GELightColorScale(Vec4<int>::AssignToAll(255), 0.99f).x, 253);
	return true;
}

static bool TestGELineCoverageAlpha() {
	// Antialiased line pixels measured on a PSP (exp112): line ends in subpixels, the pixel, its alpha.
	// The first three have the offset exactly on a step, which the reciprocal decides.
	static const int measured[][7] = {
		{ 5832, 1205, 6108, 1039, 364, 75, 80 }, { 2470, 191, 2544, 154, 158, 9, 31 }, { 2470, 191, 2544, 154, 156, 10, 31 },
		{ 328, 240, 198, 171, 13, 11, 73 }, { 7310, 112, 7502, 157, 461, 8, 22 }, { 5529, 766, 5559, 569, 347, 37, 37 }, { 4038, 929, 4232, 1136, 253, 59, 71 },
		{ 2801, 1456, 2632, 1657, 172, 94, 31 }, { 1616, 2013, 1699, 1796, 101, 124, 127 }, { 2279, 2535, 2310, 2229, 144, 141, 40 }, { 7629, 2356, 7550, 2350, 474, 147, 20 },
		{ 6299, 2652, 6609, 2836, 403, 171, 109 }, { 4407, 3243, 4531, 3239, 275, 202, 80 },
	};
	for (const auto &m : measured) {
		const int a = GELineCoverageAlpha(m[0], m[1], m[2], m[3], m[4], m[5]);
		if (a != m[6]) {
			printf("GELineCoverageAlpha((%d, %d) - (%d, %d), pixel %d, %d) = %d, PSP %d\n", m[0], m[1], m[2], m[3], m[4], m[5], a, m[6]);
			return false;
		}
	}
	return true;
}

bool TestGEMath() {
	bool ok = true;
	ok = TestFloat24() && ok;
	ok = TestGERecip() && ok;
	ok = TestGERsqrt() && ok;
	ok = TestGEAdd() && ok;
	ok = TestGEDot() && ok;
	ok = TestGESetupRecip() && ok;
	ok = TestGELog16() && ok;
	ok = TestTexCoordPrecision() && ok;
	ok = TestGELightPow() && ok;
	ok = TestGELightColor() && ok;
	ok = TestGELineCoverageAlpha() && ok;
	return ok;
}
