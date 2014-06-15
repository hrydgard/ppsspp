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
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Math3D.h"

using namespace Math3D;

typedef u16 fixed16;
typedef u16 u10; // TODO: erm... :/

typedef Vec3<float> ModelCoords;
typedef Vec3<float> WorldCoords;
typedef Vec3<float> ViewCoords;
typedef Vec4<float> ClipCoords; // Range: -w <= x/y/z <= w

struct SplinePatch;

struct ScreenCoords
{
	ScreenCoords() {}
	ScreenCoords(fixed16 x, fixed16 y, u16 z) : x(x), y(y), z(z) {}

	fixed16 x;
	fixed16 y;
	u16 z;

	Vec2<fixed16> xy() const { return Vec2<fixed16>(x, y); }

	ScreenCoords operator * (const float t) const
	{
		return ScreenCoords((fixed16)(x * t), (fixed16)(y * t), (u16)(z * t));
	}

	ScreenCoords operator / (const int t) const
	{
		return ScreenCoords(x / t, y / t, z / t);
	}

	ScreenCoords operator + (const ScreenCoords& oth) const
	{
		return ScreenCoords(x + oth.x, y + oth.y, z + oth.z);
	}
};

struct DrawingCoords
{
	DrawingCoords() {}
	DrawingCoords(u10 x, u10 y, u16 z) : x(x), y(y), z(z) {}

	u10 x;
	u10 y;
	u16 z;

	Vec2<u10> xy() const { return Vec2<u10>(x, y); }

	DrawingCoords operator * (const float t) const
	{
		return DrawingCoords((u10)(x * t), (u10)(y * t), (u16)(z * t));
	}

	DrawingCoords operator + (const DrawingCoords& oth) const
	{
		return DrawingCoords(x + oth.x, y + oth.y, z + oth.z);
	}
};

struct VertexData
{
	void Lerp(float t, const VertexData& a, const VertexData& b)
	{
		// World coords only needed for lighting, so we don't Lerp those

		modelpos = ::Lerp(a.modelpos, b.modelpos, t);
		clippos = ::Lerp(a.clippos, b.clippos, t);
		screenpos = ::Lerp(a.screenpos, b.screenpos, t);  // TODO: Should use a LerpInt (?)
		texturecoords = ::Lerp(a.texturecoords, b.texturecoords, t);
		normal = ::Lerp(a.normal, b.normal, t);

		u16 t_int =(u16)(t*256);
		color0 = LerpInt<Vec4<int>,256>(a.color0, b.color0, t_int);
		color1 = LerpInt<Vec3<int>,256>(a.color1, b.color1, t_int);
	}

	ModelCoords modelpos;
	WorldCoords worldpos; // TODO: Storing this is dumb, should transform the light to clip space instead
	ClipCoords clippos;
	ScreenCoords screenpos; // TODO: Shouldn't store this ?
	Vec2<float> texturecoords;
	Vec3<float> normal;
	WorldCoords worldnormal;
	Vec4<int> color0;
	Vec3<int> color1;
};

class TransformUnit
{
public:
	static WorldCoords ModelToWorldNormal(const ModelCoords& coords);
	static WorldCoords ModelToWorld(const ModelCoords& coords);
	static ViewCoords WorldToView(const WorldCoords& coords);
	static ClipCoords ViewToClip(const ViewCoords& coords);
	static ScreenCoords ClipToScreen(const ClipCoords& coords);
	static DrawingCoords ScreenToDrawing(const ScreenCoords& coords);
	static ScreenCoords DrawingToScreen(const DrawingCoords& coords);

	static void SubmitSpline(void* control_points, void* indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertex_type);
	static void SubmitPrimitive(void* vertices, void* indices, u32 prim_type, int vertex_count, u32 vertex_type, int *bytesRead);

	static bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	static SplinePatch *patchBuffer_;
	static int patchBufferSize_;
};
