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

#ifndef ARM
#error Should not be compiled on non-ARM.
#endif

static const u16 MEMORY_ALIGNED16(QuickTexHashInitial[8]) = {0x0001U, 0x0083U, 0x4309U, 0x4d9bU, 0xb651U, 0x4b73U, 0x9bd9U, 0xc00bU};

u32 QuickTexHashNEON(const void *checkp, u32 size) {
	u32 check = 0;
	__builtin_prefetch(checkp, 0, 0);

	if (((intptr_t)checkp & 0xf) == 0 && (size & 0x3f) == 0) {
#ifdef IOS
		uint32x4_t cursor = vdupq_n_u32(0);
		uint32x4_t cursor2 = vld1q_u32((const u32 *)QuickTexHashInitial);
		uint32x4_t update = vdupq_n_u32(0x24552455U);

		const u32 *p = (const u32 *)checkp;
		for (u32 i = 0; i < size / 16; i += 4) {
			cursor = vmlaq_u32(cursor, vld1q_u32(&p[4 * 0]), cursor2);
			cursor = veorq_u32(cursor, vld1q_u32(&p[4 * 1]));
			cursor = vaddq_u32(cursor, vld1q_u32(&p[4 * 2]));
			cursor = veorq_u32(cursor, vmulq_u32(vld1q_u32(&p[4 * 3]), cursor2));
			cursor2 = vaddq_u32(cursor2, update);

			p += 4 * 4;
		}

		cursor = vaddq_u32(cursor, cursor2);
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
			"movw r0, 0x0001\n"
			"movt r0, 0x0083\n"
			"movw r1, 0x4309\n"
			"movt r1, 0x4d9b\n"
			"vmov d2, r0, r1\n"
			"movw r0, 0xb651\n"
			"movt r0, 0x4b73\n"
			"movw r1, 0x9bd9\n"
			"movt r1, 0xc00b\n"
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
			"vmla.i32 q0, q1, q8\n"
			"vmul.i32 q11, q11, q1\n"
			"veor.i32 q0, q0, q9\n"
			"cmp %2, r0\n"
			"vadd.i32 q0, q0, q10\n"
			"vadd.i32 q1, q1, q2\n"
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
