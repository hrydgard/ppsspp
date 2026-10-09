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
#include <vector>

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

// GEAdd4 against GEAdd, bit for bit: any bits (not just float24s), close and distant exponents, signs,
// zeros, denormals, inf and NaN.
static bool TestGEAdd4() {
	uint32_t state = 777;
	auto next = [&]() {
		state = state * 1664525u + 1013904223u;
		return state;
	};
	auto randomBits = [&](int center, int spread) {
		const uint32_t r = next();
		uint32_t e;
		switch (r & 15) {
		case 0: e = 0; break;
		case 1: e = 255; break;
		default: e = (uint32_t)std::clamp(center + (int)(next() % (2 * spread + 1)) - spread, 1, 254); break;
		}
		const uint32_t bits = (e << 23) | (next() & 0x007FFFFF) | ((r & 16) ? 0x80000000 : 0);
		float f;
		memcpy(&f, &bits, sizeof(f));
		return f;
	};
	for (int i = 0; i < 200000; ++i) {
		const int spread = (i % 5 == 0) ? 120 : 20;
		const int center = (i % 7 == 0) ? (int)(next() % 254) + 1 : 127;
		alignas(16) float a[4], b[4], got[4];
		for (int l = 0; l < 4; ++l) {
			a[l] = randomBits(center, spread);
			b[l] = randomBits(center, spread);
		}
		GEAdd4(Vec4F32::Load(a), Vec4F32::Load(b)).Store(got);
		for (int l = 0; l < 4; ++l) {
			const float want = GEAdd(a[l], b[l]);
			if (memcmp(&got[l], &want, sizeof(float)) != 0) {
				printf("GEAdd4: %a + %a = %a, want %a\n", a[l], b[l], got[l], want);
				return false;
			}
		}
	}
	return true;
}

// GEMulFloat24x4 against ProductToFloat24, bit for bit, on the same kind of bits as TestGEAdd4.
static bool TestGEMulFloat24x4() {
	uint32_t state = 4321;
	auto next = [&]() {
		state = state * 1664525u + 1013904223u;
		return state;
	};
	auto randomBits = [&](int center, int spread) {
		const uint32_t r = next();
		uint32_t e;
		switch (r & 15) {
		case 0: e = 0; break;
		case 1: e = 255; break;
		default: e = (uint32_t)std::clamp(center + (int)(next() % (2 * spread + 1)) - spread, 1, 254); break;
		}
		const uint32_t mant = (r & 32) ? 0 : (next() & 0x007FFFFF);
		const uint32_t bits = (e << 23) | mant | ((r & 16) ? 0x80000000 : 0);
		float f;
		memcpy(&f, &bits, sizeof(f));
		return f;
	};
	for (int i = 0; i < 200000; ++i) {
		const int spread = (i % 5 == 0) ? 120 : 20;
		const int center = (i % 7 == 0) ? (int)(next() % 254) + 1 : 127;
		alignas(16) float a[4], b[4], got[4];
		for (int l = 0; l < 4; ++l) {
			a[l] = randomBits(center, spread);
			b[l] = randomBits(center, spread);
		}
		GEMulFloat24x4(Vec4F32::Load(a), Vec4F32::Load(b)).Store(got);
		for (int l = 0; l < 4; ++l) {
			const float ta = TruncateToFloat24(a[l]), tb = TruncateToFloat24(b[l]);
			const float want = ta == 0.0f || tb == 0.0f ? 0.0f : ProductToFloat24((double)ta * tb);
			if (memcmp(&got[l], &want, sizeof(float)) != 0) {
				printf("GEMulFloat24x4: %a * %a = %a, want %a\n", a[l], b[l], got[l], want);
				return false;
			}
		}
	}
	return true;
}

