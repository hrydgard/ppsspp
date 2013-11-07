// Copyright (c) 2012- PPSSPP Project.

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

#include <map>

#include <d3d9.h>
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Directx9/VertexDecoderDX9.h"

struct DecVtxFormat;

namespace DX9 {

class LinkedShaderDX9;
class ShaderManagerDX9;
class TextureCacheDX9;
class FramebufferManagerDX9;

// States transitions:
// On creation: DRAWN_NEW
// DRAWN_NEW -> DRAWN_HASHING
// DRAWN_HASHING -> DRAWN_RELIABLE
// DRAWN_HASHING -> DRAWN_UNRELIABLE
// DRAWN_ONCE -> UNRELIABLE
// DRAWN_RELIABLE -> DRAWN_SAFE
// UNRELIABLE -> death
// DRAWN_ONCE -> death
// DRAWN_RELIABLE -> death


// Don't bother storing information about draws smaller than this.
enum {
	VERTEX_CACHE_THRESHOLD = 20,
};

// Try to keep this POD.
class VertexArrayInfoDX9 {
public:
	VertexArrayInfoDX9() {
		status = VAI_NEW;
		vbo = 0;
		ebo = 0;
		numDCs = 0;
		prim = GE_PRIM_INVALID;
		numDraws = 0;
		numFrames = 0;
		lastFrame = gpuStats.numFlips;
		numVerts = 0;
		drawsUntilNextFullHash = 0;
	}
	~VertexArrayInfoDX9();
	enum Status {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	u32 hash;

	Status status;

	LPDIRECT3DVERTEXBUFFER9 vbo;
	LPDIRECT3DINDEXBUFFER9 ebo;

	
	// Precalculated parameter for drawdrawElements
	u16 numVerts;
	s8 prim;

	// ID information
	u8 numDCs;
	int numDraws;
	int numFrames;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash;
};


// Handles transform, lighting and drawing.
class TransformDrawEngineDX9 {
public:
	TransformDrawEngineDX9();
	virtual ~TransformDrawEngineDX9();
	void SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertexType, int forceIndexType, int *bytesRead);
	void SubmitSpline(void* control_points, void* indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertex_type);
	void SubmitBezier(void* control_points, void* indices, int count_u, int count_v, GEPatchPrimType prim_type, u32 vertex_type);

	// legacy
	void DrawBezier(int ucount, int vcount);

	void DecodeVerts();
	void SetShaderManager(ShaderManagerDX9 *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetTextureCache(TextureCacheDX9 *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManagerDX9 *fbManager) {
		framebufferManager_ = fbManager;
	}
	void InitDeviceObjects();
	void DestroyDeviceObjects();
	void GLLost() {};

	void DecimateTrackedVertexArrays();
	void ClearTrackedVertexArrays();

	void SetupVertexDecoder(u32 vertType);

	// This requires a SetupVertexDecoder call first.
	int EstimatePerVertexCost();

	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

private:
	void DoFlush();
	void SoftwareTransformAndDraw(int prim, u8 *decoded, LinkedShaderDX9 *program, int vertexCount, u32 vertexType, void *inds, int indexType, const DecVtxFormat &decVtxFormat, int maxIndex);
	void ApplyDrawState(int prim);
	bool IsReallyAClear(int numVerts) const;

	// drawcall ID
	u32 ComputeFastDCID();
	u32 ComputeHash();  // Reads deferred vertex data.

	VertexDecoderDX9 *GetVertexDecoder(u32 vtype);

	// Defer all vertex decoding to a Flush, so that we can hash and cache the
	// generated buffers without having to redecode them every time.
	struct DeferredDrawCall {
		void *verts;
		void *inds;
		u32 vertType;
		u8 indexType;
		u8 prim;
		u16 vertexCount;
		u16 indexLowerBound;
		u16 indexUpperBound;
	};

	// Vertex collector state
	IndexGenerator indexGen;
	int collectedVerts;
	GEPrimitiveType prevPrim_;

	// Cached vertex decoders
	std::map<u32, VertexDecoderDX9 *> decoderMap_;
	VertexDecoderDX9 *dec_;	
	VertexDecoderJitCache *decJitCache_;
	u32 lastVType_;
	
	// Vertex collector buffers
	u8 *decoded;
	u16 *decIndex;

	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;

	std::map<u32, VertexArrayInfoDX9 *> vai_;
	
	// Other
	ShaderManagerDX9 *shaderManager_;
	TextureCacheDX9 *textureCache_;
	FramebufferManagerDX9 *framebufferManager_;

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };
	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls;
	int vertexCountInDrawCalls;

	int decimationCounter_;

	UVScale *uvScale;
};

// Only used by SW transform
struct Color4 {
	float a, r, g, b;

	Color4() : r(0), g(0), b(0), a(0) { }
	Color4(float _r, float _g, float _b, float _a=1.0f)
		: r(_r), g(_g), b(_b), a(_a) {
	}
	Color4(const float in[4]) {a=in[0];r=in[1];g=in[2];b=in[3];}
	Color4(const float in[3], float alpha) {r=in[0];g=in[1];b=in[2];a=alpha;}

	const float &operator [](int i) const {return *(&a + i);}

	Color4 operator *(float f) const {
		return Color4(f*r,f*g,f*b,f*a);
	}
	Color4 operator *(const Color4 &c) const {
		return Color4(r*c.r,g*c.g,b*c.b,a*c.a);
	}
	Color4 operator +(const Color4 &c) const {
		return Color4(r+c.r,g+c.g,b+c.b,a+c.a);
	}
	void operator +=(const Color4 &c) {
		r+=c.r;
		g+=c.g;
		b+=c.b;
		a+=c.a;
	}
	void GetFromRGB(u32 col) {
		b = ((col>>16) & 0xff)/255.0f;
		g = ((col>>8) & 0xff)/255.0f;
		r = ((col>>0) & 0xff)/255.0f;
	}
	void GetFromA(u32 col) {
		a = (col&0xff)/255.0f;
	}
};

};
