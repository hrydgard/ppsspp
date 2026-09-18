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

// Correctness and speed of the Media Engine's colour conversion, which is the hottest thing in
// video playback once sceMpeg runs the real mpeg.prx - sceMpegBaseCscAvc sits at the top of a
// profile of a movie.
//
// The correctness half is a reference implementation written out longhand, so an optimized
// MpegCscRange has something to be wrong against that isn't itself. The speed half reports
// megapixels per second for a 480x272 frame, the size a PSP movie actually is.

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/TimeUtil.h"
#include "Core/HLE/sceMpegbase.h"
#include "Core/HLE/sceVideocodec.h"
#include "GPU/ge_constants.h"

#include "unittest/UnitTest.h"

// The conversion, spelled out. Deliberately the slowest, most obvious thing that could work: it is
// here to disagree with MpegCscRange when MpegCscRange is wrong, so it must not share any of its
// cleverness.
static u32 ReferencePixel(int y, int cbv, int crv, int pixelMode) {
	const int c = y - 16, d = cbv - 128, e = crv - 128;
	int r = (298 * c + 409 * e + 128) >> 8;
	int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
	int b = (298 * c + 516 * d + 128) >> 8;
	r = r < 0 ? 0 : (r > 255 ? 255 : r);
	g = g < 0 ? 0 : (g > 255 ? 255 : g);
	b = b < 0 ? 0 : (b > 255 ? 255 : b);
	// Alpha zero, matching the hardware - see the note in sceMpegbase.cpp.
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

static void ReferenceCscRange(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	const int bpp = pixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const int width2 = width >> 1;
	for (int y = 0; y < rangeHeight; y++) {
		for (int x = 0; x < rangeWidth; x++) {
			const int sx = rangeX + x, sy = rangeY + y;
			const int ci = (sy >> 1) * width2 + (sx >> 1);
			const u32 pixel = ReferencePixel(luma[sy * width + sx], cb[ci], cr[ci], pixelMode);
			u8 *out = dest + (y * destStride + x) * bpp;
			if (bpp == 4) {
				memcpy(out, &pixel, 4);
			} else {
				const u16 p16 = (u16)pixel;
				memcpy(out, &p16, 2);
			}
		}
	}
}

// A frame with something in every direction: a gradient so neighbouring pixels differ, plus values
// that drive the conversion past both ends of the 0..255 clamp, since that is where an optimized
// version is most likely to disagree.
struct TestFrame {
	int width = 0;
	int height = 0;
	std::vector<u8> luma, cb, cr;

	TestFrame(int w, int h) : width(w), height(h) {
		luma.resize((size_t)w * h);
		cb.resize((size_t)(w / 2) * (h / 2));
		cr.resize((size_t)(w / 2) * (h / 2));
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				luma[(size_t)y * w + x] = (u8)((x * 3 + y * 5) & 0xFF);
			}
		}
		for (int y = 0; y < h / 2; y++) {
			for (int x = 0; x < w / 2; x++) {
				const size_t i = (size_t)y * (w / 2) + x;
				cb[i] = (u8)((x * 7 + y * 2) & 0xFF);
				cr[i] = (u8)((x * 2 + y * 11) & 0xFF);
			}
		}
	}
};

static const int pixelModes[4] = {
	GE_CMODE_16BIT_BGR5650,
	GE_CMODE_16BIT_ABGR5551,
	GE_CMODE_16BIT_ABGR4444,
	GE_CMODE_32BIT_ABGR8888,
};

static const char *PixelModeName(int mode) {
	switch (mode) {
	case GE_CMODE_16BIT_BGR5650: return "5650";
	case GE_CMODE_16BIT_ABGR5551: return "5551";
	case GE_CMODE_16BIT_ABGR4444: return "4444";
	default: return "8888";
	}
}

