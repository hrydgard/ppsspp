#include <cmath>

#include "Common/BitScan.h"

#include "Core/MIPS/MIPSVFPUFallbacks.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

// MIPSVFPUUtils now has the high precision instructions implemented by fp64
// in https://github.com/hrydgard/ppsspp/pull/16984 .
//
// These are our older approximations that are quite good but has flaws,
// but we need them to fall back to if the table files are missing.
//
// Note that currently, some of the new functions are not integrated in the JIT
// and are thus not normally used anyway.

// First the "trivial" fallbacks where we haven't done any accuracy work previously.

float vfpu_asin_fallback(float angle) {
	return (float)(asinf(angle) / M_PI_2);
}

float vfpu_rcp_fallback(float x) {
	return 1.0f / x;
}

float vfpu_log2_fallback(float x) {
	return log2f(x);
}

float vfpu_exp2_fallback(float x) {
	return exp2f(x);
}

// Flushes the angle to 0 if exponent smaller than this in vfpu_sin/vfpu_cos/vfpu_sincos.
// Was measured to be around 0x68, but GTA on Mac is somehow super sensitive
// to the shape of the sine curve which seem to be very slightly different.
//
// So setting a lower value.
#define PRECISION_EXP_THRESHOLD 0x65

union float2int {
	uint32_t i;
	float f;
};

float vfpu_sqrt_fallback(float a) {
	float2int val;
	val.f = a;

	if ((val.i & 0xff800000) == 0x7f800000) {
		if ((val.i & 0x007fffff) != 0) {
			val.i = 0x7f800001;
		}
		return val.f;
	}
	if ((val.i & 0x7f800000) == 0) {
		// Kill any sign.
		val.i = 0;
		return val.f;
	}
	if (val.i & 0x80000000) {
		val.i = 0x7f800001;
		return val.f;
	}

	int k = get_exp(val.i);
	uint32_t sp = get_mant(val.i);
	int less_bits = k & 1;
	k >>= 1;

	uint32_t z = 0x00C00000 >> less_bits;
	int64_t halfsp = sp >> 1;
	halfsp <<= 23 - less_bits;
	for (int i = 0; i < 6; ++i) {
		z = (z >> 1) + (uint32_t)(halfsp / z);
	}

	val.i = ((k + 127) << 23) | ((z << less_bits) & 0x007FFFFF);
	// The lower two bits never end up set on the PSP, it seems like.
	val.i &= 0xFFFFFFFC;

	return val.f;
}

static inline uint32_t mant_mul(uint32_t a, uint32_t b) {
	uint64_t m = (uint64_t)a * (uint64_t)b;
	if (m & 0x007FFFFF) {
		m += 0x01437000;
	}
	return (uint32_t)(m >> 23);
}

float vfpu_rsqrt_fallback(float a) {
	float2int val;
	val.f = a;

	if (val.i == 0x7f800000) {
		return 0.0f;
	}
	if ((val.i & 0x7fffffff) > 0x7f800000) {
		val.i = (val.i & 0x80000000) | 0x7f800001;
		return val.f;
	}
	if ((val.i & 0x7f800000) == 0) {
		val.i = (val.i & 0x80000000) | 0x7f800000;
		return val.f;
	}
	if (val.i & 0x80000000) {
		val.i = 0xff800001;
		return val.f;
	}

	int k = get_exp(val.i);
	uint32_t sp = get_mant(val.i);
	int less_bits = k & 1;
	k = -(k >> 1);

	uint32_t z = 0x00800000 >> less_bits;
	uint32_t halfsp = sp >> (1 + less_bits);
	for (int i = 0; i < 6; ++i) {
		uint32_t zsq = mant_mul(z, z);
		uint32_t correction = 0x00C00000 - mant_mul(halfsp, zsq);
		z = mant_mul(z, correction);
	}

	int8_t shift = (int8_t)clz32_nonzero(z) - 8 + less_bits;
	if (shift < 1) {
		z >>= -shift;
		k += -shift;
	} else if (shift > 0) {
		z <<= shift;
		k -= shift;
	}

	z >>= less_bits;

	val.i = ((k + 127) << 23) | (z & 0x007FFFFF);
	val.i &= 0xFFFFFFFC;

	return val.f;
}

float vfpu_sin_fallback(float a) {
	float2int val;
	val.f = a;

	int32_t k = get_uexp(val.i);
	if (k == 255) {
		val.i = (val.i & 0xFF800001) | 1;
		return val.f;
	}

	if (k < PRECISION_EXP_THRESHOLD) {
		val.i &= 0x80000000;
		return val.f;
	}

	// Okay, now modulus by 4 to begin with (identical wave every 4.)
	int32_t mantissa = get_mant(val.i);
	if (k > 0x80) {
		const uint8_t over = k & 0x1F;
		mantissa = (mantissa << over) & 0x00FFFFFF;
		k = 0x80;
	}
	// This subtracts off the 2.  If we do, flip sign to inverse the wave.
	if (k == 0x80 && mantissa >= (1 << 23)) {
		val.i ^= 0x80000000;
		mantissa -= 1 << 23;
	}

	int8_t norm_shift = mantissa == 0 ? 32 : (int8_t)clz32_nonzero(mantissa) - 8;
	mantissa <<= norm_shift;
	k -= norm_shift;

	if (k <= 0 || mantissa == 0) {
		val.i &= 0x80000000;
		return val.f;
	}

	// This is the value with modulus applied.
	val.i = (val.i & 0x80000000) | (k << 23) | (mantissa & ~(1 << 23));
	val.f = (float)sin((double)val.f * M_PI_2);
	val.i &= 0xFFFFFFFC;
	return val.f;
}

