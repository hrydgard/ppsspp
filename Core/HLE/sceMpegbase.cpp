// Copyright (c) 2026- PPSSPP Project.

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

// sceMpegbase - the Media Engine's colour space conversion, and the DMA that feeds it.
//
// mpeg.prx drives these directly, so they have to be real for the firmware module to run in place
// of our sceMpeg HLE.

#include <algorithm>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Swap.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceMpegbase.h"
#include "Core/HLE/scePower.h"
#include "Core/HLE/sceVideocodec.h"
#include "Core/MemMapHelpers.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Common/Math/CrossSIMD.h"

// Set by sceMpegBaseCscInit / sceMpegBaseCscSetPixelMode, and used when the caller passes 0.
static int g_mpegBaseBufferWidth = 512;
static int g_mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888;

// Scratch for the planes the de-tiling produces. Reused between calls: a movie converts one of
// these every frame, and they are a couple of hundred kilobytes, so allocating them per call was
// pure overhead. Nothing here needs saving - it is rebuilt from the ME's buffers on every call.
static std::vector<u8> g_untileScratch;

void __MpegBaseInit() {
	// None of this survives a boot on hardware.
	g_untileScratch.clear();
	g_untileScratch.shrink_to_fit();
	g_mpegBaseBufferWidth = 512;
	g_mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888;
}

void __MpegBaseShutdown() {
	// The scratch is a few hundred kilobytes, and a game that played one video early on has no
	// use for it afterwards.
	g_untileScratch.clear();
	g_untileScratch.shrink_to_fit();
}

void __MpegBaseDoState(PointerWrap &p) {
	auto s = p.Section("sceMpegbase", 0, 2);
	if (!s) {
		return;
	}
	// The pixel mode decides both the colour packing and the bytes per pixel of the output, and a
	// game sets it once per movie rather than per frame - so without it here, a state resumed
	// mid-movie converted at the default until the next sceMpegBaseCscInit, which may never come.
	Do(p, g_mpegBaseBufferWidth);
	Do(p, g_mpegBasePixelMode);
	if (s < 2) {
		// Used to hold the gathered PES payloads. They live in Media Engine memory now, which
		// sceVideocodec saves, so read the old table to get past it and let it go.
		std::map<u32, std::vector<u8>> oldPesPackets;
		Do(p, oldPesPackets);
	}
}

// p pointing to a SceMpegLLI structure consists of video frame blocks.
static u32 sceMpegBasePESpacketCopy(u32 p)
{
	int nBlocks = 0;
	auto lli = PSPPointer<SceMpegLLI>::Create(p);
	while (lli.IsValid()) {
		nBlocks++;
		if (lli->Next == 0) {
			// Last block
			break;
		}
		++lli;
	}
	MpegSetPmpVideoSource(p, nBlocks);

	// The DMA copy. Each block specifies where in ME memory it lands. In some games that use very small
	// reads, multiple of these blocks will form one video frame packet - so we can't just keep track of
	// block addresses, we have to copy them to a simulated ME memory.
	lli = PSPPointer<SceMpegLLI>::Create(p);
	u32 firstDest = 0;
	int copied = 0;
	for (int i = 0; i < nBlocks && lli.IsValid(); i++) {
		if (i == 0) {
			firstDest = lli->pDst;
		}
		// The list is game-supplied, so check the span validity before taking a pointer to it.
		const u8 *src = (lli->iSize > 0 && Memory::IsValidRange(lli->pSrc, lli->iSize))
			? Memory::GetTypedPointerRange<u8>(lli->pSrc, lli->iSize) : nullptr;
		if (src) {
			if (u8 *me = MEGetPointerRange(lli->pDst, lli->iSize)) {
				memcpy(me, src, lli->iSize);
				copied += lli->iSize;
			} else if (Memory::IsValidRange(lli->pDst, lli->iSize)) {
				Memory::MemcpyUnchecked(lli->pDst, src, lli->iSize);
				copied += lli->iSize;
			} else {
				// Padding? Not sure what this is about. Doesn't seem to have any ill effects though.
				// Seen in the Kosmodrones homebrew.
				WARN_LOG(Log::Mpeg, "sceMpegBasePESpacketCopy: %d bytes to %08x went nowhere",
					(int)lli->iSize, (u32)lli->pDst);
			}
		}
		++lli;
	}

	DEBUG_LOG(Log::Mpeg, "sceMpegBasePESpacketCopy(%08x), %d block(s) -> %08x, %d bytes",
		p, nBlocks, firstDest, copied);
	return 0;
}


// --- sceMpegbase colour conversion ---------------------------------------------------------
//
// mpeg.prx hands the ME's decoded output to these to be converted to RGB. The descriptor it
// passes is 48 bytes, which matches the range mpegbase.prx bounds-checks before using it.
//
// mpeg.prx builds this on its own stack before each call (1.3 at 08805698, 1.8 at 08805898, the
// same shape) from the eight buffer addresses sceVideocodec published and the dimensions from its
// own context. mpegbase.prx reads the dimensions at 0x00/0x04 and the eight buffers at 0x10..0x2c.
//
// The buffers live either in Media Engine memory (a freshly decoded frame) or in the game's
// memory (once sceMpegBaseYCrCbCopy has moved one out).
struct SceMp4AvcCscStruct {
	s32_le height;        // 0x00  in macroblocks
	s32_le width;         // 0x04
	s32_le unk08;         // 0x08  1: the field in buffers 0/1 and 4/5 only (see MpegBaseCscRange)
	s32_le unk0c;         // 0x0c
	u32_le buffer[8];     // 0x10  four luma, then four chroma
};
static_assert(sizeof(SceMp4AvcCscStruct) == 0x30);

// Addresses in these descriptors are in Media Engine space. The exception is a frame that
// sceMpegBaseYCrCbCopy has moved into game's memory, which is why there is a second look.
// TODO: Although, this should really be revisited - the caller should know what space it's in.
static const u8 *MpegBaseFramePointer(u32 addr, int size) {
	if (const u8 *me = MEGetPointerRange(addr, size)) {
		return me;
	}
	return Memory::GetTypedPointerRange<u8>(addr, size);
}

