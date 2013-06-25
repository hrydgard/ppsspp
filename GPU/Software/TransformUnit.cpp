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

#include "TransformUnit.h"
#include "../GPUState.h"
#include "../GLES/VertexDecoder.h"

const int FB_WIDTH = 480;
const int FB_HEIGHT = 272;
extern u8* fb;

WorldCoords TransformUnit::ModelToWorld(const ModelCoords& coords)
{
	Mat3x3<float> world_matrix(gstate.worldMatrix);
	return WorldCoords(world_matrix * coords) + Vec3<float>(gstate.worldMatrix[9], gstate.worldMatrix[10], gstate.worldMatrix[11]);
}

ViewCoords TransformUnit::WorldToView(const WorldCoords& coords)
{
	Mat3x3<float> view_matrix(gstate.viewMatrix);
	return ViewCoords(view_matrix * coords) + Vec3<float>(gstate.viewMatrix[9], gstate.viewMatrix[10], gstate.viewMatrix[11]);
}

ClipCoords TransformUnit::ViewToClip(const ViewCoords& coords)
{
	Vec4<float> coords4(coords.x, coords.y, coords.z, 1.0f);
	Mat4x4<float> projection_matrix(gstate.projMatrix);
	return ClipCoords(projection_matrix * coords4);
}

ScreenCoords TransformUnit::ClipToScreen(const ClipCoords& coords)
{
	ScreenCoords ret;
	float vpx1 = getFloat24(gstate.viewportx1);
	float vpx2 = getFloat24(gstate.viewportx2);
	float vpy1 = getFloat24(gstate.viewporty1);
	float vpy2 = getFloat24(gstate.viewporty2);
	float vpz1 = getFloat24(gstate.viewportz1);
	float vpz2 = getFloat24(gstate.viewportz2);
	// TODO: Check for invalid parameters (x2 < x1, etc)
	ret.x = (coords.x * vpx1 / coords.w + vpx2) / 4095.9375 * 0xFFFF;
	ret.y = (coords.y * vpy1 / coords.w + vpy2) / 4096.9375 * 0xFFFF;
	ret.z = (coords.z * vpz1 / coords.w + vpz2) / 4096.9375 * 0xFFFF;
	return ret;
}

DrawingCoords TransformUnit::ScreenToDrawing(const ScreenCoords& coords)
{
	DrawingCoords ret;
	// TODO: What to do when offset > coord?
	// TODO: Mask can be re-enabled now, I guess.
	ret.x = (((u32)coords.x - (gstate.offsetx&0xffff))/16) & 0x3ff;
	ret.y = (((u32)coords.y - (gstate.offsety&0xffff))/16) & 0x3ff;
	return ret;
}

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

void TransformUnit::SubmitPrimitive(void* vertices, u32 prim_type, int vertex_count, u32 vertex_type)
{
	// TODO: Cache VertexDecoder objects
	VertexDecoder vdecoder;
	vdecoder.SetVertexType(vertex_type);
	const DecVtxFormat& vtxfmt = vdecoder.GetDecVtxFmt();

	static u8 buf[102400]; // yolo
	vdecoder.DecodeVerts(buf, vertices, 0, vertex_count - 1);

	VertexReader vreader(buf, vtxfmt, vertex_type);

	// We only support triangle lists, for now.
	for (int vtx = 0; vtx < vertex_count; ++vtx)
	{
		DrawingCoords dcoords[3];
		for (unsigned int i = 0; i < 3; ++i)
		{
			float pos[3];
			vreader.Goto(vtx+i);
			vreader.ReadPos(pos);

			ModelCoords mcoords(pos[0], pos[1], pos[2]);
			ClipCoords ccoords(ClipCoords(TransformUnit::ViewToClip(TransformUnit::WorldToView(TransformUnit::ModelToWorld(mcoords)))));

			// TODO: Split primitives in these cases!
			// TODO: Check if the equal case needs to be included, too
			if (ccoords.x < -ccoords.w || ccoords.x > ccoords.w) {
				ERROR_LOG(G3D, "X outside view volume!");
				goto skip;
			}
			if (ccoords.y < -ccoords.w || ccoords.y > ccoords.w) {
				ERROR_LOG(G3D, "Y outside view volume!");
				goto skip;
			}
			if (ccoords.z < -ccoords.w || ccoords.z > ccoords.w) {
				ERROR_LOG(G3D, "Z outside view volume!");
				goto skip;
			}
			dcoords[i] = DrawingCoords(TransformUnit::ScreenToDrawing(TransformUnit::ClipToScreen(ccoords)));
		}
		DrawLine(fb, dcoords[0], dcoords[1]);
		DrawLine(fb, dcoords[1], dcoords[2]);
		DrawLine(fb, dcoords[2], dcoords[0]);
skip:;
	}
}