static bool TestGEDot3() {
	uint32_t state = 4242;
	auto next = [&]() {
		state = state * 1664525u + 1013904223u;
		return state;
	};
	auto randomBits = [&](int center, int spread) {
		const uint32_t r = next();
		uint32_t e;
		switch (r & 31) {
		case 0: e = 0; break;
		case 1: e = 255; break;
		default: e = (uint32_t)std::clamp(center + (int)(next() % (2 * spread + 1)) - spread, 1, 254); break;
		}
		const uint32_t bits = (e << 23) | (next() & 0x007FFFFF) | ((r & 32) ? 0x80000000 : 0);
		float f;
		memcpy(&f, &bits, sizeof(f));
		return f;
	};
	for (int i = 0; i < 200000; ++i) {
		const int spread = (i % 5 == 0) ? 60 : ((i % 3 == 0) ? 10 : 2);
		const int center = (i % 7 == 0) ? (int)(next() % 254) + 1 : 127;
		// Lane 3 is anything, and must not matter.
		alignas(16) float a[4], b[4];
		for (int l = 0; l < 4; ++l) {
			a[l] = randomBits(center, spread);
			b[l] = randomBits(center, spread);
		}
		const Vec3f a3(a[0], a[1], a[2]);
		const Vec3f b3(b[0], b[1], b[2]);
		const float got = GEDot3(Vec4F32::Load(a), Vec4F32::Load(b));
		const float want = GEDot(a3, b3);
		if (memcmp(&got, &want, sizeof(float)) != 0) {
			printf("GEDot3: (%a %a %a) . (%a %a %a) = %a, want %a\n", a[0], a[1], a[2], b[0], b[1], b[2], got, want);
			return false;
		}

		Vec4F32 v = Vec4F32::Load(a);
		Vec3f v3 = a3;
		const float gotLen = GENormalize4(v);
		const float wantLen = GENormalize(v3);
		alignas(16) float out[4];
		v.Store(out);
		// GENormalize4 makes denormals zero, where GENormalize keeps them.
		auto denormal = [](float f) { return f != 0.0f && std::fpclassify(f) == FP_SUBNORMAL; };
		if (denormal(a[0]) || denormal(a[1]) || denormal(a[2]) || denormal(v3.x) || denormal(v3.y) || denormal(v3.z))
			continue;
		if (memcmp(&gotLen, &wantLen, sizeof(float)) != 0 || memcmp(out, v3.AsArray(), 3 * sizeof(float)) != 0) {
			printf("GENormalize4: (%a %a %a) -> %a (%a %a %a), want %a (%a %a %a)\n", a[0], a[1], a[2],
				gotLen, out[0], out[1], out[2], wantLen, v3.x, v3.y, v3.z);
			return false;
		}
	}
	return true;
}

