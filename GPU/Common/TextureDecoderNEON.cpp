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

#include <arm_neon.h>
#include "GPU/Common/TextureDecoder.h"

#if !defined(ARM) && !defined(ARM64)
#error Should not be compiled on non-ARM.
#endif

static const u16 MEMORY_ALIGNED16(QuickTexHashInitial[8]) = {0xc00bU, 0x9bd9U, 0x4b73U, 0xb651U, 0x4d9bU, 0x4309U, 0x0083U, 0x0001U};

u32 QuickTexHashNEON(const void *checkp, u32 size) {
	u32 check = 0;
	__builtin_prefetch(checkp, 0, 0);

	if (((intptr_t)checkp & 0xf) == 0 && (size & 0x3f) == 0) {
#if defined(IOS) || defined(ARM64)
		uint32x4_t cursor = vdupq_n_u32(0);
		uint16x8_t cursor2 = vld1q_u16(QuickTexHashInitial);
		uint16x8_t update = vdupq_n_u16(0x2455U);

		const u32 *p = (const u32 *)checkp;
		for (u32 i = 0; i < size / 16; i += 4) {
			cursor = vreinterpretq_u32_u16(vmlaq_u16(vreinterpretq_u16_u32(cursor), vreinterpretq_u16_u32(vld1q_u32(&p[4 * 0])), cursor2));
			cursor = veorq_u32(cursor, vld1q_u32(&p[4 * 1]));
			cursor = vaddq_u32(cursor, vld1q_u32(&p[4 * 2]));
			cursor = veorq_u32(cursor, vreinterpretq_u32_u16(vmulq_u16(vreinterpretq_u16_u32(vld1q_u32(&p[4 * 3])), cursor2)));
			cursor2 = vaddq_u16(cursor2, update);

			p += 4 * 4;
		}

		cursor = vaddq_u32(cursor, vreinterpretq_u32_u16(cursor2));
		check = vgetq_lane_u32(cursor, 0) + vgetq_lane_u32(cursor, 1) + vgetq_lane_u32(cursor, 2) + vgetq_lane_u32(cursor, 3);
#else
		// TODO: Why does this crash on iOS, but only certain devices?
		// It's faster than the above, but I guess it sucks to be using an iPhone.

		// d0/d1 (q0) - cursor
		// d2/d3 (q1) - cursor2
		// d4/d5 (q2) - update
		// d16-d23 (q8-q11) - memory transfer
		asm volatile (
			// Initialize cursor.
			"vmov.i32 q0, #0\n"

			// Initialize cursor2.
			"movw r0, 0xc00b\n"
			"movt r0, 0x9bd9\n"
			"movw r1, 0x4b73\n"
			"movt r1, 0xb651\n"
			"vmov d2, r0, r1\n"
			"movw r0, 0x4d9b\n"
			"movt r0, 0x4309\n"
			"movw r1, 0x0083\n"
			"movt r1, 0x0001\n"
			"vmov d3, r0, r1\n"

			// Initialize update.
			"movw r0, 0x2455\n"
			"vdup.i16 q2, r0\n"

			// This is where we end.
			"add r0, %1, %2\n"

			// Okay, do the memory hashing.
			"QuickTexHashNEON_next:\n"
			"pld [%2, #0xc0]\n"
			"vldmia %2!, {d16-d23}\n"
			"vmla.i16 q0, q1, q8\n"
			"vmul.i16 q11, q11, q1\n"
			"veor.i32 q0, q0, q9\n"
			"cmp %2, r0\n"
			"vadd.i32 q0, q0, q10\n"
			"vadd.i16 q1, q1, q2\n"
			"veor.i32 q0, q0, q11\n"
			"blo QuickTexHashNEON_next\n"

			// Now let's get the result.
			"vadd.i32 q0, q0, q1\n"
			"vadd.i32 d0, d0, d1\n"
			"vmov r0, r1, d0\n"
			"add %0, r0, r1\n"

			: "=r"(check)
			: "r"(size), "r"(checkp)
			: "r0", "r1", "d0", "d1", "d2", "d3", "d4", "d5", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "cc"
		);
#endif
	} else {
		const u32 size_u32 = size / 4;
		const u32 *p = (const u32 *)checkp;
		for (u32 i = 0; i < size_u32; i += 4) {
			check += p[i + 0];
			check ^= p[i + 1];
			check += p[i + 2];
			check ^= p[i + 3];
		}
	}

	return check;
}

