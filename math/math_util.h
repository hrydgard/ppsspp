#ifndef _MATHUTILS_H
#define _MATHUTILS_H

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


#define PI 3.141592653589793f
#ifndef M_PI
#define M_PI 3.141592653589793f
#endif

// The stuff in this file is from all over the web, esp. dspmusic.org. I think it's all public domain.
// In any case, very little of it is used anywhere at the moment.

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

inline bool my_isinf(float f) {
	union {
		float f;
		uint32_t u;
	} f2u;
	f2u.f = f;
	return f2u.u == 0x7f800000 ||
		f2u.u == 0xff800000;
}

inline bool my_isnan(float f) {
	union {
		float f;
		uint32_t u;
	} f2u;
	f2u.f = f;
	// NaNs have non-zero mantissa
	return ((f2u.u & 0x7F800000) == 0x7F800000) && (f2u.u & 0x7FFFFF);
}

// FPU control.
void EnableFZ();
void DisableFZ();

#endif
