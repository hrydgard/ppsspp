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

#include <cmath>
#include <cstring>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/TimeUtil.h"
#include "Core/HLE/sceMpegbase.h"
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
	switch (pixelMode) {
	case GE_CMODE_16BIT_BGR5650:
		return ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
	case GE_CMODE_16BIT_ABGR5551:
		return (1 << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
	case GE_CMODE_16BIT_ABGR4444:
		return (0xF << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4);
	default:
		return 0xFF000000 | (b << 16) | (g << 8) | r;
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

	MpegCscRange(got.data(), destStride, pixelMode, frame.luma.data(), frame.cb.data(),
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

static double MeasureMegapixelsPerSecond(const TestFrame &frame, int pixelMode, std::vector<u8> &dest) {
	const int destStride = 512;
	// Long enough to swamp the clock's own resolution, short enough not to pad the test run.
	const double seconds = 0.2;
	int frames = 0;
	const double start = time_now_d();
	do {
		for (int i = 0; i < 4; i++) {
			MpegCscRange(dest.data(), destStride, pixelMode, frame.luma.data(), frame.cb.data(),
				frame.cr.data(), frame.width, 0, 0, frame.width, frame.height);
			frames++;
		}
	} while (time_now_d() - start < seconds);
	const double elapsed = time_now_d() - start;
	return (double)frames * frame.width * frame.height / elapsed / 1000000.0;
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

	std::vector<u8> dest((size_t)512 * 272 * 4, 0);
	printf("MpegCscRange, 480x272:\n");
	for (int pixelMode : pixelModes) {
		const double mps = MeasureMegapixelsPerSecond(frame, pixelMode, dest);
		// A movie is 480*272 at ~30fps, so 3.9 MPix/s is what playback needs of it.
		printf("  %s: %6.1f MPix/s (%5.2f ms/frame)\n", PixelModeName(pixelMode), mps,
			480.0 * 272.0 / mps / 1000.0);
	}

	return true;
}