static bool CompareAgainstReference(const TestFrame &frame, int pixelMode,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight, int destStride) {
	const int bpp = pixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	// Padded, and prefilled with a value neither implementation would write, so that writing
	// outside the range - or short of it - is a failure rather than a coincidence.
	const size_t destSize = (size_t)(rangeHeight + 2) * destStride * bpp;
	std::vector<u8> got(destSize, 0xCD), want(destSize, 0xCD);

	MpegCscRangeScalar(got.data(), destStride, pixelMode, frame.luma.data(), frame.cb.data(),
		frame.cr.data(), frame.width, rangeX, rangeY, rangeWidth, rangeHeight);
	ReferenceCscRange(want.data(), destStride, pixelMode, frame.luma.data(), frame.cb.data(),
		frame.cr.data(), frame.width, rangeX, rangeY, rangeWidth, rangeHeight);

	for (size_t i = 0; i < destSize; i++) {
		if (got[i] != want[i]) {
			printf("  %s %dx%d at %d,%d stride %d: byte %d is %02x, should be %02x\n",
				PixelModeName(pixelMode), rangeWidth, rangeHeight, rangeX, rangeY, destStride,
				(int)i, got[i], want[i]);
			return false;
		}
	}
	return true;
}

typedef void (*CscFunc)(u8 *, int, int, const u8 *, const u8 *, const u8 *, int, int, int, int, int);

static double MeasureMegapixelsPerSecond(const TestFrame &frame, int pixelMode, std::vector<u8> &dest,
	CscFunc fn = &MpegCscRange) {
	const int destStride = 512;
	// Long enough to swamp the clock's own resolution, short enough not to pad the test run.
	const double seconds = 0.2;
	int frames = 0;
	const double start = time_now_d();
	do {
		for (int i = 0; i < 4; i++) {
			fn(dest.data(), destStride, pixelMode, frame.luma.data(), frame.cb.data(),
				frame.cr.data(), frame.width, 0, 0, frame.width, frame.height);
			frames++;
		}
	} while (time_now_d() - start < seconds);
	const double elapsed = time_now_d() - start;
	return (double)frames * frame.width * frame.height / elapsed / 1000000.0;
}

// The de-tiling as it was originally written, straight from the description of the layout: bounds
// checked per pixel, chroma pulled apart one byte at a time. Kept as the thing UntileYCbCr has to
// agree with, and as something to measure it against.
static void ReferenceUntile(u8 *luma, u8 *cb, u8 *cr, const u8 *const src[8], const int sizes[8],
	int width, int height) {
	const int width2 = width >> 1, height2 = height >> 1;
	const int *ySize = sizes;
	const int *cSize = sizes + 4;
	for (int b = 0; b < 4; b++) {
		if (!src[b]) {
			continue;
		}
		const int xOffset = (b & 1) ? 16 : 0;
		const int yStart = (b >> 1) ? 1 : 0;
		int j = 0;
		for (int bandX = xOffset; bandX < width; bandX += 32) {
			const int run = width - bandX < 16 ? width - bandX : 16;
			for (int row = yStart; row < height; row += 2, j += 16) {
				if (run <= 0 || j + run > ySize[b]) {
					continue;
				}
				memcpy(luma + (size_t)row * width + bandX, src[b] + j, run);
			}
		}
	}
	for (int b = 0; b < 4; b++) {
		if (!src[b + 4]) {
			continue;
		}
		const int xOffset = (b & 1) ? 8 : 0;
		const int yStart = (b >> 1) ? 1 : 0;
		int j = 0;
		for (int bandX = xOffset; bandX < width2; bandX += 16) {
			for (int row = yStart; row < height2; row += 2) {
				for (int k = 0; k < 8; k++, j += 2) {
					const int x = bandX + k;
					if (x >= width2 || j + 1 >= cSize[b]) {
						continue;
					}
					const size_t i = (size_t)row * width2 + x;
					cb[i] = src[b + 4][j];
					cr[i] = src[b + 4][j + 1];
				}
			}
		}
	}
}

// The eight buffers the Media Engine would have produced, laid out as UntileYCbCr expects. The
// contents do not matter for speed and the de-tiling is a pure shuffle, so any pattern will do -
// but make it vary so a broken copy is visible.
struct TiledFrame {
	std::vector<u8> storage[8];
	const u8 *src[8]{};
	int sizes[8]{};

	TiledFrame(int width, int height) {
		VideocodecFrameBufferLayout(width, height, sizes, nullptr);
		for (int i = 0; i < 8; i++) {
			storage[i].resize(sizes[i] > 0 ? sizes[i] : 1);
			for (int j = 0; j < sizes[i]; j++) {
				storage[i][j] = (u8)((j * 7 + i * 31) & 0xFF);
			}
			src[i] = sizes[i] > 0 ? storage[i].data() : nullptr;
		}
	}
};

