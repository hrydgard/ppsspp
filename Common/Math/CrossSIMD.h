// CrossSIMD
//
// This file will contain cross-instruction-set SIMD instruction wrappers.
//
// This specific file (and a future CrossSIMD.cpp) file is under public domain or MIT, unlike most of the rest of the emulator.

#pragma once

#include <cstring>
#include "Common/Math/SIMDHeaders.h"

#define TEST_FALLBACK 0

#if PPSSPP_ARCH(SSE2) && !TEST_FALLBACK

// The point of this, as opposed to a float4 array, is to almost force the compiler
// to keep the matrix in registers, rather than loading on every access.
struct Mat4F32 {
	Mat4F32() {}
	Mat4F32(const float *matrix) {
		col0 = _mm_loadu_ps(matrix);
		col1 = _mm_loadu_ps(matrix + 4);
		col2 = _mm_loadu_ps(matrix + 8);
		col3 = _mm_loadu_ps(matrix + 12);
	}
	void Store(float *m) {
		_mm_storeu_ps(m, col0);
		_mm_storeu_ps(m + 4, col1);
		_mm_storeu_ps(m + 8, col2);
		_mm_storeu_ps(m + 12, col3);
	}

	// Unlike the old one, this one is careful about not loading out-of-range data.
	// The last two loads overlap.
	static Mat4F32 Load4x3(const float *m) {
		Mat4F32 result;
		alignas(16) static const uint32_t mask[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0 };
		alignas(16) static const float onelane3[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		__m128 mask1110 = _mm_loadu_ps((const float *)mask);
		result.col0 = _mm_and_ps(_mm_loadu_ps(m), mask1110);
		result.col1 = _mm_and_ps(_mm_loadu_ps(m + 3), mask1110);
		result.col2 = _mm_and_ps(_mm_loadu_ps(m + 6), mask1110);
		__m128 lastCol = _mm_loadu_ps(m + 8);
		result.col3 = _mm_or_ps(_mm_and_ps(_mm_shuffle_ps(lastCol, lastCol, _MM_SHUFFLE(3, 3, 2, 1)), mask1110), _mm_load_ps(onelane3));
		return result;
	}

	__m128 col0;
	__m128 col1;
	__m128 col2;
	__m128 col3;
};

// The columns are spread out between the data*. This is just intermediate storage for multiplication.
struct Mat4x3F32 {
	Mat4x3F32(const float *matrix) {
		data0 = _mm_loadu_ps(matrix);
		data1 = _mm_loadu_ps(matrix + 4);
		data2 = _mm_loadu_ps(matrix + 8);
	}

	__m128 data0;
	__m128 data1;
	__m128 data2;
};

inline Mat4F32 Mul4x4By4x4(Mat4F32 a, Mat4F32 b) {
	Mat4F32 result;

	__m128 r_col = _mm_mul_ps(b.col0, _mm_splat_lane_ps(a.col0, 0));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col1, _mm_splat_lane_ps(a.col0, 1)));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col2, _mm_splat_lane_ps(a.col0, 2)));
	result.col0 = _mm_add_ps(r_col, _mm_mul_ps(b.col3, _mm_splat_lane_ps(a.col0, 3)));

	r_col = _mm_mul_ps(b.col0, _mm_splat_lane_ps(a.col1, 0));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col1, _mm_splat_lane_ps(a.col1, 1)));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col2, _mm_splat_lane_ps(a.col1, 2)));
	result.col1 = _mm_add_ps(r_col, _mm_mul_ps(b.col3, _mm_splat_lane_ps(a.col1, 3)));

	r_col = _mm_mul_ps(b.col0, _mm_splat_lane_ps(a.col2, 0));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col1, _mm_splat_lane_ps(a.col2, 1)));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col2, _mm_splat_lane_ps(a.col2, 2)));
	result.col2 = _mm_add_ps(r_col, _mm_mul_ps(b.col3, _mm_splat_lane_ps(a.col2, 3)));

	r_col = _mm_mul_ps(b.col0, _mm_splat_lane_ps(a.col3, 0));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col1, _mm_splat_lane_ps(a.col3, 1)));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col2, _mm_splat_lane_ps(a.col3, 2)));
	result.col3 = _mm_add_ps(r_col, _mm_mul_ps(b.col3, _mm_splat_lane_ps(a.col3, 3)));

	return result;
}

inline Mat4F32 Mul4x3By4x4(Mat4x3F32 a, Mat4F32 b) {
	Mat4F32 result;

	__m128 r_col = _mm_mul_ps(b.col0, _mm_splat_lane_ps(a.data0, 0));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col1, _mm_splat_lane_ps(a.data0, 1)));
	result.col0 = _mm_add_ps(r_col, _mm_mul_ps(b.col2, _mm_splat_lane_ps(a.data0, 2)));

	r_col = _mm_mul_ps(b.col0, _mm_splat_lane_ps(a.data0, 3));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col1, _mm_splat_lane_ps(a.data1, 0)));
	result.col1 = _mm_add_ps(r_col, _mm_mul_ps(b.col2, _mm_splat_lane_ps(a.data1, 1)));

	r_col = _mm_mul_ps(b.col0, _mm_splat_lane_ps(a.data1, 2));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col1, _mm_splat_lane_ps(a.data1, 3)));
	result.col2 = _mm_add_ps(r_col, _mm_mul_ps(b.col2, _mm_splat_lane_ps(a.data2, 0)));

	r_col = _mm_mul_ps(b.col0, _mm_splat_lane_ps(a.data2, 1));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col1, _mm_splat_lane_ps(a.data2, 2)));
	r_col = _mm_add_ps(r_col, _mm_mul_ps(b.col2, _mm_splat_lane_ps(a.data2, 3)));

	// The last entry has an implied 1.0f.
	result.col3 = _mm_add_ps(r_col, b.col3);
	return result;
}

struct Vec4S32 {
	__m128i v;

	static Vec4S32 Zero() { return Vec4S32{ _mm_setzero_si128() }; }
	static Vec4S32 Splat(int lane) { return Vec4S32{ _mm_set1_epi32(lane) }; }

	static Vec4S32 Load(const int *src) { return Vec4S32{ _mm_loadu_si128((const __m128i *)src) }; }
	static Vec4S32 LoadAligned(const int *src) { return Vec4S32{ _mm_load_si128((const __m128i *)src) }; }
	void Store(int *dst) { _mm_storeu_si128((__m128i *)dst, v); }
	void Store2(int *dst) { _mm_storel_epi64((__m128i *)dst, v); }
	void StoreAligned(int *dst) { _mm_store_si128((__m128i *)dst, v);}

	Vec4S32 SignBits32ToMask() {
		return Vec4S32{
			_mm_srai_epi32(v, 31)
		};
	}

	// Reads 16 bits from both operands, produces a 32-bit result per lane.
	// On SSE2, much faster than _mm_mullo_epi32_SSE2.
	// On NEON though, it'll read the full 32 bits, so beware.
	// See https://fgiesen.wordpress.com/2016/04/03/sse-mind-the-gap/.
	Vec4S32 Mul16(Vec4S32 other) const {
		// Note that we only need to mask one of the inputs, so we get zeroes - multiplying
		// by zero is zero, so it doesn't matter what the upper halfword of each 32-bit word is
		// in the other register.
		return Vec4S32{ _mm_madd_epi16(v, _mm_and_si128(other.v, _mm_set1_epi32(0x0000FFFF))) };
	}

	Vec4S32 SignExtend16() const { return Vec4S32{ _mm_srai_epi32(_mm_slli_epi32(v, 16), 16) }; }
	// NOTE: These can be done in sequence, but when done, you must FixupAfterMinMax to get valid output.
	Vec4S32 Min16(Vec4S32 other) const { return Vec4S32{ _mm_min_epi16(v, other.v) }; }
	Vec4S32 Max16(Vec4S32 other) const { return Vec4S32{ _mm_max_epi16(v, other.v) }; }
	Vec4S32 FixupAfterMinMax() const { return SignExtend16(); }

