#pragma once

// SIMD headers
// Let's include these in one consistent way across the code base.
// Here we'll also add wrappers that paper over differences between different versions
// of an instruction set, like NEON vs ASIMD (64-bit).

#pragma once

#include "ppsspp_config.h"

#include "stdint.h"

#ifdef __clang__
// Weird how you can't just use #pragma in a macro.
#define DO_NOT_VECTORIZE_LOOP _Pragma("clang loop vectorize(disable)")
#else
#define DO_NOT_VECTORIZE_LOOP
#endif

#if PPSSPP_ARCH(SSE2)
#include <emmintrin.h>
#endif

#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

// Basic types

#if PPSSPP_ARCH(ARM64_NEON)

// No special ones here.

#elif PPSSPP_ARCH(ARM_NEON)

// Compatibility wrappers making ARM64 NEON code run on ARM32
// With optimization on, these should compile down to the optimal code.

static inline float32x4_t vmulq_laneq_f32(float32x4_t a, float32x4_t b, int lane) {
	switch (lane & 3) {
	case 0: return vmulq_lane_f32(a, vget_low_f32(b), 0);
	case 1: return vmulq_lane_f32(a, vget_low_f32(b), 1);
	case 2: return vmulq_lane_f32(a, vget_high_f32(b), 0);
	default: return vmulq_lane_f32(a, vget_high_f32(b), 1);
	}
}

static inline float32x4_t vmlaq_laneq_f32(float32x4_t a, float32x4_t b, float32x4_t c, int lane) {
	switch (lane & 3) {
	case 0: return vmlaq_lane_f32(a, b, vget_low_f32(c), 0);
	case 1: return vmlaq_lane_f32(a, b, vget_low_f32(c), 1);
	case 2: return vmlaq_lane_f32(a, b, vget_high_f32(c), 0);
	default: return vmlaq_lane_f32(a, b, vget_high_f32(c), 1);
	}
}

static inline uint32x4_t vcgezq_f32(float32x4_t v) {
	return vcgeq_f32(v, vdupq_n_f32(0.0f));
}

#endif