// The ME doesn't write plain planar YCbCr. The layout below was established by analysing
// sceMpegBaseYCrCbCopy output on a real PSP (documented in JPCSP's sceVideocodec), and it uses
// all eight buffers in the descriptor:
//
//   The image is divided into vertical bands 32 pixels wide, each split into two 16-pixel halves.
//   Luma is one byte per pixel, and which buffer a row lands in depends on whether it is even or
//   odd:
//     buffer0: left half,  even rows        buffer1: right half, even rows
//     buffer2: left half,  odd rows         buffer3: right half, odd rows
//   Within a buffer, rows are stored 16 bytes at a time, band by band, top to bottom.
//
//   Chroma is one (Cb,Cr) byte pair per 2x2 pixel square, so in chroma coordinates the bands are
//   16 wide with 8-pixel halves, and the same even/odd split applies:
//     buffer4: left half,  even chroma rows  buffer5: right half, even chroma rows
//     buffer6: left half,  odd chroma rows   buffer7: right half, odd chroma rows
//
// Untangling it into plain planes costs one pass per frame, which keeps the conversion below
// readable and is not where the time goes.
// The de-tiling itself, with the address resolution left outside so it can be measured and
// checked on its own - see TestMpegCsc. src is the eight buffers in sceVideocodec order (four
// luma, then four chroma) and sizes says how big each one is.
//
// Every byte of the output is written for any frame the hardware can produce, so the caller does
// not have to clear it first.
void UntileYCbCr(u8 *luma, u8 *cb, u8 *cr, const u8 *const src[8], const int sizes[8],
	int width, int height) {
	const int width2 = width >> 1;
	const int height2 = height >> 1;
	const int *ySize = sizes;
	const int *cSize = sizes + 4;

	// Luma: four buffers, keyed by (left/right half of the band, even/odd row).
	for (int b = 0; b < 4; b++) {
		if (!src[b]) {
			continue;
		}
		const int xOffset = (b & 1) ? 16 : 0;
		const int yStart = (b >> 1) ? 1 : 0;
		int j = 0;
		for (int bandX = xOffset; bandX < width; bandX += 32) {
			const int run = std::min(16, width - bandX);
			for (int row = yStart; row < height; row += 2, j += 16) {
				if (run <= 0 || j + run > ySize[b]) {
					continue;
				}
				// A constant size gets inlined instead of a library call per 16 bytes.
				if (run == 16) {
					memcpy(luma + (size_t)row * width + bandX, src[b] + j, 16);
				} else {
					memcpy(luma + (size_t)row * width + bandX, src[b] + j, run);
				}
			}
		}
	}

	// Chroma: same shape in half-resolution coordinates, but the two planes arrive interleaved as
	// (Cb,Cr) pairs, so each group of 8 pixels is a 16-byte run to pull apart. The bounds the old
	// version checked per pixel only depend on the group, so they are hoisted out here - that inner
	// loop was the expensive half of this function.
	for (int b = 0; b < 4; b++) {
		if (!src[b + 4]) {
			continue;
		}
		const int xOffset = (b & 1) ? 8 : 0;
		const int yStart = (b >> 1) ? 1 : 0;
		int j = 0;
		for (int bandX = xOffset; bandX < width2; bandX += 16) {
			for (int row = yStart; row < height2; row += 2, j += 16) {
				// How many of the 8 fit both in the row and in what the buffer actually holds.
				const int fits = std::min(8, (cSize[b] - j) >> 1);
				const int run = std::min(width2 - bandX, fits);
				if (run <= 0) {
					continue;
				}
				const u8 *from = src[b + 4] + j;
				u8 *toCb = cb + (size_t)row * width2 + bandX;
				u8 *toCr = cr + (size_t)row * width2 + bandX;
				for (int k = 0; k < run; k++) {
					toCb[k] = from[k * 2];
					toCr[k] = from[k * 2 + 1];
				}
			}
		}
	}
}

bool ReadTiledYCbCr(const u32 *buffers, int width, int height,
	const u8 **luma, const u8 **cb, const u8 **cr, bool evenRowsOnly) {
	int sizes[8];
	VideocodecFrameBufferLayout(width, height, sizes, nullptr);

	const u8 *src[8]{};
	for (int i = 0; i < 8; i++) {
		// Buffers 2/3 and 6/7 are the odd rows.
		if (evenRowsOnly && (i & 2)) {
			continue;
		}
		if (sizes[i] > 0) {
			src[i] = MpegBaseFramePointer(buffers[i], sizes[i]);
			if (!src[i]) {
				return false;
			}
		}
	}

	const size_t lumaBytes = (size_t)width * height;
	const size_t chromaBytes = (size_t)(width >> 1) * (height >> 1);
	if (g_untileScratch.size() < lumaBytes + chromaBytes * 2) {
		g_untileScratch.resize(lumaBytes + chromaBytes * 2);
	}
	u8 *l = g_untileScratch.data();
	u8 *b = l + lumaBytes;
	u8 *r = b + chromaBytes;
	UntileYCbCr(l, b, r, src, sizes, width, height);
	*luma = l;
	*cb = b;
	*cr = r;
	return true;
}