	Vec4S32 operator +(Vec4S32 other) const { return Vec4S32{ _mm_add_epi32(v, other.v) }; }
	Vec4S32 operator -(Vec4S32 other) const { return Vec4S32{ _mm_sub_epi32(v, other.v) }; }
	Vec4S32 operator |(Vec4S32 other) const { return Vec4S32{ _mm_or_si128(v, other.v) }; }
	Vec4S32 operator &(Vec4S32 other) const { return Vec4S32{ _mm_and_si128(v, other.v) }; }
	Vec4S32 operator ^(Vec4S32 other) const { return Vec4S32{ _mm_xor_si128(v, other.v) }; }
	// TODO: andnot
	void operator +=(Vec4S32 other) { v = _mm_add_epi32(v, other.v); }
	void operator -=(Vec4S32 other) { v = _mm_sub_epi32(v, other.v); }
	void operator &=(Vec4S32 other) { v = _mm_and_si128(v, other.v); }
	void operator |=(Vec4S32 other) { v = _mm_or_si128(v, other.v); }
	void operator ^=(Vec4S32 other) { v = _mm_xor_si128(v, other.v); }

	Vec4S32 AndNot(Vec4S32 inverted) const { return Vec4S32{ _mm_andnot_si128(inverted.v, v) }; }  // NOTE: with _mm_andnot, the first parameter is inverted, and then and is performed.
	Vec4S32 Mul(Vec4S32 other) const { return *this * other; }

	template<int imm>
	Vec4S32 Shl() const { return Vec4S32{ imm == 0 ? v : _mm_slli_epi32(v, imm) }; }

	// NOTE: May be slow.
	int operator[](size_t index) const { return ((int *)&v)[index]; }

	// NOTE: This uses a CrossSIMD wrapper if we don't compile with SSE4 support, and is thus slow.
	Vec4S32 operator *(Vec4S32 other) const { return Vec4S32{ _mm_mullo_epi32_SSE2(v, other.v) }; }  // (ab3,ab2,ab1,ab0)

	Vec4S32 CompareEq(Vec4S32 other) const { return Vec4S32{ _mm_cmpeq_epi32(v, other.v) }; }
	Vec4S32 CompareLt(Vec4S32 other) const { return Vec4S32{ _mm_cmplt_epi32(v, other.v) }; }
	Vec4S32 CompareGt(Vec4S32 other) const { return Vec4S32{ _mm_cmpgt_epi32(v, other.v) }; }
};

inline bool AnyZeroSignBit(Vec4S32 value) {
	return _mm_movemask_ps(_mm_castsi128_ps(value.v)) != 0xF;
}

struct Vec4F32 {
	__m128 v;

	static Vec4F32 Zero() { return Vec4F32{ _mm_setzero_ps() }; }
	static Vec4F32 Splat(float lane) { return Vec4F32{ _mm_set1_ps(lane) }; }

	static Vec4F32 Load(const float *src) { return Vec4F32{ _mm_loadu_ps(src) }; }
	static Vec4F32 LoadAligned(const float *src) { return Vec4F32{ _mm_load_ps(src) }; }
	static Vec4F32 LoadS8Norm(const int8_t *src) {
		__m128i value = _mm_set1_epi32(*((uint32_t *)src));
		__m128i value32 = _mm_unpacklo_epi16(_mm_unpacklo_epi8(value, value), value);
		// Sign extension. A bit ugly without SSE4.
		value32 = _mm_srai_epi32(value32, 24);
		return Vec4F32 { _mm_mul_ps(_mm_cvtepi32_ps(value32), _mm_set1_ps(1.0f / 128.0f)) };
	}
	static Vec4F32 LoadS16Norm(const int16_t *src) {  // Divides by 32768.0f
		__m128i bits = _mm_loadl_epi64((const __m128i*)src);
		// Sign extension. A bit ugly without SSE4.
		bits = _mm_srai_epi32(_mm_unpacklo_epi16(bits, bits), 16);
		return Vec4F32 { _mm_mul_ps(_mm_cvtepi32_ps(bits), _mm_set1_ps(1.0f / 32768.0f)) };
	}

	static Vec4F32 LoadConvertS16(const int16_t *src) {  // Note: will load 8 bytes
		__m128i value = _mm_loadl_epi64((const __m128i *)src);
		// 16-bit to 32-bit, use the upper words and an arithmetic shift right to sign extend
		return Vec4F32{ _mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(value, value), 16)) };
	}

	static Vec4F32 LoadConvertS8(const int8_t *src) {  // Note: will load 8 bytes
		__m128i value = _mm_loadl_epi64((const __m128i *)src);
		__m128i value16 = _mm_unpacklo_epi8(value, value);
		// 16-bit to 32-bit, use the upper words and an arithmetic shift right to sign extend
		return Vec4F32{ _mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(value16, value16), 24)) };
	}

	// NOTE: Does not normalize to 0..255 range.
	static Vec4F32 LoadConvertU8(const uint8_t *src) {  // Note: will load 8 bytes
		__m128i value = _mm_loadl_epi64((const __m128i *)src);
		__m128i zero = _mm_setzero_si128();
		__m128i value16 = _mm_unpacklo_epi8(value, zero);
		// 16-bit to 32-bit, use the upper words and an arithmetic shift right to sign extend
		return Vec4F32{ _mm_cvtepi32_ps(_mm_unpacklo_epi16(value16, zero)) };
	}

	static Vec4F32 LoadF24x3_One(const uint32_t *src) {
		alignas(16) static const uint32_t mask[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0 };
		alignas(16) static const float onelane3[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		__m128 value = _mm_castsi128_ps(_mm_slli_epi32(_mm_loadu_si128((const __m128i *)src), 8));
		return Vec4F32{ _mm_or_ps(_mm_and_ps(value, _mm_load_ps((const float *)mask)), _mm_load_ps(onelane3)) };
	}

	void Store(float *dst) { _mm_storeu_ps(dst, v); }
	void Store2(float *dst) { _mm_storel_epi64((__m128i *)dst, _mm_castps_si128(v)); }
	void StoreAligned(float *dst) { _mm_store_ps(dst, v); }
	void Store3(float *dst) {
		// This seems to be the best way with SSE2.
		_mm_storel_pd((double *)dst, _mm_castps_pd(v));
		_mm_store_ss(dst + 2, _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2)));
	}
	void StoreConvertToU8(uint8_t *dst) {
		__m128i zero = _mm_setzero_si128();
		__m128i ivalue = _mm_packus_epi16(_mm_packs_epi32(_mm_cvttps_epi32(v), zero), zero);
		int32_t lo = _mm_cvtsi128_si32(ivalue);
		memcpy(dst, &lo, 4);
	}

	static Vec4F32 FromVec4S32(Vec4S32 other) { return Vec4F32{ _mm_cvtepi32_ps(other.v) }; }

	Vec4F32 operator +(Vec4F32 other) const { return Vec4F32{ _mm_add_ps(v, other.v) }; }
	Vec4F32 operator -(Vec4F32 other) const { return Vec4F32{ _mm_sub_ps(v, other.v) }; }
	Vec4F32 operator *(Vec4F32 other) const { return Vec4F32{ _mm_mul_ps(v, other.v) }; }
	Vec4F32 Min(Vec4F32 other) const { return Vec4F32{ _mm_min_ps(v, other.v) }; }
	Vec4F32 Max(Vec4F32 other) const { return Vec4F32{ _mm_max_ps(v, other.v) }; }
	void operator +=(Vec4F32 other) { v = _mm_add_ps(v, other.v); }
	void operator -=(Vec4F32 other) { v = _mm_sub_ps(v, other.v); }
	void operator *=(Vec4F32 other) { v = _mm_mul_ps(v, other.v); }
	void operator /=(Vec4F32 other) { v = _mm_div_ps(v, other.v); }
	void operator &=(Vec4S32 other) { v = _mm_and_ps(v, _mm_castsi128_ps(other.v)); }
	Vec4F32 operator *(float f) const { return Vec4F32{_mm_mul_ps(v, _mm_set1_ps(f))}; }
	void operator *=(float f) { v = _mm_mul_ps(v, _mm_set1_ps(f)); }
	// NOTE: May be slow.
	float operator[](size_t index) const { return ((float *)&v)[index]; }

	Vec4F32 Mul(float f) const { return Vec4F32{ _mm_mul_ps(v, _mm_set1_ps(f)) }; }
	Vec4F32 RecipApprox() const { return Vec4F32{ _mm_rcp_ps(v) }; }
	Vec4F32 Recip() const { return Vec4F32{ _mm_div_ps(_mm_set1_ps(1.0f), v) }; }

	Vec4F32 Clamp(float lower, float higher) const {
		return Vec4F32{
			_mm_min_ps(_mm_max_ps(v, _mm_set1_ps(lower)), _mm_set1_ps(higher))
		};
	}

	Vec4F32 WithLane3Zero() const {
		alignas(16) static const uint32_t mask[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0 };
		return Vec4F32{ _mm_and_ps(v, _mm_load_ps((const float *)mask)) };
	}

	Vec4F32 WithLane3One() const {
		alignas(16) static const uint32_t mask[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0 };
		alignas(16) static const float onelane3[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		return Vec4F32{ _mm_or_ps(_mm_and_ps(v, _mm_load_ps((const float *)mask)), _mm_load_ps((const float *)onelane3)) };
	}

	inline Vec4F32 AsVec3ByMatrix44(const Mat4F32 &m) {
		return Vec4F32{ _mm_add_ps(
			_mm_add_ps(
				_mm_mul_ps(m.col0, _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0))),
				_mm_mul_ps(m.col1, _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1)))
			),
			_mm_add_ps(
				_mm_mul_ps(m.col2, _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2))),
				m.col3)
			)
		};
	}

	static void Transpose(Vec4F32 &col0, Vec4F32 &col1, Vec4F32 &col2, Vec4F32 &col3) {
		_MM_TRANSPOSE4_PS(col0.v, col1.v, col2.v, col3.v);
	}

	// This is here because ARM64 can do this very efficiently.
	static void LoadTranspose(const float *src, Vec4F32 &col0, Vec4F32 &col1, Vec4F32 &col2, Vec4F32 &col3) {
		col0.v = _mm_loadu_ps(src);
		col1.v = _mm_loadu_ps(src + 4);
		col2.v = _mm_loadu_ps(src + 8);
		col3.v = _mm_loadu_ps(src + 12);
		_MM_TRANSPOSE4_PS(col0.v, col1.v, col2.v, col3.v);
	}

	Vec4S32 CompareEq(Vec4F32 other) const { return Vec4S32{ _mm_castps_si128(_mm_cmpeq_ps(v, other.v)) }; }
	Vec4S32 CompareLt(Vec4F32 other) const { return Vec4S32{ _mm_castps_si128(_mm_cmplt_ps(v, other.v)) }; }
	Vec4S32 CompareGt(Vec4F32 other) const { return Vec4S32{ _mm_castps_si128(_mm_cmpgt_ps(v, other.v)) }; }

	template<int i> float GetLane() const {
		return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(i, i, i, i)));
	}
};

