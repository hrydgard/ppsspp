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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM_NEON)

#include <arm_neon.h>
#include "ColorConvNEON.h"
#include "Common.h"
#include "CPUDetect.h"

// TODO: More NEON color conversion funcs.

void ConvertRGBA4444ToABGR4444NEON(u16 *dst, const u16 *src, u32 numPixels) {
	const uint16x8_t mask0040 = vdupq_n_u16(0x00F0);

	if (((uintptr_t)dst & 15) == 0 && ((uintptr_t)src & 15) == 0) {
		u32 simdable = (numPixels / 8) * 8;
		for (u32 i = 0; i < simdable; i += 8) {
			uint16x8_t c = vld1q_u16(src);

			const uint16x8_t a = vshrq_n_u16(c, 12);
			const uint16x8_t b = vandq_u16(vshrq_n_u16(c, 4), mask0040);
			const uint16x8_t g = vshlq_n_u16(vandq_u16(c, mask0040), 4);
			const uint16x8_t r = vshlq_n_u16(c, 12);

			uint16x8_t res = vorrq_u16(vorrq_u16(r, g), vorrq_u16(b, a));
			vst1q_u16(dst, res);

			src += 8;
			dst += 8;
		}
		numPixels -= simdable;
	}

	// Finish off the rest, if there were any outside the simdable range.
	if (numPixels > 0) {
		// Note that we've already moved src/dst forward.
		ConvertRGBA4444ToABGR4444Basic(dst, src, numPixels);
	}
}

void ConvertRGBA5551ToABGR1555NEON(u16 *dst, const u16 *src, u32 numPixels) {
	const uint16x8_t maskB = vdupq_n_u16(0x003E);
	const uint16x8_t maskG = vdupq_n_u16(0x07C0);

	if (((uintptr_t)dst & 15) == 0 && ((uintptr_t)src & 15) == 0) {
		u32 simdable = (numPixels / 8) * 8;
		for (u32 i = 0; i < simdable; i += 8) {
			uint16x8_t c = vld1q_u16(src);

			const uint16x8_t a = vshrq_n_u16(c, 15);
			const uint16x8_t b = vandq_u16(vshrq_n_u16(c, 9), maskB);
			const uint16x8_t g = vandq_u16(vshlq_n_u16(c, 1), maskG);
			const uint16x8_t r = vshlq_n_u16(c, 11);

			uint16x8_t res = vorrq_u16(vorrq_u16(r, g), vorrq_u16(b, a));
			vst1q_u16(dst, res);

			src += 8;
			dst += 8;
		}
		numPixels -= simdable;
	}

	// Finish off the rest, if there were any outside the simdable range.
	if (numPixels > 0) {
		// Note that we've already moved src/dst forward.
		ConvertRGBA5551ToABGR1555Basic(dst, src, numPixels);
	}
}

void ConvertRGB565ToBGR565NEON(u16 *dst, const u16 *src, u32 numPixels) {
	const uint16x8_t maskG = vdupq_n_u16(0x07E0);

	if (((uintptr_t)dst & 15) == 0 && ((uintptr_t)src & 15) == 0) {
		u32 simdable = (numPixels / 8) * 8;
		for (u32 i = 0; i < simdable; i += 8) {
			uint16x8_t c = vld1q_u16(src);

			const uint16x8_t b = vshrq_n_u16(c, 11);
			const uint16x8_t g = vandq_u16(c, maskG);
			const uint16x8_t r = vshlq_n_u16(c, 11);

			uint16x8_t res = vorrq_u16(vorrq_u16(r, g), b);
			vst1q_u16(dst, res);

			src += 8;
			dst += 8;
		}
		numPixels -= simdable;
	}
	// Finish off the rest, if there were any outside the simdable range.
	if (numPixels > 0) {
		// Note that we've already moved src/dst forward.
		ConvertRGB565ToBGR565Basic(dst, src, numPixels);
	}
}

#endif // PPSSPP_ARCH(ARM_NEON)