// The layout sceMpegBaseCscVme takes: the two fields as plain planes, luma in buffers 0 and 1 and
// each field's Cb and Cr in 2 and 3, and 4 and 5 (pspautotests video/mpeg/cscvme). Woven back
// into a frame here, with each field's chroma on its own rows, as MpegBaseCscFields expects.
static bool ReadVmeFields(const u32 *buffers, int width, int height,
	const u8 **luma, const u8 **cb, const u8 **cr) {
	const int width2 = width >> 1;
	const int fieldHeight = height >> 1;
	const int fieldChromaHeight = fieldHeight >> 1;
	const size_t lumaBytes = (size_t)width * height;
	const size_t chromaBytes = (size_t)width2 * (height >> 1);
	const u8 *src[6];
	for (int i = 0; i < 6; i++) {
		src[i] = MpegBaseFramePointer(buffers[i], i < 2 ? width * fieldHeight : width2 * fieldChromaHeight);
		if (!src[i]) {
			return false;
		}
	}
	if (g_untileScratch.size() < lumaBytes + chromaBytes * 2) {
		g_untileScratch.resize(lumaBytes + chromaBytes * 2);
	}
	u8 *l = g_untileScratch.data();
	u8 *b = l + lumaBytes;
	u8 *r = b + chromaBytes;
	for (int parity = 0; parity < 2; parity++) {
		for (int y = 0; y < fieldHeight; y++) {
			memcpy(l + (size_t)(y * 2 + parity) * width, src[parity] + (size_t)y * width, width);
		}
		for (int y = 0; y < fieldChromaHeight; y++) {
			memcpy(b + (size_t)(y * 2 + parity) * width2, src[2 + parity * 2] + (size_t)y * width2, width2);
			memcpy(r + (size_t)(y * 2 + parity) * width2, src[3 + parity * 2] + (size_t)y * width2, width2);
		}
	}
	*luma = l;
	*cb = b;
	*cr = r;
	return true;
}

static u32 YCbCrToPixel(int y, int cbv, int crv, int pixelMode) {
	const int c = y - 16, d = cbv - 128, e = crv - 128;
	int r = (298 * c + 409 * e + 128) >> 8;
	int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
	int b = (298 * c + 516 * d + 128) >> 8;
	r = std::min(255, std::max(0, r));
	g = std::min(255, std::max(0, g));
	b = std::min(255, std::max(0, b));
	// Alpha comes out zero, not opaque. That is what the hardware does - our sceMpeg HLE masks it
	// off for the same reason, and names Sword Art Online as a game that depends on it, because it
	// doesn't clear the alpha in the buffer it hands over and expects the video to leave it clear.
	switch (pixelMode) {
	case GE_CMODE_16BIT_BGR5650:
		return ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
	case GE_CMODE_16BIT_ABGR5551:
		return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
	case GE_CMODE_16BIT_ABGR4444:
		return ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4);
	default:
		return (b << 16) | (g << 8) | r;
	}
}

// The conversion itself, with nothing around it. Pure, so that TestMpegCsc can measure it and
// check it - it is the hottest thing in video playback, and the point of having it out here is
// that it can be worked on without a game in the loop.
//
// luma is width by height; cb and cr are half that in both directions, as YUV420 is. dest is
// destStride pixels wide in the format pixelMode names, and the converted range always lands at
// its origin.
void MpegCscRangeScalar(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	const int bpp = pixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const int width2 = width >> 1;
	for (int y = 0; y < rangeHeight; y++) {
		const int sy = rangeY + y;
		for (int x = 0; x < rangeWidth; x++) {
			const int sx = rangeX + x;
			const int ci = (sy >> 1) * width2 + (sx >> 1);
			const u32 pixel = YCbCrToPixel(luma[sy * width + sx], cb[ci], cr[ci], pixelMode);
			if (bpp == 4) {
				memcpy(dest + (y * destStride + x) * 4, &pixel, 4);
			} else {
				const u16 p16 = (u16)pixel;
				memcpy(dest + (y * destStride + x) * 2, &p16, 2);
			}
		}
	}
}

#if PPSSPP_ARCH(SSE2) || PPSSPP_ARCH(ARM_NEON)