inline Vec4S32 Vec4S32FromF32(Vec4F32 f) { return Vec4S32{ _mm_cvttps_epi32(f.v) }; }
inline Vec4F32 Vec4F32FromS32(Vec4S32 f) { return Vec4F32{ _mm_cvtepi32_ps(f.v) }; }

inline bool AnyZeroSignBit(Vec4F32 value) {
	return _mm_movemask_ps(value.v) != 0xF;
}

// Make sure the W component of scale is 1.0f.
inline void ScaleInplace(Mat4F32 &m, Vec4F32 scale) {
	m.col0 = _mm_mul_ps(m.col0, scale.v);
	m.col1 = _mm_mul_ps(m.col1, scale.v);
	m.col2 = _mm_mul_ps(m.col2, scale.v);
	m.col3 = _mm_mul_ps(m.col3, scale.v);
}

inline void TranslateAndScaleInplace(Mat4F32 &m, Vec4F32 scale, Vec4F32 translate) {
	m.col0 = _mm_add_ps(_mm_mul_ps(m.col0, scale.v), _mm_mul_ps(translate.v, _mm_shuffle_ps(m.col0, m.col0, _MM_SHUFFLE(3,3,3,3))));
	m.col1 = _mm_add_ps(_mm_mul_ps(m.col1, scale.v), _mm_mul_ps(translate.v, _mm_shuffle_ps(m.col1, m.col1, _MM_SHUFFLE(3,3,3,3))));
	m.col2 = _mm_add_ps(_mm_mul_ps(m.col2, scale.v), _mm_mul_ps(translate.v, _mm_shuffle_ps(m.col2, m.col2, _MM_SHUFFLE(3,3,3,3))));
	m.col3 = _mm_add_ps(_mm_mul_ps(m.col3, scale.v), _mm_mul_ps(translate.v, _mm_shuffle_ps(m.col3, m.col3, _MM_SHUFFLE(3,3,3,3))));
}

struct Vec4U16 {
	__m128i v;  // we only use the lower 64 bits.

	static Vec4U16 Zero() { return Vec4U16{ _mm_setzero_si128() }; }
	// static Vec4U16 AllOnes() { return Vec4U16{ _mm_cmpeq_epi16(_mm_setzero_si128(), _mm_setzero_si128()) }; }

	static Vec4U16 Load(const uint16_t *mem) { return Vec4U16{ _mm_loadl_epi64((__m128i *)mem) }; }
	void Store(uint16_t *mem) { _mm_storel_epi64((__m128i *)mem, v); }

	// NOTE: 16-bit signed saturation! Will work for a lot of things, but not all.
	static Vec4U16 FromVec4S32(Vec4S32 v) {
		return Vec4U16{ _mm_packu_epi32_SSE2(v.v)};
	}
	static Vec4U16 FromVec4F32(Vec4F32 v) {
		return Vec4U16{ _mm_packu_epi32_SSE2(_mm_cvtps_epi32(v.v)) };
	}

	Vec4U16 operator |(Vec4U16 other) const { return Vec4U16{ _mm_or_si128(v, other.v) }; }
	Vec4U16 operator &(Vec4U16 other) const { return Vec4U16{ _mm_and_si128(v, other.v) }; }
	Vec4U16 operator ^(Vec4U16 other) const { return Vec4U16{ _mm_xor_si128(v, other.v) }; }

	Vec4U16 Max(Vec4U16 other) const { return Vec4U16{ _mm_max_epu16_SSE2(v, other.v) }; }
	Vec4U16 Min(Vec4U16 other) const { return Vec4U16{ _mm_min_epu16_SSE2(v, other.v) }; }
	Vec4U16 CompareLT(Vec4U16 other) { return Vec4U16{ _mm_cmplt_epu16(v, other.v) }; }

	inline Vec4U16 AndNot(Vec4U16 inverted) {
		return Vec4U16{
			_mm_andnot_si128(inverted.v, v)  // NOTE: with _mm_andnot, the first parameter is inverted, and then and is performed.
		};
	}
};

struct Vec8U16 {
	__m128i v;

	static Vec8U16 Zero() { return Vec8U16{ _mm_setzero_si128() }; }
	static Vec8U16 Splat(uint16_t value) { return Vec8U16{ _mm_set1_epi16((int16_t)value) }; }

	static Vec8U16 Load(const uint16_t *mem) { return Vec8U16{ _mm_loadu_si128((__m128i *)mem) }; }
	void Store(uint16_t *mem) { _mm_storeu_si128((__m128i *)mem, v); }
};

inline Vec4U16 SignBits32ToMaskU16(Vec4S32 v) {
	__m128i temp = _mm_srai_epi32(v.v, 31);
	return Vec4U16 {
		_mm_packs_epi32(temp, temp)
	};
}

#elif PPSSPP_ARCH(ARM_NEON) && !TEST_FALLBACK

struct Mat4F32 {
	Mat4F32() {}
	Mat4F32(const float *matrix) {
		col0 = vld1q_f32(matrix);
		col1 = vld1q_f32(matrix + 4);
		col2 = vld1q_f32(matrix + 8);
		col3 = vld1q_f32(matrix + 12);
	}
	void Store(float *m) {
		vst1q_f32(m, col0);
		vst1q_f32(m + 4, col1);
		vst1q_f32(m + 8, col2);
		vst1q_f32(m + 12, col3);
	}

	// Unlike the old one, this one is careful about not loading out-of-range data.
	// The last two loads overlap.
	static Mat4F32 Load4x3(const float *m) {
		Mat4F32 result;
		result.col0 = vsetq_lane_f32(0.0f, vld1q_f32(m), 3);
		result.col1 = vsetq_lane_f32(0.0f, vld1q_f32(m + 3), 3);
		result.col2 = vsetq_lane_f32(0.0f, vld1q_f32(m + 6), 3);
		result.col3 = vsetq_lane_f32(1.0f, vld1q_f32(m + 9), 3);  // TODO: Fix this out of bounds read
		return result;
	}

	float32x4_t col0;
	float32x4_t col1;
	float32x4_t col2;
	float32x4_t col3;
};

