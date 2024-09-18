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

#include "ppsspp_config.h"

#include "ext/xxhash.h"

#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Common/Math/CrossSIMD.h"

#include "GPU/GPU.h"
#include "GPU/GPUState.h"
#include "GPU/Common/TextureDecoder.h"

#ifdef _M_SSE
#include <emmintrin.h>
#include <smmintrin.h>
#endif

#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

const u8 textureBitsPerPixel[16] = {
	16,  //GE_TFMT_5650,
	16,  //GE_TFMT_5551,
	16,  //GE_TFMT_4444,
	32,  //GE_TFMT_8888,
	4,   //GE_TFMT_CLUT4,
	8,   //GE_TFMT_CLUT8,
	16,  //GE_TFMT_CLUT16,
	32,  //GE_TFMT_CLUT32,
	4,   //GE_TFMT_DXT1,
	8,   //GE_TFMT_DXT3,
	8,   //GE_TFMT_DXT5,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
};

#ifdef _M_SSE

static u32 QuickTexHashSSE2(const void *checkp, u32 size) {
	u32 check = 0;

	if (((intptr_t)checkp & 0xf) == 0 && (size & 0x3f) == 0) {
		__m128i cursor = _mm_set1_epi32(0);
		__m128i cursor2 = _mm_set_epi16(0x0001U, 0x0083U, 0x4309U, 0x4d9bU, 0xb651U, 0x4b73U, 0x9bd9U, 0xc00bU);
		__m128i update = _mm_set1_epi16(0x2455U);
		const __m128i *p = (const __m128i *)checkp;
		for (u32 i = 0; i < size / 16; i += 4) {
			__m128i chunk = _mm_mullo_epi16(_mm_load_si128(&p[i]), cursor2);
			cursor = _mm_add_epi16(cursor, chunk);
			cursor = _mm_xor_si128(cursor, _mm_load_si128(&p[i + 1]));
			cursor = _mm_add_epi32(cursor, _mm_load_si128(&p[i + 2]));
			chunk = _mm_mullo_epi16(_mm_load_si128(&p[i + 3]), cursor2);
			cursor = _mm_xor_si128(cursor, chunk);
			cursor2 = _mm_add_epi16(cursor2, update);
		}
		cursor = _mm_add_epi32(cursor, cursor2);
		// Add the four parts into the low i32.
		cursor = _mm_add_epi32(cursor, _mm_srli_si128(cursor, 8));
		cursor = _mm_add_epi32(cursor, _mm_srli_si128(cursor, 4));
		check = _mm_cvtsi128_si32(cursor);
	} else {
		const u32 *p = (const u32 *)checkp;
		for (u32 i = 0; i < size / 8; ++i) {
			check += *p++;
			check ^= *p++;
		}
	}

	return check;
}
#endif

#if PPSSPP_ARCH(ARM_NEON)

alignas(16) static const u16 QuickTexHashInitial[8] = { 0xc00bU, 0x9bd9U, 0x4b73U, 0xb651U, 0x4d9bU, 0x4309U, 0x0083U, 0x0001U };

