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
#include "GPU/Software/SoftGpu.h"
#include "GPU/Math3D.h"

using namespace Math3D;

static constexpr int32_t SCREEN_SCALE_FACTOR = 16;

typedef u16 u10; // TODO: erm... :/

typedef Vec3<float> ModelCoords;
typedef Vec3<float> WorldCoords;
typedef Vec3<float> ViewCoords;
typedef Vec4<float> ClipCoords; // Range: -w <= x/y/z <= w

class BinManager;
struct TransformState;

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

struct DrawingCoords {
	DrawingCoords() {}
	DrawingCoords(s16 x, s16 y) : x(x), y(y) {}

	s16 x;
	s16 y;
};

struct VertexData {
	void Lerp(float t, const VertexData &a, const VertexData &b) {
		clippos = ::Lerp(a.clippos, b.clippos, t);
		// Ignore screenpos because Lerp() is only used pre-calculation of screenpos.
		texturecoords = ::Lerp(a.texturecoords, b.texturecoords, t);
		fogdepth = ::Lerp(a.fogdepth, b.fogdepth, t);

		u16 t_int = (u16)(t*256);
		color0 = LerpInt<Vec4<int>,256>(a.color0, b.color0, t_int);
		color1 = LerpInt<Vec3<int>,256>(a.color1, b.color1, t_int);
	}

	ClipCoords clippos;
	Vec2<float> texturecoords;
	Vec4<int> color0;
	Vec3<int> color1;
	ScreenCoords screenpos; // TODO: Shouldn't store this ?
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
	static inline DrawingCoords ScreenToDrawing(int x, int y) {
		DrawingCoords ret;
		// When offset > coord, this is negative and force-scissors.
		ret.x = x / SCREEN_SCALE_FACTOR;
		ret.y = y / SCREEN_SCALE_FACTOR;
		return ret;
	}
	static inline DrawingCoords ScreenToDrawing(const ScreenCoords &coords) {
		return ScreenToDrawing(coords.x, coords.y);
	}
	static ScreenCoords DrawingToScreen(const DrawingCoords &coords, u16 z);

	void SubmitPrimitive(const void* vertices, const void* indices, GEPrimitiveType prim_type, int vertex_count, u32 vertex_type, int *bytesRead, SoftwareDrawEngine *drawEngine);

	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	void Flush(const char *reason);
	void FlushIfOverlap(const char *reason, uint32_t addr, uint32_t stride, uint32_t w, uint32_t h);
	void NotifyClutUpdate(const void *src);

	void GetStats(char *buffer, size_t bufsize);

	void SetDirty(SoftDirty flags);
	SoftDirty GetDirty();

private:
	VertexData ReadVertex(VertexReader &vreader, const TransformState &lstate, bool &outside_range_flag);

	u8 *decoded_ = nullptr;
	BinManager *binner_ = nullptr;
};

class SoftwareDrawEngine : public DrawEngineCommon {
public:
	SoftwareDrawEngine();
	~SoftwareDrawEngine();

	void DispatchFlush() override;
	void DispatchSubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int cullMode, int *bytesRead) override;
	void DispatchSubmitImm(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int cullMode, int *bytesRead) override;

	VertexDecoder *FindVertexDecoder(u32 vtype);

	TransformUnit transformUnit;

protected:
	bool UpdateUseHWTessellation(bool enable) override { return false; }
};
