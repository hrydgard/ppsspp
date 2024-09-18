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

enum class CullType {
	CW = 0,
	CCW = 1,
	OFF = 2,
};

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

struct alignas(16) VertexData {
	Vec3Packedf texturecoords;
	float clipw;
	uint32_t color0;
	uint32_t color1;
	ScreenCoords screenpos;
	float fogdepth;
};

struct ClipVertexData {
	void Lerp(float t, const ClipVertexData &a, const ClipVertexData &b) {
		clippos = ::Lerp(a.clippos, b.clippos, t);
		// Ignore screenpos because Lerp() is only used pre-calculation of screenpos.
		v.texturecoords = ::Lerp(a.v.texturecoords, b.v.texturecoords, t);
		v.fogdepth = ::Lerp(a.v.fogdepth, b.v.fogdepth, t);

		u16 t_int = (u16)(t * 256);
		v.color0 = LerpInt<Vec4<int>, 256>(Vec4<int>::FromRGBA(a.v.color0), Vec4<int>::FromRGBA(b.v.color0), t_int).ToRGBA();
		v.color1 = LerpInt<Vec3<int>, 256>(Vec3<int>::FromRGB(a.v.color1), Vec3<int>::FromRGB(b.v.color1), t_int).ToRGB();
	}

	bool OutsideRange() const {
		return v.screenpos.x == 0x7FFFFFFF;
	}

	ClipCoords clippos;
	VertexData v;
};

class VertexReader;

class SoftwareDrawEngine;
class SoftwareVertexReader;

class TransformUnit {
public:
	TransformUnit();
	~TransformUnit();

	bool IsStarted();

	static WorldCoords ModelToWorldNormal(const ModelCoords& coords);
	static WorldCoords ModelToWorld(const ModelCoords& coords);
	static ViewCoords WorldToView(const WorldCoords& coords);
	static ClipCoords ViewToClip(const ViewCoords& coords);
	static ScreenCoords ClipToScreen(const ClipCoords &coords, bool *outsideRangeFlag);
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
	void SubmitImmVertex(const ClipVertexData &vert, SoftwareDrawEngine *drawEngine);

	static bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	void Flush(const char *reason);
	void FlushIfOverlap(const char *reason, bool modifying, uint32_t addr, uint32_t stride, uint32_t w, uint32_t h);
	void NotifyClutUpdate(const void *src);

	void GetStats(char *buffer, size_t bufsize);

	void SetDirty(SoftDirty flags);
	SoftDirty GetDirty();

private:
	ClipVertexData ReadVertex(const VertexReader &vreader, const TransformState &state);
	void SendTriangle(CullType cullType, const ClipVertexData *verts, int provoking = 2);

	u8 *decoded_ = nullptr;
	BinManager *binner_ = nullptr;

	// Normally max verts per prim is 3, but we temporarily need 4 to detect rectangles from strips.
	ClipVertexData data_[4];
	// This is the index of the next vert in data (or higher, may need modulus.)
	int data_index_ = 0;
	GEPrimitiveType prev_prim_ = GE_PRIM_POINTS;
	bool hasDraws_ = false;
	bool isImmDraw_ = false;

	friend SoftwareVertexReader;
};

class SoftwareDrawEngine : public DrawEngineCommon {
public:
	SoftwareDrawEngine();
	~SoftwareDrawEngine();

	void DeviceLost() override {}
	void DeviceRestore(Draw::DrawContext *draw) override {}

	void NotifyConfigChanged() override;
	void DispatchFlush() override;
	void DispatchSubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, bool clockwise, int *bytesRead) override;
	void DispatchSubmitImm(GEPrimitiveType prim, TransformedVertex *buffer, int vertexCount, int cullMode, bool continuation) override;

	VertexDecoder *FindVertexDecoder(u32 vtype);

	TransformUnit transformUnit;

#if PPSSPP_ARCH(32BIT)
#undef new
	void *operator new(size_t s) {
		return AllocateAlignedMemory(s, 16);
	}
	void operator delete(void *p) {
		FreeAlignedMemory(p);
	}
#endif

protected:
	bool UpdateUseHWTessellation(bool enable) const override { return false; }
};
