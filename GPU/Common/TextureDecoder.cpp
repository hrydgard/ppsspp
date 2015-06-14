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

#include "ext/xxhash.h"
#include "Common/CPUDetect.h"
#include "Common/ColorConv.h"
#include "GPU/Common/TextureDecoder.h"
// NEON is in a separate file so that it can be compiled with a runtime check.
#include "GPU/Common/TextureDecoderNEON.h"

// TODO: Move some common things into here.

#ifdef _M_SSE
#include <xmmintrin.h>
#if _M_SSE >= 0x401
#include <smmintrin.h>
#endif

u32 QuickTexHashSSE2(const void *checkp, u32 size) {
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

u32 QuickTexHashNonSSE(const void *checkp, u32 size) {
	u32 check = 0;

	if (((intptr_t)checkp & 0xf) == 0 && (size & 0x3f) == 0) {
		static const u16 cursor2_initial[8] = {0xc00bU, 0x9bd9U, 0x4b73U, 0xb651U, 0x4d9bU, 0x4309U, 0x0083U, 0x0001U};
		union u32x4_u16x8 {
			u32 x32[4];
			u16 x16[8];
		};
		u32x4_u16x8 cursor = {0, 0, 0, 0};
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

static u32 QuickTexHashBasic(const void *checkp, u32 size) {
#if defined(ARM) && defined(__GNUC__)
	__builtin_prefetch(checkp, 0, 0);

	u32 check;
	asm volatile (
		// Let's change size to the end address.
		"add %1, %1, %2\n"
		"mov r6, #0\n"

		".align 2\n"

		// If we have zero sized input, we'll return garbage.  Oh well, shouldn't happen.
		"QuickTexHashBasic_next:\n"
		"ldmia %2!, {r2-r5}\n"
		"add r6, r6, r2\n"
		"eor r6, r6, r3\n"
		"cmp %2, %1\n"
		"add r6, r6, r4\n"
		"eor r6, r6, r5\n"
		"blo QuickTexHashBasic_next\n"

		".align 2\n"

		"QuickTexHashBasic_done:\n"
		"mov %0, r6\n"

		: "=r"(check)
		: "r"(size), "r"(checkp)
		: "r2", "r3", "r4", "r5", "r6"
	);
#else
	u32 check = 0;
	const u32 size_u32 = size / 4;
	const u32 *p = (const u32 *)checkp;
	for (u32 i = 0; i < size_u32; i += 4) {
		check += p[i + 0];
		check ^= p[i + 1];
		check += p[i + 2];
		check ^= p[i + 3];
	}
#endif

	return check;
}

void DoSwizzleTex16(const u32 *ysrcp, u8 *texptr, int bxc, int byc, u32 pitch, u32 rowWidth) {
#ifdef _M_SSE
	__m128i *dest = (__m128i *)texptr;
	for (int by = 0; by < byc; by++) {
		const __m128i *xsrc = (const __m128i *)ysrcp;
		for (int bx = 0; bx < bxc; bx++) {
			const __m128i *src = xsrc;
			for (int n = 0; n < 2; n++) {
				// Textures are always 16-byte aligned so this is fine.
				__m128i temp1 = _mm_load_si128(src);
				src += pitch >> 2;
				__m128i temp2 = _mm_load_si128(src);
				src += pitch >> 2;
				__m128i temp3 = _mm_load_si128(src);
				src += pitch >> 2;
				__m128i temp4 = _mm_load_si128(src);
				src += pitch >> 2;

				_mm_store_si128(dest, temp1);
				_mm_store_si128(dest + 1, temp2);
				_mm_store_si128(dest + 2, temp3);
				_mm_store_si128(dest + 3, temp4);
				dest += 4;
			}
			xsrc++;
		}
		ysrcp += (rowWidth * 8) / 4;
	}
#else
	u32 *dest = (u32 *)texptr;
	for (int by = 0; by < byc; by++) {
		const u32 *xsrc = ysrcp;
		for (int bx = 0; bx < bxc; bx++) {
			const u32 *src = xsrc;
			for (int n = 0; n < 8; n++) {
				memcpy(dest, src, 16);
				src += pitch;
				dest += 4;
			}
			xsrc += 4;
		}
		ysrcp += (rowWidth * 8) / 4;
	}
#endif
}

void DoUnswizzleTex16Basic(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch, u32 rowWidth) {
#ifdef _M_SSE
	const __m128i *src = (const __m128i *)texptr;
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
				dest += pitch >> 2;
				_mm_store_si128(dest, temp2);
				dest += pitch >> 2;
				_mm_store_si128(dest, temp3);
				dest += pitch >> 2;
				_mm_store_si128(dest, temp4);
				dest += pitch >> 2;
				src += 4;
			}
			xdest++;
		}
		ydestp += (rowWidth * 8) / 4;
	}
#else
	const u32 *src = (const u32 *)texptr;
	for (int by = 0; by < byc; by++) {
		u32 *xdest = ydestp;
		for (int bx = 0; bx < bxc; bx++) {
			u32 *dest = xdest;
			for (int n = 0; n < 8; n++) {
				memcpy(dest, src, 16);
				dest += pitch;
				src += 4;
			}
			xdest += 4;
		}
		ydestp += (rowWidth * 8) / 4;
	}
#endif
}

