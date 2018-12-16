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
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Math3D.h"

using namespace Math3D;

typedef u16 u10; // TODO: erm... :/

typedef Vec3<float> ModelCoords;
typedef Vec3<float> WorldCoords;
typedef Vec3<float> ViewCoords;
typedef Vec4<float> ClipCoords; // Range: -w <= x/y/z <= w

struct SplinePatch;

struct ScreenCoords
{
	ScreenCoords() {}
	ScreenCoords(int x, int y, u16 z) : x(x), y(y), z(z) {}

	int x;
	int y;
	u16 z;

	Vec2<int> xy() const { return Vec2<int>(x, y); }

	ScreenCoords operator * (const float t) const
	{
		return ScreenCoords((int)(x * t), (int)(y * t), (u16)(z * t));
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
	DrawingCoords(s16 x, s16 y, u16 z) : x(x), y(y), z(z) {}

	s16 x;
	s16 y;
	u16 z;

	Vec2<s16> xy() const { return Vec2<s16>(x, y); }

	DrawingCoords operator * (const float t) const
	{
		return DrawingCoords((s16)(x * t), (s16)(y * t), (u16)(z * t));
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
		fogdepth = ::Lerp(a.fogdepth, b.fogdepth, t);

		u16 t_int = (u16)(t*256);
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
	float fogdepth;
};

class VertexReader;

class SoftwareDrawEngine;

class TransformUnit {
public:
	TransformUnit();
	~TransformUnit();

	static WorldCoords ModelToWorldNormal(const ModelCoords& coords);
	static WorldCoords ModelToWorld(const ModelCoords& coords);
	static ViewCoords WorldToView(const WorldCoords& coords);
	static ClipCoords ViewToClip(const ViewCoords& coords);
	static ScreenCoords ClipToScreen(const ClipCoords& coords);
	static DrawingCoords ScreenToDrawing(const ScreenCoords& coords);
	static ScreenCoords DrawingToScreen(const DrawingCoords& coords);

	void SubmitPrimitive(void* vertices, void* indices, GEPrimitiveType prim_type, int vertex_count, u32 vertex_type, int *bytesRead, SoftwareDrawEngine *drawEngine);

	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);
	VertexData ReadVertex(VertexReader& vreader);

	bool outside_range_flag = false;
	u8 *buf;
};

class SoftwareDrawEngine : public DrawEngineCommon {
public:
	SoftwareDrawEngine();
	~SoftwareDrawEngine();

	void DispatchFlush() override;
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int cullMode, int *bytesRead) override;

	VertexDecoder *FindVertexDecoder(u32 vtype);

	TransformUnit transformUnit;
};