// Eight pixels of YCbCrToPixel at a time, computing the same integers, so the result is identical
// to the scalar path's. y points at eight luma samples and cb/cr at the four chroma samples under
// them.
//
// 298 * c doesn't fit in 16 bits, but each coefficient splits into a multiple of 256 and a small
// remainder, and the multiple of 256 comes through the shift whole: (298c + 409e + 128) >> 8 is
// c + e + ((42c + 153e + 128) >> 8). The sums in parentheses stay within +-30000 for any input, so
// all of it is 16-bit lanes, eight pixels per instruction.
//   r = c + e  + ((42c + 153e + 128) >> 8)          (298c + 409e = 256(c + e) + 42c + 153e)
//   g = c - e  + ((42c - 100d + 48e + 128) >> 8)    (298c - 100d - 208e = 256(c - e) + 42c - 100d + 48e)
//   b = c + 2d + ((42c + 4d + 128) >> 8)            (298c + 516d = 256(c + 2d) + 42c + 4d)
#if !PPSSPP_ARCH(SSE2)
static inline void CscEightNeon(u8 *dest, int pixelMode, uint8x8_t y8, uint8x8_t cb8, uint8x8_t cr8);
#endif
static inline void CscEight(u8 *dest, int pixelMode, const u8 *y, const u8 *cb, const u8 *cr) {
#if PPSSPP_ARCH(SSE2)
	const __m128i zero = _mm_setzero_si128();
	const __m128i c = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)y), zero), _mm_set1_epi16(16));
	__m128i cb8 = _mm_cvtsi32_si128((int)LoadU32Unaligned(cb));
	__m128i cr8 = _mm_cvtsi32_si128((int)LoadU32Unaligned(cr));
	// Each chroma sample covers two pixels.
	cb8 = _mm_unpacklo_epi8(cb8, cb8);
	cr8 = _mm_unpacklo_epi8(cr8, cr8);
	const __m128i d = _mm_sub_epi16(_mm_unpacklo_epi8(cb8, zero), _mm_set1_epi16(128));
	const __m128i e = _mm_sub_epi16(_mm_unpacklo_epi8(cr8, zero), _mm_set1_epi16(128));

	auto mul = [](__m128i v, int k) { return _mm_mullo_epi16(v, _mm_set1_epi16((short)k)); };
	const __m128i c42 = _mm_add_epi16(mul(c, 42), _mm_set1_epi16(128));
	__m128i r = _mm_add_epi16(_mm_add_epi16(c, e), _mm_srai_epi16(_mm_add_epi16(c42, mul(e, 153)), 8));
	__m128i g = _mm_add_epi16(_mm_sub_epi16(c, e), _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(c42, mul(d, -100)), mul(e, 48)), 8));
	__m128i b = _mm_add_epi16(_mm_add_epi16(c, _mm_add_epi16(d, d)), _mm_srai_epi16(_mm_add_epi16(c42, _mm_slli_epi16(d, 2)), 8));
	const __m128i max = _mm_set1_epi16(255);
	r = _mm_min_epi16(_mm_max_epi16(r, zero), max);
	g = _mm_min_epi16(_mm_max_epi16(g, zero), max);
	b = _mm_min_epi16(_mm_max_epi16(b, zero), max);

	switch (pixelMode) {
	case GE_CMODE_16BIT_BGR5650:
		_mm_storeu_si128((__m128i *)dest, _mm_or_si128(_mm_or_si128(
			_mm_slli_epi16(_mm_srli_epi16(b, 3), 11), _mm_slli_epi16(_mm_srli_epi16(g, 2), 5)), _mm_srli_epi16(r, 3)));
		break;
	case GE_CMODE_16BIT_ABGR5551:
		_mm_storeu_si128((__m128i *)dest, _mm_or_si128(_mm_or_si128(
			_mm_slli_epi16(_mm_srli_epi16(b, 3), 10), _mm_slli_epi16(_mm_srli_epi16(g, 3), 5)), _mm_srli_epi16(r, 3)));
		break;
	case GE_CMODE_16BIT_ABGR4444:
		_mm_storeu_si128((__m128i *)dest, _mm_or_si128(_mm_or_si128(
			_mm_slli_epi16(_mm_srli_epi16(b, 4), 8), _mm_slli_epi16(_mm_srli_epi16(g, 4), 4)), _mm_srli_epi16(r, 4)));
		break;
	default:
	{
		const __m128i rg = _mm_or_si128(r, _mm_slli_epi16(g, 8));
		_mm_storeu_si128((__m128i *)dest, _mm_unpacklo_epi16(rg, b));
		_mm_storeu_si128((__m128i *)(dest + 16), _mm_unpackhi_epi16(rg, b));
		break;
	}
	}
#else
	// Each chroma sample covers two pixels.
	const uint8x8_t cb4 = vreinterpret_u8_u32(vdup_n_u32(LoadU32Unaligned(cb)));
	const uint8x8_t cr4 = vreinterpret_u8_u32(vdup_n_u32(LoadU32Unaligned(cr)));
	CscEightNeon(dest, pixelMode, vld1_u8(y), vzip_u8(cb4, cb4).val[0], vzip_u8(cr4, cr4).val[0]);
#endif
}

#if !PPSSPP_ARCH(SSE2)
// CscEight's arithmetic, with the chroma already spread over the eight pixels.
static inline void CscEightNeon(u8 *dest, int pixelMode, uint8x8_t y8, uint8x8_t cb8, uint8x8_t cr8) {
	const int16x8_t c = vreinterpretq_s16_u16(vsubl_u8(y8, vdup_n_u8(16)));
	const int16x8_t d = vreinterpretq_s16_u16(vsubl_u8(cb8, vdup_n_u8(128)));
	const int16x8_t e = vreinterpretq_s16_u16(vsubl_u8(cr8, vdup_n_u8(128)));
	const int16x8_t c42 = vmlaq_n_s16(vdupq_n_s16(128), c, 42);
	// vqmovun is the clamp to 0..255.
	const uint8x8_t r = vqmovun_s16(vaddq_s16(vaddq_s16(c, e), vshrq_n_s16(vmlaq_n_s16(c42, e, 153), 8)));
	const uint8x8_t g = vqmovun_s16(vaddq_s16(vsubq_s16(c, e), vshrq_n_s16(vmlaq_n_s16(vmlaq_n_s16(c42, d, -100), e, 48), 8)));
	const uint8x8_t b = vqmovun_s16(vaddq_s16(vaddq_s16(c, vshlq_n_s16(d, 1)), vshrq_n_s16(vmlaq_n_s16(c42, d, 4), 8)));

	switch (pixelMode) {
	case GE_CMODE_16BIT_BGR5650:
	{
		uint16x8_t p = vshll_n_u8(b, 8);
		p = vsriq_n_u16(p, vshll_n_u8(g, 8), 5);
		p = vsriq_n_u16(p, vshll_n_u8(r, 8), 11);
		vst1q_u16((u16 *)dest, p);
		break;
	}
	case GE_CMODE_16BIT_ABGR5551:
	{
		uint16x8_t p = vshrq_n_u16(vshll_n_u8(b, 8), 1);
		p = vsriq_n_u16(p, vshll_n_u8(g, 8), 6);
		p = vsriq_n_u16(p, vshll_n_u8(r, 8), 11);
		vst1q_u16((u16 *)dest, p);
		break;
	}
	case GE_CMODE_16BIT_ABGR4444:
	{
		uint16x8_t p = vshrq_n_u16(vshll_n_u8(b, 8), 4);
		p = vsriq_n_u16(p, vshll_n_u8(g, 8), 8);
		p = vsriq_n_u16(p, vshll_n_u8(r, 8), 12);
		vst1q_u16((u16 *)dest, p);
		break;
	}
	default:
	{
		uint8x8x4_t p;
		p.val[0] = r;
		p.val[1] = g;
		p.val[2] = b;
		p.val[3] = vdup_n_u8(0);
		vst4_u8(dest, p);
		break;
	}
	}
}
#endif

#endif