#ifndef _M_SSE
#ifndef ARM64
QuickTexHashFunc DoQuickTexHash = &QuickTexHashBasic;
UnswizzleTex16Func DoUnswizzleTex16 = &DoUnswizzleTex16Basic;
ReliableHash32Func DoReliableHash32 = &XXH32;
#endif
ReliableHash64Func DoReliableHash64 = &XXH64;
#endif

// This has to be done after CPUDetect has done its magic.
void SetupTextureDecoder() {
#ifdef HAVE_ARMV7
	if (cpu_info.bNEON) {
		DoQuickTexHash = &QuickTexHashNEON;
		DoUnswizzleTex16 = &DoUnswizzleTex16NEON;
#ifndef IOS
		// Not sure if this is safe on iOS, it's had issues with xxhash.
		DoReliableHash32 = &ReliableHash32NEON;
#endif
	}
#endif
}

static inline u32 makecol(int r, int g, int b, int a) {
	return (a << 24) | (r << 16) | (g << 8) | b;
}

// This could probably be done faster by decoding two or four blocks at a time with SSE/NEON.
void DecodeDXT1Block(u32 *dst, const DXT1Block *src, int pitch, bool ignore1bitAlpha) {
	// S3TC Decoder
	// Needs more speed and debugging.
	u16 c1 = (src->color1);
	u16 c2 = (src->color2);
	int red1 = Convert5To8(c1 & 0x1F);
	int red2 = Convert5To8(c2 & 0x1F);
	int green1 = Convert6To8((c1 >> 5) & 0x3F);
	int green2 = Convert6To8((c2 >> 5) & 0x3F);
	int blue1 = Convert5To8((c1 >> 11) & 0x1F);
	int blue2 = Convert5To8((c2 >> 11) & 0x1F);

	u32 colors[4];
	colors[0] = makecol(red1, green1, blue1, 255);
	colors[1] = makecol(red2, green2, blue2, 255);
	if (c1 > c2 || ignore1bitAlpha) {
		int blue3 = ((blue2 - blue1) >> 1) - ((blue2 - blue1) >> 3);
		int green3 = ((green2 - green1) >> 1) - ((green2 - green1) >> 3);
		int red3 = ((red2 - red1) >> 1) - ((red2 - red1) >> 3);				
		colors[2] = makecol(red1 + red3, green1 + green3, blue1 + blue3, 255);
		colors[3] = makecol(red2 - red3, green2 - green3, blue2 - blue3, 255);
	} else {
		colors[2] = makecol((red1 + red2 + 1) / 2, // Average
			(green1 + green2 + 1) / 2,
			(blue1 + blue2 + 1) / 2, 255);
		colors[3] = makecol(red2, green2, blue2, 0);	// Color2 but transparent
	}

	for (int y = 0; y < 4; y++) {
		int val = src->lines[y];
		for (int x = 0; x < 4; x++) {
			dst[x] = colors[val & 3];
			val >>= 2;
		}
		dst += pitch;
	}
}

void DecodeDXT3Block(u32 *dst, const DXT3Block *src, int pitch)
{
	DecodeDXT1Block(dst, &src->color, pitch, true);

	for (int y = 0; y < 4; y++) {
		u32 line = src->alphaLines[y];
		for (int x = 0; x < 4; x++) {
			const u8 a4 = line & 0xF;
			dst[x] = (dst[x] & 0xFFFFFF) | (a4 << 24) | (a4 << 28);
			line >>= 4;
		}
		dst += pitch;
	}
}

static inline u8 lerp8(const DXT5Block *src, int n) {
	float d = n / 7.0f;
	return (u8)(src->alpha1 + (src->alpha2 - src->alpha1) * d);
}