// GERowSum4 against GERowSum, bit for bit, on random float24s: mixed signs, zeros and denormals, exponents
// close together (so the terms' alignment matters) and far apart, including ones past the vector path.
static bool TestGERowSum4() {
	uint32_t state = 12345;
	auto next = [&]() {
		state = state * 1664525u + 1013904223u;
		return state;
	};
	auto randomFloat24 = [&](int center, int spread) {
		const uint32_t r = next();
		if ((r & 31) == 0)
			return 0.0f;
		uint32_t bits;
		if ((r & 31) == 1) {
			bits = next() & 0x007FFF00;  // denormal
		} else {
			const int e = std::clamp(center + (int)(next() % (2 * spread + 1)) - spread, 1, 254);
			bits = ((uint32_t)e << 23) | (next() & 0x007FFF00);
		}
		if (r & 64)
			bits |= 0x80000000;
		float f;
		memcpy(&f, &bits, sizeof(f));
		return f;
	};
	for (int i = 0; i < 200000; ++i) {
		const int count = 1 + (i & 3);
		const int spread = (i % 7 == 0) ? 120 : ((i % 3 == 0) ? 20 : 3);
		const int center = (i % 11 == 0) ? (int)(next() % 254) + 1 : 127;
		alignas(16) float a[4] = {};
		alignas(16) float b[4][4];
		Vec4F32 bv[4];
		for (int k = 0; k < count; ++k) {
			a[k] = randomFloat24(center, spread);
			for (int l = 0; l < 4; ++l)
				b[k][l] = randomFloat24(center, spread);
			bv[k] = Vec4F32::Load(b[k]);
		}
		alignas(16) float got[4];
		Vec4F32 av = Vec4F32::Load(a);
		switch (count) {
		case 1: av = GERowSum4<1>(av, bv); break;
		case 2: av = GERowSum4<2>(av, bv); break;
		case 3: av = GERowSum4<3>(av, bv); break;
		default: av = GERowSum4<4>(av, bv); break;
		}
		av.Store(got);
		for (int l = 0; l < 4; ++l) {
			GERowTerm terms[4];
			for (int k = 0; k < count; ++k)
				terms[k] = GEProduct(a[k], b[k][l]);
			const float want = GERowSum(terms, count);
			if (memcmp(&got[l], &want, sizeof(float)) != 0) {
				printf("GERowSum4: case %d lane %d: %a, want %a (count %d)\n", i, l, got[l], want, count);
				for (int k = 0; k < count; ++k)
					printf("  %a * %a\n", a[k], b[k][l]);
				return false;
			}
		}
	}

	// Speed: a vertex transform's four rows, as GERowSum4 and as four scalar GERowSums.
	{
		const int count = 1024;
		std::vector<float> pos(count * 4);
		for (float &f : pos)
			f = TruncateToFloat24(randomFloat24(127, 8));
		alignas(16) float m[16];
		for (float &f : m)
			f = randomFloat24(127, 4);
		const Vec4F32 rows[4] = { Vec4F32::Load(m), Vec4F32::Load(m + 4), Vec4F32::Load(m + 8), Vec4F32::Load(m + 12) };
		volatile float sink = 0.0f;
		const double simd = CallsPerSecond([&] {
			Vec4F32 acc = Vec4F32::Zero();
			for (int i = 0; i < count; ++i)
				acc = acc + GERowSum4<4>(Vec4F32::Load(&pos[i * 4]), rows);
			alignas(16) float out[4];
			acc.Store(out);
			sink = out[0];
		}, 0.5, 1);
		const double scalar = CallsPerSecond([&] {
			float acc = 0.0f;
			for (int i = 0; i < count; ++i) {
				for (int c = 0; c < 4; ++c) {
					GERowTerm terms[4];
					for (int k = 0; k < 4; ++k)
						terms[k] = GEProduct(pos[i * 4 + k], m[k * 4 + c]);
					acc += GERowSum(terms, 4);
				}
			}
			sink = acc;
		}, 0.5, 1);
		printf("GERowSum4: %.1f M transforms/s, scalar GERowSum: %.1f M/s\n", simd * count / 1e6, scalar * count / 1e6);

		const double dot4 = CallsPerSecond([&] {
			float acc = 0.0f;
			for (int i = 0; i < count - 1; ++i)
				acc += GEDot3(Vec4F32::Load(&pos[i * 4]), Vec4F32::Load(&pos[i * 4 + 4]));
			sink = acc;
		}, 0.5, 1);
		const double dot = CallsPerSecond([&] {
			float acc = 0.0f;
			for (int i = 0; i < count - 1; ++i)
				acc += GEDot(Vec3f(pos[i * 4], pos[i * 4 + 1], pos[i * 4 + 2]), Vec3f(pos[i * 4 + 4], pos[i * 4 + 5], pos[i * 4 + 6]));
			sink = acc;
		}, 0.5, 1);
		printf("GEDot3: %.1f M/s, scalar GEDot: %.1f M/s\n", dot4 * count / 1e6, dot * count / 1e6);
	}
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
	ok = TestGERowSum4() && ok;
	ok = TestGEAdd4() && ok;
	ok = TestGEMulFloat24x4() && ok;
	ok = TestGEDot3() && ok;
	ok = TestGESetupRecip() && ok;
	ok = TestGELog16() && ok;
	ok = TestTexCoordPrecision() && ok;
	ok = TestGELightPow() && ok;
	ok = TestGELightColor() && ok;
	ok = TestGELineCoverageAlpha() && ok;
	return ok;
}
