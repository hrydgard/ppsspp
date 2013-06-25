// Copyright (c) 2013- PPSSPP Project.

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

#include "../GPUState.h"

#include "Rasterizer.h"

const int FB_WIDTH = 480;
const int FB_HEIGHT = 272;
extern u8* fb;

namespace Rasterizer {

static int orient2d(const DrawingCoords& v0, const DrawingCoords& v1, const DrawingCoords& v2)
{
	return ((int)v1.x-(int)v0.x)*((int)v2.y-(int)v0.y) - ((int)v1.y-(int)v0.y)*((int)v2.x-(int)v0.x);
}

void DrawTriangle(DrawingCoords vertices[3])
{
	int minX = std::min(std::min(vertices[0].x, vertices[1].x), vertices[2].x);
	int minY = std::min(std::min(vertices[0].y, vertices[1].y), vertices[2].y);
	int maxX = std::max(std::max(vertices[0].x, vertices[1].x), vertices[2].x);
	int maxY = std::max(std::max(vertices[0].y, vertices[1].y), vertices[2].y);

	minX = std::max(minX, gstate.getScissorX1());
	maxX = std::min(maxX, gstate.getScissorX2());
	minY = std::max(minY, gstate.getScissorY1());
	maxY = std::min(maxY, gstate.getScissorY2());

	DrawingCoords p(minX, minY);
	for (p.y = minY; p.y <= maxY; ++p.y)
	{
		for (p.x = minX; p.x <= maxX; ++p.x)
		{
			int w0 = orient2d(vertices[1], vertices[2], p);
			int w1 = orient2d(vertices[2], vertices[0], p);
			int w2 = orient2d(vertices[0], vertices[1], p);

			// If p is on or inside all edges, render pixel
			// TODO: Should only render when it's on the left of the right edge
			if (w0 >=0 && w1 >= 0 && w2 >= 0)
			{
				fb[p.x*4+p.y*FB_WIDTH*4] = 0xff;
				fb[p.x*4+p.y*FB_WIDTH*4+1] = 0xff;
				fb[p.x*4+p.y*FB_WIDTH*4+2] = 0xff;
				fb[p.x*4+p.y*FB_WIDTH*4+3] = 0xff;
			}
		}
	}
}

} // namespace
