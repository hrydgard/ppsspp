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
#include <cmath>
#include <vector>

#include "Common/Render/DropShadow.h"

// In UI units.
static constexpr float UI_SHADOW_RADIUS = 3.0f;
static constexpr float UI_SHADOW_INTENSITY = 0.66f;

DropShadowParams UIDropShadowParams(float pixelsPerUnit) {
	DropShadowParams params;
	params.radius = std::max(1, (int)(UI_SHADOW_RADIUS * pixelsPerUnit));
	params.intensity = UI_SHADOW_INTENSITY;
	return params;
}

void CompositeDropShadow(const u32 *src, int w, int h, u32 *dst, int dstStride, const DropShadowParams &params) {
	const int radius = std::max(1, params.radius);
	const int pad = params.Padding();
	const int outW = w + pad * 2;
	const int outH = h + pad * 2;

	const float sigma = radius * 0.5f;
	std::vector<float> kernel(radius * 2 + 1);
	float kernelSum = 0.0f;
	for (int i = -radius; i <= radius; i++) {
		kernel[i + radius] = expf(-(float)(i * i) / (2.0f * sigma * sigma));
		kernelSum += kernel[i + radius];
	}
	for (float &k : kernel) {
		k /= kernelSum;
	}

	// Blur the offset alpha, scattering each texel horizontally into rows, then the rows vertically.
	// Scattering lets us skip the transparent texels, which are most of them.
	std::vector<float> rows(outW * h);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const u32 a = src[y * w + x] >> 24;
			if (!a) {
				continue;
			}
			const float fa = a * (1.0f / 255.0f);
			float *row = &rows[y * outW + x + pad + params.offsetX - radius];
			for (int i = 0; i <= radius * 2; i++) {
				row[i] += fa * kernel[i];
			}
		}
	}
	std::vector<float> shadow(outW * outH);
	for (int y = 0; y < h; y++) {
		const float *row = &rows[y * outW];
		for (int i = 0; i <= radius * 2; i++) {
			float *out = &shadow[(y + pad + params.offsetY - radius + i) * outW];
			const float k = kernel[i];
			for (int x = 0; x < outW; x++) {
				out[x] += row[x] * k;
			}
		}
	}

	for (int y = 0; y < outH; y++) {
		for (int x = 0; x < outW; x++) {
			const int sx = x - pad;
			const int sy = y - pad;
			const u32 color = (sx >= 0 && sx < w && sy >= 0 && sy < h) ? src[sy * w + sx] : 0;
			const u32 alpha = color >> 24;
			const float shadowAlpha = std::min(1.0f, shadow[y * outW + x] * params.intensity);
			const u32 outAlpha = alpha + (u32)((255 - alpha) * shadowAlpha + 0.5f);
			dst[y * dstStride + x] = (outAlpha << 24) | (color & 0x00FFFFFF);
		}
	}
}