void MpegCscRange(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
#if PPSSPP_ARCH(SSE2) || PPSSPP_ARCH(ARM_NEON)
	// An odd left edge starts half a chroma sample in. Nothing asks for one (ranges arrive in
	// macroblocks), so it's left to the scalar path.
	if (!(rangeX & 1)) {
		const int bpp = pixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
		const int width2 = width >> 1;
		const int simdWidth = rangeWidth & ~7;
		for (int y = 0; y < rangeHeight; y++) {
			const int sy = rangeY + y;
			const u8 *lumaRow = luma + (size_t)sy * width + rangeX;
			const size_t chromaOffset = (size_t)(sy >> 1) * width2 + (rangeX >> 1);
			u8 *destRow = dest + (size_t)y * destStride * bpp;
			for (int x = 0; x < simdWidth; x += 8) {
				CscEight(destRow + x * bpp, pixelMode, lumaRow + x, cb + chromaOffset + (x >> 1), cr + chromaOffset + (x >> 1));
			}
			if (simdWidth < rangeWidth) {
				MpegCscRangeScalar(destRow + simdWidth * bpp, destStride, pixelMode, luma, cb, cr, width,
					rangeX + simdWidth, sy, rangeWidth - simdWidth, 1);
			}
		}
		return;
	}
#endif
	MpegCscRangeScalar(dest, destStride, pixelMode, luma, cb, cr, width,
		rangeX, rangeY, rangeWidth, rangeHeight);
}

// The field-splitting cases of MpegBaseCscRange (see there). Each field is converted as a picture
// of its own, with its own chroma: in interlaced 4:2:0 the top field's chroma is rows 0, 2, 4... and
// the bottom field's 1, 3, 5..., which is how the ME lays them out (buffers 4/5 and 6/7), and why a
// caller that copied only the top field's buffers can convert it.
static int MpegBaseCscFields(u32 bufferRGB, u32 bufferRGB2, bool topFieldOnly, int bufferWidth, int bpp,
	const u8 *luma, const u8 *cb, const u8 *cr, int width, int height, int rangeX, int rangeY, int rangeWidth, int rangeHeight,
	bool scalar = false) {
	const u32 rowBytes = (u32)(bufferWidth * bpp);
	const int width2 = width >> 1;
	const int fieldHeight = height >> 1;

	u32 rowsWritten = 0;
	// The odd field first, so that the even one is what's left if both go to the same buffer.
	for (int parity = 1; parity >= 0; parity--) {
		if (parity == 1 && topFieldOnly) {
			continue;
		}
		// The field's rows within the range (frame row y is row y >> 1 of its field).
		const int first = (rangeY + ((parity - rangeY) & 1)) >> 1;
		const int end = (rangeY + rangeHeight - parity + 1) >> 1;
		const int rows = std::min(end, fieldHeight) - first;
		if (rows <= 0) {
			continue;
		}
		u32 addr;
		int destStride;
		if (bufferRGB2 != 0) {
			// Packed: the field's rows one after another in their own buffer.
			addr = parity ? bufferRGB2 : bufferRGB;
			destStride = bufferWidth;
		} else {
			// In place, at the frame rows it came from.
			addr = bufferRGB + (u32)(first * 2 + parity - rangeY) * rowBytes;
			destStride = bufferWidth * 2;
		}
		const u32 span = (u32)(rows - 1) * destStride * bpp + rangeWidth * bpp;
		if (!Memory::IsValidRange(addr, span)) {
			return hleLogError(Log::Mpeg, -1, "output at %08x not writable", addr);
		}
		// A field is every other row of the frame, so it's the frame read at twice the stride: that
		// doubles the chroma stride too, which picks every other chroma row, the field's own.
		auto convert = scalar ? &MpegCscRangeScalar : &MpegCscRange;
		convert(Memory::GetPointerWriteUnchecked(addr), destStride, g_mpegBasePixelMode,
			luma + (size_t)parity * width, cb + (size_t)parity * width2, cr + (size_t)parity * width2, width * 2,
			rangeX, first, rangeWidth, rows);
		rowsWritten += rows;
	}

	const u32 extent = (bufferRGB2 != 0 ? (u32)((rangeHeight + 1) >> 1) : (u32)rangeHeight) * rowBytes;
	const bool second = bufferRGB2 != 0 && bufferRGB2 != bufferRGB && !topFieldOnly;
	// Fields packed back to back (as the VSH's Motion JPEG player has them) are one picture to the
	// GPU. Telling it about them separately makes the first a framebuffer of its own, and a texture
	// over the whole picture stops at it.
	const bool adjacent = second && bufferRGB2 == bufferRGB + extent;
	NotifyMemInfo(MemBlockFlags::WRITE, bufferRGB, extent, "MpegBaseCsc");
	gpu->PerformWriteFormattedFromMemory(bufferRGB, adjacent ? extent * 2 : extent, bufferWidth, (GEBufferFormat)g_mpegBasePixelMode, rangeWidth);
	if (second) {
		NotifyMemInfo(MemBlockFlags::WRITE, bufferRGB2, extent, "MpegBaseCsc");
		if (!adjacent) {
			gpu->PerformWriteFormattedFromMemory(bufferRGB2, extent, bufferWidth, (GEBufferFormat)g_mpegBasePixelMode, rangeWidth);
		}
	}
	const int cscUs = std::max(1, PowerScaleFromDefaultClock((int)(2400LL * rangeWidth * rowsWritten / (480 * 272))));
	return hleDelayResult(hleLogDebug(Log::Mpeg, 0, "%dx%d at %d,%d -> %08x/%08x stride %d, %s",
		rangeWidth, rangeHeight, rangeX, rangeY, bufferRGB, bufferRGB2, bufferWidth, topFieldOnly ? "top field" : "both fields"), "mpegbase csc", cscUs);
}

