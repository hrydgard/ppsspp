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

#include "IndexGenerator.h"
#include "VertexDecoder.h"
#include "gfx/gl_lost_manager.h"

class LinkedShader;
class ShaderManager;
struct DecVtxFormat;

// Handles transform, lighting and drawing.
class TransformDrawEngine : public GfxResourceHolder {
public:
	TransformDrawEngine();
	~TransformDrawEngine();
	void SubmitPrim(void *verts, void *inds, int prim, int vertexCount, u32 vertexType, int forceIndexType, int *bytesRead);
	void DrawBezier(int ucount, int vcount);
	void DrawSpline(int ucount, int vcount, int utype, int vtype);
	void Flush();
	void SetShaderManager(ShaderManager *shaderManager) {
		shaderManager_ = shaderManager;
	}

	void InitDeviceObjects();
	void DestroyDeviceObjects();
	void GLLost();

private:
	void SoftwareTransformAndDraw(int prim, u8 *decoded, LinkedShader *program, int vertexCount, u32 vertexType, void *inds, int indexType, const DecVtxFormat &decVtxFormat, int maxIndex);

	// Vertex collector state
	IndexGenerator indexGen;
	int collectedVerts;

	// Vertex collector buffers
	VertexDecoder dec;
	u32 lastVType;
	u8 *decoded;
	u16 *decIndex;

	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;

	// Vertex buffer objects
	// Element buffer objects
	enum { NUM_VBOS = 128 };
	GLuint vbo_[NUM_VBOS];
	GLuint ebo_[NUM_VBOS];
	int curVbo_;

	// Other
	ShaderManager *shaderManager_;
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
