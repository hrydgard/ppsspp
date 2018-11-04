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

#include "GPU/GPU.h"
#include "GPU/GPUState.h"
#include "GPU/Common/TextureDecoder.h"
// NEON is in a separate file so that it can be compiled with a runtime check.
#include "GPU/Common/TextureDecoderNEON.h"

// TODO: Move some common things into here.

#ifdef _M_SSE
#include <emmintrin.h>
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
	if (texaddr < PSP_GetKernelMemoryEnd())
		return gstate.texbufwidth[level] & 0x1FFF;

	u32 bufw = gstate.texbufwidth[level] & textureAlignMask16[format];
	if (bufw == 0) {
		// If it's less than 16 bytes, use 16 bytes.
		bufw = (8 * 16) / textureBitsPerPixel[format];
	}
	return bufw;
}

u32 QuickTexHashNonSSE(const void *checkp, u32 size) {
	u32 check = 0;

	if (((intptr_t)checkp & 0xf) == 0 && (size & 0x3f) == 0) {
		static const u16 cursor2_initial[8] = {0xc00bU, 0x9bd9U, 0x4b73U, 0xb651U, 0x4d9bU, 0x4309U, 0x0083U, 0x0001U};
		union u32x4_u16x8 {
			u32 x32[4];
			u16 x16[8];
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

#if !PPSSPP_ARCH(ARM64) && !defined(_M_SSE)
static u32 QuickTexHashBasic(const void *checkp, u32 size) {
#if PPSSPP_ARCH(ARM) && defined(__GNUC__)
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
#endif

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

void DoUnswizzleTex16Basic(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch) {
	// ydestp is in 32-bits, so this is convenient.
	const u32 pitchBy32 = pitch >> 2;

#ifdef _M_SSE
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

#if !PPSSPP_ARCH(ARM64) && !defined(_M_SSE)
QuickTexHashFunc DoQuickTexHash = &QuickTexHashBasic;
QuickTexHashFunc StableQuickTexHash = &QuickTexHashNonSSE;
UnswizzleTex16Func DoUnswizzleTex16 = &DoUnswizzleTex16Basic;
ReliableHash32Func DoReliableHash32 = &XXH32;
ReliableHash64Func DoReliableHash64 = &XXH64;
#endif

// This has to be done after CPUDetect has done its magic.
void SetupTextureDecoder() {
#if PPSSPP_ARCH(ARM_NEON) && !PPSSPP_ARCH(ARM64)
	if (cpu_info.bNEON) {
		DoQuickTexHash = &QuickTexHashNEON;
		StableQuickTexHash = &QuickTexHashNEON;
		DoUnswizzleTex16 = &DoUnswizzleTex16NEON;
#if !PPSSPP_PLATFORM(IOS)
		// Not sure if this is safe on iOS, it's had issues with xxhash.
		DoReliableHash32 = &ReliableHash32NEON;
#endif
	}
#endif
}

// S3TC / DXT Decoder
class DXTDecoder {
public:
	inline void DecodeColors(const DXT1Block *src, bool ignore1bitAlpha);
	inline void DecodeAlphaDXT5(const DXT5Block *src);
	inline void WriteColorsDXT1(u32 *dst, const DXT1Block *src, int pitch, int height);
	inline void WriteColorsDXT3(u32 *dst, const DXT3Block *src, int pitch, int height);
	inline void WriteColorsDXT5(u32 *dst, const DXT5Block *src, int pitch, int height);

protected:
	u32 colors_[4];
	u8 alpha_[8];
};

static inline u32 makecol(int r, int g, int b, int a) {
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static inline int mix_2_3(int c1, int c2) {
	return (c1 + c1 + c2) / 3;
}

// This could probably be done faster by decoding two or four blocks at a time with SSE/NEON.
void DXTDecoder::DecodeColors(const DXT1Block *src, bool ignore1bitAlpha) {
	u16 c1 = src->color1;
	u16 c2 = src->color2;
	int red1 = (c1 << 3) & 0xF8;
	int red2 = (c2 << 3) & 0xF8;
	int green1 = (c1 >> 3) & 0xFC;
	int green2 = (c2 >> 3) & 0xFC;
	int blue1 = (c1 >> 8) & 0xF8;
	int blue2 = (c2 >> 8) & 0xF8;

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
	}
}

static inline u8 lerp8(const DXT5Block *src, int n) {
	// These weights translate alpha1/alpha2 to fixed 8.8 point, pre-divided by 7.
	int weight1 = ((7 - n) << 8) / 7;
	int weight2 = (n << 8) / 7;
	return (u8)((src->alpha1 * weight1 + src->alpha2 * weight2 + 255) >> 8);
}

static inline u8 lerp6(const DXT5Block *src, int n) {
	int weight1 = ((5 - n) << 8) / 5;
	int weight2 = (n << 8) / 5;
	return (u8)((src->alpha1 * weight1 + src->alpha2 * weight2 + 255) >> 8);
}

void DXTDecoder::DecodeAlphaDXT5(const DXT5Block *src) {
	// TODO: Check if alpha is still not 100% correct.
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

void DXTDecoder::WriteColorsDXT1(u32 *dst, const DXT1Block *src, int pitch, int height) {
	for (int y = 0; y < height; y++) {
		int colordata = src->lines[y];
		for (int x = 0; x < 4; x++) {
			dst[x] = colors_[colordata & 3];
			colordata >>= 2;
		}
		dst += pitch;
	}
}

void DXTDecoder::WriteColorsDXT3(u32 *dst, const DXT3Block *src, int pitch, int height) {
	for (int y = 0; y < height; y++) {
		int colordata = src->color.lines[y];
		u32 alphadata = src->alphaLines[y];
		for (int x = 0; x < 4; x++) {
			dst[x] = colors_[colordata & 3] | (alphadata << 28);
			colordata >>= 2;
			alphadata >>= 4;
		}
		dst += pitch;
	}
}

void DXTDecoder::WriteColorsDXT5(u32 *dst, const DXT5Block *src, int pitch, int height) {
	// 48 bits, 3 bit index per pixel, 12 bits per line.
	u64 alphadata = ((u64)(u16)src->alphadata1 << 32) | (u32)src->alphadata2;

	for (int y = 0; y < height; y++) {
		int colordata = src->color.lines[y];
		for (int x = 0; x < 4; x++) {
			dst[x] = colors_[colordata & 3] | (alpha_[alphadata & 7] << 24);
			colordata >>= 2;
			alphadata >>= 3;
		}
		dst += pitch;
	}
}

// This could probably be done faster by decoding two or four blocks at a time with SSE/NEON.
void DecodeDXT1Block(u32 *dst, const DXT1Block *src, int pitch, int height, bool ignore1bitAlpha) {
	DXTDecoder dxt;
	dxt.DecodeColors(src, ignore1bitAlpha);
	dxt.WriteColorsDXT1(dst, src, pitch, height);
}

void DecodeDXT3Block(u32 *dst, const DXT3Block *src, int pitch, int height) {
	DXTDecoder dxt;
	dxt.DecodeColors(&src->color, true);
	dxt.WriteColorsDXT3(dst, src, pitch, height);
}

// The alpha channel is not 100% correct 
void DecodeDXT5Block(u32 *dst, const DXT5Block *src, int pitch, int height) {
	DXTDecoder dxt;
	dxt.DecodeColors(&src->color, true);
	dxt.DecodeAlphaDXT5(src);
	dxt.WriteColorsDXT5(dst, src, pitch, height);
}

#ifdef _M_SSE
static inline u32 CombineSSEBitsToDWORD(const __m128i &v) {
	__m128i temp;
	temp = _mm_or_si128(v, _mm_srli_si128(v, 8));
	temp = _mm_or_si128(temp, _mm_srli_si128(temp, 4));
	return _mm_cvtsi128_si32(temp);
}

CheckAlphaResult CheckAlphaRGBA8888SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i mask = _mm_set1_epi32(0xFF000000);

	const __m128i *p = (const __m128i *)pixelData;
	const int w4 = w / 4;
	const int stride4 = stride / 4;

	__m128i bits = mask;
	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < w4; ++i) {
			const __m128i a = _mm_load_si128(&p[i]);
			bits = _mm_and_si128(bits, a);
		}

		__m128i result = _mm_xor_si128(bits, mask);
		if (CombineSSEBitsToDWORD(result) != 0) {
			return CHECKALPHA_ANY;
		}

		p += stride4;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaABGR4444SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i mask = _mm_set1_epi16((short)0x000F);

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
			return CHECKALPHA_ANY;
		}

		p += stride8;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaABGR1555SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i mask = _mm_set1_epi16((short)0x0001);

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
			return CHECKALPHA_ANY;
		}

		p += stride8;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaRGBA4444SSE2(const u32 *pixelData, int stride, int w, int h) {
	const __m128i mask = _mm_set1_epi16((short)0xF000);

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
			return CHECKALPHA_ANY;
		}

		p += stride8;
	}

	return CHECKALPHA_FULL;
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
			return CHECKALPHA_ANY;
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
#elif PPSSPP_ARCH(ARMV7) || PPSSPP_ARCH(ARM64)
		if (cpu_info.bNEON) {
			return CheckAlphaRGBA8888NEON(pixelData, stride, w, h);
		}
#endif
	}

	const u32 *p = pixelData;
	for (int y = 0; y < h; ++y) {
		u32 bits = 0xFF000000;
		for (int i = 0; i < w; ++i) {
			bits &= p[i];
		}

		if (bits != 0xFF000000) {
			// We're done, we hit non-full alpha.
			return CHECKALPHA_ANY;
		}

		p += stride;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaABGR4444Basic(const u32 *pixelData, int stride, int w, int h) {
	// Use SIMD if aligned to 16 bytes / 8 pixels (usually the case.)
	if ((w & 7) == 0 && (stride & 7) == 0) {
#ifdef _M_SSE
		return CheckAlphaABGR4444SSE2(pixelData, stride, w, h);
#elif PPSSPP_ARCH(ARMV7) || PPSSPP_ARCH(ARM64)
		if (cpu_info.bNEON) {
			return CheckAlphaABGR4444NEON(pixelData, stride, w, h);
		}
#endif
	}

	const u32 *p = pixelData;
	const int w2 = (w + 1) / 2;
	const int stride2 = (stride + 1) / 2;

	for (int y = 0; y < h; ++y) {
		u32 bits = 0x000F000F;
		for (int i = 0; i < w2; ++i) {
			bits &= p[i];
		}

		if (bits != 0x000F000F) {
			// We're done, we hit non-full alpha.
			return CHECKALPHA_ANY;
		}

		p += stride2;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaABGR1555Basic(const u32 *pixelData, int stride, int w, int h) {
	// Use SIMD if aligned to 16 bytes / 8 pixels (usually the case.)
	if ((w & 7) == 0 && (stride & 7) == 0) {
#ifdef _M_SSE
		return CheckAlphaABGR1555SSE2(pixelData, stride, w, h);
#elif PPSSPP_ARCH(ARMV7) || PPSSPP_ARCH(ARM64)
		if (cpu_info.bNEON) {
			return CheckAlphaABGR1555NEON(pixelData, stride, w, h);
		}
#endif
	}

	const u32 *p = pixelData;
	const int w2 = (w + 1) / 2;
	const int stride2 = (stride + 1) / 2;

	for (int y = 0; y < h; ++y) {
		u32 bits = 0x00010001;
		for (int i = 0; i < w2; ++i) {
			bits &= p[i];
		}

		if (bits != 0x00010001) {
			return CHECKALPHA_ANY;
		}

		p += stride2;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaRGBA4444Basic(const u32 *pixelData, int stride, int w, int h) {
	// Use SSE if aligned to 16 bytes / 8 pixels (usually the case.)
	if ((w & 7) == 0 && (stride & 7) == 0) {
#ifdef _M_SSE
		return CheckAlphaRGBA4444SSE2(pixelData, stride, w, h);
#elif PPSSPP_ARCH(ARMV7) || PPSSPP_ARCH(ARM64)
		if (cpu_info.bNEON) {
			return CheckAlphaRGBA4444NEON(pixelData, stride, w, h);
		}
#endif
	}

	const u32 *p = pixelData;
	const int w2 = (w + 1) / 2;
	const int stride2 = (stride + 1) / 2;

	for (int y = 0; y < h; ++y) {
		u32 bits = 0xF000F000;
		for (int i = 0; i < w2; ++i) {
			bits &= p[i];
		}

		if (bits != 0xF000F000) {
			// We're done, we hit non-full alpha.
			return CHECKALPHA_ANY;
		}

		p += stride2;
	}

	return CHECKALPHA_FULL;
}

CheckAlphaResult CheckAlphaRGBA5551Basic(const u32 *pixelData, int stride, int w, int h) {
	// Use SSE if aligned to 16 bytes / 8 pixels (usually the case.)
	if ((w & 7) == 0 && (stride & 7) == 0) {
#ifdef _M_SSE
		return CheckAlphaRGBA5551SSE2(pixelData, stride, w, h);
#elif PPSSPP_ARCH(ARMV7) || PPSSPP_ARCH(ARM64)
		if (cpu_info.bNEON) {
			return CheckAlphaRGBA5551NEON(pixelData, stride, w, h);
		}
#endif
	}

	const u32 *p = pixelData;
	const int w2 = (w + 1) / 2;
	const int stride2 = (stride + 1) / 2;

	for (int y = 0; y < h; ++y) {
		u32 bits = 0x80008000;
		for (int i = 0; i < w2; ++i) {
			bits &= p[i];
		}

		if (bits != 0x80008000) {
			return CHECKALPHA_ANY;
		}

		p += stride2;
	}

	return CHECKALPHA_FULL;
}