void DoUnswizzleTex16NEON(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch, u32 rowWidth) {
	__builtin_prefetch(texptr, 0, 0);
	__builtin_prefetch(ydestp, 1, 1);

	const u32 *src = (const u32 *)texptr;
	for (int by = 0; by < byc; by++) {
		u32 *xdest = ydestp;
		for (int bx = 0; bx < bxc; bx++) {
			u32 *dest = xdest;
			for (int n = 0; n < 2; n++) {
				// Textures are always 16-byte aligned so this is fine.
				uint32x4_t temp1 = vld1q_u32(src);
				uint32x4_t temp2 = vld1q_u32(src + 4);
				uint32x4_t temp3 = vld1q_u32(src + 8);
				uint32x4_t temp4 = vld1q_u32(src + 12);
				vst1q_u32(dest, temp1);
				dest += pitch;
				vst1q_u32(dest, temp2);
				dest += pitch;
				vst1q_u32(dest, temp3);
				dest += pitch;
				vst1q_u32(dest, temp4);
				dest += pitch;
				src += 16;
			}
			xdest += 4;
		}
		ydestp += (rowWidth * 8) / 4;
	}
}

// NOTE: This is just a NEON version of xxhash.
// GCC sucks at making things NEON and can't seem to handle it.

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   // C99
# include <stdint.h>
  typedef uint8_t  BYTE;
  typedef uint16_t U16;
  typedef uint32_t U32;
  typedef  int32_t S32;
  typedef uint64_t U64;
#else
  typedef unsigned char      BYTE;
  typedef unsigned short     U16;
  typedef unsigned int       U32;
  typedef   signed int       S32;
  typedef unsigned long long U64;
#endif

#define PRIME32_1   2654435761U
#define PRIME32_2   2246822519U
#define PRIME32_3   3266489917U
#define PRIME32_4    668265263U
#define PRIME32_5    374761393U

#if defined(_MSC_VER)
#  define XXH_rotl32(x,r) _rotl(x,r)
#else
#  define XXH_rotl32(x,r) ((x << r) | (x >> (32 - r)))
#endif

u32 ReliableHash32NEON(const void *input, size_t len, u32 seed) {
	const u8 *p = (const u8 *)input;
	const u8 *const bEnd = p + len;
	U32 h32;

#ifdef XXH_ACCEPT_NULL_INPUT_POINTER
	if (p==NULL) { len=0; p=(const BYTE*)(size_t)16; }
#endif

	if (len>=16)
	{
		const BYTE* const limit = bEnd - 16;
		U32 v1 = seed + PRIME32_1 + PRIME32_2;
		U32 v2 = seed + PRIME32_2;
		U32 v3 = seed + 0;
		U32 v4 = seed - PRIME32_1;

		uint32x4_t prime32_1q = vdupq_n_u32(PRIME32_1);
		uint32x4_t prime32_2q = vdupq_n_u32(PRIME32_2);
		uint32x4_t vq = vcombine_u32(vcreate_u32(v1 | ((U64)v2 << 32)), vcreate_u32(v3 | ((U64)v4 << 32)));

		do
		{
			__builtin_prefetch(p + 0xc0, 0, 0);
			vq = vmlaq_u32(vq, vld1q_u32((const U32*)p), prime32_2q);
			vq = vorrq_u32(vshlq_n_u32(vq, 13), vshrq_n_u32(vq, 32 - 13));
			p += 16;
			vq = vmulq_u32(vq, prime32_1q);
		} while (p<=limit);

		v1 = vgetq_lane_u32(vq, 0);
		v2 = vgetq_lane_u32(vq, 1);
		v3 = vgetq_lane_u32(vq, 2);
		v4 = vgetq_lane_u32(vq, 3);

		h32 = XXH_rotl32(v1, 1) + XXH_rotl32(v2, 7) + XXH_rotl32(v3, 12) + XXH_rotl32(v4, 18);
	}
	else
	{
		h32  = seed + PRIME32_5;
	}

	h32 += (U32) len;

	while (p<=bEnd-4)
	{
		h32 += *(const U32*)p * PRIME32_3;
		h32  = XXH_rotl32(h32, 17) * PRIME32_4 ;
		p+=4;
	}

	while (p<bEnd)
	{
		h32 += (*p) * PRIME32_5;
		h32 = XXH_rotl32(h32, 11) * PRIME32_1 ;
		p++;
	}

	h32 ^= h32 >> 15;
	h32 *= PRIME32_2;
	h32 ^= h32 >> 13;
	h32 *= PRIME32_3;
	h32 ^= h32 >> 16;

	return h32;
}