// The columns are spread out between the data*. This is just intermediate storage for multiplication.
struct Mat4x3F32 {
	Mat4x3F32(const float *matrix) {
		data0 = vld1q_f32(matrix);
		data1 = vld1q_f32(matrix + 4);
		data2 = vld1q_f32(matrix + 8);
	}

	float32x4_t data0;
	float32x4_t data1;
	float32x4_t data2;
};

inline Mat4F32 Mul4x4By4x4(Mat4F32 a, Mat4F32 b) {
	Mat4F32 result;

	float32x4_t r_col = vmulq_laneq_f32(b.col0, a.col0, 0);
	r_col = vfmaq_laneq_f32(r_col, b.col1, a.col0, 1);
	r_col = vfmaq_laneq_f32(r_col, b.col2, a.col0, 2);
	result.col0 = vfmaq_laneq_f32(r_col, b.col3, a.col0, 3);

	r_col = vmulq_laneq_f32(b.col0, a.col1, 0);
	r_col = vfmaq_laneq_f32(r_col, b.col1, a.col1, 1);
	r_col = vfmaq_laneq_f32(r_col, b.col2, a.col1, 2);
	result.col1 = vfmaq_laneq_f32(r_col, b.col3, a.col1, 3);

	r_col = vmulq_laneq_f32(b.col0, a.col2, 0);
	r_col = vfmaq_laneq_f32(r_col, b.col1, a.col2, 1);
	r_col = vfmaq_laneq_f32(r_col, b.col2, a.col2, 2);
	result.col2 = vfmaq_laneq_f32(r_col, b.col3, a.col2, 3);

	r_col = vmulq_laneq_f32(b.col0, a.col3, 0);
	r_col = vfmaq_laneq_f32(r_col, b.col1, a.col3, 1);
	r_col = vfmaq_laneq_f32(r_col, b.col2, a.col3, 2);
	result.col3 = vfmaq_laneq_f32(r_col, b.col3, a.col3, 3);

	return result;
}

inline Mat4F32 Mul4x3By4x4(Mat4x3F32 a, Mat4F32 b) {
	Mat4F32 result;

	float32x4_t r_col = vmulq_laneq_f32(b.col0, a.data0, 0);
	r_col = vfmaq_laneq_f32(r_col, b.col1, a.data0, 1);
	result.col0 = vfmaq_laneq_f32(r_col, b.col2, a.data0, 2);

	r_col = vmulq_laneq_f32(b.col0, a.data0, 3);
	r_col = vfmaq_laneq_f32(r_col, b.col1, a.data1, 0);
	result.col1 = vfmaq_laneq_f32(r_col, b.col2, a.data1, 1);

	r_col = vmulq_laneq_f32(b.col0, a.data1, 2);
	r_col = vfmaq_laneq_f32(r_col, b.col1, a.data1, 3);
	result.col2 = vfmaq_laneq_f32(r_col, b.col2, a.data2, 0);

	r_col = vmulq_laneq_f32(b.col0, a.data2, 1);
	r_col = vfmaq_laneq_f32(r_col, b.col1, a.data2, 2);
	r_col = vfmaq_laneq_f32(r_col, b.col2, a.data2, 3);

	// The last entry has an implied 1.0f.
	result.col3 = vaddq_f32(r_col, b.col3);
	return result;
}

struct Vec4S32 {
	int32x4_t v;

	static Vec4S32 Zero() { return Vec4S32{ vdupq_n_s32(0) }; }
	static Vec4S32 Splat(int lane) { return Vec4S32{ vdupq_n_s32(lane) }; }

	static Vec4S32 Load(const int *src) { return Vec4S32{ vld1q_s32(src) }; }
	static Vec4S32 LoadAligned(const int *src) { return Vec4S32{ vld1q_s32(src) }; }
	void Store(int *dst) { vst1q_s32(dst, v); }
	void Store2(int *dst) { vst1_s32(dst, vget_low_s32(v)); }
	void StoreAligned(int *dst) { vst1q_s32(dst, v); }

	// Warning: Unlike on x86, this is a full 32-bit multiplication.
	Vec4S32 Mul16(Vec4S32 other) const { return Vec4S32{ vmulq_s32(v, other.v) }; }

	Vec4S32 SignExtend16() const { return Vec4S32{ vshrq_n_s32(vshlq_n_s32(v, 16), 16) }; }
	// NOTE: These can be done in sequence, but when done, you must FixupAfterMinMax to get valid output (on SSE2 at least).
	Vec4S32 Min16(Vec4S32 other) const { return Vec4S32{ vminq_s32(v, other.v) }; }
	Vec4S32 Max16(Vec4S32 other) const { return Vec4S32{ vmaxq_s32(v, other.v) }; }
	Vec4S32 FixupAfterMinMax() const { return Vec4S32{ v }; }

	// NOTE: May be slow.
	int operator[](size_t index) const { return ((int *)&v)[index]; }

	Vec4S32 operator +(Vec4S32 other) const { return Vec4S32{ vaddq_s32(v, other.v) }; }
	Vec4S32 operator -(Vec4S32 other) const { return Vec4S32{ vsubq_s32(v, other.v) }; }
	Vec4S32 operator *(Vec4S32 other) const { return Vec4S32{ vmulq_s32(v, other.v) }; }
	Vec4S32 operator |(Vec4S32 other) const { return Vec4S32{ vorrq_s32(v, other.v) }; }
	Vec4S32 operator &(Vec4S32 other) const { return Vec4S32{ vandq_s32(v, other.v) }; }
	Vec4S32 operator ^(Vec4S32 other) const { return Vec4S32{ veorq_s32(v, other.v) }; }
	Vec4S32 AndNot(Vec4S32 inverted) const { return Vec4S32{ vandq_s32(v, vmvnq_s32(inverted.v))}; }
	Vec4S32 Mul(Vec4S32 other) const { return Vec4S32{ vmulq_s32(v, other.v) }; }
	void operator &=(Vec4S32 other) { v = vandq_s32(v, other.v); }

	template<int imm>
	Vec4S32 Shl() const { return Vec4S32{ vshlq_n_s32(v, imm) }; }

	void operator +=(Vec4S32 other) { v = vaddq_s32(v, other.v); }
	void operator -=(Vec4S32 other) { v = vsubq_s32(v, other.v); }

	Vec4S32 CompareEq(Vec4S32 other) const { return Vec4S32{ vreinterpretq_s32_u32(vceqq_s32(v, other.v)) }; }
	Vec4S32 CompareLt(Vec4S32 other) const { return Vec4S32{ vreinterpretq_s32_u32(vcltq_s32(v, other.v)) }; }
	Vec4S32 CompareGt(Vec4S32 other) const { return Vec4S32{ vreinterpretq_s32_u32(vcgtq_s32(v, other.v)) }; }
	Vec4S32 CompareGtZero() const { return Vec4S32{ vreinterpretq_s32_u32(vcgtq_s32(v, vdupq_n_s32(0))) }; }
};

struct Vec4F32 {
	float32x4_t v;

	static Vec4F32 Zero() { return Vec4F32{ vdupq_n_f32(0.0f) }; }
	static Vec4F32 Splat(float lane) { return Vec4F32{ vdupq_n_f32(lane) }; }

	static Vec4F32 Load(const float *src) { return Vec4F32{ vld1q_f32(src) }; }
	static Vec4F32 LoadS8Norm(const int8_t *src) {
		const int8x8_t value = (int8x8_t)vdup_n_u32(*((uint32_t *)src));
		const int16x8_t value16 = vmovl_s8(value);
		return Vec4F32 { vcvtq_n_f32_s32(vmovl_s16(vget_low_s16(value16)), 7) };
	}
	static Vec4F32 LoadS16Norm(const int16_t *src) {  // Divides by 32768.0f
		return Vec4F32 { vcvtq_n_f32_s32(vmovl_s16(vld1_s16(src)), 15) };
	}
	static Vec4F32 LoadAligned(const float *src) { return Vec4F32{ vld1q_f32(src) }; }

	static Vec4F32 LoadConvertS16(const int16_t *src) {
		int16x4_t value = vld1_s16(src);
		return Vec4F32{ vcvtq_f32_s32(vmovl_s16(value)) };
	}

	static Vec4F32 LoadConvertS8(const int8_t *src) {  // Note: will load 8 bytes, not 4. Only the first 4 bytes will be used.
		int8x8_t value = vld1_s8(src);
		int16x4_t value16 = vget_low_s16(vmovl_s8(value));
		return Vec4F32{ vcvtq_f32_s32(vmovl_s16(value16)) };
	}