float vfpu_cos_fallback(float a) {
	float2int val;
	val.f = a;
	bool negate = false;

	int32_t k = get_uexp(val.i);
	if (k == 255) {
		// Note: unlike sin, cos always returns +NAN.
		val.i = (val.i & 0x7F800001) | 1;
		return val.f;
	}

	if (k < PRECISION_EXP_THRESHOLD)
		return 1.0f;

	// Okay, now modulus by 4 to begin with (identical wave every 4.)
	int32_t mantissa = get_mant(val.i);
	if (k > 0x80) {
		const uint8_t over = k & 0x1F;
		mantissa = (mantissa << over) & 0x00FFFFFF;
		k = 0x80;
	}
	// This subtracts off the 2.  If we do, negate the result value.
	if (k == 0x80 && mantissa >= (1 << 23)) {
		mantissa -= 1 << 23;
		negate = true;
	}

	int8_t norm_shift = mantissa == 0 ? 32 : (int8_t)clz32_nonzero(mantissa) - 8;
	mantissa <<= norm_shift;
	k -= norm_shift;

	if (k <= 0 || mantissa == 0)
		return negate ? -1.0f : 1.0f;

	// This is the value with modulus applied.
	val.i = (val.i & 0x80000000) | (k << 23) | (mantissa & ~(1 << 23));
	if (val.f == 1.0f || val.f == -1.0f) {
		return negate ? 0.0f : -0.0f;
	}
	val.f = (float)cos((double)val.f * M_PI_2);
	val.i &= 0xFFFFFFFC;
	return negate ? -val.f : val.f;
}

void vfpu_sincos_fallback(float a, float &s, float &c) {
	float2int val;
	val.f = a;
	// For sin, negate the input, for cos negate the output.
	bool negate = false;

	int32_t k = get_uexp(val.i);
	if (k == 255) {
		val.i = (val.i & 0xFF800001) | 1;
		s = val.f;
		val.i &= 0x7F800001;
		c = val.f;
		return;
	}

	if (k < PRECISION_EXP_THRESHOLD) {
		val.i &= 0x80000000;
		s = val.f;
		c = 1.0f;
		return;
	}

	// Okay, now modulus by 4 to begin with (identical wave every 4.)
	int32_t mantissa = get_mant(val.i);
	if (k > 0x80) {
		const uint8_t over = k & 0x1F;
		mantissa = (mantissa << over) & 0x00FFFFFF;
		k = 0x80;
	}
	// This subtracts off the 2.  If we do, flip signs.
	if (k == 0x80 && mantissa >= (1 << 23)) {
		mantissa -= 1 << 23;
		negate = true;
	}

	int8_t norm_shift = mantissa == 0 ? 32 : (int8_t)clz32_nonzero(mantissa) - 8;
	mantissa <<= norm_shift;
	k -= norm_shift;

	if (k <= 0 || mantissa == 0) {
		val.i &= 0x80000000;
		if (negate)
			val.i ^= 0x80000000;
		s = val.f;
		c = negate ? -1.0f : 1.0f;
		return;
	}

	// This is the value with modulus applied.
	val.i = (val.i & 0x80000000) | (k << 23) | (mantissa & ~(1 << 23));
	float2int i_sine, i_cosine;
	if (val.f == 1.0f) {
		i_sine.f = negate ? -1.0f : 1.0f;
		i_cosine.f = negate ? 0.0f : -0.0f;
	} else if (val.f == -1.0f) {
		i_sine.f = negate ? 1.0f : -1.0f;
		i_cosine.f = negate ? 0.0f : -0.0f;
	} else if (negate) {
		i_sine.f = (float)sin((double)-val.f * M_PI_2);
		i_cosine.f = -(float)cos((double)val.f * M_PI_2);
	} else {
		double angle = (double)val.f * M_PI_2;
#if defined(__linux__)
		double d_sine;
		double d_cosine;
		sincos(angle, &d_sine, &d_cosine);
		i_sine.f = (float)d_sine;
		i_cosine.f = (float)d_cosine;
#else
		i_sine.f = (float)sin(angle);
		i_cosine.f = (float)cos(angle);
#endif
	}

	i_sine.i &= 0xFFFFFFFC;
	i_cosine.i &= 0xFFFFFFFC;
	s = i_sine.f;
	c = i_cosine.f;
	return;
}
