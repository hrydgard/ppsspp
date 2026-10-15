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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Common/CommonTypes.h"
#include "GPU/Common/DepthRaster.h"
#include "unittest/UnitTest.h"

namespace {

struct RasterRng {
	u32 state = 0x9E3779B9;
	u32 Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	int Range(int lo, int hi) { return lo + (int)(Next() % (u32)(hi - lo + 1)); }
};

constexpr int BUF_W = 512;
constexpr int BUF_H = 280;

// Which pixels DepthRaster4Triangles covers, one pixel at a time: inside (or on) all three edges, within the
// triangle's bounds clipped to the scissor, with X widened to whole groups of four.
void ReferenceCoverage(std::vector<u8> &covered, int x0, int y0, int x1, int y1, int x2, int y2, const DepthScissor &scissor) {
	const int minX = std::max(std::min({ x0, x1, x2 }), (int)scissor.x1);
	const int maxX = std::min(std::max({ x0, x1, x2 }), (int)scissor.x2);
	const int minY = std::max(std::min({ y0, y1, y2 }), (int)scissor.y1);
	const int maxY = std::min(std::max({ y0, y1, y2 }), (int)scissor.y2);
	if (maxX <= minX || maxY <= minY)
		return;
	if ((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) < 10)
		return;
	for (int y = minY; y <= maxY; y++) {
		for (int x = minX & ~3; x <= (maxX & ~3) + 3; x++) {
			const int w0 = (y1 - y2) * x + (x2 - x1) * y + (x1 * y2 - y1 * x2);
			const int w1 = (y2 - y0) * x + (x0 - x2) * y + (x2 * y0 - y2 * x0);
			const int w2 = (y0 - y1) * x + (x1 - x0) * y + (x0 * y1 - y0 * x1);
			if ((w0 | w1 | w2) >= 0)
				covered[y * BUF_W + x] = 1;
		}
	}
}

}  // namespace

// The depth rasterizer covers exactly the pixels a per-pixel edge test does. All triangles are at one depth, so
// with a cleared buffer and Greater, the written pixels are the covered ones. (Low quality is left out: it
// copies each row's blocks to the next row, along with what was there.)
bool TestDepthRaster() {
	RasterRng rng;
	std::vector<u16> depth(BUF_W * BUF_H);
	std::vector<u8> covered(BUF_W * BUF_H);
	int mismatches = 0;
	int pixels = 0;

	for (int trial = 0; trial < 4000; trial++) {
		DepthScissor scissor;
		scissor.x1 = (u16)rng.Range(0, 100);
		scissor.y1 = (u16)rng.Range(0, 60);
		scissor.x2 = (u16)rng.Range(380, 480);
		scissor.y2 = (u16)rng.Range(200, 270);
		DepthDraw draw{};
		draw.prim = GE_PRIM_TRIANGLES;
		draw.compareMode = ZCompareMode::Greater;

		// Groups of four triangles: x, y and z of vertex 0 of each, then vertex 1, then vertex 2.
		const int groups = 1 + rng.Range(0, 3);
		alignas(16) int tx[48];
		alignas(16) int ty[48];
		alignas(16) float tz[48];
		std::fill(depth.begin(), depth.end(), 0);
		std::fill(covered.begin(), covered.end(), 0);
		// Small and thin triangles as well as big ones, some partly off the scissor.
		const int size = trial % 3 == 0 ? 8 : trial % 3 == 1 ? 60 : 400;
		for (int g = 0; g < groups; g++) {
			for (int t = 0; t < 4; t++) {
				const int cx = rng.Range(-40, 520);
				const int cy = rng.Range(-40, 300);
				int x[3], y[3];
				for (int v = 0; v < 3; v++) {
					x[v] = cx + rng.Range(-size, size);
					y[v] = cy + rng.Range(-size, size);
					tx[g * 12 + v * 4 + t] = x[v];
					ty[g * 12 + v * 4 + t] = y[v];
					tz[g * 12 + v * 4 + t] = 1000.0f;
				}
				ReferenceCoverage(covered, x[0], y[0], x[1], y[1], x[2], y[2], scissor);
			}
		}
		DepthRasterScreenVerts(depth.data(), BUF_W, tx, ty, tz, groups * 12, draw, scissor, false);

		for (int i = 0; i < BUF_W * BUF_H; i++) {
			const bool written = depth[i] != 0;
			pixels += written;
			if (written != (covered[i] != 0) || (written && depth[i] != 1000)) {
				if (mismatches++ < 10)
					printf("Trial %d: pixel %d,%d is %d, expected %s\n", trial, i % BUF_W, i / BUF_W, depth[i], covered[i] ? "covered" : "empty");
			}
		}
	}
	printf("%d pixels covered\n", pixels);
	EXPECT_EQ_INT(mismatches, 0);
	EXPECT_TRUE(pixels > 1000000);
	return true;
}