static u32 QuickTexHashNEON(const void *checkp, u32 size) {
	u32 check = 0;

	if (((intptr_t)checkp & 0xf) == 0 && (size & 0x3f) == 0) {
#if PPSSPP_PLATFORM(IOS) || PPSSPP_ARCH(ARM64) || defined(_MSC_VER) || !PPSSPP_ARCH(ARMV7)
		uint32x4_t cursor = vdupq_n_u32(0);
		uint16x8_t cursor2 = vld1q_u16(QuickTexHashInitial);
		uint16x8_t update = vdupq_n_u16(0x2455U);

		const u32 *p = (const u32 *)checkp;
		const u32 *pend = p + size / 4;
		while (p < pend) {
			cursor = vreinterpretq_u32_u16(vmlaq_u16(vreinterpretq_u16_u32(cursor), vreinterpretq_u16_u32(vld1q_u32(&p[4 * 0])), cursor2));
			cursor = veorq_u32(cursor, vld1q_u32(&p[4 * 1]));
			cursor = vaddq_u32(cursor, vld1q_u32(&p[4 * 2]));
			cursor = veorq_u32(cursor, vreinterpretq_u32_u16(vmulq_u16(vreinterpretq_u16_u32(vld1q_u32(&p[4 * 3])), cursor2)));
			cursor2 = vaddq_u16(cursor2, update);

			p += 4 * 4;
		}

		cursor = vaddq_u32(cursor, vreinterpretq_u32_u16(cursor2));
		uint32x2_t mixed = vadd_u32(vget_high_u32(cursor), vget_low_u32(cursor));
		check = vget_lane_u32(mixed, 0) + vget_lane_u32(mixed, 1);
#else
		// TODO: Why does this crash on iOS, but only certain devices?
		// It's faster than the above, but I guess it sucks to be using an iPhone.
		// As of 2020 clang, it's still faster by ~1.4%.

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

#endif  // PPSSPP_ARCH(ARM_NEON)

// Masks to downalign bufw to 16 bytes, and wrap at 2048.
static const u32 textureAlignMask16[16] = {
	0x7FF & ~(((8 * 16) / 16) - 1),  //GE_TFMT_5650,
	0x7FF & ~(((8 * 16) / 16) - 1),  //GE_TFMT_5551,
	0x7FF & ~(((8 * 16) / 16) - 1),  //GE_TFMT_4444,
	0x7FF & ~(((8 * 16) / 32) - 1),  //GE_TFMT_8888,
	0x7FF & ~(((8 * 16) / 4) - 1),   //GE_TFMT_CLUT4,
	0x7FF & ~(((8 * 16) / 8) - 1),   //GE_TFMT_CLUT8,
	0x7FF & ~(((8 * 16) / 16) - 1),  //GE_TFMT_CLUT16,
	0x7FF & ~(((8 * 16) / 32) - 1),  //GE_TFMT_CLUT32,
	0x7FF, //GE_TFMT_DXT1,
	0x7FF, //GE_TFMT_DXT3,
	0x7FF, //GE_TFMT_DXT5,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
};

u32 GetTextureBufw(int level, u32 texaddr, GETextureFormat format) {
	// This is a hack to allow for us to draw the huge PPGe texture, which is always in kernel ram.
	if (texaddr >= PSP_GetKernelMemoryBase() && texaddr < PSP_GetKernelMemoryEnd())
		return gstate.texbufwidth[level] & 0x1FFF;

	u32 bufw = gstate.texbufwidth[level] & textureAlignMask16[format];
	if (bufw == 0 && format <= GE_TFMT_DXT5) {
		// If it's less than 16 bytes, use 16 bytes.
		bufw = (8 * 16) / textureBitsPerPixel[format];
	}
	return bufw;
}

// Matches QuickTexHashNEON/SSE, see #7029.
static u32 QuickTexHashNonSSE(const void *checkp, u32 size) {
	u32 check = 0;

	if (((intptr_t)checkp & 0xf) == 0 && (size & 0x3f) == 0) {
		static const u16 cursor2_initial[8] = {0xc00bU, 0x9bd9U, 0x4b73U, 0xb651U, 0x4d9bU, 0x4309U, 0x0083U, 0x0001U};
		union u32x4_u16x8 {
#if defined(__GNUC__)
			uint32_t x32 __attribute__((vector_size(16)));
			uint16_t x16 __attribute__((vector_size(16)));
#else
			u32 x32[4];
			u16 x16[8];
#endif
		};
		u32x4_u16x8 cursor{};
		u32x4_u16x8 cursor2;
		static const u16 update[8] = {0x2455U, 0x2455U, 0x2455U, 0x2455U, 0x2455U, 0x2455U, 0x2455U, 0x2455U};

		for (u32 j = 0; j < 8; ++j) {
			cursor2.x16[j] = cursor2_initial[j];
		}

		const u32x4_u16x8 *p = (const u32x4_u16x8 *)checkp;
		for (u32 i = 0; i < size / 16; i += 4) {
			for (u32 j = 0; j < 8; ++j) {
				const u16 temp = p[i + 0].x16[j] * cursor2.x16[j];
				cursor.x16[j] += temp;
			}
			for (u32 j = 0; j < 4; ++j) {
				cursor.x32[j] ^= p[i + 1].x32[j];
				cursor.x32[j] += p[i + 2].x32[j];
			}
			for (u32 j = 0; j < 8; ++j) {
				const u16 temp = p[i + 3].x16[j] * cursor2.x16[j];
				cursor.x16[j] ^= temp;
			}
			for (u32 j = 0; j < 8; ++j) {
				cursor2.x16[j] += update[j];
			}
		}

		for (u32 j = 0; j < 4; ++j) {
			cursor.x32[j] += cursor2.x32[j];
		}
		check = cursor.x32[0] + cursor.x32[1] + cursor.x32[2] + cursor.x32[3];
	} else {
		const u32 *p = (const u32 *)checkp;
		for (u32 i = 0; i < size / 8; ++i) {
			check += *p++;
			check ^= *p++;
		}
	}

	return check;
}

u32 StableQuickTexHash(const void *checkp, u32 size) {
#if defined(_M_SSE)
	return QuickTexHashSSE2(checkp, size);
#elif PPSSPP_ARCH(ARM_NEON)
	return QuickTexHashNEON(checkp, size);
#else
	return QuickTexHashNonSSE(checkp, size);
#endif
}

void DoSwizzleTex16(const u32 *ysrcp, u8 *texptr, int bxc, int byc, u32 pitch) {
	// ysrcp is in 32-bits, so this is convenient.
	const u32 pitchBy32 = pitch >> 2;
#ifdef _M_SSE
	if (((uintptr_t)ysrcp & 0xF) == 0 && (pitch & 0xF) == 0) {
		__m128i *dest = (__m128i *)texptr;
		// The pitch parameter is in bytes, so shift down for 128-bit.
		// Note: it's always aligned to 16 bytes, so this is safe.
		const u32 pitchBy128 = pitch >> 4;
		for (int by = 0; by < byc; by++) {
			const __m128i *xsrc = (const __m128i *)ysrcp;
			for (int bx = 0; bx < bxc; bx++) {
				const __m128i *src = xsrc;
				for (int n = 0; n < 2; n++) {
					// Textures are always 16-byte aligned so this is fine.
					__m128i temp1 = _mm_load_si128(src);
					src += pitchBy128;
					__m128i temp2 = _mm_load_si128(src);
					src += pitchBy128;
					__m128i temp3 = _mm_load_si128(src);
					src += pitchBy128;
					__m128i temp4 = _mm_load_si128(src);
					src += pitchBy128;

					_mm_store_si128(dest, temp1);
					_mm_store_si128(dest + 1, temp2);
					_mm_store_si128(dest + 2, temp3);
					_mm_store_si128(dest + 3, temp4);
					dest += 4;
				}
				xsrc++;
			}
			ysrcp += pitchBy32 * 8;
		}
	} else
#endif
	{
		u32 *dest = (u32 *)texptr;
		for (int by = 0; by < byc; by++) {
			const u32 *xsrc = ysrcp;
			for (int bx = 0; bx < bxc; bx++) {
				const u32 *src = xsrc;
				for (int n = 0; n < 8; n++) {
					memcpy(dest, src, 16);
					src += pitchBy32;
					dest += 4;
				}
				xsrc += 4;
			}
			ysrcp += pitchBy32 * 8;
		}
	}
}

void DoUnswizzleTex16(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch) {
	// ydestp is in 32-bits, so this is convenient.
	const u32 pitchBy32 = pitch >> 2;

#ifdef _M_SSE
	// This check is pretty much a given, right?
	if (((uintptr_t)ydestp & 0xF) == 0 && (pitch & 0xF) == 0) {
		const __m128i *src = (const __m128i *)texptr;
		// The pitch parameter is in bytes, so shift down for 128-bit.
		// Note: it's always aligned to 16 bytes, so this is safe.
		const u32 pitchBy128 = pitch >> 4;
		for (int by = 0; by < byc; by++) {
			__m128i *xdest = (__m128i *)ydestp;
			for (int bx = 0; bx < bxc; bx++) {
				__m128i *dest = xdest;
				for (int n = 0; n < 2; n++) {
					// Textures are always 16-byte aligned so this is fine.
					__m128i temp1 = _mm_load_si128(src);
					__m128i temp2 = _mm_load_si128(src + 1);
					__m128i temp3 = _mm_load_si128(src + 2);
					__m128i temp4 = _mm_load_si128(src + 3);
					_mm_store_si128(dest, temp1);
					dest += pitchBy128;
					_mm_store_si128(dest, temp2);
					dest += pitchBy128;
					_mm_store_si128(dest, temp3);
					dest += pitchBy128;
					_mm_store_si128(dest, temp4);
					dest += pitchBy128;
					src += 4;
				}
				xdest++;
			}
			ydestp += pitchBy32 * 8;
		}
	} else
#elif PPSSPP_ARCH(ARM_NEON)
	if (((uintptr_t)ydestp & 0xF) == 0 && (pitch & 0xF) == 0) {
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
					dest += pitchBy32;
					vst1q_u32(dest, temp2);
					dest += pitchBy32;
					vst1q_u32(dest, temp3);
					dest += pitchBy32;
					vst1q_u32(dest, temp4);
					dest += pitchBy32;
					src += 16;
				}
				xdest += 4;
			}
			ydestp += pitchBy32 * 8;
		}
	} else
