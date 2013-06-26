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

typedef Vec2<u10> DrawingCoords; // TODO: Keep z component?

struct VertexData
{
	void Lerp(float t, const VertexData& a, const VertexData& b)
	{
		#define LINTERP(T, OUT, IN) (OUT) + ((IN - OUT) * T)

		clippos.x = LINTERP(t, a.clippos.x, b.clippos.x);
		clippos.y = LINTERP(t, a.clippos.y, b.clippos.y);
		clippos.z = LINTERP(t, a.clippos.z, b.clippos.z);
		clippos.w = LINTERP(t, a.clippos.w, b.clippos.w);

		drawpos.x = LINTERP(t, a.drawpos.x, b.drawpos.x);
		drawpos.y = LINTERP(t, a.drawpos.y, b.drawpos.y);

		texturecoords.x = LINTERP(t, a.texturecoords.x, b.texturecoords.x);
		texturecoords.y = LINTERP(t, a.texturecoords.y, b.texturecoords.y);

		color0.x = LINTERP(t, a.color0.x, b.color0.x);
		color0.y = LINTERP(t, a.color0.y, b.color0.y);
		color0.z = LINTERP(t, a.color0.z, b.color0.z);
		color0.w = LINTERP(t, a.color0.w, b.color0.w);

		color1.x = LINTERP(t, a.color1.x, b.color1.x);
		color1.y = LINTERP(t, a.color1.y, b.color1.y);
		color1.z = LINTERP(t, a.color1.z, b.color1.z);
	}

	ClipCoords clippos;
	DrawingCoords drawpos; // TODO: Shouldn't store this ?
	Vec2<float> texturecoords;
	Vec4<float> color0;
	Vec3<float> color1;
};

class TransformUnit
{
public:
	static WorldCoords ModelToWorld(const ModelCoords& coords);
	static ViewCoords WorldToView(const WorldCoords& coords);
	static ClipCoords ViewToClip(const ViewCoords& coords);
	static ScreenCoords ClipToScreen(const ClipCoords& coords);
	static DrawingCoords ScreenToDrawing(const ScreenCoords& coords);

	static void SubmitPrimitive(void* vertices, u32 prim_type, int vertex_count, u32 vertex_type);
};
