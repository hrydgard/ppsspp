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

// Tests for Common/Math/CrossSIMD.h.
//
// CrossSIMD has four independent implementations - SSE2, NEON, LoongArch LSX and a plain scalar
// fallback - and only one of them is compiled on any given machine. So the point of these tests is
// to check whatever got compiled against results worked out by hand, rather than against each other;
// that way every architecture we build for verifies its own copy. Running the unit tests under
// qemu covers the ones we have no hardware for.
//
// Only functions that all four implementations provide are tested here, otherwise this wouldn't
// build everywhere.

#include "ppsspp_config.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/Math/CrossSIMD.h"
#include "unittest/UnitTest.h"

static bool CompareFloats(const float *values, const float *known_good, int count, int line) {
	int wrongCount = 0;
	for (int i = 0; i < count; i++) {
		if (values[i] != known_good[i]) {
			wrongCount++;
		}
	}
	if (wrongCount > 0) {
		for (int i = 0; i < count; i++) {
			bool wrong = values[i] != known_good[i];
			printf("%d: %0.3f vs %0.3f %s\n", i + 1, values[i], known_good[i], wrong ? "!! MISMATCH" : "");
		}
		printf("At TestCrossSIMD.cpp:%d: %d / %d were wrong\n", line, wrongCount, count);
		return false;
	}
	return true;
}

// For the approximate operations (reciprocals), which are allowed to differ between architectures.
static bool CompareFloatsApprox(const float *values, const float *known_good, int count, float tolerance, int line) {
	for (int i = 0; i < count; i++) {
		const float diff = fabsf(values[i] - known_good[i]);
		const float scale = fabsf(known_good[i]) > 1.0f ? fabsf(known_good[i]) : 1.0f;
		if (diff / scale > tolerance) {
			printf("At TestCrossSIMD.cpp:%d: lane %d: %0.6f vs %0.6f\n", line, i, values[i], known_good[i]);
			return false;
		}
	}
	return true;
}

static bool TestVec4S32() {
	const int a_values[4] = { 3, -7, 0x4000, -1 };
	const int b_values[4] = { 5, 11, -2, 0x7FFF };

	Vec4S32 a = Vec4S32::Load(a_values);
	Vec4S32 b = Vec4S32::Load(b_values);

	int result[4];

	(a + b).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], a_values[i] + b_values[i]);
	}

	(a - b).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], a_values[i] - b_values[i]);
	}

	a.Mul(b).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], a_values[i] * b_values[i]);
	}

	// Mul16 only promises correct results when both sides fit in 16 bits.
	const int small_a[4] = { 3, -7, 300, -1 };
	const int small_b[4] = { 5, 11, -2, 1000 };
	Vec4S32 sa = Vec4S32::Load(small_a);
	Vec4S32 sb = Vec4S32::Load(small_b);
	sa.Mul16(sb).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], small_a[i] * small_b[i]);
	}

	Vec4S32::Splat(-5).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], -5);
	}

	Vec4S32::Zero().Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], 0);
	}

	a.Shl<2>().Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], a_values[i] << 2);
	}

	// operator[] should agree with Store.
	a.Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(a[i], result[i]);
	}

	// Min16/Max16 likewise operate on 16-bit lanes.
	sa.Min16(sb).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], small_a[i] < small_b[i] ? small_a[i] : small_b[i]);
	}
	sa.Max16(sb).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_INT(result[i], small_a[i] > small_b[i] ? small_a[i] : small_b[i]);
	}

	const int sext_values[4] = { 0x0000FFFF, 0x00007FFF, 0x00008000, 0x00000001 };
	Vec4S32::Load(sext_values).SignExtend16().Store(result);
	EXPECT_EQ_INT(result[0], -1);
	EXPECT_EQ_INT(result[1], 32767);
	EXPECT_EQ_INT(result[2], -32768);
	EXPECT_EQ_INT(result[3], 1);

	return true;
}

