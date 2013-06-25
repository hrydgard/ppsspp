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

static void DrawVLine(u8* target, DrawingCoords a, DrawingCoords b)
{
	if (a.y > b.y) {
		DrawVLine(target, b, a);
		return;
	}

	for (int y = a.y; y < b.y; ++y) {
		float u = (float)(y-a.y)/(float)(b.y-a.y);
		int x = (1-u)*a.x+u*b.x;
		if (x < gstate.getScissorX1()) continue;
		if (x > gstate.getScissorX2()) continue;
		if (y < gstate.getScissorY1()) continue;
		if (y > gstate.getScissorY2()) continue;
		target[x*4+y*FB_WIDTH*4] = 0xff;
		target[x*4+y*FB_WIDTH*4+1] = 0xff;
		target[x*4+y*FB_WIDTH*4+2] = 0xff;
		target[x*4+y*FB_WIDTH*4+3] = 0xff;
	}
}

static void DrawLine(u8* target, DrawingCoords a, DrawingCoords b)
{
	if (a.x > b.x) {
		DrawLine(target, b, a);
		return;
	}

	if (a.y > b.y && a.x - b.x < a.y - b.y)
	{
		DrawVLine(target, a, b);
		return;
	}

	if (a.y < b.y && a.x - b.x < b.y - a.y)
	{
		DrawVLine(target, a, b);
		return;
	}

	for (int x = a.x; x < b.x; ++x) {
		float u = (float)(x-a.x)/(float)(b.x-a.x);
		int y = (1-u)*a.y+u*b.y;
		if (x < gstate.getScissorX1()) continue;
		if (x > gstate.getScissorX2()) continue;
		if (y < gstate.getScissorY1()) continue;
		if (y > gstate.getScissorY2()) continue;
		target[x*4+y*FB_WIDTH*4] = 0xff;
		target[x*4+y*FB_WIDTH*4+1] = 0xff;
		target[x*4+y*FB_WIDTH*4+2] = 0xff;
		target[x*4+y*FB_WIDTH*4+3] = 0xff;
	}
}

void DrawTriangle(DrawingCoords vertices[3])
{
	// TODO: Well yeah, that's not quite it, yet.. :p
	DrawLine(fb, vertices[0], vertices[1]);
	DrawLine(fb, vertices[1], vertices[2]);
	DrawLine(fb, vertices[2], vertices[0]);
}

} // namespace