static inline bool VectorIsNonZeroNEON(const uint32x4_t &v) {
	u64 low = vgetq_lane_u64(vreinterpretq_u64_u32(v), 0);
	u64 high = vgetq_lane_u64(vreinterpretq_u64_u32(v), 1);

	return (low | high) != 0;
}

static inline bool VectorIsNonZeroNEON(const uint16x8_t &v) {
	u64 low = vgetq_lane_u64(vreinterpretq_u64_u16(v), 0);
	u64 high = vgetq_lane_u64(vreinterpretq_u64_u16(v), 1);

	return (low | high) != 0;
}

CheckAlphaResult CheckAlphaRGBA8888NEON(const u32 *pixelData, int stride, int w, int h) {
	const uint32x4_t zero = vdupq_n_u32(0);
	const uint32x4_t full = vdupq_n_u32(0xFF);

	const u32 *p = (const u32 *)pixelData;

	// Have alpha values == 0 been seen?
	uint32x4_t foundAZero = zero;

	for (int y = 0; y < h; ++y) {
		// Have alpha values > 0 and < 0xFF been seen?
		uint32x4_t foundFraction = zero;

		for (int i = 0; i < w; i += 4) {
			const uint32x4_t a = vshrq_n_u32(vld1q_u32(&p[i]), 24);

			const uint32x4_t isZero = vceqq_u32(a, zero);
			foundAZero = vorrq_u32(foundAZero, isZero);

			// If a = FF, isNotFull will be 0 -> foundFraction will be 0.
			// If a = 00, a & isNotFull will be 0 -> foundFraction will be 0.
			// In any other case, foundFraction will have some bits set.
			const uint32x4_t isNotFull = vcltq_u32(a, full);
			foundFraction = vorrq_u32(foundFraction, vandq_u32(a, isNotFull));
		}
		p += stride;

		// We check any early, in case we can skip the rest of the rows.
		if (VectorIsNonZeroNEON(foundFraction)) {
			return CHECKALPHA_ANY;
		}
	}

	// Now let's sum up the bits.
	if (VectorIsNonZeroNEON(foundAZero)) {
		return CHECKALPHA_ZERO;
	} else {
		return CHECKALPHA_FULL;
	}
}

CheckAlphaResult CheckAlphaABGR4444NEON(const u32 *pixelData, int stride, int w, int h) {
	const uint16x8_t zero = vdupq_n_u16(0);
	const uint16x8_t full = vdupq_n_u16(0xF000);

	const u16 *p = (const u16 *)pixelData;

	// Have alpha values == 0 been seen?
	uint16x8_t foundAZero = zero;

	for (int y = 0; y < h; ++y) {
		// Have alpha values > 0 and < 0xFF been seen?
		uint16x8_t foundFraction = zero;

		for (int i = 0; i < w; i += 8) {
			const uint16x8_t a = vshlq_n_u16(vld1q_u16(&p[i]), 12);

			const uint16x8_t isZero = vceqq_u16(a, zero);
			foundAZero = vorrq_u16(foundAZero, isZero);

			// If a = F, isNotFull will be 0 -> foundFraction will be 0.
			// If a = 0, a & isNotFull will be 0 -> foundFraction will be 0.
			// In any other case, foundFraction will have some bits set.
			const uint16x8_t isNotFull = vcltq_u16(a, full);
			foundFraction = vorrq_u16(foundFraction, vandq_u16(a, isNotFull));
		}
		p += stride;

		// We check any early, in case we can skip the rest of the rows.
		if (VectorIsNonZeroNEON(foundFraction)) {
			return CHECKALPHA_ANY;
		}
	}

	// Now let's sum up the bits.
	if (VectorIsNonZeroNEON(foundAZero)) {
		return CHECKALPHA_ZERO;
	} else {
		return CHECKALPHA_FULL;
	}
}

CheckAlphaResult CheckAlphaABGR1555NEON(const u32 *pixelData, int stride, int w, int h) {
	const u16 *p = (const u16 *)pixelData;

	const uint16x8_t mask = vdupq_n_u16(1);
	uint16x8_t bits = vdupq_n_u16(1);
	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w; i += 8) {
			const uint16x8_t a = vld1q_u16(&p[i]);

			bits = vandq_u16(bits, a);
		}

		uint16x8_t result = veorq_u16(bits, mask);
		if (VectorIsNonZeroNEON(result)) {
			return CHECKALPHA_ZERO;
		}

		p += stride;
	}

	return CHECKALPHA_FULL;
}