static bool TestVec4S32Compares() {
	const int a_values[4] = { 1, 2, 3, 4 };
	const int b_values[4] = { 1, 5, 0, 4 };
	Vec4S32 a = Vec4S32::Load(a_values);
	Vec4S32 b = Vec4S32::Load(b_values);

	int result[4];

	// Compares produce all-ones or all-zeros per lane.
	a.CompareEq(b).Store(result);
	EXPECT_EQ_HEX((u32)result[0], 0xFFFFFFFFu);
	EXPECT_EQ_HEX((u32)result[1], 0u);
	EXPECT_EQ_HEX((u32)result[2], 0u);
	EXPECT_EQ_HEX((u32)result[3], 0xFFFFFFFFu);

	a.CompareLt(b).Store(result);
	EXPECT_EQ_HEX((u32)result[0], 0u);
	EXPECT_EQ_HEX((u32)result[1], 0xFFFFFFFFu);
	EXPECT_EQ_HEX((u32)result[2], 0u);
	EXPECT_EQ_HEX((u32)result[3], 0u);

	a.CompareGt(b).Store(result);
	EXPECT_EQ_HEX((u32)result[0], 0u);
	EXPECT_EQ_HEX((u32)result[1], 0u);
	EXPECT_EQ_HEX((u32)result[2], 0xFFFFFFFFu);
	EXPECT_EQ_HEX((u32)result[3], 0u);

	// AllCompareBitsSet / AnyCompareBitsSet over those masks.
	EXPECT_FALSE(AllCompareBitsSet(a.CompareEq(b)));
	EXPECT_TRUE(AnyCompareBitsSet(a.CompareEq(b)));
	EXPECT_TRUE(AllCompareBitsSet(a.CompareEq(a)));
	EXPECT_FALSE(AnyCompareBitsSet(a.CompareLt(a)));

	return true;
}

static bool TestVec4F32Arith() {
	const float a_values[4] = { 1.0f, -2.0f, 3.5f, 0.25f };
	const float b_values[4] = { 4.0f, 8.0f, -0.5f, 2.0f };

	Vec4F32 a = Vec4F32::Load(a_values);
	Vec4F32 b = Vec4F32::Load(b_values);

	float result[4];
	float expected[4];

	(a + b).Store(result);
	for (int i = 0; i < 4; i++) expected[i] = a_values[i] + b_values[i];
	if (!CompareFloats(result, expected, 4, __LINE__)) return false;

	(a - b).Store(result);
	for (int i = 0; i < 4; i++) expected[i] = a_values[i] - b_values[i];
	if (!CompareFloats(result, expected, 4, __LINE__)) return false;

	(a * b).Store(result);
	for (int i = 0; i < 4; i++) expected[i] = a_values[i] * b_values[i];
	if (!CompareFloats(result, expected, 4, __LINE__)) return false;

	a.Mul(2.0f).Store(result);
	for (int i = 0; i < 4; i++) expected[i] = a_values[i] * 2.0f;
	if (!CompareFloats(result, expected, 4, __LINE__)) return false;

	a.Min(b).Store(result);
	for (int i = 0; i < 4; i++) expected[i] = a_values[i] < b_values[i] ? a_values[i] : b_values[i];
	if (!CompareFloats(result, expected, 4, __LINE__)) return false;

	a.Max(b).Store(result);
	for (int i = 0; i < 4; i++) expected[i] = a_values[i] > b_values[i] ? a_values[i] : b_values[i];
	if (!CompareFloats(result, expected, 4, __LINE__)) return false;

	a.Clamp(0.0f, 3.0f).Store(result);
	static const float known_clamp[4] = { 1.0f, 0.0f, 3.0f, 0.25f };
	if (!CompareFloats(result, known_clamp, 4, __LINE__)) return false;

	// Recip is allowed to be approximate on some architectures, RecipApprox definitely is.
	b.Recip().Store(result);
	for (int i = 0; i < 4; i++) expected[i] = 1.0f / b_values[i];
	if (!CompareFloatsApprox(result, expected, 4, 0.001f, __LINE__)) return false;

	b.RecipApprox().Store(result);
	if (!CompareFloatsApprox(result, expected, 4, 0.01f, __LINE__)) return false;

	Vec4F32::Splat(1.5f).Store(result);
	static const float known_splat[4] = { 1.5f, 1.5f, 1.5f, 1.5f };
	if (!CompareFloats(result, known_splat, 4, __LINE__)) return false;

	Vec4F32::Zero().Store(result);
	static const float known_zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (!CompareFloats(result, known_zero, 4, __LINE__)) return false;

	// Dot products. Dot3 ignores lane 3.
	EXPECT_EQ_FLOAT(a.Dot3(b), a_values[0] * b_values[0] + a_values[1] * b_values[1] + a_values[2] * b_values[2]);
	EXPECT_EQ_FLOAT(a.Dot4(b), a_values[0] * b_values[0] + a_values[1] * b_values[1] + a_values[2] * b_values[2] + a_values[3] * b_values[3]);

	// operator[] and GetLane<> should agree with Store.
	a.Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_FLOAT(a[i], result[i]);
	}
	EXPECT_EQ_FLOAT(a.GetLane<0>(), a_values[0]);
	EXPECT_EQ_FLOAT(a.GetLane<3>(), a_values[3]);

	return true;
}

