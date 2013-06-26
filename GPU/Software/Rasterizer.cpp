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

#include "../../Core/MemMap.h"
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

u32 SampleNearest(int level, float s, float t)
{
	int texfmt = gstate.texformat & 0xF;
	u32 texaddr = (gstate.texaddr[level] & 0xFFFFF0) | ((gstate.texbufwidth[level] << 8) & 0x0F000000);
	u8* srcptr = (u8*)Memory::GetPointer(texaddr); // TODO: not sure if this is the right place to load from...?

	int width = 1 << (gstate.texsize[level] & 0xf);
	int height = 1 << ((gstate.texsize[level]>>8) & 0xf);

	int u = s * width; // TODO: -1?
	int v = t * height; // TODO: -1?

	// TODO: Assert tmode.hsm == 0 (normal storage mode)
	// TODO: Assert tmap.tmn == 0 (uv texture mapping mode)

	if (texfmt == GE_TFMT_4444) {
		srcptr += 2 * v * width + 2 * u;
		u8 r = (*srcptr) >> 4;
		u8 g = (*srcptr) & 0xF;
		u8 b = (*(srcptr+1)) >> 4;
		u8 a = (*(srcptr+1)) & 0xF;
		r = (r << 4) | r;
		g = (g << 4) | g;
		b = (b << 4) | b;
		a = (a << 4) | a;
		return (r << 24) | (g << 16) | (b << 8) | a;
	} else if (texfmt == GE_TFMT_5551) {
		srcptr += 2 * v * width + 2 * u;
		u8 r = (*srcptr) & 0x1F;
		u8 g = (((*srcptr) & 0xE0) >> 5) | (((*(srcptr+1))&0x3) << 3);
		u8 b = ((*srcptr+1) & 0x7C) >> 2;
		u8 a = (*(srcptr+1)) >> 7;
		r = (r << 3) | (r >> 2);
		g = (g << 3) | (g >> 2);
		b = (b << 3) | (b >> 2);
		a = (a) ? 0xff : 0;
		return (r << 24) | (g << 16) | (b << 8) | a;
	} else if (texfmt == GE_TFMT_5650) {
		srcptr += 2 * v * width + 2 * u;
		u8 r = (*srcptr) & 0x1F;
		u8 g = (((*srcptr) & 0xE0) >> 5) | (((*(srcptr+1))&0x7) << 3);
		u8 b = ((*srcptr+1) & 0xF8) >> 3;
		u8 a = 0xff;
		r = (r << 3) | (r >> 2);
		g = (g << 2) | (g >> 4);
		b = (b << 3) | (b >> 2);
		return (r << 24) | (g << 16) | (b << 8) | a;
	} else if (texfmt == GE_TFMT_8888) {
		srcptr += 4 * v * width + 4 * u;
		u8 r = *srcptr++;
		u8 g = *srcptr++;
		u8 b = *srcptr++;
		u8 a = *srcptr++;
		return (r << 24) | (g << 16) | (b << 8) | a;
	}
}

void DrawTriangle(VertexData vertexdata[3])
{
	DrawingCoords vertices[3] = { vertexdata[0].drawpos, vertexdata[1].drawpos, vertexdata[2].drawpos };

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
				float den = 1.0f/vertexdata[0].clippos.w * w0 + 1.0f/vertexdata[1].clippos.w * w1 + 1.0f/vertexdata[2].clippos.w * w2;
				float s = (vertexdata[0].texturecoords.s() * w0 / vertexdata[0].clippos.w + vertexdata[1].texturecoords.s() * w1 / vertexdata[1].clippos.w + vertexdata[2].texturecoords.s() * w2 / vertexdata[2].clippos.w) / den;
				float t = (vertexdata[0].texturecoords.t() * w0 / vertexdata[0].clippos.w + vertexdata[1].texturecoords.t() * w1 / vertexdata[1].clippos.w + vertexdata[2].texturecoords.t() * w2 / vertexdata[2].clippos.w) / den;
				u32 vcol0 = (int)((vertexdata[0].color0.x * w0 / vertexdata[0].clippos.w + vertexdata[1].color0.x * w1 / vertexdata[1].clippos.w + vertexdata[2].color0.x * w2 / vertexdata[2].clippos.w) / den * 255)*256*256*256 +
							(int)((vertexdata[0].color0.y * w0 / vertexdata[0].clippos.w + vertexdata[1].color0.y * w1 / vertexdata[1].clippos.w + vertexdata[2].color0.y * w2 / vertexdata[2].clippos.w) / den * 255)*256*256 +
							(int)((vertexdata[0].color0.z * w0 / vertexdata[0].clippos.w + vertexdata[1].color0.z * w1 / vertexdata[1].clippos.w + vertexdata[2].color0.z * w2 / vertexdata[2].clippos.w) / den * 255)*256 +
							(int)((vertexdata[0].color0.w * w0 / vertexdata[0].clippos.w + vertexdata[1].color0.w * w1 / vertexdata[1].clippos.w + vertexdata[2].color0.w * w2 / vertexdata[2].clippos.w) / den * 255);
				u32 color = /*TextureDecoder::*/SampleNearest(0, s, t);
				*(u32*)&fb[p.x*4+p.y*FB_WIDTH*4] = color | vcol0;
			}
		}
	}
}

} // namespace