	static Vec4F32 LoadConvertU8(const uint8_t *src) {  // Note: will load 8 bytes, not 4. Only the first 4 bytes will be used.
		uint8x8_t value = vld1_u8(src);
		uint16x4_t value16 = vget_low_u16(vmovl_u8(value));
		return Vec4F32{ vcvtq_f32_u32(vmovl_u16(value16)) };
	}

	static Vec4F32 LoadF24x3_One(const uint32_t *src) {
		return Vec4F32{ vsetq_lane_f32(1.0f, vreinterpretq_f32_u32(vshlq_n_u32(vld1q_u32(src), 8)), 3) };
	}

	static Vec4F32 FromVec4S32(Vec4S32 other) {
		return Vec4F32{ vcvtq_f32_s32(other.v) };
	}

	void Store(float *dst) { vst1q_f32(dst, v); }
	void Store2(float *dst) { vst1_f32(dst, vget_low_f32(v)); }
	void StoreAligned(float *dst) { vst1q_f32(dst, v); }
	void Store3(float *dst) {
		// TODO: There might be better ways. Try to avoid this when possible.
		vst1_f32(dst, vget_low_f32(v));
#if PPSSPP_ARCH(ARM64_NEON)
		vst1q_lane_f32(dst + 2, v, 2);
#else
		dst[2] = vgetq_lane_f32(v, 2);
#endif
	}
	void StoreConvertToU8(uint8_t *dest) {
		uint32x4_t ivalue32 = vcvtq_u32_f32(v);
		uint16x4_t ivalue16 = vqmovn_u32(ivalue32);
		uint8x8_t ivalue8 = vqmovn_u16(vcombine_u16(ivalue16, ivalue16));  // Is there no way to avoid the combine here?
		uint32_t value = vget_lane_u32(vreinterpret_u32_u8(ivalue8), 0);
		memcpy(dest, &value, sizeof(uint32_t));
	}

	// NOTE: May be slow.
	float operator[](size_t index) const { return ((float *)&v)[index]; }

	Vec4F32 operator +(Vec4F32 other) const { return Vec4F32{ vaddq_f32(v, other.v) }; }
	Vec4F32 operator -(Vec4F32 other) const { return Vec4F32{ vsubq_f32(v, other.v) }; }
	Vec4F32 operator *(Vec4F32 other) const { return Vec4F32{ vmulq_f32(v, other.v) }; }
	Vec4F32 Min(Vec4F32 other) const { return Vec4F32{ vminq_f32(v, other.v) }; }
	Vec4F32 Max(Vec4F32 other) const { return Vec4F32{ vmaxq_f32(v, other.v) }; }
	void operator +=(Vec4F32 other) { v = vaddq_f32(v, other.v); }
	void operator -=(Vec4F32 other) { v = vsubq_f32(v, other.v); }
	void operator *=(Vec4F32 other) { v = vmulq_f32(v, other.v); }
#if PPSSPP_ARCH(ARM64_NEON)
	void operator /=(Vec4F32 other) { v = vdivq_f32(v, other.v); }
#else
	// ARM32 doesn't have vdivq.
	void operator /=(Vec4F32 other) { v = vmulq_f32(v, other.Recip().v); }
#endif
	void operator &=(Vec4S32 other) { v = vreinterpretq_f32_s32(vandq_s32(vreinterpretq_s32_f32(v), other.v)); }
	Vec4F32 operator *(float f) const { return Vec4F32{ vmulq_f32(v, vdupq_n_f32(f)) }; }
	void operator *=(float f) { v = vmulq_f32(v, vdupq_n_f32(f)); }

	Vec4F32 Mul(float f) const { return Vec4F32{ vmulq_f32(v, vdupq_n_f32(f)) }; }

	Vec4F32 Recip() const {
		float32x4_t recip = vrecpeq_f32(v);
		// Use a couple Newton-Raphson steps to refine the estimate.
		// To save one iteration at the expense of accuracy, use RecipApprox().
		recip = vmulq_f32(vrecpsq_f32(v, recip), recip);
		recip = vmulq_f32(vrecpsq_f32(v, recip), recip);
		return Vec4F32{ recip };
	}

	Vec4F32 RecipApprox() const {
		float32x4_t recip = vrecpeq_f32(v);
		// To approximately match the precision of x86-64's rcpps, do a single iteration.
		recip = vmulq_f32(vrecpsq_f32(v, recip), recip);
		return Vec4F32{ recip };
	}

	Vec4F32 Clamp(float lower, float higher) const {
		return Vec4F32{
			vminq_f32(vmaxq_f32(v, vdupq_n_f32(lower)), vdupq_n_f32(higher))
		};
	}

	Vec4F32 WithLane3Zero() const {
		return Vec4F32{ vsetq_lane_f32(0.0f, v, 3) };
	}

	Vec4F32 WithLane3One() const {
		return Vec4F32{ vsetq_lane_f32(1.0f, v, 3) };
	}

	Vec4S32 CompareEq(Vec4F32 other) const { return Vec4S32{ vreinterpretq_s32_u32(vceqq_f32(v, other.v)) }; }
	Vec4S32 CompareLt(Vec4F32 other) const { return Vec4S32{ vreinterpretq_s32_u32(vcltq_f32(v, other.v)) }; }
	Vec4S32 CompareGt(Vec4F32 other) const { return Vec4S32{ vreinterpretq_s32_u32(vcgtq_f32(v, other.v)) }; }
	Vec4S32 CompareLe(Vec4F32 other) const { return Vec4S32{ vreinterpretq_s32_u32(vcleq_f32(v, other.v)) }; }
	Vec4S32 CompareGe(Vec4F32 other) const { return Vec4S32{ vreinterpretq_s32_u32(vcgeq_f32(v, other.v)) }; }

	// One of many possible solutions. Sometimes we could also use vld4q_f32 probably..
	static void Transpose(Vec4F32 &col0, Vec4F32 &col1, Vec4F32 &col2, Vec4F32 &col3) {
#if PPSSPP_ARCH(ARM64_NEON)
		// Only works on ARM64
		float32x4_t temp0 = vzip1q_f32(col0.v, col2.v);
		float32x4_t temp1 = vzip2q_f32(col0.v, col2.v);
		float32x4_t temp2 = vzip1q_f32(col1.v, col3.v);
		float32x4_t temp3 = vzip2q_f32(col1.v, col3.v);
		col0.v = vzip1q_f32(temp0, temp2);
		col1.v = vzip2q_f32(temp0, temp2);
		col2.v = vzip1q_f32(temp1, temp3);
		col3.v = vzip2q_f32(temp1, temp3);
#else
   		float32x4x2_t col01 = vtrnq_f32(col0.v, col1.v);
        float32x4x2_t col23 = vtrnq_f32(col2.v, col3.v);
        col0.v = vcombine_f32(vget_low_f32(col01.val[0]), vget_low_f32(col23.val[0]));
        col1.v = vcombine_f32(vget_low_f32(col01.val[1]), vget_low_f32(col23.val[1]));
        col2.v = vcombine_f32(vget_high_f32(col01.val[0]), vget_high_f32(col23.val[0]));
        col3.v = vcombine_f32(vget_high_f32(col01.val[1]), vget_high_f32(col23.val[1]));
#endif
	}

	static void LoadTranspose(const float *src, Vec4F32 &col0, Vec4F32 &col1, Vec4F32 &col2, Vec4F32 &col3) {
		// The optimizer hopefully gets rid of the copies below.
		float32x4x4_t r = vld4q_f32(src);
		col0.v = r.val[0];
		col1.v = r.val[1];
		col2.v = r.val[2];
		col3.v = r.val[3];
	}

	inline Vec4F32 AsVec3ByMatrix44(const Mat4F32 &m) {
#if PPSSPP_ARCH(ARM64_NEON)
		float32x4_t sum = vaddq_f32(
			vaddq_f32(vmulq_laneq_f32(m.col0, v, 0), vmulq_laneq_f32(m.col1, v, 1)),
			vaddq_f32(vmulq_laneq_f32(m.col2, v, 2), m.col3));
#else
		float32x4_t sum = vaddq_f32(
			vaddq_f32(vmulq_lane_f32(m.col0, vget_low_f32(v), 0), vmulq_lane_f32(m.col1, vget_low_f32(v), 1)),
			vaddq_f32(vmulq_lane_f32(m.col2, vget_high_f32(v), 0), m.col3));
#endif
		return Vec4F32{ sum };
	}

