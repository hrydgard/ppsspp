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

#include "IndexGenerator.h"
#include "VertexDecoder.h"
#include "gfx/gl_lost_manager.h"

class LinkedShader;
class ShaderManager;
class TextureCache;
class FramebufferManager;

struct DecVtxFormat;

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
class VertexArrayInfo {
public:
	VertexArrayInfo() {
		status = VAI_NEW;
		vbo = 0;
		ebo = 0;
		numDCs = 0;
		prim = -1;
		numDraws = 0;
		numFrames = 0;
		lastFrame = gpuStats.numFrames;
		numVerts = 0;
		drawsUntilNextFullHash = 0;
	}
	~VertexArrayInfo();
	enum Status {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	u32 hash;

	Status status;

	u32 vbo;
	u32 ebo;

	// TODO: see if we can avoid having this full thing here.
	DecVtxFormat decFmt;
	
	// Precalculated parameter for drawdrawElements
	u16 numVerts;
	u8 prim;

	// ID information
	u8 numDCs;
	int numDraws;
	int numFrames;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash;
};


// Handles transform, lighting and drawing.
class TransformDrawEngine : public GfxResourceHolder {
public:
	TransformDrawEngine();
	virtual ~TransformDrawEngine();
	void SubmitPrim(void *verts, void *inds, int prim, int vertexCount, u32 vertexType, int forceIndexType, int *bytesRead);
	void DrawBezier(int ucount, int vcount);
	void DrawSpline(int ucount, int vcount, int utype, int vtype);
	void DecodeVerts();
	void Flush();
	void SetShaderManager(ShaderManager *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetTextureCache(TextureCache *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManager *fbManager) {
		framebufferManager_ = fbManager;
	}
	void InitDeviceObjects();
	void DestroyDeviceObjects();
	void GLLost();

	void DecimateTrackedVertexArrays();
	void ClearTrackedVertexArrays();

	void SetupVertexDecoder(u32 vertType);

	// This requires a SetupVertexDecoder call first.
	int EstimatePerVertexCost();

private:
	void SoftwareTransformAndDraw(int prim, u8 *decoded, LinkedShader *program, int vertexCount, u32 vertexType, void *inds, int indexType, const DecVtxFormat &decVtxFormat, int maxIndex);
	void ApplyDrawState(int prim);
	bool IsReallyAClear(int numVerts) const;

	// drawcall ID
	u32 ComputeFastDCID();
	u32 ComputeHash();  // Reads deferred vertex data.

	VertexDecoder *GetVertexDecoder(u32 vtype);

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
	int prevPrim_;

	// Cached vertex decoders
	std::map<u32, VertexDecoder *> decoderMap_;
	VertexDecoder *dec_;
	u32 lastVType_;
	
	// Vertex collector buffers
	u8 *decoded;
	u16 *decIndex;

	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;

	std::map<u32, VertexArrayInfo *> vai_;

	// Vertex buffer objects
	// Element buffer objects
	enum { NUM_VBOS = 128 };
	GLuint vbo_[NUM_VBOS];
	GLuint ebo_[NUM_VBOS];
	int curVbo_;

	// Other
	ShaderManager *shaderManager_;
	TextureCache *textureCache_;
	FramebufferManager *framebufferManager_;

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };
	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls;
};

// Only used by SW transform
struct Color4 {
	float r, g, b, a;

	Color4() : r(0), g(0), b(0), a(0) { }
	Color4(float _r, float _g, float _b, float _a=1.0f)
		: r(_r), g(_g), b(_b), a(_a) {
	}
	Color4(const float in[4]) {r=in[0];g=in[1];b=in[2];a=in[3];}
	Color4(const float in[3], float alpha) {r=in[0];g=in[1];b=in[2];a=alpha;}

	const float &operator [](int i) const {return *(&r + i);}

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