// The shared body of sceMpegBaseCscAvc and sceMpegBaseCscAvcRange - the former is just the
// latter over the whole frame.
// Where the converted rows go (pspautotests video/mpeg/cscavc). With no second buffer, every row
// lands where it is in the frame, and +0x08 in the descriptor leaves out the odd ones. With one, the
// fields are split: the even rows packed together into bufferRGB and the odd ones into the second,
// and +0x08 leaves the odd field out. When both fields go to the same buffer, the even one is what's
// left.
//
// The VSH's UMD Video player converts one field of a 720x480 picture per vsync, alternating, into
// 240 rows: the same buffer twice with +0x08 set, in vertical strips. It gets the bottom field by
// passing the descriptor with the field buffers swapped (2/3 and 6/7 in the places of 0/1 and 4/5),
// so the "even" field converted here is whichever one it put first.
static int MpegBaseCscRange(u32 bufferRGB, u32 bufferRGB2, u32 cscAddr, int bufferWidth,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	auto csc = PSPPointer<SceMp4AvcCscStruct>::Create(cscAddr);
	if (!csc.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "bad csc struct pointer");
	}
	if (bufferWidth == 0) {
		bufferWidth = g_mpegBaseBufferWidth;
	}
	// The stride goes into the output size and row addresses, so keep that math in range.
	if (bufferWidth < 0 || bufferWidth > 4096) {
		return hleLogError(Log::Mpeg, -1, "bad buffer width %d", bufferWidth);
	}

	u32 buffers[8]{};
	for (int i = 0; i < 8; i++) {
		buffers[i] = csc->buffer[i];
	}
	const int width = csc->width << 4;
	const int height = csc->height << 4;
	if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
		return hleLogError(Log::Mpeg, -1, "unreasonable frame size %dx%d", width, height);
	}
	if (rangeWidth <= 0 || rangeHeight <= 0) {
		return hleLogDebug(Log::Mpeg, 0, "empty range");
	}
	rangeWidth = std::min(rangeWidth, width - rangeX);
	rangeHeight = std::min(rangeHeight, height - rangeY);
	// The output buffer is sized from the stride below, so a row can't be wider than one. Every
	// game seen so far passes a stride comfortably wider than the frame (512 for 480), but nothing
	// guarantees it, and writing a wider row than we measured would run off the end of the buffer.
	rangeWidth = std::min(rangeWidth, bufferWidth);
	if (rangeX < 0 || rangeY < 0 || rangeWidth <= 0 || rangeHeight <= 0) {
		return hleLogError(Log::Mpeg, -1, "range outside the frame");
	}

	const bool topFieldOnly = csc->unk08 != 0;
	const u8 *luma, *cb, *cr;
	// Only the even rows are converted with +0x08 set, and the UMD Video player doesn't keep the
	// other field's buffers up to date in the copy it converts from anyway.
	if (!ReadTiledYCbCr(buffers, width, height, &luma, &cb, &cr, topFieldOnly)) {
		return hleLogError(Log::Mpeg, -1, "YCbCr buffers not readable");
	}

	const int bpp = g_mpegBasePixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	if (bufferRGB2 != 0 || topFieldOnly) {
		return MpegBaseCscFields(bufferRGB, bufferRGB2, topFieldOnly, bufferWidth, bpp, luma, cb, cr, width, height,
			rangeX, rangeY, rangeWidth, rangeHeight);
	}
	const u32 destSize = (u32)(rangeHeight * bufferWidth * bpp);
	if (!Memory::IsValidRange(bufferRGB, destSize)) {
		return hleLogError(Log::Mpeg, -1, "output buffer not writable");
	}
	u8 *dest = Memory::GetTypedPointerWriteRange<u8>(bufferRGB, destSize);
	if (!dest) {
		return hleLogError(Log::Mpeg, -1, "output buffer not writable");
	}

	MpegCscRange(dest, bufferWidth, g_mpegBasePixelMode, luma, cb, cr, width,
		rangeX, rangeY, rangeWidth, rangeHeight);
	NotifyMemInfo(MemBlockFlags::WRITE, bufferRGB, destSize, "MpegBaseCsc");
	// The CPU just wrote a video frame into what is usually a display buffer. The hardware backends
	// don't see that on their own, so without telling them the screen keeps showing the last frame
	// the GE drew (the same notification our sceMpegAvcCsc HLE does). The pixel mode numbering
	// matches GEBufferFormat, as it does there.
	gpu->PerformWriteFormattedFromMemory(bufferRGB, destSize, bufferWidth, (GEBufferFormat)g_mpegBasePixelMode, rangeWidth);
	// This runs on the DMACPLUS and takes real time. A psmfplayer game blits the current video
	// frame every render frame while it waits for the next, so an instant return is a tight loop
	// that never yields and starves the audio thread that paces playback, and the A/V pipeline
	// deadlocks a few frames in (SOCOM: Tactical Strike hangs exactly here). Our sceMpeg HLE delays
	// sceMpegAvcCsc the same way. On a PSP, sceMpegAvcCsc of a 480x272 frame to 8888 takes 2.4ms
	// (pspautotests video/mpeg/playertiming).
	const int cscUs = std::max(1, PowerScaleFromDefaultClock((int)(2400LL * rangeWidth * rangeHeight / (480 * 272))));
	return hleDelayResult(hleLogDebug(Log::Mpeg, 0, "%dx%d at %d,%d -> %08x stride %d",
		rangeWidth, rangeHeight, rangeX, rangeY, bufferRGB, bufferWidth), "mpegbase csc", cscUs);
}

static int sceMpegBaseCscInit(int bufferWidth) {
	g_mpegBaseBufferWidth = bufferWidth ? bufferWidth : 512;
	return hleLogInfo(Log::Mpeg, 0, "bufferWidth %d", bufferWidth);
}