static inline u8 lerp6(const DXT5Block *src, int n) {
	float d = n / 5.0f;
	return (u8)(src->alpha1 + (src->alpha2 - src->alpha1) * d);
}

// The alpha channel is not 100% correct 
void DecodeDXT5Block(u32 *dst, const DXT5Block *src, int pitch) {
	DecodeDXT1Block(dst, &src->color, pitch, true);
	u8 alpha[8];

	alpha[0] = src->alpha1;
	alpha[1] = src->alpha2;
	if (alpha[0] > alpha[1]) {
		alpha[2] = lerp8(src, 1);
		alpha[3] = lerp8(src, 2);
		alpha[4] = lerp8(src, 3);
		alpha[5] = lerp8(src, 4);
		alpha[6] = lerp8(src, 5);
		alpha[7] = lerp8(src, 6);
	} else {
		alpha[2] = lerp6(src, 1);
		alpha[3] = lerp6(src, 2);
		alpha[4] = lerp6(src, 3);
		alpha[5] = lerp6(src, 4);
		alpha[6] = 0;
		alpha[7] = 255;
	}

	u64 data = ((u64)(u16)src->alphadata1 << 32) | (u32)src->alphadata2;

	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			dst[x] = (dst[x] & 0xFFFFFF) | (alpha[data & 7] << 24);
			data >>= 3;
		}
		dst += pitch;
	}
}

#ifdef _M_SSE
static inline u32 CombineSSEBitsToDWORD(const __m128i &v) {
	__m128i temp;
	temp = _mm_or_si128(v, _mm_srli_si128(v, 8));
	temp = _mm_or_si128(temp, _mm_srli_si128(temp, 4));
	return _mm_cvtsi128_si32(temp);
}

CheckAlphaResult CheckAlphaRGBA8888SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i zero = _mm_setzero_si128();
	const __m128i full = _mm_set1_epi32(0xFF);

	const __m128i *p = (const __m128i *)pixelData;
	const int w4 = w / 4;
	const int stride4 = stride / 4;

	// Have alpha values == 0 been seen?
	__m128i hasZeroCursor = _mm_setzero_si128();
	for (int y = 0; y < h; ++y) {
		// Have alpha values > 0 and < 0xFF been seen?
		__m128i hasAnyCursor = _mm_setzero_si128();

		for (int i = 0; i < w4; ++i) {
			const __m128i a = _mm_srli_epi32(_mm_load_si128(&p[i]), 24);

			const __m128i isZero = _mm_cmpeq_epi32(a, zero);
			hasZeroCursor = _mm_or_si128(hasZeroCursor, isZero);

			// If a = FF, isNotFull will be 0 -> hasAny will be 0.
			// If a = 00, a & isNotFull will be 0 -> hasAny will be 0.
			// In any other case, hasAny will have some bits set.
			const __m128i isNotFull = _mm_cmplt_epi32(a, full);
			hasAnyCursor = _mm_or_si128(hasAnyCursor, _mm_and_si128(a, isNotFull));
		}
		p += stride4;

		// We check any early, in case we can skip the rest of the rows.
		if (CombineSSEBitsToDWORD(hasAnyCursor) != 0) {
			return CHECKALPHA_ANY;
		}
	}

	// Now let's sum up the bits.
	if (CombineSSEBitsToDWORD(hasZeroCursor) != 0) {
		return CHECKALPHA_ZERO;
	} else {
		return CHECKALPHA_FULL;
	}
}

CheckAlphaResult CheckAlphaABGR4444SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i zero = _mm_setzero_si128();
	const __m128i full = _mm_set1_epi16((short)0xF000);

	const __m128i *p = (const __m128i *)pixelData;
	const int w8 = w / 8;
	const int stride8 = stride / 8;

	__m128i hasZeroCursor = _mm_setzero_si128();
	for (int y = 0; y < h; ++y) {
		__m128i hasAnyCursor = _mm_setzero_si128();

		for (int i = 0; i < w8; ++i) {
			const __m128i a = _mm_slli_epi16(_mm_load_si128(&p[i]), 12);

			const __m128i isZero = _mm_cmpeq_epi16(a, zero);
			hasZeroCursor = _mm_or_si128(hasZeroCursor, isZero);

			// If a = F, isNotFull will be 0 -> hasAny will be 0.
			// If a = 0, a & isNotFull will be 0 -> hasAny will be 0.
			// In any other case, hasAny will have some bits set.
			const __m128i isNotFull = _mm_cmplt_epi32(a, full);
			hasAnyCursor = _mm_or_si128(hasAnyCursor, _mm_and_si128(a, isNotFull));
		}
		p += stride8;

		// We check any early, in case we can skip the rest of the rows.
		if (CombineSSEBitsToDWORD(hasAnyCursor) != 0) {
			return CHECKALPHA_ANY;
		}
	}

	// Now let's sum up the bits.
	if (CombineSSEBitsToDWORD(hasZeroCursor) != 0) {
		return CHECKALPHA_ZERO;
	} else {
		return CHECKALPHA_FULL;
	}
}