#endif
	{
		const u32 *src = (const u32 *)texptr;
		for (int by = 0; by < byc; by++) {
			u32 *xdest = ydestp;
			for (int bx = 0; bx < bxc; bx++) {
				u32 *dest = xdest;
				for (int n = 0; n < 8; n++) {
					memcpy(dest, src, 16);
					dest += pitchBy32;
					src += 4;
				}
				xdest += 4;
			}
			ydestp += pitchBy32 * 8;
		}
	}
}

// S3TC / DXT Decoder
class DXTDecoder {
public:
	inline void DecodeColors(const DXT1Block *src, bool ignore1bitAlpha);
	inline void DecodeAlphaDXT5(const DXT5Block *src);
	inline void WriteColorsDXT1(u32 *dst, const DXT1Block *src, int pitch, int width, int height);
	inline void WriteColorsDXT3(u32 *dst, const DXT3Block *src, int pitch, int width, int height);
	inline void WriteColorsDXT5(u32 *dst, const DXT5Block *src, int pitch, int width, int height);

	bool AnyNonFullAlpha() const { return anyNonFullAlpha_; }

protected:
	u32 colors_[4];
	u8 alpha_[8];
	bool alphaMode_ = false;
	bool anyNonFullAlpha_ = false;
};

static inline u32 makecol(int r, int g, int b, int a) {
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static inline int mix_2_3(int c1, int c2) {
	return (c1 + c1 + c2) / 3;
}

// This could probably be done faster by decoding two or four blocks at a time with SSE/NEON.
void DXTDecoder::DecodeColors(const DXT1Block *src, bool ignore1bitAlpha) {
	u16 c1 = src->color1;
	u16 c2 = src->color2;
	int blue1 = (c1 << 3) & 0xF8;
	int blue2 = (c2 << 3) & 0xF8;
	int green1 = (c1 >> 3) & 0xFC;
	int green2 = (c2 >> 3) & 0xFC;
	int red1 = (c1 >> 8) & 0xF8;
	int red2 = (c2 >> 8) & 0xF8;

	// Keep alpha zero for non-DXT1 to skip masking the colors.
	int alpha = ignore1bitAlpha ? 0 : 255;

	colors_[0] = makecol(red1, green1, blue1, alpha);
	colors_[1] = makecol(red2, green2, blue2, alpha);
	if (c1 > c2) {
		colors_[2] = makecol(mix_2_3(red1, red2), mix_2_3(green1, green2), mix_2_3(blue1, blue2), alpha);
		colors_[3] = makecol(mix_2_3(red2, red1), mix_2_3(green2, green1), mix_2_3(blue2, blue1), alpha);
	} else {
		// Average - these are always left shifted, so no need to worry about ties.
		int red3 = (red1 + red2) / 2;
		int green3 = (green1 + green2) / 2;
		int blue3 = (blue1 + blue2) / 2;
		colors_[2] = makecol(red3, green3, blue3, alpha);
		colors_[3] = makecol(0, 0, 0, 0);
		if (alpha == 255) {
			alphaMode_ = true;
		}
	}
}

static inline u8 lerp8(const DXT5Block *src, int n) {
	// These weights multiple alpha1/alpha2 to fixed 8.8 point.
	int alpha1 = (src->alpha1 * ((7 - n) << 8)) / 7;
	int alpha2 = (src->alpha2 * (n << 8)) / 7;
	return (u8)((alpha1 + alpha2 + 31) >> 8);
}

static inline u8 lerp6(const DXT5Block *src, int n) {
	int alpha1 = (src->alpha1 * ((5 - n) << 8)) / 5;
	int alpha2 = (src->alpha2 * (n << 8)) / 5;
	return (u8)((alpha1 + alpha2 + 31) >> 8);
}

void DXTDecoder::DecodeAlphaDXT5(const DXT5Block *src) {
	alpha_[0] = src->alpha1;
	alpha_[1] = src->alpha2;
	if (alpha_[0] > alpha_[1]) {
		alpha_[2] = lerp8(src, 1);
		alpha_[3] = lerp8(src, 2);
		alpha_[4] = lerp8(src, 3);
		alpha_[5] = lerp8(src, 4);
		alpha_[6] = lerp8(src, 5);
		alpha_[7] = lerp8(src, 6);
	} else {
		alpha_[2] = lerp6(src, 1);
		alpha_[3] = lerp6(src, 2);
		alpha_[4] = lerp6(src, 3);
		alpha_[5] = lerp6(src, 4);
		alpha_[6] = 0;
		alpha_[7] = 255;
	}
}

void DXTDecoder::WriteColorsDXT1(u32 *dst, const DXT1Block *src, int pitch, int width, int height) {
	bool anyColor3 = false;
	for (int y = 0; y < height; y++) {
		int colordata = src->lines[y];
		for (int x = 0; x < width; x++) {
			int col = colordata & 3;
			if (col == 3) {
				anyColor3 = true;
			}
			dst[x] = colors_[col];
			colordata >>= 2;
		}
		dst += pitch;
	}

	if (alphaMode_ && anyColor3) {
		anyNonFullAlpha_ = true;
	}
}

void DXTDecoder::WriteColorsDXT3(u32 *dst, const DXT3Block *src, int pitch, int width, int height) {
	for (int y = 0; y < height; y++) {
		int colordata = src->color.lines[y];
		u32 alphadata = src->alphaLines[y];
		for (int x = 0; x < width; x++) {
			dst[x] = colors_[colordata & 3] | (alphadata << 28);
			colordata >>= 2;
			alphadata >>= 4;
		}
		dst += pitch;
	}
}

void DXTDecoder::WriteColorsDXT5(u32 *dst, const DXT5Block *src, int pitch, int width, int height) {
	// 48 bits, 3 bit index per pixel, 12 bits per line.
	u64 allAlpha = ((u64)(u16)src->alphadata1 << 32) | (u32)src->alphadata2;

	for (int y = 0; y < height; y++) {
		uint32_t colordata = src->color.lines[y];
		uint32_t alphadata = allAlpha >> (12 * y);
		for (int x = 0; x < width; x++) {
			dst[x] = colors_[colordata & 3] | (alpha_[alphadata & 7] << 24);
			colordata >>= 2;
			alphadata >>= 3;
		}
		dst += pitch;
	}
}

uint32_t GetDXTTexelColor(const DXT1Block *src, int x, int y, int alpha) {
	_dbg_assert_(x >= 0 && x < 4);
	_dbg_assert_(y >= 0 && y < 4);

	uint16_t c1 = src->color1;
	uint16_t c2 = src->color2;
	int blue1 = (c1 << 3) & 0xF8;
	int blue2 = (c2 << 3) & 0xF8;
	int green1 = (c1 >> 3) & 0xFC;
	int green2 = (c2 >> 3) & 0xFC;
	int red1 = (c1 >> 8) & 0xF8;
	int red2 = (c2 >> 8) & 0xF8;

	int colorIndex = (src->lines[y] >> (x * 2)) & 3;
	if (colorIndex == 0) {
		return makecol(red1, green1, blue1, alpha);
	} else if (colorIndex == 1) {
		return makecol(red2, green2, blue2, alpha);
	} else if (c1 > c2) {
		if (colorIndex == 2) {
			return makecol(mix_2_3(red1, red2), mix_2_3(green1, green2), mix_2_3(blue1, blue2), alpha);
		}
		return makecol(mix_2_3(red2, red1), mix_2_3(green2, green1), mix_2_3(blue2, blue1), alpha);
	} else if (colorIndex == 3) {
		return makecol(0, 0, 0, 0);
	}

	// Average - these are always left shifted, so no need to worry about ties.
	int red3 = (red1 + red2) / 2;
	int green3 = (green1 + green2) / 2;
	int blue3 = (blue1 + blue2) / 2;
	return makecol(red3, green3, blue3, alpha);
}

uint32_t GetDXT1Texel(const DXT1Block *src, int x, int y) {
	return GetDXTTexelColor(src, x, y, 255);
}

uint32_t GetDXT3Texel(const DXT3Block *src, int x, int y) {
	uint32_t color = GetDXTTexelColor(&src->color, x, y, 0);
	u32 alpha = (src->alphaLines[y] >> (x * 4)) & 0xF;
	return color | (alpha << 28);
}

uint32_t GetDXT5Texel(const DXT5Block *src, int x, int y) {
	uint32_t color = GetDXTTexelColor(&src->color, x, y, 0);
	uint64_t alphadata = ((uint64_t)(uint16_t)src->alphadata1 << 32) | (uint32_t)src->alphadata2;
	int alphaIndex = (alphadata >> (y * 12 + x * 3)) & 7;

	if (alphaIndex == 0) {
		return color | (src->alpha1 << 24);
	} else if (alphaIndex == 1) {
		return color | (src->alpha2 << 24);
	} else if (src->alpha1 > src->alpha2) {
		return color | (lerp8(src, alphaIndex - 1) << 24);
	} else if (alphaIndex == 6) {
		return color;
	} else if (alphaIndex == 7) {
		return color | 0xFF000000;
	}
	return color | (lerp6(src, alphaIndex - 1) << 24);
}

// This could probably be done faster by decoding two or four blocks at a time with SSE/NEON.
void DecodeDXT1Block(u32 *dst, const DXT1Block *src, int pitch, int width, int height, u32 *alpha) {
	DXTDecoder dxt;
	dxt.DecodeColors(src, false);
	dxt.WriteColorsDXT1(dst, src, pitch, width, height);
	*alpha &= dxt.AnyNonFullAlpha() ? 0 : 1;
}

void DecodeDXT3Block(u32 *dst, const DXT3Block *src, int pitch, int width,  int height) {
	DXTDecoder dxt;
	dxt.DecodeColors(&src->color, true);
	dxt.WriteColorsDXT3(dst, src, pitch, width, height);
}

void DecodeDXT5Block(u32 *dst, const DXT5Block *src, int pitch, int width, int height) {
	DXTDecoder dxt;
	dxt.DecodeColors(&src->color, true);
	dxt.DecodeAlphaDXT5(src);
	dxt.WriteColorsDXT5(dst, src, pitch, width, height);
}

#ifdef _M_SSE
inline u32 SSEReduce32And(__m128i value) {
	value = _mm_and_si128(value, _mm_shuffle_epi32(value, _MM_SHUFFLE(1, 0, 3, 2)));
	value = _mm_and_si128(value, _mm_shuffle_epi32(value, _MM_SHUFFLE(1, 1, 1, 1)));
	return _mm_cvtsi128_si32(value);
}
inline u32 SSEReduce16And(__m128i value) {
	u32 mask = SSEReduce32And(value);
	return mask & (mask >> 16);
}
#endif

#if PPSSPP_ARCH(ARM_NEON)
inline u32 NEONReduce32And(uint32x4_t value) {
	// TODO: Maybe a shuffle and a vector and, or something?
	return vgetq_lane_u32(value, 0) & vgetq_lane_u32(value, 1) & vgetq_lane_u32(value, 2) & vgetq_lane_u32(value, 3);
}
inline u32 NEONReduce16And(uint16x8_t value) {
	uint32x4_t value32 = vreinterpretq_u32_u16(value);
	// TODO: Maybe a shuffle and a vector and, or something?
	u32 mask = vgetq_lane_u32(value32, 0) & vgetq_lane_u32(value32, 1) & vgetq_lane_u32(value32, 2) & vgetq_lane_u32(value32, 3);
	return mask & (mask >> 16);
}
#endif

// TODO: SSE/SIMD
// At least on x86, compiler actually SIMDs these pretty well.
void CopyAndSumMask16(u16 *dst, const u16 *src, int width, u32 *outMask) {
	u16 mask = 0xFFFF;
#ifdef _M_SSE
	if (width >= 8) {
		__m128i wideMask = _mm_set1_epi32(0xFFFFFFFF);
		while (width >= 8) {
			__m128i color = _mm_loadu_si128((__m128i *)src);
			wideMask = _mm_and_si128(wideMask, color);
			_mm_storeu_si128((__m128i *)dst, color);
			src += 8;
			dst += 8;
			width -= 8;
		}
		mask = SSEReduce16And(wideMask);
	}
#elif PPSSPP_ARCH(ARM_NEON)
	if (width >= 8) {
		uint16x8_t wideMask = vdupq_n_u16(0xFFFF);
		while (width >= 8) {
			uint16x8_t colors = vld1q_u16(src);
			wideMask = vandq_u16(wideMask, colors);
			vst1q_u16(dst, colors);
			src += 8;
			dst += 8;
			width -= 8;
		}
		mask = NEONReduce16And(wideMask);
	}
#endif

	DO_NOT_VECTORIZE_LOOP
	for (int i = 0; i < width; i++) {
		u16 color = src[i];
		mask &= color;
		dst[i] = color;
	}
	*outMask &= (u32)mask;
}

// Used in video playback so nice to have being fast.
void CopyAndSumMask32(u32 *dst, const u32 *src, int width, u32 *outMask) {
	u32 mask = 0xFFFFFFFF;
#ifdef _M_SSE
	if (width >= 4) {
		__m128i wideMask = _mm_set1_epi32(0xFFFFFFFF);
		while (width >= 4) {
			__m128i color = _mm_loadu_si128((__m128i *)src);
			wideMask = _mm_and_si128(wideMask, color);
			_mm_storeu_si128((__m128i *)dst, color);
			src += 4;
			dst += 4;
			width -= 4;
		}
		mask = SSEReduce32And(wideMask);
	}
#elif PPSSPP_ARCH(ARM_NEON)
	if (width >= 4) {
		uint32x4_t wideMask = vdupq_n_u32(0xFFFFFFFF);
		while (width >= 4) {
			uint32x4_t colors = vld1q_u32(src);
			wideMask = vandq_u32(wideMask, colors);
			vst1q_u32(dst, colors);
			src += 4;
			dst += 4;
			width -= 4;
		}
		mask = NEONReduce32And(wideMask);
	}
#endif

	DO_NOT_VECTORIZE_LOOP
	for (int i = 0; i < width; i++) {
		u32 color = src[i];
		mask &= color;
		dst[i] = color;
	}
	*outMask &= (u32)mask;
}

void CheckMask16(const u16 *src, int width, u32 *outMask) {
	u16 mask = 0xFFFF;
#ifdef _M_SSE
	if (width >= 8) {
		__m128i wideMask = _mm_set1_epi32(0xFFFFFFFF);
		while (width >= 8) {
			wideMask = _mm_and_si128(wideMask, _mm_loadu_si128((__m128i *)src));
			src += 8;
			width -= 8;
		}
		mask = SSEReduce16And(wideMask);
	}
#elif PPSSPP_ARCH(ARM_NEON)
	if (width >= 8) {
		uint16x8_t wideMask = vdupq_n_u16(0xFFFF);
		while (width >= 8) {
			wideMask = vandq_u16(wideMask, vld1q_u16(src));
			src += 8;
			width -= 8;
		}
		mask = NEONReduce16And(wideMask);
	}
#endif

	DO_NOT_VECTORIZE_LOOP
	for (int i = 0; i < width; i++) {
		mask &= src[i];
	}
	*outMask &= (u32)mask;
}

void CheckMask32(const u32 *src, int width, u32 *outMask) {
	u32 mask = 0xFFFFFFFF;
#ifdef _M_SSE
	if (width >= 4) {
		__m128i wideMask = _mm_set1_epi32(0xFFFFFFFF);
		while (width >= 4) {
			wideMask = _mm_and_si128(wideMask, _mm_loadu_si128((__m128i *)src));
			src += 4;
			width -= 4;
		}
		mask = SSEReduce32And(wideMask);
	}
#elif PPSSPP_ARCH(ARM_NEON)
	if (width >= 4) {
		uint32x4_t wideMask = vdupq_n_u32(0xFFFFFFFF);
		while (width >= 4) {
			wideMask = vandq_u32(wideMask, vld1q_u32(src));
			src += 4;
			width -= 4;
		}
		mask = NEONReduce32And(wideMask);
	}
#endif

	DO_NOT_VECTORIZE_LOOP
	for (int i = 0; i < width; i++) {
		mask &= src[i];
	}
	*outMask &= (u32)mask;
}