// sceMpegBaseCscInit with the colour conversion matrix passed in: in mpegbase_260.prx, CscInit is this
// function called with its built-in 22-byte table (mpegbase+0x1804). The VSH's video player passes
// its own. We convert with the standard matrix whatever this says.
static int sceMpegBaseCscInitMatrix(int bufferWidth, u32 matrixAddr) {
	g_mpegBaseBufferWidth = bufferWidth ? bufferWidth : 512;
	if (!Memory::IsValidRange(matrixAddr, 22)) {
		return hleLogError(Log::Mpeg, -1, "bad matrix pointer");
	}
	char hex[22 * 2 + 1];
	for (int i = 0; i < 22; i++) {
		snprintf(hex + i * 2, 3, "%02x", Memory::ReadUnchecked_U8(matrixAddr + i));
	}
	return hleLogInfo(Log::Mpeg, 0, "bufferWidth %d, matrix %s", bufferWidth, hex);
}

// Sets the output pixel format for the conversions below. The official name isn't known - this one
// says what it does. Inside mpegbase.prx it stores the value and hands it to the DMACPLUS colour
// conversion hardware, and mpeg.prx gets it by indexing a table of {1, 2, 3, 0} with the pixel mode
// the game passed to sceMpegAvcDecodeMode. So the numbering is its own, not the GE's.
static int sceMpegBaseCscSetPixelMode(int pixelMode) {
	static const int toGeMode[4] = {
		GE_CMODE_32BIT_ABGR8888,
		GE_CMODE_16BIT_BGR5650,
		GE_CMODE_16BIT_ABGR5551,
		GE_CMODE_16BIT_ABGR4444,
	};
	if (pixelMode < 0 || pixelMode >= (int)ARRAY_SIZE(toGeMode)) {
		return hleLogError(Log::Mpeg, -1, "bad pixel mode %d", pixelMode);
	}
	g_mpegBasePixelMode = toGeMode[pixelMode];
	return hleLogInfo(Log::Mpeg, 0, "pixel mode %d -> GE mode %d", pixelMode, g_mpegBasePixelMode);
}

static int sceMpegBaseCscAvc(u32 bufferRGB, u32 bufferRGB2, int bufferWidth, u32 cscAddr) {
	auto csc = PSPPointer<SceMp4AvcCscStruct>::Create(cscAddr);
	if (!csc.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "bad csc struct pointer");
	}
	// The whole frame. MpegBaseCscRange clamps to the real frame size (from the allocation, not the
	// descriptor), so pass more than any frame can be.
	return MpegBaseCscRange(bufferRGB, bufferRGB2, cscAddr, bufferWidth, 0, 0, 1024, 1024);
}

static u32 sceMpegBaseCscAvcRange(u32 bufferRGB, u32 bufferRGB2, u32 rangeAddr, int bufferWidth, u32 cscAddr) {
	if (!Memory::IsValidRange(rangeAddr, 16)) {
		return hleLogError(Log::Mpeg, -1, "bad range pointer");
	}
	// Also in macroblocks.
	const int rangeX = Memory::ReadUnchecked_U32(rangeAddr + 0) << 4;
	const int rangeY = Memory::ReadUnchecked_U32(rangeAddr + 4) << 4;
	const int rangeWidth = Memory::ReadUnchecked_U32(rangeAddr + 8) << 4;
	const int rangeHeight = Memory::ReadUnchecked_U32(rangeAddr + 12) << 4;
	return MpegBaseCscRange(bufferRGB, bufferRGB2, cscAddr, bufferWidth, rangeX, rangeY, rangeWidth, rangeHeight);
}

// Takes the same arguments as sceMpegBaseCscAvc, with a descriptor for planar fields (see
// ReadVmeFields) and the same dimensions in macroblocks. The official name isn't known; JPCSP calls
// it this. The rows land as they do for sceMpegBaseCscAvc with two buffers or none: the fields packed
// into one each, or woven together in the first. The VSH's video player passes the second buffer
// right after the first, with the top half of a picture as the first field and the bottom half as
// the second, so its picture comes out whole. The colours match our scalar conversion exactly
// (pspautotests video/mpeg/cscvme), and the frames are small, so that's what this uses.
static int sceMpegBaseCscVme(u32 bufferRGB, u32 bufferRGB2, int bufferWidth, u32 cscAddr) {
	if (!Memory::IsValidRange(cscAddr, 0x28)) {
		return hleLogError(Log::Mpeg, -1, "bad csc struct pointer");
	}
	if (bufferWidth == 0) {
		bufferWidth = g_mpegBaseBufferWidth;
	}
	// The stride goes into the output size and row addresses, so keep that math in range.
	if (bufferWidth < 0 || bufferWidth > 4096) {
		return hleLogError(Log::Mpeg, -1, "bad buffer width %d", bufferWidth);
	}
	auto csc = PSPPointer<SceMp4AvcCscStruct>::Create(cscAddr);
	u32 buffers[6];
	for (int i = 0; i < 6; i++) {
		buffers[i] = csc->buffer[i];
	}
	const int width = csc->width << 4;
	const int height = csc->height << 4;
	if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
		return hleLogError(Log::Mpeg, -1, "unreasonable frame size %dx%d", width, height);
	}
	const u8 *luma, *cb, *cr;
	if (!ReadVmeFields(buffers, width, height, &luma, &cb, &cr)) {
		return hleLogError(Log::Mpeg, -1, "YCbCr buffers not readable");
	}
	const int bpp = g_mpegBasePixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	return MpegBaseCscFields(bufferRGB, bufferRGB2, false, bufferWidth, bpp, luma, cb, cr, width, height,
		0, 0, std::min(width, bufferWidth), height, true);
}