	template<int i> float GetLane() const {
		return vgetq_lane_f32(v, i);
	}
};

inline Vec4S32 Vec4S32FromF32(Vec4F32 f) { return Vec4S32{ vcvtq_s32_f32(f.v) }; }
inline Vec4F32 Vec4F32FromS32(Vec4S32 s) { return Vec4F32{ vcvtq_f32_s32(s.v) }; }

// Make sure the W component of scale is 1.0f.
inline void ScaleInplace(Mat4F32 &m, Vec4F32 scale) {
	m.col0 = vmulq_f32(m.col0, scale.v);
	m.col1 = vmulq_f32(m.col1, scale.v);
	m.col2 = vmulq_f32(m.col2, scale.v);
	m.col3 = vmulq_f32(m.col3, scale.v);
}

// Make sure the W component of scale is 1.0f, and the W component of translate should be 0.
inline void TranslateAndScaleInplace(Mat4F32 &m, Vec4F32 scale, Vec4F32 translate) {
	m.col0 = vaddq_f32(vmulq_f32(m.col0, scale.v), vmulq_laneq_f32(translate.v, m.col0, 3));
	m.col1 = vaddq_f32(vmulq_f32(m.col1, scale.v), vmulq_laneq_f32(translate.v, m.col1, 3));
	m.col2 = vaddq_f32(vmulq_f32(m.col2, scale.v), vmulq_laneq_f32(translate.v, m.col2, 3));
	m.col3 = vaddq_f32(vmulq_f32(m.col3, scale.v), vmulq_laneq_f32(translate.v, m.col3, 3));
}

inline bool AnyZeroSignBit(Vec4S32 value) {
#if PPSSPP_ARCH(ARM64_NEON)
	// Shortcut on arm64
	return vmaxvq_s32(value.v) >= 0;
#else
	// Very suboptimal, let's optimize later.
	int32x2_t prod = vand_s32(vget_low_s32(value.v), vget_high_s32(value.v));
	int mask = vget_lane_s32(prod, 0) & vget_lane_s32(prod, 1);
	return (mask & 0x80000000) == 0;
#endif
}

inline bool AnyZeroSignBit(Vec4F32 value) {
	int32x4_t ival = vreinterpretq_s32_f32(value.v);
#if PPSSPP_ARCH(ARM64_NEON)
	// Shortcut on arm64
	return vmaxvq_s32(ival) >= 0;
#else
	int32x2_t prod = vand_s32(vget_low_s32(ival), vget_high_s32(ival));
	int mask = vget_lane_s32(prod, 0) & vget_lane_s32(prod, 1);
	return (mask & 0x80000000) == 0;
#endif
}

struct Vec4U16 {
	uint16x4_t v;  // 64 bits.

	static Vec4U16 Zero() { return Vec4U16{ vdup_n_u16(0) }; }
	static Vec4U16 Splat(uint16_t value) { return Vec4U16{ vdup_n_u16(value) }; }

	static Vec4U16 Load(const uint16_t *mem) { return Vec4U16{ vld1_u16(mem) }; }
	void Store(uint16_t *mem) { vst1_u16(mem, v); }

	static Vec4U16 FromVec4S32(Vec4S32 v) {
		return Vec4U16{ vmovn_u32(vreinterpretq_u32_s32(v.v)) };
	}
	static Vec4U16 FromVec4F32(Vec4F32 v) {
		return Vec4U16{ vmovn_u32(vreinterpretq_u32_s32(vcvtq_s32_f32(v.v))) };
	}

	Vec4U16 operator |(Vec4U16 other) const { return Vec4U16{ vorr_u16(v, other.v) }; }
	Vec4U16 operator &(Vec4U16 other) const { return Vec4U16{ vand_u16(v, other.v) }; }
	Vec4U16 operator ^(Vec4U16 other) const { return Vec4U16{ veor_u16(v, other.v) }; }

	Vec4U16 Max(Vec4U16 other) const { return Vec4U16{ vmax_u16(v, other.v) }; }
	Vec4U16 Min(Vec4U16 other) const { return Vec4U16{ vmin_u16(v, other.v) }; }
	Vec4U16 CompareLT(Vec4U16 other) { return Vec4U16{ vclt_u16(v, other.v) }; }

	Vec4U16 AndNot(Vec4U16 inverted) { return Vec4U16{ vand_u16(v, vmvn_u16(inverted.v)) }; }
};

inline Vec4U16 SignBits32ToMaskU16(Vec4S32 v) {
	int32x4_t sign_mask = vshrq_n_s32(v.v, 31);
	uint16x4_t result = vreinterpret_u16_s16(vmovn_s32(sign_mask));
	return Vec4U16{ result };
}

struct Vec8U16 {
	uint16x8_t v;

	static Vec8U16 Zero() { return Vec8U16{ vdupq_n_u16(0) }; }
	static Vec8U16 Splat(uint16_t value) { return Vec8U16{ vdupq_n_u16(value) }; }

	static Vec8U16 Load(const uint16_t *mem) { return Vec8U16{ vld1q_u16(mem) }; }
	void Store(uint16_t *mem) { vst1q_u16(mem, v); }
};

#else

#define CROSSSIMD_SLOW 1

// Fake SIMD by using scalar.

struct Mat4F32 {
	Mat4F32() {}
	Mat4F32(const float *src) {
		memcpy(m, src, sizeof(m));
	}
	void Store(float *dest) {
		memcpy(dest, m, sizeof(m));
	}
	static Mat4F32 Load4x3(const float *src) {
		Mat4F32 mat;
		mat.m[0] = src[0];
		mat.m[1] = src[1];
		mat.m[2] = src[2];
		mat.m[3] = 0.0f;
		mat.m[4] = src[3];
		mat.m[5] = src[4];
		mat.m[6] = src[5];
		mat.m[7] = 0.0f;
		mat.m[8] = src[6];
		mat.m[9] = src[7];
		mat.m[10] = src[8];
		mat.m[11] = 0.0f;
		mat.m[12] = src[9];
		mat.m[13] = src[10];
		mat.m[14] = src[11];
		mat.m[15] = 1.0f;
		return mat;
	}

	// cols are consecutive
	float m[16];
};

// The columns are consecutive but missing the last row (implied 0,0,0,1).
// This is just intermediate storage for multiplication.
struct Mat4x3F32 {
	Mat4x3F32(const float *matrix) {
		memcpy(m, matrix, 12 * sizeof(float));
	}
	float m[12];
};

struct Vec4S32 {
	int32_t v[4];

	static Vec4S32 Zero() { return Vec4S32{}; }
	static Vec4S32 Splat(int lane) { return Vec4S32{ { lane, lane, lane, lane } }; }

	static Vec4S32 Load(const int *src) { return Vec4S32{ { src[0], src[1], src[2], src[3] }}; }
	static Vec4S32 LoadAligned(const int *src) { return Load(src); }
	void Store(int *dst) { memcpy(dst, v, sizeof(v)); }
	void Store2(int *dst) { memcpy(dst, v, sizeof(v[0]) * 2); }
	void StoreAligned(int *dst) { memcpy(dst, v, sizeof(v)); }

	// Warning: Unlike on x86 SSE2, this is a full 32-bit multiplication.
	Vec4S32 Mul16(Vec4S32 other) const { return Vec4S32{ { v[0] * other.v[0], v[1] * other.v[1], v[2] * other.v[2], v[3] * other.v[3] } }; }

	Vec4S32 SignExtend16() const {
		Vec4S32 tmp;
		for (int i = 0; i < 4; i++) {
			tmp.v[i] = (int32_t)(int16_t)v[i];
		}
		return tmp;
	}
	// NOTE: These can be done in sequence, but when done, you must FixupAfterMinMax to get valid output (on SSE2 at least).
	Vec4S32 Min16(Vec4S32 other) const {
		Vec4S32 tmp;
		for (int i = 0; i < 4; i++) {
			tmp.v[i] = other.v[i] < v[i] ? other.v[i] : v[i];
		}
		return tmp;
	}
	Vec4S32 Max16(Vec4S32 other) const {
		Vec4S32 tmp;
		for (int i = 0; i < 4; i++) {
			tmp.v[i] = other.v[i] > v[i] ? other.v[i] : v[i];
		}
		return tmp;
	}
	Vec4S32 FixupAfterMinMax() const { return *this; }