static bool TestVec4F32Compares() {
	const float a_values[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
	const float b_values[4] = { 1.0f, 5.0f, 0.0f, 4.0f };
	Vec4F32 a = Vec4F32::Load(a_values);
	Vec4F32 b = Vec4F32::Load(b_values);

	int result[4];

	a.CompareEq(b).Store(result);
	EXPECT_EQ_HEX((u32)result[0], 0xFFFFFFFFu);
	EXPECT_EQ_HEX((u32)result[1], 0u);

	a.CompareLt(b).Store(result);
	EXPECT_EQ_HEX((u32)result[1], 0xFFFFFFFFu);
	EXPECT_EQ_HEX((u32)result[2], 0u);

	a.CompareGt(b).Store(result);
	EXPECT_EQ_HEX((u32)result[2], 0xFFFFFFFFu);
	EXPECT_EQ_HEX((u32)result[1], 0u);

	a.CompareLe(b).Store(result);
	EXPECT_EQ_HEX((u32)result[0], 0xFFFFFFFFu);
	EXPECT_EQ_HEX((u32)result[2], 0u);

	a.CompareGe(b).Store(result);
	EXPECT_EQ_HEX((u32)result[0], 0xFFFFFFFFu);
	EXPECT_EQ_HEX((u32)result[1], 0u);

	return true;
}

static bool TestVec4F32Lanes() {
	const float values[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
	const float other_values[4] = { 9.0f, 9.0f, 9.0f, 42.0f };
	Vec4F32 v = Vec4F32::Load(values);
	Vec4F32 other = Vec4F32::Load(other_values);

	float result[4];

	v.WithLane3Zero().Store(result);
	static const float known_zero3[4] = { 1.0f, 2.0f, 3.0f, 0.0f };
	if (!CompareFloats(result, known_zero3, 4, __LINE__)) return false;

	v.WithLane3One().Store(result);
	static const float known_one3[4] = { 1.0f, 2.0f, 3.0f, 1.0f };
	if (!CompareFloats(result, known_one3, 4, __LINE__)) return false;

	v.WithLane3From(other).Store(result);
	static const float known_from3[4] = { 1.0f, 2.0f, 3.0f, 42.0f };
	if (!CompareFloats(result, known_from3, 4, __LINE__)) return false;

	v.ShuffleXXXX().Store(result);
	static const float known_xxxx[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	if (!CompareFloats(result, known_xxxx, 4, __LINE__)) return false;

	v.ShuffleYYYY().Store(result);
	static const float known_yyyy[4] = { 2.0f, 2.0f, 2.0f, 2.0f };
	if (!CompareFloats(result, known_yyyy, 4, __LINE__)) return false;

	v.ShuffleZZZZ().Store(result);
	static const float known_zzzz[4] = { 3.0f, 3.0f, 3.0f, 3.0f };
	if (!CompareFloats(result, known_zzzz, 4, __LINE__)) return false;

	v.ShuffleWWWW().Store(result);
	static const float known_wwww[4] = { 4.0f, 4.0f, 4.0f, 4.0f };
	if (!CompareFloats(result, known_wwww, 4, __LINE__)) return false;

	v.ShuffleXXYY().Store(result);
	static const float known_xxyy[4] = { 1.0f, 1.0f, 2.0f, 2.0f };
	if (!CompareFloats(result, known_xxyy, 4, __LINE__)) return false;

	v.ShuffleZZWW().Store(result);
	static const float known_zzww[4] = { 3.0f, 3.0f, 4.0f, 4.0f };
	if (!CompareFloats(result, known_zzww, 4, __LINE__)) return false;

	// Store2/Store3 must leave the rest of the destination alone.
	float partial[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
	v.Store2(partial);
	static const float known_store2[4] = { 1.0f, 2.0f, -1.0f, -1.0f };
	if (!CompareFloats(partial, known_store2, 4, __LINE__)) return false;

	float partial3[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
	v.Store3(partial3);
	static const float known_store3[4] = { 1.0f, 2.0f, 3.0f, -1.0f };
	if (!CompareFloats(partial3, known_store3, 4, __LINE__)) return false;

	// Transpose of four columns.
	static const float col_values[4][4] = {
		{ 0.0f, 1.0f, 2.0f, 3.0f },
		{ 4.0f, 5.0f, 6.0f, 7.0f },
		{ 8.0f, 9.0f, 10.0f, 11.0f },
		{ 12.0f, 13.0f, 14.0f, 15.0f },
	};
	Vec4F32 c0 = Vec4F32::Load(col_values[0]);
	Vec4F32 c1 = Vec4F32::Load(col_values[1]);
	Vec4F32 c2 = Vec4F32::Load(col_values[2]);
	Vec4F32 c3 = Vec4F32::Load(col_values[3]);
	Vec4F32::Transpose(c0, c1, c2, c3);
	float transposed[16];
	c0.Store(transposed);
	c1.Store(transposed + 4);
	c2.Store(transposed + 8);
	c3.Store(transposed + 12);
	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			EXPECT_EQ_FLOAT(transposed[row * 4 + col], col_values[col][row]);
		}
	}

	// LoadTranspose should match a plain Load of each row followed by Transpose.
	static const float flat[16] = {
		0.0f, 1.0f, 2.0f, 3.0f,
		4.0f, 5.0f, 6.0f, 7.0f,
		8.0f, 9.0f, 10.0f, 11.0f,
		12.0f, 13.0f, 14.0f, 15.0f,
	};
	Vec4F32 t0, t1, t2, t3;
	Vec4F32::LoadTranspose(flat, t0, t1, t2, t3);
	float loadTransposed[16];
	t0.Store(loadTransposed);
	t1.Store(loadTransposed + 4);
	t2.Store(loadTransposed + 8);
	t3.Store(loadTransposed + 12);
	if (!CompareFloats(loadTransposed, transposed, 16, __LINE__)) return false;

	return true;
}

static bool TestVec4F32NaNInf() {
	const float inf = INFINITY;
	const float nan = NAN;
	const float values[4] = { 1.0f, nan, inf, -inf };
	Vec4F32 v = Vec4F32::Load(values);

	float result[4];

	v.ZeroNaNs().Store(result);
	EXPECT_EQ_FLOAT(result[0], 1.0f);
	EXPECT_EQ_FLOAT(result[1], 0.0f);
	// ZeroNaNs leaves infinities alone.
	EXPECT_TRUE(std::isinf(result[2]));
	EXPECT_TRUE(std::isinf(result[3]));

	v.CleanNaNInfs().Store(result);
	static const float known_clean[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	if (!CompareFloats(result, known_clean, 4, __LINE__)) return false;

	return true;
}

static bool TestVec4F32Loads() {
	float result[4];

	// LoadF24x3_One: three 24-bit values shifted up into floats, lane 3 forced to 1.0f.
	const uint32_t f24_values[4] = { 0x3F8000 >> 0, 0x400000, 0x3F0000, 0x123456 };
	Vec4F32::LoadF24x3_One(f24_values).Store(result);
	for (int i = 0; i < 3; i++) {
		float expected;
		uint32_t bits = f24_values[i] << 8;
		memcpy(&expected, &bits, 4);
		EXPECT_EQ_FLOAT(result[i], expected);
	}
	EXPECT_EQ_FLOAT(result[3], 1.0f);

	// LoadF24x4: same, but all four lanes come from the source.
	Vec4F32::LoadF24x4(f24_values).Store(result);
	for (int i = 0; i < 4; i++) {
		float expected;
		uint32_t bits = f24_values[i] << 8;
		memcpy(&expected, &bits, 4);
		EXPECT_EQ_FLOAT(result[i], expected);
	}

	// The normalizing loads. Some of these read 8 bytes, so give them room.
	const int8_t s8_values[8] = { -1, -128, 127, 45, 0, 0, 0, 0 };
	Vec4F32::LoadS8Norm(s8_values).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_FLOAT(result[i], (float)s8_values[i] / 128.0f);
	}

	const int16_t s16_values[8] = { -1, -32768, 32767, 1234, 0, 0, 0, 0 };
	Vec4F32::LoadS16Norm(s16_values).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_FLOAT(result[i], (float)s16_values[i] / 32768.0f);
	}

	const uint8_t u8_values[8] = { 0, 255, 128, 7, 0, 0, 0, 0 };
	Vec4F32::LoadU8Norm(u8_values).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_APPROX_EQ_FLOAT(result[i], (float)u8_values[i] / 255.0f);
	}

	// The converting (non-normalizing) loads.
	Vec4F32::LoadConvertS16(s16_values).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_FLOAT(result[i], (float)s16_values[i]);
	}

	Vec4F32::LoadConvertS8(s8_values).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_FLOAT(result[i], (float)s8_values[i]);
	}

	Vec4F32::LoadConvertU8(u8_values).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_FLOAT(result[i], (float)u8_values[i]);
	}

	// StoreConvertToU8 truncates towards zero and saturates to 0-255.
	const float to_u8_values[4] = { 0.0f, 255.0f, 12.75f, 200.0f };
	uint8_t u8_out[4];
	Vec4F32::Load(to_u8_values).StoreConvertToU8(u8_out);
	EXPECT_EQ_INT(u8_out[0], 0);
	EXPECT_EQ_INT(u8_out[1], 255);
	EXPECT_EQ_INT(u8_out[2], 12);
	EXPECT_EQ_INT(u8_out[3], 200);

	const float saturate_values[4] = { -1.0f, 300.0f, -0.5f, 1000.0f };
	Vec4F32::Load(saturate_values).StoreConvertToU8(u8_out);
	EXPECT_EQ_INT(u8_out[0], 0);
	EXPECT_EQ_INT(u8_out[1], 255);
	EXPECT_EQ_INT(u8_out[2], 0);
	EXPECT_EQ_INT(u8_out[3], 255);

	// Int to float.
	const int int_values[4] = { 0, -7, 1000, -32768 };
	Vec4F32::FromVec4S32(Vec4S32::Load(int_values)).Store(result);
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ_FLOAT(result[i], (float)int_values[i]);
	}

	// Load2 only fills the first two lanes.
	const float two_values[2] = { 3.0f, 4.0f };
	Vec4F32::Load2(two_values).Store(result);
	EXPECT_EQ_FLOAT(result[0], 3.0f);
	EXPECT_EQ_FLOAT(result[1], 4.0f);

	return true;
}

