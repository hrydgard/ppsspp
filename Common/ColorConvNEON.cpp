// Copyright (c) 2015- PPSSPP Project.

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
#include "ColorConvNEON.h"
#include "Common.h"
#include "CPUDetect.h"

#if !defined(ARM) && !defined(ARM64)
#error Should not be compiled on non-ARM.
#endif

// TODO: More NEON color conversion funcs.

void ConvertRGBA4444ToABGR4444NEON(u16 *dst, const u16 *src, const u32 numPixels) {
	const uint16x8_t mask0040 = vdupq_n_u16(0x00F0);

	u32 simdable = (numPixels / 8) * 8;
	const u16 *srcp = src;
	u16 *dstp = dst;
	for (u32 i = 0; i < simdable; i += 8) {
		uint16x8_t c = vld1q_u16(srcp);

		const uint16x8_t a = vshrq_n_u16(c, 12);
		const uint16x8_t b = vandq_u16(vshrq_n_u16(c, 4), mask0040);
		const uint16x8_t g = vshlq_n_u16(vandq_u16(c, mask0040), 4);
		const uint16x8_t r = vshlq_n_u16(c, 12);

		uint16x8_t res = vorrq_u16(vorrq_u16(r, g), vorrq_u16(b, a));
		vst1q_u16(dstp, res);

		srcp += 8;
		dstp += 8;
	}

	// Finish off the rest, if there were any outside the simdable range.
	if (numPixels > simdable) {
		// Note that we've already moved srcp/dstp forward.
		ConvertRGBA4444ToABGR4444Basic(dstp, srcp, numPixels - simdable);
	}
}

void ConvertRGBA5551ToABGR1555NEON(u16 *dst, const u16 *src, const u32 numPixels) {
	const uint16x8_t maskB = vdupq_n_u16(0x003E);
	const uint16x8_t maskG = vdupq_n_u16(0x07C0);

	u32 simdable = (numPixels / 8) * 8;
	const u16 *srcp = src;
	u16 *dstp = dst;
	for (u32 i = 0; i < simdable; i += 8) {
		uint16x8_t c = vld1q_u16(srcp);

		const uint16x8_t a = vshrq_n_u16(c, 15);
		const uint16x8_t b = vandq_u16(vshrq_n_u16(c, 9), maskB);
		const uint16x8_t g = vandq_u16(vshlq_n_u16(c, 1), maskG);
		const uint16x8_t r = vshlq_n_u16(c, 11);

		uint16x8_t res = vorrq_u16(vorrq_u16(r, g), vorrq_u16(b, a));
		vst1q_u16(dstp, res);

		srcp += 8;
		dstp += 8;
	}

	// Finish off the rest, if there were any outside the simdable range.
	if (numPixels > simdable) {
		// Note that we've already moved srcp/dstp forward.
		ConvertRGBA5551ToABGR1555Basic(dstp, srcp, numPixels - simdable);
	}
}

void ConvertRGB565ToBGR565NEON(u16 *dst, const u16 *src, const u32 numPixels) {
	const uint16x8_t maskG = vdupq_n_u16(0x07E0);

	u32 simdable = (numPixels / 8) * 8;
	const u16 *srcp = src;
	u16 *dstp = dst;
	for (u32 i = 0; i < simdable; i += 8) {
		uint16x8_t c = vld1q_u16(srcp);

		const uint16x8_t b = vshrq_n_u16(c, 11);
		const uint16x8_t g = vandq_u16(c, maskG);
		const uint16x8_t r = vshlq_n_u16(c, 11);

		uint16x8_t res = vorrq_u16(vorrq_u16(r, g), b);
		vst1q_u16(dstp, res);

		srcp += 8;
		dstp += 8;
	}

	// Finish off the rest, if there were any outside the simdable range.
	if (numPixels > simdable) {
		// Note that we've already moved srcp/dstp forward.
		ConvertRGB565ToBGR565Basic(dstp, srcp, numPixels - simdable);
	}
}