	int operator[](size_t index) const { return v[index]; }

	Vec4S32 operator +(Vec4S32 other) const {
		return Vec4S32{ { v[0] + other.v[0], v[1] + other.v[1], v[2] + other.v[2], v[3] + other.v[3], } };
	}
	Vec4S32 operator -(Vec4S32 other) const {
		return Vec4S32{ { v[0] - other.v[0], v[1] - other.v[1], v[2] - other.v[2], v[3] - other.v[3], } };
	}
	Vec4S32 operator *(Vec4S32 other) const {
		return Vec4S32{ { v[0] * other.v[0], v[1] * other.v[1], v[2] * other.v[2], v[3] * other.v[3], } };
	}
	// TODO: Can optimize the bitwise ones with 64-bit operations.
	Vec4S32 operator |(Vec4S32 other) const {
		return Vec4S32{ { v[0] | other.v[0], v[1] | other.v[1], v[2] | other.v[2], v[3] | other.v[3], } };
	}
	Vec4S32 operator &(Vec4S32 other) const {
		return Vec4S32{ { v[0] & other.v[0], v[1] & other.v[1], v[2] & other.v[2], v[3] & other.v[3], } };
	}
	Vec4S32 operator ^(Vec4S32 other) const {
		return Vec4S32{ { v[0] ^ other.v[0], v[1] ^ other.v[1], v[2] ^ other.v[2], v[3] ^ other.v[3], } };
	}
	Vec4S32 AndNot(Vec4S32 other) const {
		return Vec4S32{ { v[0] & ~other.v[0], v[1] & ~other.v[1], v[2] & ~other.v[2], v[3] & ~other.v[3], } };
	}
	Vec4S32 Mul(Vec4S32 other) const { return *this * other; }

	void operator &=(Vec4S32 other) { for (int i = 0; i < 4; i++) v[i] &= other.v[i]; }
	void operator +=(Vec4S32 other) { for (int i = 0; i < 4; i++) v[i] += other.v[i]; }
	void operator -=(Vec4S32 other) { for (int i = 0; i < 4; i++) v[i] -= other.v[i]; }

	template<int imm>
	Vec4S32 Shl() const { return Vec4S32{ { v[0] << imm, v[1] << imm, v[2] << imm, v[3] << imm } }; }

	Vec4S32 CompareEq(Vec4S32 other) const {
		Vec4S32 out;
		for (int i = 0; i < 4; i++) {
			out.v[i] = v[i] == other.v[i] ? 0xFFFFFFFF : 0;
		}
		return out;
	}
	Vec4S32 CompareLt(Vec4S32 other) const {
		Vec4S32 out;
		for (int i = 0; i < 4; i++) {
			out.v[i] = v[i] < other.v[i] ? 0xFFFFFFFF : 0;
		}
		return out;
	}
	Vec4S32 CompareGt(Vec4S32 other) const {
		Vec4S32 out;
		for (int i = 0; i < 4; i++) {
			out.v[i] = v[i] > other.v[i] ? 0xFFFFFFFF : 0;
		}
		return out;
	}
	Vec4S32 CompareGtZero() const {
		Vec4S32 out;
		for (int i = 0; i < 4; i++) {
			out.v[i] = v[i] > 0 ? 0xFFFFFFFF : 0;
		}
		return out;
	}
};

struct Vec4F32 {
	float v[4];

	static Vec4F32 Zero() { return Vec4F32{}; }
	static Vec4F32 Splat(float lane) { return Vec4F32{ { lane, lane, lane, lane } }; }

	static Vec4F32 Load(const float *src) { return Vec4F32{ { src[0], src[1], src[2], src[3] } }; }
	static Vec4F32 LoadAligned(const float *src) { return Vec4F32{ { src[0], src[1], src[2], src[3] } }; }
	static Vec4F32 LoadS8Norm(const int8_t *src) {
		Vec4F32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = (float)src[i] * (1.0f / 128.0f);
		}
		return temp;
	}
	static Vec4F32 LoadS16Norm(const int16_t *src) {  // Divides by 32768.0f
		Vec4F32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = (float)src[i] * (1.0f / 32768.0f);
		}
		return temp;
	}
	void Store(float *dst) { memcpy(dst, v, sizeof(v)); }
	void Store2(float *dst) { memcpy(dst, v, sizeof(v[0]) * 2); }
	void StoreAligned(float *dst) { memcpy(dst, v, sizeof(v)); }
	void Store3(float *dst) {
		memcpy(dst, v, sizeof(v[0]) * 3);
	}

	static Vec4F32 LoadConvertS16(const int16_t *src) {
		Vec4F32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = (float)src[i];
		}
		return temp;
	}

	static Vec4F32 LoadConvertS8(const int8_t *src) {  // Note: will load 8 bytes, not 4. Only the first 4 bytes will be used.
		Vec4F32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = (float)src[i];
		}
		return temp;
	}

	static Vec4F32 LoadF24x3_One(const uint32_t *src) {
		uint32_t shifted[4] = { src[0] << 8, src[1] << 8, src[2] << 8, 0 };
		Vec4F32 temp;
		memcpy(temp.v, shifted, sizeof(temp.v));
		return temp;
	}

	static Vec4F32 FromVec4S32(Vec4S32 src) {
		Vec4F32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = (float)src[i];
		}
		return temp;
	}

	float operator[](size_t index) const { return v[index]; }

	Vec4F32 operator +(Vec4F32 other) const {
		return Vec4F32{ { v[0] + other.v[0], v[1] + other.v[1], v[2] + other.v[2], v[3] + other.v[3], } };
	}
	Vec4F32 operator -(Vec4F32 other) const {
		return Vec4F32{ { v[0] - other.v[0], v[1] - other.v[1], v[2] - other.v[2], v[3] - other.v[3], } };
	}
	Vec4F32 operator *(Vec4F32 other) const {
		return Vec4F32{ { v[0] * other.v[0], v[1] * other.v[1], v[2] * other.v[2], v[3] * other.v[3], } };
	}
	Vec4F32 Min(Vec4F32 other) const {
		Vec4F32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] < other.v[i] ? v[i] : other.v[i];
		}
		return temp;
	}
	Vec4F32 Max(Vec4F32 other) const {
		Vec4F32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] > other.v[i] ? v[i] : other.v[i];
		}
		return temp;
	}
	void operator +=(Vec4F32 other) {
		for (int i = 0; i < 4; i++) {
			v[i] += other.v[i];
		}
	}
	void operator -=(Vec4F32 other) {
		for (int i = 0; i < 4; i++) {
			v[i] -= other.v[i];
		}
	}
	void operator *=(Vec4F32 other) {
		for (int i = 0; i < 4; i++) {
			v[i] *= other.v[i];
		}
	}
	void operator /=(Vec4F32 other) {
		for (int i = 0; i < 4; i++) {
			v[i] /= other.v[i];
		}
	}
	void operator &=(Vec4S32 other) {
		// TODO: This can be done simpler, although with some ugly casts.
		for (int i = 0; i < 4; i++) {
			uint32_t val;
			memcpy(&val, &v[i], 4);
			val &= other.v[i];
			memcpy(&v[i], &val, 4);
		}
	}
	Vec4F32 operator *(float f) const {
		return Vec4F32{ { v[0] * f, v[1] * f, v[2] * f, v[3] * f } };
	}

	Vec4F32 Mul(float f) const {
		return Vec4F32{ { v[0] * f, v[1] * f, v[2] * f, v[3] * f } };
	}

	Vec4F32 Recip() const {
		return Vec4F32{ { 1.0f / v[0], 1.0f / v[1], 1.0f / v[2], 1.0f / v[3] } };
	}

	Vec4F32 RecipApprox() const {
		return Vec4F32{ { 1.0f / v[0], 1.0f / v[1], 1.0f / v[2], 1.0f / v[3] } };
	}

	Vec4F32 Clamp(float lower, float higher) const {
		Vec4F32 temp;
		for (int i = 0; i < 4; i++) {
			if (v[i] > higher) {
				temp.v[i] = higher;
			} else if (v[i] < lower) {
				temp.v[i] = lower;
			} else {
				temp.v[i] = v[i];
			}
		}
		return temp;
	}

	Vec4F32 WithLane3Zero() const {
		return Vec4F32{ { v[0], v[1], v[2], 0.0f } };
	}

	Vec4F32 WithLane3One() const {
		return Vec4F32{ { v[0], v[1], v[2], 1.0f } };
	}

	Vec4S32 CompareEq(Vec4F32 other) const {
		Vec4S32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] == other.v[i] ? 0xFFFFFFFF : 0;
		}
		return temp;
	}
	Vec4S32 CompareLt(Vec4F32 other) const {
		Vec4S32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] < other.v[i] ? 0xFFFFFFFF : 0;
		}
		return temp;
	}
	Vec4S32 CompareGt(Vec4F32 other) const {
		Vec4S32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] > other.v[i] ? 0xFFFFFFFF : 0;
		}
		return temp;
	}
	Vec4S32 CompareLe(Vec4F32 other) const {
		Vec4S32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] <= other.v[i] ? 0xFFFFFFFF : 0;
		}
		return temp;
	}
	Vec4S32 CompareGe(Vec4F32 other) const {
		Vec4S32 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] >= other.v[i] ? 0xFFFFFFFF : 0;
		}
		return temp;
	}

	// In-place transpose.
	static void Transpose(Vec4F32 &col0, Vec4F32 &col1, Vec4F32 &col2, Vec4F32 &col3) {
		float m[16];
		for (int i = 0; i < 4; i++) {
			m[0 + i] = col0.v[i];
			m[4 + i] = col1.v[i];
			m[8 + i] = col2.v[i];
			m[12 + i] = col3.v[i];
		}
		for (int i = 0; i < 4; i++) {
			col0.v[i] = m[i * 4 + 0];
			col1.v[i] = m[i * 4 + 1];
			col2.v[i] = m[i * 4 + 2];
			col3.v[i] = m[i * 4 + 3];
		}
	}

	inline Vec4F32 AsVec3ByMatrix44(const Mat4F32 &m) {
		float x = m.m[0] * v[0] + m.m[4] * v[1] + m.m[8] * v[2] + m.m[12];
		float y = m.m[1] * v[0] + m.m[5] * v[1] + m.m[9] * v[2] + m.m[13];
		float z = m.m[2] * v[0] + m.m[6] * v[1] + m.m[10] * v[2] + m.m[14];

		return Vec4F32{ { x, y, z, 1.0f } };
	}

	template<int i> float GetLane() const {
		return v[i];
	}
};