CheckAlphaResult CheckAlphaABGR1555SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i mask = _mm_set1_epi16(1);

	const __m128i *p = (const __m128i *)pixelData;
	const int w8 = w / 8;
	const int stride8 = stride / 8;

	__m128i bits = mask;
	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w8; ++i) {
			const __m128i a = _mm_load_si128(&p[i]);
			bits = _mm_and_si128(bits, a);
		}

		__m128i result = _mm_xor_si128(bits, mask);
		if (CombineSSEBitsToDWORD(result) != 0) {
			return CHECKALPHA_ZERO;
		}

		p += stride8;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaRGBA4444SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i zero = _mm_setzero_si128();
	const __m128i full = _mm_set1_epi16(0x000F);

	const __m128i *p = (const __m128i *)pixelData;
	const int w8 = w / 8;
	const int stride8 = stride / 8;

	__m128i hasZeroCursor = _mm_setzero_si128();
	for (int y = 0; y < h; ++y) {
		__m128i hasAnyCursor = _mm_setzero_si128();

		for (int i = 0; i < w8; ++i) {
			const __m128i a = _mm_srli_epi16(_mm_load_si128(&p[i]), 12);

			const __m128i isZero = _mm_cmpeq_epi16(a, zero);
			hasZeroCursor = _mm_or_si128(hasZeroCursor, isZero);

			// If a = F, isNotFull will be 0 -> hasAny will be 0.
			// If a = 0, a & isNotFull will be 0 -> hasAny will be 0.
			// In any other case, hasAny will have some bits set.
			const __m128i isNotFull = _mm_cmplt_epi32(a, full);
			hasAnyCursor = _mm_or_si128(hasAnyCursor, _mm_and_si128(a, isNotFull));
		}
		p += stride8;

		// We check any early, in case we can skip the rest of the rows.
		if (CombineSSEBitsToDWORD(hasAnyCursor) != 0) {
			return CHECKALPHA_ANY;
		}
	}

	// Now let's sum up the bits.
	if (CombineSSEBitsToDWORD(hasZeroCursor) != 0) {
		return CHECKALPHA_ZERO;
	} else {
		return CHECKALPHA_FULL;
	}
}

CheckAlphaResult CheckAlphaRGBA5551SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i mask = _mm_set1_epi16((short)0x8000);

	const __m128i *p = (const __m128i *)pixelData;
	const int w8 = w / 8;
	const int stride8 = stride / 8;

	__m128i bits = mask;
	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w8; ++i) {
			const __m128i a = _mm_load_si128(&p[i]);
			bits = _mm_and_si128(bits, a);
		}

		__m128i result = _mm_xor_si128(bits, mask);
		if (CombineSSEBitsToDWORD(result) != 0) {
			return CHECKALPHA_ZERO;
		}

		p += stride8;
	}

	return CHECKALPHA_FULL;
}
#endif

CheckAlphaResult CheckAlphaRGBA8888Basic(const u32 *pixelData, int stride, int w, int h) {
	// Use SIMD if aligned to 16 bytes / 4 pixels (almost always the case.)
	if ((w & 3) == 0 && (stride & 3) == 0) {
#ifdef _M_SSE
		return CheckAlphaRGBA8888SSE2(pixelData, stride, w, h);
#elif (defined(ARM) && defined(HAVE_ARMV7)) || defined(ARM64)
		if (cpu_info.bNEON) {
			return CheckAlphaRGBA8888NEON(pixelData, stride, w, h);
		}
#endif
	}

	u32 hitZeroAlpha = 0;

	const u32 *p = pixelData;
	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w; ++i) {
			u32 a = p[i] & 0xFF000000;
			hitZeroAlpha |= a ^ 0xFF000000;
			if (a != 0xFF000000 && a != 0) {
				// We're done, we hit non-zero, non-full alpha.
				return CHECKALPHA_ANY;
			}
		}
		p += stride;
	}

	if (hitZeroAlpha) {
		return CHECKALPHA_ZERO;
	} else {
		return CHECKALPHA_FULL;
	}
}

