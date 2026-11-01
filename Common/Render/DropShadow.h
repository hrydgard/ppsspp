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

#pragma once

#include <cstdlib>

#include "Common/CommonTypes.h"

// A soft black shadow, shared by the UI atlas images and the text drawer.
struct DropShadowParams {
	// Blur kernel half-width in pixels. The gaussian's sigma is half of it.
	int radius = 1;
	// Shadow offset in pixels, positive is right/down.
	int offsetX = 0;
	int offsetY = 0;
	// Multiplies the shadow's alpha, which is then clamped to 1.
	float intensity = 1.0f;

	// Room the shadow needs on each side of the image.
	int Padding() const {
		const int x = abs(offsetX);
		const int y = abs(offsetY);
		return radius + (x > y ? x : y);
	}
};

// The UI's shadow, used for both the atlas images and text, so the two match on screen.
DropShadowParams UIDropShadowParams(float pixelsPerUnit);

// src is premultiplied RGBA with alpha in the top byte. Writes it composited over its shadow to dst,
// (w + 2 * padding) x (h + 2 * padding) pixels with the image centered. Since the shadow is black,
// only alpha changes under the image.
void CompositeDropShadow(const u32 *src, int w, int h, u32 *dst, int dstStride, const DropShadowParams &params);