// Moves a decoded frame between two sets of buffers. Both arguments are descriptors; what gets
// copied is the pixels they point at, not the descriptors.
//
// mpegbase.prx builds a DMA list over the eight buffers (080010f8 in mpegbase_260.prx): flag bit 0
// selects buffers 0,1,4,5 and bit 1 selects 2,3,6,7, with the per-buffer sizes sceVideocodec lays
// its frame out with. mpeg.prx always passes 3 (all eight). The source is the ME's frame; the
// destination is wherever the caller wants it (for psmfplayer, a slot in its output pool).
static int sceMpegBaseYCrCbCopy(u32 dstAddr, u32 srcAddr, int flags) {
	auto dst = PSPPointer<SceMp4AvcCscStruct>::Create(dstAddr);
	auto src = PSPPointer<SceMp4AvcCscStruct>::Create(srcAddr);
	if (!dst.IsValid() || !src.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "bad descriptor pointer");
	}

	// The size comes from the source descriptor, in pixels, which is how sceVideocodec states it.
	// The destination's can hold anything (the VSH's video player leaves it unset).
	const int width = src->width;
	const int height = src->height;
	if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
		return hleLogError(Log::Mpeg, -1, "unreasonable frame size %dx%d", width, height);
	}
	int sizes[8];
	VideocodecFrameBufferLayout(width, height, sizes, nullptr);
	// mpegbase.prx's own limit.
	if (sizes[0] > 0xBFF40) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_SIZE, "frame too large (%dx%d)", width, height);
	}

	int copied = 0;
	for (int i = 0; i < 8; i++) {
		// Buffers 0,1,4,5 go with bit 0 and 2,3,6,7 with bit 1 (the even and odd row halves of luma
		// and chroma).
		const int bit = ((i & 3) < 2) ? 1 : 2;
		if (!(flags & bit) || sizes[i] <= 0) {
			continue;
		}
		const u8 *from = MpegBaseFramePointer(src->buffer[i], sizes[i]);
		if (!from) {
			return hleLogError(Log::Mpeg, -1, "source buffer %d not readable", i);
		}
		if (!Memory::IsValidRange(dst->buffer[i], sizes[i])) {
			return hleLogError(Log::Mpeg, -1, "destination buffer %d (%08x, %d bytes) not writable",
				i, (u32)dst->buffer[i], sizes[i]);
		}
		Memory::MemcpyUnchecked(dst->buffer[i], from, sizes[i]);
		copied += sizes[i];
	}
	return hleLogDebug(Log::Mpeg, 0, "flags %d, %dx%d, %d bytes", flags, width, height, copied);
}

// sceMpegBaseYCrCbCopy for the six buffers sceMpegBaseCscVme takes (official name unknown, JPCSP's).
// Bit 0 of the flags copies buffers 0, 2 and 3 (the first field), bit 1 buffers 1, 4 and 5. The sizes
// are worked out as mpegbase_260.prx does it (08001210), from the source descriptor's height and
// width in pixels, and run a little past the planes they cover (pspautotests video/mpeg/cscvme).
static int sceMpegBaseYCrCbCopyVme(u32 dstAddr, u32 srcAddr, int flags) {
	if (!Memory::IsValidRange(dstAddr, 0x28) || !Memory::IsValidRange(srcAddr, 0x28)) {
		return hleLogError(Log::Mpeg, -1, "bad descriptor pointer");
	}
	auto dst = PSPPointer<SceMp4AvcCscStruct>::Create(dstAddr);
	auto src = PSPPointer<SceMp4AvcCscStruct>::Create(srcAddr);
	const u32 height = src->height;
	const u32 width = src->width;
	const u32 blocks = (width >> 4) + ((width & 0x7F) == 0x50 ? 3 : 2);
	const u32 lumaSize = blocks * height * 8;
	const u32 chromaSize = blocks * height * 2;
	if (lumaSize > 0xBFF40) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_SIZE, "frame too large (%dx%d)", width, height);
	}

	int copied = 0;
	for (int i = 0; i < 6; i++) {
		const int bit = (i == 0 || i == 2 || i == 3) ? 1 : 2;
		if (!(flags & bit)) {
			continue;
		}
		const u32 size = i < 2 ? lumaSize : chromaSize;
		const u8 *from = MpegBaseFramePointer(src->buffer[i], size);
		if (!from) {
			return hleLogError(Log::Mpeg, -1, "source buffer %d not readable", i);
		}
		if (!Memory::IsValidRange(dst->buffer[i], size)) {
			return hleLogError(Log::Mpeg, -1, "destination buffer %d (%08x, %d bytes) not writable", i, (u32)dst->buffer[i], size);
		}
		Memory::MemcpyUnchecked(dst->buffer[i], from, size);
		copied += size;
	}
	return hleLogDebug(Log::Mpeg, 0, "flags %d, %dx%d, %d bytes", flags, width, height, copied);
}

const HLEFunction sceMpegbase[] =
{
	{0XBEA18F91, &WrapU_U<sceMpegBasePESpacketCopy>,           "sceMpegBasePESpacketCopy",           'x', "x"      },
	{0X492B5E4B, &WrapI_I<sceMpegBaseCscInit>,                  "sceMpegBaseCscInit",                 'i', "i"      },
	{0X0530BE4E, &WrapI_I<sceMpegBaseCscSetPixelMode>,          "sceMpegbase_0530BE4E",               'i', "i"      },
	{0X91929A21, &WrapI_UUIU<sceMpegBaseCscAvc>,               "sceMpegBaseCscAvc",                  'i', "xxix"   },
	{0X304882E1, &WrapU_UUUIU<sceMpegBaseCscAvcRange>,         "sceMpegBaseCscAvcRange",             'x', "xxxix"  },
	{0X7AC0321A, &WrapI_UUI<sceMpegBaseYCrCbCopy>,             "sceMpegBaseYCrCbCopy",               'i', "xxi"    },
	{0XAC9E717E, &WrapI_IU<sceMpegBaseCscInitMatrix>,          "sceMpegBaseCscInitMatrix",           'i', "ix"     },
	{0XBE45C284, &WrapI_UUI<sceMpegBaseYCrCbCopyVme>,          "sceMpegBaseYCrCbCopyVme",            'i', "xxi"    },
	{0XCE8EB837, &WrapI_UUIU<sceMpegBaseCscVme>,               "sceMpegBaseCscVme",                  'i', "xxix"   },
};

void Register_sceMpegbase()
{
	RegisterHLEModule("sceMpegbase", ARRAY_SIZE(sceMpegbase), sceMpegbase);
};
