#pragma once

// Some of the stuff in this file are snippets from all over the web, esp. dspmusic.org. I think it's all public domain.
// In any case, very little of it is used anywhere at the moment.

#include <cmath>
#include <cstring>

#include "base/basictypes.h"

inline float sqr(float f)	{return f*f;}
inline float sqr_signed(float f) {return f<0 ? -f*f : f*f;}

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

// Calculate pseudo-random 32 bit number based on linear congruential method. 
void SetSeed(unsigned int seed);
unsigned int GenerateRandomNumber();
inline float GenerateRandomFloat01() {
	return (float)((double)GenerateRandomNumber() / 0xFFFFFFFF);
}
inline float GenerateRandomSignedFloat() {
	return (float)((double)GenerateRandomNumber() / 0x80000000) - 1.0f;
}


inline float GaussRand()
{
	float R1 = GenerateRandomFloat01();
	float R2 = GenerateRandomFloat01();

	float X = sqrtf(-2.0f * logf(R1)) * cosf(2.0f * PI * R2);
	if (X > 4.0f) X = 4.0f;
	if (X < -4.0f) X = -4.0f;
	return X;
}

// Accuracy unknown
inline double atan_fast(double x) {
	return (x / (1.0 + 0.28 * (x * x)));
}

template<class T>
inline T clamp_value(T val, T floor, T cap) {
	if (val > cap)
		return cap;
	else if (val < floor)
		return floor;
	else
		return val;
}

// linear -> dB conversion
inline float lin2dB(float lin) {
	const float LOG_2_DB = 8.6858896380650365530225783783321f;	// 20 / ln( 10 )
	return logf(lin) * LOG_2_DB;
}

// dB -> linear conversion
inline float dB2lin(float dB) {
	const float DB_2_LOG = 0.11512925464970228420089957273422f;	// ln( 10 ) / 20
	return expf(dB * DB_2_LOG);
}

union FP32 {
	uint32_t u;
	float f;
};

struct FP16 {
	uint16_t u;
};

inline bool my_isinf(float f) {
	FP32 f2u;
	f2u.f = f;
	return f2u.u == 0x7f800000 ||
		f2u.u == 0xff800000;
}

inline bool my_isnan(float f) {
	FP32 f2u;
	f2u.f = f;
	// NaNs have non-zero mantissa
	return ((f2u.u & 0x7F800000) == 0x7F800000) && (f2u.u & 0x7FFFFF);
}

inline bool my_isnanorinf(float f) {
	FP32 f2u;
	f2u.f = f;
	// NaNs have non-zero mantissa, infs have zero mantissa. That is, we just ignore the mantissa here.
	return ((f2u.u & 0x7F800000) == 0x7F800000);
}

inline int is_even(float d) {
	float int_part;
	modff(d / 2.0f, &int_part);
	return 2.0f * int_part == d;
}

// Rounds *.5 to closest even number
inline double round_ieee_754(double d) {
	float i = floor(d);
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
inline FP32 half_to_float_fast5(FP16 h)
{
	static const FP32 magic = { (127 + (127 - 15)) << 23 };
	static const FP32 was_infnan = { (127 + 16) << 23 };
	FP32 o;
	o.u = (h.u & 0x7fff) << 13;     // exponent/mantissa bits
	o.f *= magic.f;                 // exponent adjust
	if (o.f >= was_infnan.f)        // make sure Inf/NaN survive (retain the low bits)
		o.u = (255 << 23) | (h.u & 0x03ff);
	o.u |= (h.u & 0x8000) << 16;    // sign bit
	return o;
}

inline float ExpandHalf(uint16_t half) {
	FP16 fp16;
	fp16.u = half;
	FP32 fp = half_to_float_fast5(fp16);
	return fp.f;
}

// More magic code: https://gist.github.com/rygorous/2156668
inline FP16 float_to_half_fast3(FP32 f)
{
	static const FP32 f32infty = { 255 << 23 };
	static const FP32 f16infty = { 31 << 23 };
	static const FP32 magic = { 15 << 23 };
	static const uint32_t sign_mask = 0x80000000u;
	static const uint32_t round_mask = ~0xfffu;
	FP16 o = { 0 };

	uint32_t sign = f.u & sign_mask;
	f.u ^= sign;

	if (f.u >= f32infty.u) // Inf or NaN (all exponent bits set)
		o.u = (f.u > f32infty.u) ? (0x7e00 | (f.u & 0x3ff)) : 0x7c00; // NaN->qNaN and Inf->Inf
	else // (De)normalized number or zero
	{
		f.u &= round_mask;
		f.f *= magic.f;
		f.u -= round_mask;
		if (f.u > f16infty.u) f.u = f16infty.u; // Clamp to signed infinity if overflowed

		o.u = f.u >> 13; // Take the bits!
	}

	o.u |= sign >> 16;
	return o;
}

inline uint16_t ShrinkToHalf(float full) {
	FP32 fp32;
	fp32.f = full;
	FP16 fp = float_to_half_fast3(fp32);
	return fp.u;
}

// FPU control.
void EnableFZ();

// Enable both FZ and Default-NaN. Is documented to flip some ARM implementation into a "run-fast" mode
// where they can schedule VFP instructions on the NEON unit (these implementations have
// very slow VFP units).
// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0274h/Babffifj.html
void FPU_SetFastMode();