typedef void (*UntileFunc)(u8 *, u8 *, u8 *, const u8 *const[8], const int[8], int, int);

static double MeasureUntileMegapixelsPerSecond(const TiledFrame &tiled, int width, int height,
	std::vector<u8> &planes, UntileFunc fn = &UntileYCbCr) {
	u8 *luma = planes.data();
	u8 *cb = luma + (size_t)width * height;
	u8 *cr = cb + (size_t)(width / 2) * (height / 2);
	const double seconds = 0.2;
	int frames = 0;
	const double start = time_now_d();
	do {
		for (int i = 0; i < 4; i++) {
			fn(luma, cb, cr, tiled.src, tiled.sizes, width, height);
			frames++;
		}
	} while (time_now_d() - start < seconds);
	const double elapsed = time_now_d() - start;
	return (double)frames * width * height / elapsed / 1000000.0;
}

bool TestMpegCsc() {
	// The size a PSP movie is, so the speed below is the speed that matters.
	TestFrame frame(480, 272);

	for (int pixelMode : pixelModes) {
		// A whole frame, which is what sceMpegBaseCscAvc asks for.
		EXPECT_TRUE(CompareAgainstReference(frame, pixelMode, 0, 0, 480, 272, 512));
		// Partial ranges, as sceMpegBaseCscAvcRange asks for. Odd offsets and sizes on purpose:
		// chroma is half resolution, so an odd left edge starts mid-chroma-sample, and an odd
		// width leaves a pixel that a two-at-a-time inner loop would have to handle separately.
		EXPECT_TRUE(CompareAgainstReference(frame, pixelMode, 16, 16, 64, 32, 512));
		EXPECT_TRUE(CompareAgainstReference(frame, pixelMode, 1, 1, 63, 31, 512));
		EXPECT_TRUE(CompareAgainstReference(frame, pixelMode, 33, 7, 17, 5, 128));
		// A range reaching the far edge, where reading one sample too far would go off the frame.
		EXPECT_TRUE(CompareAgainstReference(frame, pixelMode, 464, 256, 16, 16, 64));
		// One pixel, one row, one column.
		EXPECT_TRUE(CompareAgainstReference(frame, pixelMode, 5, 9, 1, 1, 16));
		EXPECT_TRUE(CompareAgainstReference(frame, pixelMode, 0, 100, 480, 1, 512));
		EXPECT_TRUE(CompareAgainstReference(frame, pixelMode, 100, 0, 1, 272, 16));
	}

	// De-tiling, which runs once per frame ahead of the conversion.
	{
		const size_t planeBytes = (size_t)480 * 272 + (size_t)240 * 136 * 2;
		// Odd frame sizes as well as the real one: the guards in here are about buffers that don't
		// divide evenly into bands, which is the only thing that makes them fire.
		for (auto dims : { std::make_pair(480, 272), std::make_pair(64, 32), std::make_pair(48, 16) }) {
			const int w = dims.first, h = dims.second;
			TiledFrame tiled(w, h);
			std::vector<u8> got((size_t)w * h + (size_t)(w / 2) * (h / 2) * 2, 0xCD);
			std::vector<u8> want(got.size(), 0xCD);
			u8 *gl = got.data(), *gb = gl + (size_t)w * h, *gr = gb + (size_t)(w / 2) * (h / 2);
			u8 *wl = want.data(), *wb = wl + (size_t)w * h, *wr = wb + (size_t)(w / 2) * (h / 2);
			UntileYCbCr(gl, gb, gr, tiled.src, tiled.sizes, w, h);
			ReferenceUntile(wl, wb, wr, tiled.src, tiled.sizes, w, h);
			EXPECT_TRUE(got == want);
			// Nothing left at the fill value: the de-tiling covers every byte of a frame, which is
			// what lets the caller skip clearing the planes first. A real frame could contain the
			// fill byte by chance, so this is looking for whole rows left behind, not exact cover.
			size_t untouched = 0;
			for (u8 v : got) {
				if (v == 0xCD) {
					untouched++;
				}
			}
			EXPECT_TRUE(untouched < got.size() / 100);
		}

		TiledFrame tiled(480, 272);
		std::vector<u8> planes(planeBytes, 0);
		const double mps = MeasureUntileMegapixelsPerSecond(tiled, 480, 272, planes);
		const double refMps = MeasureUntileMegapixelsPerSecond(tiled, 480, 272, planes, ReferenceUntile);
		printf("UntileYCbCr, 480x272: %6.1f MPix/s (%5.2f ms/frame), was %6.1f (%5.2f ms)\n",
			mps, 480.0 * 272.0 / mps / 1000.0, refMps, 480.0 * 272.0 / refMps / 1000.0);
	}

	std::vector<u8> dest((size_t)512 * 272 * 4, 0);
	// How far swscale lands from the conversion written out longhand. It rounds its own way, so
	// this is not expected to be zero - the question is whether it is close enough to use.
	printf("swscale against the reference, per channel:\n");
	for (int pixelMode : pixelModes) {
		const int bpp = pixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
		std::vector<u8> sws((size_t)512 * 272 * 4, 0), ref((size_t)512 * 272 * 4, 0);
		if (!MpegCscRangeSws(sws.data(), 512, pixelMode, frame.luma.data(), frame.cb.data(),
				frame.cr.data(), 480, 0, 0, 480, 272)) {
			printf("  %s: declined\n", PixelModeName(pixelMode));
			continue;
		}
		ReferenceCscRange(ref.data(), 512, pixelMode, frame.luma.data(), frame.cb.data(),
			frame.cr.data(), 480, 0, 0, 480, 272);
		// Per channel, because that is what "how different does it look" means - a byte-wise diff
		// on a packed 16-bit pixel could be one step in one channel or a disaster in three.
		int shifts[3], masks[3];
		if (bpp == 4) {
			shifts[0] = 0; shifts[1] = 8; shifts[2] = 16;
			masks[0] = masks[1] = masks[2] = 0xFF;
		} else if (pixelMode == GE_CMODE_16BIT_BGR5650) {
			shifts[0] = 0; shifts[1] = 5; shifts[2] = 11;
			masks[0] = 0x1F; masks[1] = 0x3F; masks[2] = 0x1F;
		} else if (pixelMode == GE_CMODE_16BIT_ABGR5551) {
			shifts[0] = 0; shifts[1] = 5; shifts[2] = 10;
			masks[0] = masks[1] = masks[2] = 0x1F;
		} else {
			shifts[0] = 0; shifts[1] = 4; shifts[2] = 8;
			masks[0] = masks[1] = masks[2] = 0x0F;
		}
		int worst = 0;
		double total = 0.0;
		int count = 0;
		for (int y = 0; y < 272; y++) {
			for (int x = 0; x < 480; x++) {
				const size_t off = ((size_t)y * 512 + x) * bpp;
				u32 a = 0, b = 0;
				memcpy(&a, &sws[off], bpp);
				memcpy(&b, &ref[off], bpp);
				for (int ch = 0; ch < 3; ch++) {
					const int va = (int)((a >> shifts[ch]) & masks[ch]);
					const int vb = (int)((b >> shifts[ch]) & masks[ch]);
					const int d = va > vb ? va - vb : vb - va;
					worst = worst > d ? worst : d;
					total += d;
					count++;
				}
			}
		}
		printf("  %s: worst channel step %d, mean %.3f\n", PixelModeName(pixelMode), worst,
			total / count);
	}

	printf("MpegCscRange, 480x272:\n");
	for (int pixelMode : pixelModes) {
		const double mps = MeasureMegapixelsPerSecond(frame, pixelMode, dest, &MpegCscRangeScalar);
		const double swsMps = MeasureMegapixelsPerSecond(frame, pixelMode, dest, &MpegCscRange);
		// A movie is 480*272 at ~30fps, so 3.9 MPix/s is what playback needs of it.
		printf("  %s: scalar %6.1f MPix/s (%5.2f ms), swscale %6.1f MPix/s (%5.2f ms)\n",
			PixelModeName(pixelMode), mps, 480.0 * 272.0 / mps / 1000.0,
			swsMps, 480.0 * 272.0 / swsMps / 1000.0);
	}

	return true;
}