CheckAlphaResult CheckAlphaABGR4444Basic(const u32 *pixelData, int stride, int w, int h) {
	// Use SIMD if aligned to 16 bytes / 8 pixels (usually the case.)
	if ((w & 7) == 0 && (stride & 7) == 0) {
#ifdef _M_SSE
		return CheckAlphaABGR4444SSE2(pixelData, stride, w, h);
#elif (defined(ARM) && defined(HAVE_ARMV7)) || defined(ARM64)
		if (cpu_info.bNEON) {
			return CheckAlphaABGR4444NEON(pixelData, stride, w, h);
		}
#endif
	}

	u32 hitZeroAlpha = 0;

	const u32 *p = pixelData;
	const int w2 = (w + 1) / 2;
	const int stride2 = (stride + 1) / 2;

	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w2; ++i) {
			u32 a = p[i] & 0x000F000F;
			hitZeroAlpha |= a ^ 0x000F000F;
			if (a != 0x000F000F && a != 0x0000000F && a != 0x000F0000 && a != 0) {
				// We're done, we hit non-zero, non-full alpha.
				return CHECKALPHA_ANY;
			}
		}
		p += stride2;
	}

	if (hitZeroAlpha) {
		return CHECKALPHA_ZERO;
	} else {
		return CHECKALPHA_FULL;
	}
}

CheckAlphaResult CheckAlphaABGR1555Basic(const u32 *pixelData, int stride, int w, int h) {
	// Use SIMD if aligned to 16 bytes / 8 pixels (usually the case.)
	if ((w & 7) == 0 && (stride & 7) == 0) {
#ifdef _M_SSE
		return CheckAlphaABGR1555SSE2(pixelData, stride, w, h);
#elif (defined(ARM) && defined(HAVE_ARMV7)) || defined(ARM64)
		if (cpu_info.bNEON) {
			return CheckAlphaABGR1555NEON(pixelData, stride, w, h);
		}
#endif
	}

	u32 hitZeroAlpha = 0;

	const u32 *p = pixelData;
	const int w2 = (w + 1) / 2;
	const int stride2 = (stride + 1) / 2;

	u32 bits = 0x00010001;
	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w2; ++i) {
			bits &= p[i];
		}

		if ((bits ^ 0x00010001) != 0) {
			return CHECKALPHA_ZERO;
		}

		p += stride2;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaRGBA4444Basic(const u32 *pixelData, int stride, int w, int h) {
#ifdef _M_SSE
	// Use SSE if aligned to 16 bytes / 8 pixels (usually the case.)
	if ((w & 7) == 0 && (stride & 7) == 0) {
		return CheckAlphaRGBA4444SSE2(pixelData, stride, w, h);
	}
#endif

	u32 hitZeroAlpha = 0;

	const u32 *p = pixelData;
	const int w2 = (w + 1) / 2;
	const int stride2 = (stride + 1) / 2;

	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w2; ++i) {
			u32 a = p[i] & 0xF000F000;
			hitZeroAlpha |= a ^ 0xF000F000;
			if (a != 0xF000F000 && a != 0xF0000000 && a != 0x0000F000 && a != 0) {
				// We're done, we hit non-zero, non-full alpha.
				return CHECKALPHA_ANY;
			}
		}
		p += stride2;
	}

	if (hitZeroAlpha) {
		return CHECKALPHA_ZERO;
	} else {
		return CHECKALPHA_FULL;
	}
}

CheckAlphaResult CheckAlphaRGBA5551Basic(const u32 *pixelData, int stride, int w, int h) {
#ifdef _M_SSE
	// Use SSE if aligned to 16 bytes / 8 pixels (usually the case.)
	if ((w & 7) == 0 && (stride & 7) == 0) {
		return CheckAlphaRGBA5551SSE2(pixelData, stride, w, h);
	}
#endif

	u32 bits = 0x80008000;

	const u32 *p = pixelData;
	const int w2 = (w + 1) / 2;
	const int stride2 = (stride + 1) / 2;

	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w2; ++i) {
			bits &= p[i];
		}

		if ((bits ^ 0x80008000) != 0) {
			return CHECKALPHA_ZERO;
		}

		p += stride;
	}

	return CHECKALPHA_FULL;
}