static bool TestMatrices() {
	static const float a_values[16] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f };
	static const float b_values[16] = { -12.0f, 3.0f, -2.5f, 5.0f, 31.0f, 0.5f, 4.0f, 6.0f, 7.0f, 13.0f, 12.0f, 51.0f, 81.0f, 32.0f };
	static const float known_result[16] = { 395.0f, 171.0f, 41.5f, 170.0f, 942.0f, 410.5f, 111.5f, 475.0f, 1358.0f, 607.5f, 163.0f, 728.0f, 297.0f, 49.5f, 25.0f, 160.0f, };
	float result[16];
	Mat4F32 a(a_values);
	Mat4F32 b(b_values);

	Mul4x4By4x4(a, b).Store(result);
	if (!CompareFloats(result, known_result, 16, __LINE__)) {
		return false;
	}

	Mat4x3F32 d = Mat4x3F32(b_values + 2);
	Mul4x3By4x4(d, a).Store(result);

	static const float known_4x3_result[16] = { 332.5f, 371.0f, 404.5f, 438.0f, 80.5f, 95.0f, 105.5f, 116.0f, 192.0f, 237.0f, 269.0f, 301.0f, 790.0f, 1036.0f, 1185.0f, 1349.0f, };
	if (!CompareFloats(result, known_4x3_result, 16, __LINE__)) {
		return false;
	}

	static const float vec_values[4] = { 3.0f, 5.0f, 7.0f, 10000000.0f };
	Vec4F32 v = Vec4F32::Load(vec_values);

	v.AsVec3ByMatrix44(b).Store3(result);

	static const float known_vec_result[3] = { 249.0f, 134.5f, 96.5f, };
	if (!CompareFloats(result, known_vec_result, ARRAY_SIZE(known_vec_result), __LINE__)) {
		return false;
	}
	Vec4F32 scale = Vec4F32::Load(a_values);
	Vec4F32 translate = Vec4F32::Load(b_values);

	TranslateAndScaleInplace(a, scale, translate);
	a.Store(result);

	static const float known_scale_result[16] = { -47.0f, 16.0f, -1.0f, 36.0f, -103.0f, 41.0f, 1.5f, 81.0f, -146.0f, 61.0f, 3.5f, 117.0f, 14.0f, 30.0f, 0.0f, 0.0f,};
	if (!CompareFloats(result, known_scale_result, ARRAY_SIZE(known_scale_result), __LINE__)) {
		return false;
	}

	return true;
}

bool TestCrossSIMD() {
	if (!TestVec4S32()) return false;
	if (!TestVec4S32Compares()) return false;
	if (!TestVec4F32Arith()) return false;
	if (!TestVec4F32Compares()) return false;
	if (!TestVec4F32Lanes()) return false;
	if (!TestVec4F32NaNInf()) return false;
	if (!TestVec4F32Loads()) return false;
	if (!TestMatrices()) return false;
	return true;
}
