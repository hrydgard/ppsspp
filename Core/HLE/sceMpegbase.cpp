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
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceMpegbase.h"
#include "Core/HLE/sceVideocodec.h"
#include "Core/MemMapHelpers.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#ifdef USE_FFMPEG
extern "C" {
#include "libswscale/swscale.h"
#include "libavutil/pixfmt.h"
}
#endif

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
	MpegCscShutdown();
	g_mpegBaseBufferWidth = 512;
	g_mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888;
}

void __MpegBaseShutdown() {
	// The scratch and the swscale context are worth a few hundred kilobytes between them, and a
	// game that played one video early on has no use for either afterwards.
	g_untileScratch.clear();
	g_untileScratch.shrink_to_fit();
	MpegCscShutdown();
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
	s32_le unk08;         // 0x08
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
				memcpy(luma + (size_t)row * width + bandX, src[b] + j, run);
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
	const u8 **luma, const u8 **cb, const u8 **cr) {
	int sizes[8];
	VideocodecFrameBufferLayout(width, height, sizes, nullptr);

	const u8 *src[8]{};
	for (int i = 0; i < 8; i++) {
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

#ifdef USE_FFMPEG

// swscale is what our sceMpeg HLE converts with, and the planes the de-tiling produces are
// already the YUV420P it wants, so the same thing works here - and it is a great deal quicker
// than doing it a pixel at a time.
//
// The four output formats are the ones MediaEngine::getSwsFormat picks, for the same reasons.
// Alpha is not among them: swscale writes RGBA opaque and leaves the spare bits of the 16-bit
// formats clear, so the masking below is what makes the result match the hardware, exactly as
// the HLE does after its own sws_scale.
static AVPixelFormat SwsFormatForPixelMode(int pixelMode) {
	switch (pixelMode) {
	case GE_CMODE_16BIT_BGR5650: return AV_PIX_FMT_BGR565LE;
	case GE_CMODE_16BIT_ABGR5551: return AV_PIX_FMT_BGR555LE;
	case GE_CMODE_16BIT_ABGR4444: return AV_PIX_FMT_BGR444LE;
	default: return AV_PIX_FMT_RGBA;
	}
}

// Nothing is being scaled here, so this only picks how chroma reaches full resolution: SWS_POINT
// repeats each 2x2 block's sample, as the scalar path and presumably the hardware do, while
// SWS_BILINEAR smooths between samples, as our sceMpeg HLE does. Swap the line to taste - it
// deserves a real option eventually.
static const int MPEG_CSC_SWS_FLAGS = SWS_POINT;

static SwsContext *g_cscSws;
static int g_cscSwsWidth, g_cscSwsHeight, g_cscSwsFormat = -1;

void MpegCscShutdown() {
	if (g_cscSws) {
		sws_freeContext(g_cscSws);
		g_cscSws = nullptr;
	}
	g_cscSwsWidth = 0;
	g_cscSwsHeight = 0;
	g_cscSwsFormat = -1;
}

bool MpegCscRangeSws(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	// Chroma is half resolution, so an odd origin would start half a sample in and there is no way
	// to say that to swscale. Nothing can actually ask for one - the ranges arrive in macroblocks -
	// but the scalar path is still there for it.
	if ((rangeX & 1) || (rangeY & 1)) {
		return false;
	}

	const AVPixelFormat format = SwsFormatForPixelMode(pixelMode);
	if (rangeWidth != g_cscSwsWidth || rangeHeight != g_cscSwsHeight || (int)format != g_cscSwsFormat) {
		g_cscSws = sws_getCachedContext(g_cscSws, rangeWidth, rangeHeight, AV_PIX_FMT_YUV420P,
			rangeWidth, rangeHeight, format, MPEG_CSC_SWS_FLAGS, nullptr, nullptr, nullptr);
		if (!g_cscSws) {
			return false;
		}
		// Studio swing both ways, which is the range the coefficients in the scalar path assume.
		int *invCoeff, *coeff, srcRange, dstRange, brightness, contrast, saturation;
		if (sws_getColorspaceDetails(g_cscSws, &invCoeff, &srcRange, &coeff, &dstRange, &brightness,
			&contrast, &saturation) != -1) {
			sws_setColorspaceDetails(g_cscSws, invCoeff, 0, coeff, 0, brightness, contrast, saturation);
		}
		g_cscSwsWidth = rangeWidth;
		g_cscSwsHeight = rangeHeight;
		g_cscSwsFormat = (int)format;
	}

	const int width2 = width >> 1;
	const u8 *srcSlice[4] = {
		luma + (size_t)rangeY * width + rangeX,
		cb + (size_t)(rangeY >> 1) * width2 + (rangeX >> 1),
		cr + (size_t)(rangeY >> 1) * width2 + (rangeX >> 1),
		nullptr,
	};
	const int srcStride[4] = { width, width2, width2, 0 };
	const int bpp = pixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	u8 *dstSlice[4] = { dest, nullptr, nullptr, nullptr };
	const int dstStride[4] = { destStride * bpp, 0, 0, 0 };
	if (sws_scale(g_cscSws, srcSlice, srcStride, 0, rangeHeight, dstSlice, dstStride) <= 0) {
		return false;
	}

	// Clear the alpha swscale filled in, which the hardware leaves at zero.
	for (int y = 0; y < rangeHeight; y++) {
		u8 *row = dest + (size_t)y * destStride * bpp;
		if (bpp == 4) {
			u32_le *p32 = (u32_le *)row;
			for (int x = 0; x < rangeWidth; x++) {
				p32[x] = p32[x] & 0x00FFFFFF;
			}
		} else if (pixelMode != GE_CMODE_16BIT_BGR5650) {
			const u16 mask = pixelMode == GE_CMODE_16BIT_ABGR5551 ? 0x7FFF : 0x0FFF;
			u16_le *p16 = (u16_le *)row;
			for (int x = 0; x < rangeWidth; x++) {
				p16[x] = p16[x] & mask;
			}
		}
	}
	return true;
}

#else  // !USE_FFMPEG

bool MpegCscRangeSws(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	return false;
}

void MpegCscShutdown() {}

#endif  // USE_FFMPEG

void MpegCscRange(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
#ifdef USE_FFMPEG
	if (MpegCscRangeSws(dest, destStride, pixelMode, luma, cb, cr, width,
			rangeX, rangeY, rangeWidth, rangeHeight)) {
		return;
	}
#endif
	MpegCscRangeScalar(dest, destStride, pixelMode, luma, cb, cr, width,
		rangeX, rangeY, rangeWidth, rangeHeight);
}

// The shared body of sceMpegBaseCscAvc and sceMpegBaseCscAvcRange - the former is just the
// latter over the whole frame.
static int MpegBaseCscRange(u32 bufferRGB, u32 cscAddr, int bufferWidth,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	auto csc = PSPPointer<SceMp4AvcCscStruct>::Create(cscAddr);
	if (!csc.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "bad csc struct pointer");
	}
	if (bufferWidth == 0) {
		bufferWidth = g_mpegBaseBufferWidth;
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

	const u8 *luma, *cb, *cr;
	if (!ReadTiledYCbCr(buffers, width, height, &luma, &cb, &cr)) {
		return hleLogError(Log::Mpeg, -1, "YCbCr buffers not readable");
	}

	const int bpp = g_mpegBasePixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
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
	gpu->PerformWriteFormattedFromMemory(bufferRGB, destSize, bufferWidth, (GEBufferFormat)g_mpegBasePixelMode);
	// This runs on the DMACPLUS and takes real time. A psmfplayer game blits the current video
	// frame every render frame while it waits for the next, so an instant return is a tight loop
	// that never yields and starves the audio thread that paces playback, and the A/V pipeline
	// deadlocks a few frames in (SOCOM: Tactical Strike hangs exactly here). Our sceMpeg HLE delays
	// sceMpegAvcCsc the same way. On a PSP, sceMpegAvcCsc of a 480x272 frame to 8888 takes 2.4ms
	// (pspautotests video/mpeg/playertiming).
	const int cscUs = std::max(1, (int)(2400LL * rangeWidth * rangeHeight / (480 * 272)));
	return hleDelayResult(hleLogDebug(Log::Mpeg, 0, "%dx%d at %d,%d -> %08x stride %d",
		rangeWidth, rangeHeight, rangeX, rangeY, bufferRGB, bufferWidth), "mpegbase csc", cscUs);
}

static int sceMpegBaseCscInit(int bufferWidth) {
	g_mpegBaseBufferWidth = bufferWidth ? bufferWidth : 512;
	return hleLogInfo(Log::Mpeg, 0, "bufferWidth %d", bufferWidth);
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

static int sceMpegBaseCscAvc(u32 bufferRGB, u32 unknown, int bufferWidth, u32 cscAddr) {
	auto csc = PSPPointer<SceMp4AvcCscStruct>::Create(cscAddr);
	if (!csc.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "bad csc struct pointer");
	}
	// The whole frame. MpegBaseCscRange clamps to the real frame size (from the allocation, not the
	// descriptor), so pass more than any frame can be.
	return MpegBaseCscRange(bufferRGB, cscAddr, bufferWidth, 0, 0, 1024, 1024);
}

static u32 sceMpegBaseCscAvcRange(u32 bufferRGB, u32 unknown, u32 rangeAddr, int bufferWidth, u32 cscAddr) {
	if (!Memory::IsValidRange(rangeAddr, 16)) {
		return hleLogError(Log::Mpeg, -1, "bad range pointer");
	}
	// Also in macroblocks.
	const int rangeX = Memory::ReadUnchecked_U32(rangeAddr + 0) << 4;
	const int rangeY = Memory::ReadUnchecked_U32(rangeAddr + 4) << 4;
	const int rangeWidth = Memory::ReadUnchecked_U32(rangeAddr + 8) << 4;
	const int rangeHeight = Memory::ReadUnchecked_U32(rangeAddr + 12) << 4;
	return MpegBaseCscRange(bufferRGB, cscAddr, bufferWidth, rangeX, rangeY, rangeWidth, rangeHeight);
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

	// The destination descriptor carries the dimensions in macroblocks (as the colour conversion's
	// does); the stack-built source descriptor states them in pixels, so read the destination.
	const int width = dst->width << 4;
	const int height = dst->height << 4;
	if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
		return hleLogError(Log::Mpeg, -1, "unreasonable frame size %dx%d", width, height);
	}
	int sizes[8];
	VideocodecFrameBufferLayout(width, height, sizes, nullptr);

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

const HLEFunction sceMpegbase[] =
{
	{0XBEA18F91, &WrapU_U<sceMpegBasePESpacketCopy>,           "sceMpegBasePESpacketCopy",           'x', "x"      },
	{0X492B5E4B, &WrapI_I<sceMpegBaseCscInit>,                  "sceMpegBaseCscInit",                 'i', "i"      },
	{0X0530BE4E, &WrapI_I<sceMpegBaseCscSetPixelMode>,          "sceMpegbase_0530BE4E",               'i', "i"      },
	{0X91929A21, &WrapI_UUIU<sceMpegBaseCscAvc>,               "sceMpegBaseCscAvc",                  'i', "xxix"   },
	{0X304882E1, &WrapU_UUUIU<sceMpegBaseCscAvcRange>,         "sceMpegBaseCscAvcRange",             'x', "xxxix"  },
	{0X7AC0321A, &WrapI_UUI<sceMpegBaseYCrCbCopy>,             "sceMpegBaseYCrCbCopy",               'i', "xxi"    }
};

void Register_sceMpegbase()
{
	RegisterHLEModule("sceMpegbase", ARRAY_SIZE(sceMpegbase), sceMpegbase);
};
