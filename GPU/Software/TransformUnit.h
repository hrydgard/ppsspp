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

#pragma once

#include "CommonTypes.h"
#include "../Math3D.h"

typedef u16 fixed16;
typedef u16 u10; // TODO: erm... :/

typedef Vec3<float> ModelCoords;
typedef Vec3<float> WorldCoords;
typedef Vec3<float> ViewCoords;
typedef Vec4<float> ClipCoords; // Range: -w <= x/y/z <= w

struct ScreenCoords
{
	fixed16 x;
	fixed16 y;
	u16 z;
};

struct DrawingCoords
{
	DrawingCoords() {}
	DrawingCoords(u10 x, u10 y, u16 z) : x(x), y(y), z(z) {}

	u10 x;
	u10 y;
	u16 z;

	Vec2<u10> xy() const { return Vec2<u10>(x, y); }
};

struct VertexData
{
	void Lerp(float t, const VertexData& a, const VertexData& b)
	{
		#define LINTERP(T, OUT, IN) (OUT) + ((IN - OUT) * T)
		#define LINTERP_INT(T, OUT, IN) (OUT) + (((IN - OUT) * T) >> 8)

		// World coords only needed for lighting, so we don't Lerp those

		clippos.x = LINTERP(t, a.clippos.x, b.clippos.x);
		clippos.y = LINTERP(t, a.clippos.y, b.clippos.y);
		clippos.z = LINTERP(t, a.clippos.z, b.clippos.z);
		clippos.w = LINTERP(t, a.clippos.w, b.clippos.w);

		// TODO: Should use a LINTERP_INT, too
		drawpos.x = LINTERP(t, a.drawpos.x, b.drawpos.x);
		drawpos.y = LINTERP(t, a.drawpos.y, b.drawpos.y);
		drawpos.z = LINTERP(t, a.drawpos.z, b.drawpos.z);

		texturecoords.x = LINTERP(t, a.texturecoords.x, b.texturecoords.x);
		texturecoords.y = LINTERP(t, a.texturecoords.y, b.texturecoords.y);

		normal.x = LINTERP(t, a.normal.x, b.normal.x);
		normal.y = LINTERP(t, a.normal.y, b.normal.y);
		normal.z = LINTERP(t, a.normal.z, b.normal.z);

		u16 t_int =(u16)(t*256);
		color0.x = LINTERP_INT(t_int, a.color0.x, b.color0.x);
		color0.y = LINTERP_INT(t_int, a.color0.y, b.color0.y);
		color0.z = LINTERP_INT(t_int, a.color0.z, b.color0.z);
		color0.w = LINTERP_INT(t_int, a.color0.w, b.color0.w);

		color1.x = LINTERP_INT(t_int, a.color1.x, b.color1.x);
		color1.y = LINTERP_INT(t_int, a.color1.y, b.color1.y);
		color1.z = LINTERP_INT(t_int, a.color1.z, b.color1.z);

		#undef LINTERP
		#undef LINTERP_INT
	}

	WorldCoords worldpos; // TODO: Storing this is dumb, should transform the light to clip space instead
	ClipCoords clippos;
	DrawingCoords drawpos; // TODO: Shouldn't store this ?
	Vec2<float> texturecoords;
	Vec3<float> normal;
	WorldCoords worldnormal;
	Vec4<int> color0;
	Vec3<int> color1;
};

class TransformUnit
{
public:
	static WorldCoords ModelToWorld(const ModelCoords& coords);
	static ViewCoords WorldToView(const WorldCoords& coords);
	static ClipCoords ViewToClip(const ViewCoords& coords);
	static ScreenCoords ClipToScreen(const ClipCoords& coords);
	static DrawingCoords ScreenToDrawing(const ScreenCoords& coords);

	static void SubmitPrimitive(void* vertices, void* indices, u32 prim_type, int vertex_count, u32 vertex_type);
};