inline bool AnyZeroSignBit(Vec4S32 value) {
	for (int i = 0; i < 4; i++) {
		if (value.v[i] >= 0) {
			return true;
		}
	}
	return false;
}

inline bool AnyZeroSignBit(Vec4F32 value) {
	for (int i = 0; i < 4; i++) {
		if (value.v[i] >= 0.0f) {
			return true;
		}
	}
	return false;
}

struct Vec4U16 {
	uint16_t v[4];  // 64 bits.

	static Vec4U16 Zero() { return Vec4U16{}; }
	static Vec4U16 Splat(uint16_t lane) { return Vec4U16{ { lane, lane, lane, lane } }; }

	static Vec4U16 Load(const uint16_t *mem) { return Vec4U16{ { mem[0], mem[1], mem[2], mem[3] }}; }
	void Store(uint16_t *mem) { memcpy(mem, v, sizeof(v)); }

	static Vec4U16 FromVec4S32(Vec4S32 v) {
		return Vec4U16{ { (uint16_t)v.v[0], (uint16_t)v.v[1], (uint16_t)v.v[2], (uint16_t)v.v[3] }};
	}
	static Vec4U16 FromVec4F32(Vec4F32 v) {
		return Vec4U16{ { (uint16_t)v.v[0], (uint16_t)v.v[1], (uint16_t)v.v[2], (uint16_t)v.v[3] }};
	}

	Vec4U16 operator |(Vec4U16 other) const { return Vec4U16{ { (uint16_t)(v[0] | other.v[0]), (uint16_t)(v[1] | other.v[1]), (uint16_t)(v[2] | other.v[2]), (uint16_t)(v[3] | other.v[3]), } }; }
	Vec4U16 operator &(Vec4U16 other) const { return Vec4U16{ { (uint16_t)(v[0] & other.v[0]), (uint16_t)(v[1] & other.v[1]), (uint16_t)(v[2] & other.v[2]), (uint16_t)(v[3] & other.v[3]), } }; }
	Vec4U16 operator ^(Vec4U16 other) const { return Vec4U16{ { (uint16_t)	(v[0] ^ other.v[0]), (uint16_t)(v[1] ^ other.v[1]), (uint16_t)(v[2] ^ other.v[2]), (uint16_t)(v[3] ^ other.v[3]), } }; }

	Vec4U16 Max(Vec4U16 other) const {
		Vec4U16 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] > other.v[i] ? v[i] : other.v[i];
		}
		return temp;
	}
	Vec4U16 Min(Vec4U16 other) const {
		Vec4U16 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] < other.v[i] ? v[i] : other.v[i];
		}
		return temp;
	}
	Vec4U16 CompareLT(Vec4U16 other) const {
		Vec4U16 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] < other.v[i] ? 0xFFFF : 0;
		}
		return temp;
	}
	Vec4U16 AndNot(Vec4U16 other) const {
		Vec4U16 temp;
		for (int i = 0; i < 4; i++) {
			temp.v[i] = v[i] & ~other.v[i];
		}
		return temp;
	}
};

struct Vec8U16 {
	uint16_t v[8];

	static Vec8U16 Zero() { return Vec8U16{}; }
	static Vec8U16 Splat(uint16_t value) { return Vec8U16{ {
		value, value, value, value, value, value, value, value,
	}}; }

	static Vec8U16 Load(const uint16_t *mem) { Vec8U16 tmp; memcpy(tmp.v, mem, sizeof(v)); return tmp; }
	void Store(uint16_t *mem) { memcpy(mem, v, sizeof(v)); }
};

inline Vec4U16 SignBits32ToMaskU16(Vec4S32 v) {
	return Vec4U16{ { (uint16_t)(v.v[0] >> 31), (uint16_t)(v.v[1] >> 31), (uint16_t)(v.v[2] >> 31), (uint16_t)(v.v[3] >> 31),  } };
}

inline Vec4S32 Vec4S32FromF32(Vec4F32 f) {
	return Vec4S32{ { (int32_t)f.v[0], (int32_t)f.v[1], (int32_t)f.v[2], (int32_t)f.v[3] } };
}

inline Vec4F32 Vec4F32FromS32(Vec4S32 f) {
	return Vec4F32{ { (float)f.v[0], (float)f.v[1], (float)f.v[2], (float)f.v[3] } };
}

// Make sure the W component of scale is 1.0f.
inline void ScaleInplace(Mat4F32 &m, Vec4F32 scale) {
	for (int i = 0; i < 4; i++) {
		m.m[i * 4 + 0] *= scale.v[0];
		m.m[i * 4 + 1] *= scale.v[1];
		m.m[i * 4 + 2] *= scale.v[2];
		m.m[i * 4 + 3] *= scale.v[3];
	}
}

inline void TranslateAndScaleInplace(Mat4F32 &m, Vec4F32 scale, Vec4F32 translate) {
	for (int i = 0; i < 4; i++) {
		m.m[i * 4 + 0] = m.m[i * 4 + 0] * scale.v[0] + translate.v[0] * m.m[i * 4 + 3];
		m.m[i * 4 + 1] = m.m[i * 4 + 1] * scale.v[1] + translate.v[1] * m.m[i * 4 + 3];
		m.m[i * 4 + 2] = m.m[i * 4 + 2] * scale.v[2] + translate.v[2] * m.m[i * 4 + 3];
		m.m[i * 4 + 3] = m.m[i * 4 + 3] * scale.v[3] + translate.v[3] * m.m[i * 4 + 3];
	}
}

inline Mat4F32 Mul4x4By4x4(Mat4F32 a, Mat4F32 b) {
	Mat4F32 result;
	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			float sum = 0.0f;
			for (int k = 0; k < 4; k++) {
				sum += b.m[k * 4 + i] * a.m[j * 4 + k];
			}
			result.m[j * 4 + i] = sum;
		}
	}
	return result;
}

inline Mat4F32 Mul4x3By4x4(Mat4x3F32 a, Mat4F32 b) {
	Mat4F32 result;

	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			float sum = 0.0f;
			for (int k = 0; k < 3; k++) {
				sum += b.m[k * 4 + i] * a.m[j * 3 + k];
			}
			if (j == 3) {
				sum += b.m[12 + i];
			}
			result.m[j * 4 + i] = sum;
		}
	}
	return result;
}

#endif
