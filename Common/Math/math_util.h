#pragma once

// Some of the stuff in this file are snippets from all over the web, esp. dspmusic.org. I think it's all public domain.
// In any case, very little of it is used anywhere at the moment.

#include <cmath>
#include <cstring>
#include <cstdint>

typedef unsigned short float16;

// This ain't a 1.5.10 float16, it's a stupid hack format where we chop 16 bits off a float.
// This choice is subject to change. Don't think I'm using this for anything at all now anyway.
// DEPRECATED
inline float16 FloatToFloat16(float x) {
	int ix;
	memcpy(&ix, &x, sizeof(float));
	return ix >> 16;
}

inline float Float16ToFloat(float16 ix) {
	float x;
	memcpy(&x, &ix, sizeof(float));
	return x;
}

inline bool isPowerOf2(int n) {
	return n == 1 || (n & (n - 1)) == 0;
}

// Next power of 2.
inline uint32_t RoundUpToPowerOf2(uint32_t v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

inline uint32_t log2i(uint32_t val) {
	unsigned int ret = -1;
	while (val != 0) {
		val >>= 1; ret++;
	}
	return ret;
}

#define PI 3.141592653589793f
#ifndef M_PI
#define M_PI 3.141592653589793f
#endif

template<class T>
inline T clamp_value(T val, T floor, T cap) {
	if (val > cap)
		return cap;
	else if (val < floor)
		return floor;
	else
		return val;
}

#define ROUND_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define ROUND_DOWN(x, a) ((x) & ~((a) - 1))

template<class T>
inline void Clamp(T* val, const T& min, const T& max)
{
	if (*val < min)
		*val = min;
	else if (*val > max)
		*val = max;
}

template<class T>
inline T Clamp(const T val, const T& min, const T& max)
{
	T ret = val;
	Clamp(&ret, min, max);
	return ret;
}

struct FP16 {
	uint16_t u;
};

inline bool my_isinf(float f) {
	uint32_t u;
	memcpy(&u, &f, sizeof(uint32_t));
	return u == 0x7f800000 || u == 0xff800000;
}

inline bool my_isnan(float f) {
	uint32_t u;
	memcpy(&u, &f, sizeof(uint32_t));
	// NaNs have non-zero mantissa
	return ((u & 0x7F800000) == 0x7F800000) && (u & 0x7FFFFF);
}

inline bool my_isnanorinf(float f) {
	uint32_t u;
	memcpy(&u, &f, sizeof(uint32_t));
	// NaNs have non-zero mantissa, infs have zero mantissa. That is, we just ignore the mantissa here.
	return (u & 0x7F800000) == 0x7F800000;
}

inline int is_even(float d) {
	float int_part;
	modff(d / 2.0f, &int_part);
	return 2.0f * int_part == d;
}

// Rounds *.5 to closest even number
inline double round_ieee_754(double d) {
	float i = (float)floor(d);
	d -= i;
	if (d < 0.5f)
		return i;
	if (d > 0.5f)
		return i + 1.0f;
	if (is_even(i))
		return i;
	return i + 1.0f;
}

// magic code from ryg: http://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
// See also SSE2 version: https://gist.github.com/rygorous/2144712
inline float half_to_float_fast5(FP16 h) {
	// Should be safe for the constants.
	union FP32 {
		uint32_t u;
		float f;
	};
	static const FP32 magic = { (127 + (127 - 15)) << 23 };
	static const FP32 was_infnan = { (127 + 16) << 23 };

	uint32_t ou;
	float of;
	ou = (h.u & 0x7fff) << 13;     // exponent/mantissa bits
	memcpy(&of, &ou, sizeof(float));
	of *= magic.f;                 // exponent adjust
	memcpy(&ou, &of, sizeof(uint32_t));
	if (of >= was_infnan.f)        // make sure Inf/NaN survive (retain the low bits)
		ou = (255 << 23) | (h.u & 0x03ff);
	ou |= (h.u & 0x8000) << 16;    // sign bit
	memcpy(&of, &ou, sizeof(float));
	return of;
}

inline float ExpandHalf(uint16_t half) {
	FP16 fp16;
	fp16.u = half;
	return half_to_float_fast5(fp16);
}

// More magic code: https://gist.github.com/rygorous/2156668
inline FP16 float_to_half_fast3(float f) {
	// Should be safe for the constants.
	union FP32 {
		uint32_t u;
		float f;
	};
	static const FP32 f32infty = { 255 << 23 };
	static const FP32 f16infty = { 31 << 23 };
	static const FP32 magic = { 15 << 23 };
	static const uint32_t sign_mask = 0x80000000u;
	static const uint32_t round_mask = ~0xfffu;
	FP16 o = { 0 };

	uint32_t fu;
	memcpy(&fu, &f, sizeof(uint32_t));

	uint32_t sign = fu & sign_mask;
	fu ^= sign;

	if (fu >= f32infty.u) // Inf or NaN (all exponent bits set)
		o.u = (fu > f32infty.u) ? (0x7e00 | (fu & 0x3ff)) : 0x7c00; // NaN->qNaN and Inf->Inf
	else // (De)normalized number or zero
	{
		fu &= round_mask;
		memcpy(&f, &fu, sizeof(float));
		f *= magic.f;
		memcpy(&fu, &f, sizeof(uint32_t));
		fu -= round_mask;
		if (fu > f16infty.u)
			fu = f16infty.u; // Clamp to signed infinity if overflowed

		o.u = fu >> 13; // Take the bits!
	}

	o.u |= sign >> 16;
	return o;
}

inline uint16_t ShrinkToHalf(float full) {
	FP16 fp = float_to_half_fast3(full);
	return fp.u;
}

// FPU control.
void EnableFZ();

// Enable both FZ and Default-NaN. Is documented to flip some ARM implementation into a "run-fast" mode
// where they can schedule VFP instructions on the NEON unit (these implementations have
// very slow VFP units).
// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0274h/Babffifj.html
void FPU_SetFastMode();
