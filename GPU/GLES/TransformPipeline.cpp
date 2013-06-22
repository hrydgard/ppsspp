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

#include "base/timeutil.h"

#include "Common/MemoryUtil.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "native/gfx_es2/gl_state.h"
#include "native/ext/cityhash/city.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "StateMapping.h"
#include "TextureCache.h"
#include "TransformPipeline.h"
#include "VertexDecoder.h"
#include "ShaderManager.h"
#include "DisplayListInterpreter.h"

const GLuint glprim[8] = {
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	GL_TRIANGLES,	 // With OpenGL ES we have to expand sprites into triangles, tripling the data instead of doubling. sigh. OpenGL ES, Y U NO SUPPORT GL_QUADS?
};

enum {
	DECODED_VERTEX_BUFFER_SIZE = 65536 * 48,
	DECODED_INDEX_BUFFER_SIZE = 65536 * 20,
	TRANSFORMED_VERTEX_BUFFER_SIZE = 65536 * sizeof(TransformedVertex)
};

inline float clamp(float in, float min, float max) { 
	return in < min ? min : (in > max ? max : in); 
}

TransformDrawEngine::TransformDrawEngine()
	: collectedVerts(0),
		prevPrim_(-1),
		dec_(0),
		lastVType_(-1),
		curVbo_(0),
		shaderManager_(0),
		textureCache_(0),
		framebufferManager_(0),
		numDrawCalls(0) {
	// Allocate nicely aligned memory. Maybe graphics drivers will
	// appreciate it.
	// All this is a LOT of memory, need to see if we can cut down somehow.
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE);
	transformed = (TransformedVertex *)AllocateMemoryPages(TRANSFORMED_VERTEX_BUFFER_SIZE);
	transformedExpanded = (TransformedVertex *)AllocateMemoryPages(3 * TRANSFORMED_VERTEX_BUFFER_SIZE);
	memset(vbo_, 0, sizeof(vbo_));
	memset(ebo_, 0, sizeof(ebo_));
	indexGen.Setup(decIndex);
	InitDeviceObjects();
	register_gl_resource_holder(this);
}

TransformDrawEngine::~TransformDrawEngine() {
	DestroyDeviceObjects();
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);
	FreeMemoryPages(transformed, TRANSFORMED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(transformedExpanded, 3 * TRANSFORMED_VERTEX_BUFFER_SIZE);
	unregister_gl_resource_holder(this);
	for (auto iter = decoderMap_.begin(); iter != decoderMap_.end(); iter++) {
		delete iter->second;
	}
}

void TransformDrawEngine::InitDeviceObjects() {
	if (!vbo_[0]) {
		glGenBuffers(NUM_VBOS, &vbo_[0]);
		glGenBuffers(NUM_VBOS, &ebo_[0]);
	} else {
		ERROR_LOG(G3D, "Device objects already initialized!");
	}
}

void TransformDrawEngine::DestroyDeviceObjects() {
	glDeleteBuffers(NUM_VBOS, &vbo_[0]);
	glDeleteBuffers(NUM_VBOS, &ebo_[0]);
	memset(vbo_, 0, sizeof(vbo_));
	memset(ebo_, 0, sizeof(ebo_));
	ClearTrackedVertexArrays();
}

void TransformDrawEngine::GLLost() {
	// The objects have already been deleted.
	memset(vbo_, 0, sizeof(vbo_));
	memset(ebo_, 0, sizeof(ebo_));
	ClearTrackedVertexArrays();
	InitDeviceObjects();
}

// Just to get something on the screen, we'll just not subdivide correctly.
void TransformDrawEngine::DrawBezier(int ucount, int vcount) {
	u16 indices[3 * 3 * 6];

	Reporting::ReportMessage("Unsupported bezier curve");

	// if (gstate.patchprimitive)
	// Generate indices for a rectangular mesh.
	int c = 0;
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 3; x++) {
			indices[c++] = y * 4 + x;
			indices[c++] = y * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x;
			indices[c++] = y * 4 + x;
		}
	}

	// We are free to use the "decoded" buffer here.
	// Let's split it into two to get a second buffer, there's enough space.
	u8 *decoded2 = decoded + 65536 * 24;

	// Alright, now for the vertex data.
	// For now, we will simply inject UVs.

	float customUV[4 * 4 * 2];
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			customUV[(y * 4 + x) * 2 + 0] = (float)x/3.0f;
			customUV[(y * 4 + x) * 2 + 1] = (float)y/3.0f;
		}
	}

	if (!(gstate.vertType & GE_VTYPE_TC_MASK)) {
		VertexDecoder *dec = GetVertexDecoder(gstate.vertType);
		dec->SetVertexType(gstate.vertType);
		u32 newVertType = dec->InjectUVs(decoded2, Memory::GetPointer(gstate_c.vertexAddr), customUV, 16);
		SubmitPrim(decoded2, &indices[0], GE_PRIM_TRIANGLES, c, newVertType, GE_VTYPE_IDX_16BIT, 0);
	} else {
		SubmitPrim(Memory::GetPointer(gstate_c.vertexAddr), &indices[0], GE_PRIM_TRIANGLES, c, gstate.vertType, GE_VTYPE_IDX_16BIT, 0);
	}
	Flush();  // as our vertex storage here is temporary, it will only survive one draw.
}

// Copy code from bezier. This is not right, but allow to display something.
void TransformDrawEngine::DrawSpline(int ucount, int vcount, int utype, int vtype) {
	u16 indices[3 * 3 * 6];

	Reporting::ReportMessage("Unsupported spline curve");

	// Generate indices for a rectangular mesh.
	int c = 0;
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 3; x++) {
			indices[c++] = y * 4 + x;
			indices[c++] = y * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x + 1;
			indices[c++] = (y + 1) * 4 + x;
			indices[c++] = y * 4 + x;
		}
	}

	// We are free to use the "decoded" buffer here.
	// Let's split it into two to get a second buffer, there's enough space.
	u8 *decoded2 = decoded + 65536 * 24;

	// Alright, now for the vertex data.
	// For now, we will simply inject UVs.

	float customUV[4 * 4 * 2];
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			customUV[(y * 4 + x) * 2 + 0] = (float)x/3.0f;
			customUV[(y * 4 + x) * 2 + 1] = (float)y/3.0f;
		}
	}

	if (!(gstate.vertType & GE_VTYPE_TC_MASK)) {
		VertexDecoder *dec = GetVertexDecoder(gstate.vertType);
		dec->SetVertexType(gstate.vertType);
		u32 newVertType = dec->InjectUVs(decoded2, Memory::GetPointer(gstate_c.vertexAddr), customUV, 16);
		SubmitPrim(decoded2, &indices[0], GE_PRIM_TRIANGLES, c, newVertType, GE_VTYPE_IDX_16BIT, 0);
	} else {
		SubmitPrim(Memory::GetPointer(gstate_c.vertexAddr), &indices[0], GE_PRIM_TRIANGLES, c, gstate.vertType, GE_VTYPE_IDX_16BIT, 0);
	}
	Flush();  // as our vertex storage here is temporary, it will only survive one draw.
}

// Convenient way to do precomputation to save the parts of the lighting calculation
// that's common between the many vertices of a draw call.
class Lighter {
public:
	Lighter();
	void Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], Vec3 pos, Vec3 normal);

private:
	bool disabled_;
	Color4 globalAmbient;
	Color4 materialEmissive;
	Color4 materialAmbient;
	Color4 materialDiffuse;
	Color4 materialSpecular;
	float specCoef_;
	// Vec3 viewer_;
	bool doShadeMapping_;
	int materialUpdate_;
};

Lighter::Lighter() {
	doShadeMapping_ = (gstate.texmapmode & 0x3) == 2;
	materialEmissive.GetFromRGB(gstate.materialemissive);
	materialEmissive.a = 0.0f;
	globalAmbient.GetFromRGB(gstate.ambientcolor);
	globalAmbient.GetFromA(gstate.ambientalpha);
	materialAmbient.GetFromRGB(gstate.materialambient);
	materialAmbient.GetFromA(gstate.materialalpha);
	materialDiffuse.GetFromRGB(gstate.materialdiffuse);
	materialDiffuse.a = 1.0f;
	materialSpecular.GetFromRGB(gstate.materialspecular);
	materialSpecular.a = 1.0f;
	specCoef_ = getFloat24(gstate.materialspecularcoef);
	// viewer_ = Vec3(-gstate.viewMatrix[9], -gstate.viewMatrix[10], -gstate.viewMatrix[11]);
	materialUpdate_ = gstate.materialupdate & 7;
}

void Lighter::Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], Vec3 pos, Vec3 norm)
{
	Color4 in(colorIn);

	const Color4 *ambient;
	if (materialUpdate_ & 1)
		ambient = &in;
	else
		ambient = &materialAmbient;

	const Color4 *diffuse;
	if (materialUpdate_ & 2)
		diffuse = &in;
	else
		diffuse = &materialDiffuse;

	const Color4 *specular;
	if (materialUpdate_ & 4)
		specular = &in;
	else
		specular = &materialSpecular;

	Color4 lightSum0 = globalAmbient * *ambient + materialEmissive;
	Color4 lightSum1(0, 0, 0, 0);

	for (int l = 0; l < 4; l++)
	{
		// can we skip this light?
		if ((gstate.lightEnable[l] & 1) == 0)
			continue;

		GELightComputation comp = (GELightComputation)(gstate.ltype[l] & 3);
		GELightType type = (GELightType)((gstate.ltype[l] >> 8) & 3);
		
		Vec3 toLight(0,0,0);
		Vec3 lightDir(0,0,0);
		
		if (type == GE_LIGHTTYPE_DIRECTIONAL)
			toLight = Vec3(gstate_c.lightpos[l]);  // lightdir is for spotlights
		else
			toLight = Vec3(gstate_c.lightpos[l]) - pos;

		bool doSpecular = (comp != GE_LIGHTCOMP_ONLYDIFFUSE);
		bool poweredDiffuse = comp == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE;
		
		float distanceToLight = toLight.Length();
		float dot = 0.0f;
		float angle = 0.0f;
		float lightScale = 0.0f;
		
		if (distanceToLight > 0.0f) {
			toLight /= distanceToLight;
			dot = toLight * norm;
		}
		// Clamp dot to zero.
		if (dot < 0.0f) dot = 0.0f;

		if (poweredDiffuse)
			dot = powf(dot, specCoef_);

		// Attenuation
		switch (type) {
		case GE_LIGHTTYPE_DIRECTIONAL:
			lightScale = 1.0f;
			break;
		case GE_LIGHTTYPE_POINT:
			lightScale = clamp(1.0f / (gstate_c.lightatt[l][0] + gstate_c.lightatt[l][1]*distanceToLight + gstate_c.lightatt[l][2]*distanceToLight*distanceToLight), 0.0f, 1.0f);
			break;
		case GE_LIGHTTYPE_SPOT:
			lightDir = gstate_c.lightdir[l];
			angle = toLight.Normalized() * lightDir.Normalized();
			if (angle >= gstate_c.lightangle[l])
				lightScale = clamp(1.0f / (gstate_c.lightatt[l][0] + gstate_c.lightatt[l][1]*distanceToLight + gstate_c.lightatt[l][2]*distanceToLight*distanceToLight), 0.0f, 1.0f) * powf(angle, gstate_c.lightspotCoef[l]);
			break;
		default:
			// ILLEGAL
			break;
		}

		Color4 lightDiff(gstate_c.lightColor[1][l], 0.0f);
		Color4 diff = (lightDiff * *diffuse) * dot;

		// Real PSP specular
		Vec3 toViewer(0,0,1);
		// Better specular
		// Vec3 toViewer = (viewer - pos).Normalized();

		if (doSpecular)
		{
			Vec3 halfVec = (toLight + toViewer);
			halfVec.Normalize();

			dot = halfVec * norm;
			if (dot > 0.0f)
			{
				Color4 lightSpec(gstate_c.lightColor[2][l], 0.0f);
				lightSum1 += (lightSpec * *specular * (powf(dot, specCoef_) * lightScale));
			}
		}

		if (gstate.lightEnable[l] & 1)
		{
			Color4 lightAmbient(gstate_c.lightColor[0][l], 0.0f);
			lightSum0 += (lightAmbient * *ambient + diff) * lightScale;
		}
	}

	// 4?
	for (int i = 0; i < 4; i++) {
		colorOut0[i] = lightSum0[i] > 1.0f ? 1.0f : lightSum0[i];
		colorOut1[i] = lightSum1[i] > 1.0f ? 1.0f : lightSum1[i];
	}
}

struct GlTypeInfo {
	u16 type;
	u8 count;
	u8 normalized;
};

static const GlTypeInfo GLComp[] = {
	{0}, // 	DEC_NONE,
	{GL_FLOAT, 1, GL_FALSE}, // 	DEC_FLOAT_1,
	{GL_FLOAT, 2, GL_FALSE}, // 	DEC_FLOAT_2,
	{GL_FLOAT, 3, GL_FALSE}, // 	DEC_FLOAT_3,
	{GL_FLOAT, 4, GL_FALSE}, // 	DEC_FLOAT_4,
	{GL_BYTE, 4, GL_TRUE}, // 	DEC_S8_3,
	{GL_SHORT, 4, GL_TRUE},// 	DEC_S16_3,
	{GL_UNSIGNED_BYTE, 1, GL_TRUE},// 	DEC_U8_1,
	{GL_UNSIGNED_BYTE, 2, GL_TRUE},// 	DEC_U8_2,
	{GL_UNSIGNED_BYTE, 3, GL_TRUE},// 	DEC_U8_3,
	{GL_UNSIGNED_BYTE, 4, GL_TRUE},// 	DEC_U8_4,
	{GL_UNSIGNED_SHORT, 1, GL_TRUE},// 	DEC_U16_1,
	{GL_UNSIGNED_SHORT, 2, GL_TRUE},// 	DEC_U16_2,
	{GL_UNSIGNED_SHORT, 3, GL_TRUE},// 	DEC_U16_3,
	{GL_UNSIGNED_SHORT, 4, GL_TRUE},// 	DEC_U16_4,
	{GL_UNSIGNED_BYTE,  2, GL_FALSE},// 	DEC_U8A_2,
	{GL_UNSIGNED_SHORT, 2, GL_FALSE},// 	DEC_U16A_2,
};

static inline void VertexAttribSetup(int attrib, int fmt, int stride, u8 *ptr) {
	if (attrib != -1 && fmt) {
		const GlTypeInfo &type = GLComp[fmt];
		glVertexAttribPointer(attrib, type.count, type.type, type.normalized, stride, ptr);
	}
}

// TODO: Use VBO and get rid of the vertexData pointers - with that, we will supply only offsets
static void SetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt, u8 *vertexData) {
	VertexAttribSetup(program->a_weight0123, decFmt.w0fmt, decFmt.stride, vertexData + decFmt.w0off);
	VertexAttribSetup(program->a_weight4567, decFmt.w1fmt, decFmt.stride, vertexData + decFmt.w1off);
	VertexAttribSetup(program->a_texcoord, decFmt.uvfmt, decFmt.stride, vertexData + decFmt.uvoff);
	VertexAttribSetup(program->a_color0, decFmt.c0fmt, decFmt.stride, vertexData + decFmt.c0off);
	VertexAttribSetup(program->a_color1, decFmt.c1fmt, decFmt.stride, vertexData + decFmt.c1off);
	VertexAttribSetup(program->a_normal, decFmt.nrmfmt, decFmt.stride, vertexData + decFmt.nrmoff);
	VertexAttribSetup(program->a_position, decFmt.posfmt, decFmt.stride, vertexData + decFmt.posoff);
}

// The verts are in the order:  BR BL TL TR
static void SwapUVs(TransformedVertex &a, TransformedVertex &b) {
	float tempu = a.u;
	float tempv = a.v;
	a.u = b.u;
	a.v = b.v;
	b.u = tempu;
	b.v = tempv;
}

// 2   3       3   2        0   3          2   1
//        to           to            or
// 1   0       0   1        1   2          3   0


// See comment below where this was called before.
/*
static void RotateUV(TransformedVertex v[4]) {
	float x1 = v[2].x;
	float x2 = v[0].x;
	float y1 = v[2].y;
	float y2 = v[0].y;

	if ((x1 < x2 && y1 < y2) || (x1 > x2 && y1 > y2))
		SwapUVs(v[1], v[3]);
}*/

static void RotateUVThrough(TransformedVertex v[4]) {
	float x1 = v[2].x;
	float x2 = v[0].x;
	float y1 = v[2].y;
	float y2 = v[0].y;

	if ((x1 < x2 && y1 > y2) || (x1 > x2 && y1 < y2))
		SwapUVs(v[1], v[3]);
}


// Clears on the PSP are best done by drawing a series of vertical strips
// in clear mode. This tries to detect that.
bool TransformDrawEngine::IsReallyAClear(int numVerts) const {
	if (transformed[0].x != 0.0f || transformed[0].y != 0.0f)
		return false;

	u32 matchcolor;
	memcpy(&matchcolor, transformed[0].color0, 4);
	float matchz = transformed[0].z;

	int bufW = gstate_c.curRTWidth;
	int bufH = gstate_c.curRTHeight;

	float prevX = 0.0f;
	for (int i = 1; i < numVerts; i++) {
		u32 vcolor;
		memcpy(&vcolor, transformed[i].color0, 4);
		if (vcolor != matchcolor || transformed[i].z != matchz)
			return false;
		
		if ((i & 1) == 0) {
			// Top left of a rectangle
			if (transformed[i].y != 0)
				return false;
			if (i > 0 && transformed[i].x != transformed[i - 1].x)
				return false;
		} else {
			// Bottom right
			if (transformed[i].y != bufH)
				return false;
			if (transformed[i].x <= transformed[i - 1].x)
				return false;
		}
	}

	// The last vertical strip often extends outside the drawing area.
	if (transformed[numVerts - 1].x < bufW)
		return false;

	return true;
}

// This is the software transform pipeline, which is necessary for supporting RECT
// primitives correctly, and may be easier to use for debugging than the hardware
// transform pipeline.

// There's code here that simply expands transformed RECTANGLES into plain triangles.

// We're gonna have to keep software transforming RECTANGLES, unless we use a geom shader which we can't on OpenGL ES 2.0.
// Usually, though, these primitives don't use lighting etc so it's no biggie performance wise, but it would be nice to get rid of
// this code.

// Actually, if we find the camera-relative right and down vectors, it might even be possible to add the extra points in pre-transformed
// space and thus make decent use of hardware transform.

// Actually again, single quads could be drawn more efficiently using GL_TRIANGLE_STRIP, no need to duplicate verts as for
// GL_TRIANGLES. Still need to sw transform to compute the extra two corners though.
void TransformDrawEngine::SoftwareTransformAndDraw(
		int prim, u8 *decoded, LinkedShader *program, int vertexCount, u32 vertType, void *inds, int indexType, const DecVtxFormat &decVtxFormat, int maxIndex) {

	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
	bool lmode = (gstate.lmode & 1) && gstate.isLightingEnabled();

	// TODO: Split up into multiple draw calls for GLES 2.0 where you can't guarantee support for more than 0x10000 verts.

#if defined(USING_GLES2)
	if (vertexCount > 0x10000/3)
		vertexCount = 0x10000/3;
#endif

	float uscale = 1.0f;
	float vscale = 1.0f;
	if (throughmode) {
		uscale /= gstate_c.curTextureWidth;
		vscale /= gstate_c.curTextureHeight;
	}

	Lighter lighter;
	float fog_end = getFloat24(gstate.fog1);
	float fog_slope = getFloat24(gstate.fog2);

	VertexReader reader(decoded, decVtxFormat, vertType);
	for (int index = 0; index < maxIndex; index++) {
		reader.Goto(index);

		float v[3] = {0, 0, 0};
		float c0[4] = {1, 1, 1, 1};
		float c1[4] = {0, 0, 0, 0};
		float uv[3] = {0, 0, 0};
		float fogCoef = 1.0f;

		if (throughmode) {
			// Do not touch the coordinates or the colors. No lighting.
			reader.ReadPos(v);
			if (reader.hasColor0()) {
				reader.ReadColor0(c0);
				for (int j = 0; j < 4; j++) {
					c1[j] = 0.0f;
				}
			} else {
				c0[0] = (gstate.materialambient & 0xFF) / 255.f;
				c0[1] = ((gstate.materialambient >> 8)  & 0xFF) / 255.f;
				c0[2] = ((gstate.materialambient >> 16) & 0xFF) / 255.f;
				c0[3] = (gstate.materialalpha & 0xFF) / 255.f;
			}

			if (reader.hasUV()) {
				reader.ReadUV(uv);

				uv[0] *= uscale;
				uv[1] *= vscale;
			}
			fogCoef = 1.0f;
			// Scale UV?
		} else {
			// We do software T&L for now
			float out[3], norm[3];
			float pos[3], nrm[3];
			Vec3 normal(0, 0, 1);
			reader.ReadPos(pos);
			if (reader.hasNormal())
				reader.ReadNrm(nrm);

			if ((vertType & GE_VTYPE_WEIGHT_MASK) == GE_VTYPE_WEIGHT_NONE) {
				Vec3ByMatrix43(out, pos, gstate.worldMatrix);
				if (reader.hasNormal()) {
					Norm3ByMatrix43(norm, nrm, gstate.worldMatrix);
					normal = Vec3(norm).Normalized();
				}
			} else {
				float weights[8];
				reader.ReadWeights(weights);
				// Skinning
				Vec3 psum(0,0,0);
				Vec3 nsum(0,0,0);
				int nweights = ((vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT) + 1;
				for (int i = 0; i < nweights; i++)
				{
					if (weights[i] != 0.0f) {
						Vec3ByMatrix43(out, pos, gstate.boneMatrix+i*12);
						Vec3 tpos(out);
						psum += tpos * weights[i];
						if (reader.hasNormal()) {
							Norm3ByMatrix43(norm, nrm, gstate.boneMatrix+i*12);
							Vec3 tnorm(norm);
							nsum += tnorm * weights[i];
						}
					}
				}

				// Yes, we really must multiply by the world matrix too.
				Vec3ByMatrix43(out, psum.v, gstate.worldMatrix);
				if (reader.hasNormal()) {
					Norm3ByMatrix43(norm, nsum.v, gstate.worldMatrix);
					normal = Vec3(norm).Normalized();
				}
			}

			// Perform lighting here if enabled. don't need to check through, it's checked above.
			float unlitColor[4] = {1, 1, 1, 1};
			if (reader.hasColor0()) {
				reader.ReadColor0(unlitColor);
			} else {
				unlitColor[0] = (gstate.materialambient & 0xFF) / 255.f;
				unlitColor[1] = ((gstate.materialambient >> 8)  & 0xFF) / 255.f;
				unlitColor[2] = ((gstate.materialambient >> 16) & 0xFF) / 255.f;
				unlitColor[3] = (gstate.materialalpha & 0xFF) / 255.f;
			}
			float litColor0[4];
			float litColor1[4];
			lighter.Light(litColor0, litColor1, unlitColor, out, normal);

			if (gstate.isLightingEnabled()) {
				// Don't ignore gstate.lmode - we should send two colors in that case
				for (int j = 0; j < 4; j++) {
					c0[j] = litColor0[j];
				}
				if (lmode) {
					// Separate colors
					for (int j = 0; j < 4; j++) {
						c1[j] = litColor1[j];
					}
				} else {
					// Summed color into c0
					for (int j = 0; j < 4; j++) {
						c0[j] = ((c0[j] + litColor1[j]) > 1.0f) ? 1.0f : (c0[j] + litColor1[j]);
					}
				}
			} else {
				if (reader.hasColor0()) {
					for (int j = 0; j < 4; j++) {
						c0[j] = unlitColor[j];
					}
				} else {
					c0[0] = (gstate.materialambient & 0xFF) / 255.f;
					c0[1] = ((gstate.materialambient >> 8) & 0xFF) / 255.f;
					c0[2] = ((gstate.materialambient >> 16)& 0xFF) / 255.f;
					c0[3] = (gstate.materialalpha & 0xFF) / 255.f;
				}
				if (lmode) {
					for (int j = 0; j < 4; j++) {
						c1[j] = 0.0f;
					}
				}
			}

			float ruv[2] = {0.0f, 0.0f};
			if (reader.hasUV())
				reader.ReadUV(ruv);

			// Perform texture coordinate generation after the transform and lighting - one style of UV depends on lights.
			switch (gstate.getUVGenMode())
			{
			case 0:	// UV mapping
				// Texture scale/offset is only performed in this mode.
				uv[0] = uscale * (ruv[0]*gstate_c.uScale + gstate_c.uOff);
				uv[1] = vscale * (ruv[1]*gstate_c.vScale + gstate_c.vOff);
				uv[2] = 1.0f;
				break;
			case 1:
				{
					// Projection mapping
					Vec3 source;
					switch (gstate.getUVProjMode())
					{
					case 0: // Use model space XYZ as source
						source = pos;
						break;
					case 1: // Use unscaled UV as source
						source = Vec3(ruv[0], ruv[1], 0.0f);
						break;
					case 2: // Use normalized normal as source
						source = Vec3(norm).Normalized();
						break;
					case 3: // Use non-normalized normal as source!
						source = Vec3(norm);
						break;
					}

					float uvw[3];
					Vec3ByMatrix43(uvw, &source.x, gstate.tgenMatrix);
					uv[0] = uvw[0];
					uv[1] = uvw[1];
					uv[2] = uvw[2];
				}
				break;
			case 2:
				// Shade mapping - use two light sources to generate U and V.
				{
					Vec3 lightpos0 = Vec3(gstate_c.lightpos[gstate.getUVLS0()]).Normalized();
					Vec3 lightpos1 = Vec3(gstate_c.lightpos[gstate.getUVLS1()]).Normalized();

					uv[0] = (1.0f + (lightpos0 * normal))/2.0f;
					uv[1] = (1.0f - (lightpos1 * normal))/2.0f;
					uv[2] = 1.0f;
				}
				break;
			case 3:
				// Illegal
				break;
			}

			// Transform the coord by the view matrix.
			Vec3ByMatrix43(v, out, gstate.viewMatrix);
			fogCoef = (v[2] + fog_end) * fog_slope;
		}

		// TODO: Write to a flexible buffer, we don't always need all four components.
		memcpy(&transformed[index].x, v, 3 * sizeof(float));
		transformed[index].fog = fogCoef;
		memcpy(&transformed[index].u, uv, 3 * sizeof(float));
		if (gstate_c.flipTexture) {
			if (throughmode)
				transformed[index].v = 1.0f - transformed[index].v;
			else 
				transformed[index].v = 1.0f - transformed[index].v * 2.0f;
		}
		for (int i = 0; i < 4; i++) {
			transformed[index].color0[i] = c0[i] * 255.0f;
		}
		for (int i = 0; i < 3; i++) {
			transformed[index].color1[i] = c1[i] * 255.0f;
		}
	}

	// Here's the best opportunity to try to detect rectangles used to clear the screen, and 
	// replace them with real OpenGL clears. This can provide a speedup on certain mobile chips.
	// Disabled for now - depth does not come out exactly the same

	if (false && maxIndex > 1 && gstate.isModeClear() && prim == GE_PRIM_RECTANGLES && IsReallyAClear(maxIndex)) {
		u32 clearColor;
		memcpy(&clearColor, transformed[0].color0, 4);
		float clearDepth = transformed[0].z;
		const float col[4] = {
			((clearColor & 0xFF)) / 255.0f,
			((clearColor & 0xFF00) >> 8) / 255.0f,
			((clearColor & 0xFF0000) >> 16) / 255.0f,
			((clearColor & 0xFF000000) >> 24) / 255.0f,
		};
		int target = 0;
		if ((gstate.clearmode >> 8) & 3) target |= GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
		if ((gstate.clearmode >> 10) & 1) target |= GL_DEPTH_BUFFER_BIT;

		bool colorMask = (gstate.clearmode >> 8) & 1;
		bool alphaMask = (gstate.clearmode >> 9) & 1;
		glstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);
		glstate.stencilTest.set(false);
		glstate.scissorTest.set(false);
		bool depthMask = (gstate.clearmode >> 10) & 1;

		glClearColor(col[0], col[1], col[2], col[3]);
#ifdef USING_GLES2
		glClearDepthf(clearDepth);
#else
		glClearDepth(clearDepth);
#endif
		glClearStencil(0);  // TODO - take from alpha?
		glClear(target);
		return;
	}

	// Step 2: expand rectangles.
	const TransformedVertex *drawBuffer = transformed;
	int numTrans = 0;

	bool drawIndexed = false;

	if (prim != GE_PRIM_RECTANGLES) {
		// We can simply draw the unexpanded buffer.
		numTrans = vertexCount;
		drawIndexed = true;
	} else {
		numTrans = 0;
		drawBuffer = transformedExpanded;
		TransformedVertex *trans = &transformedExpanded[0];
		TransformedVertex saved;
		for (int i = 0; i < vertexCount; i++) {
			int index = ((u16*)inds)[i];

			TransformedVertex &transVtx = transformed[index];
			if ((i & 1) == 0)
			{
				// Save this vertex so we can generate when we get the next one. Color is taken from the last vertex.
				saved = transVtx;
			}
			else
			{
				// We have to turn the rectangle into two triangles, so 6 points. Sigh.

				// bottom right
				trans[0] = transVtx;

				// bottom left
				trans[1] = transVtx;
				trans[1].y = saved.y;
				trans[1].v = saved.v;

				// top left
				trans[2] = transVtx;
				trans[2].x = saved.x;
				trans[2].y = saved.y;
				trans[2].u = saved.u;
				trans[2].v = saved.v;

				// top right
				trans[3] = transVtx;
				trans[3].x = saved.x;
				trans[3].u = saved.u;

				// That's the four corners. Now process UV rotation.
				if (throughmode)
					RotateUVThrough(trans);
				// Apparently, non-through RotateUV just breaks things.
				// If we find a game where it helps, we'll just have to figure out how they differ.
				// Possibly, it has something to do with flipped viewport Y axis, which a few games use.
				// else
				//	RotateUV(trans);

				// bottom right
				trans[4] = trans[0];

				// top left
				trans[5] = trans[2];
				trans += 6;

				numTrans += 6;
			}
		}
	}


	// TODO: Add a post-transform cache here for multi-RECTANGLES only.
	// Might help for text drawing.

	// these spam the gDebugger log.
	const int vertexSize = sizeof(transformed[0]);

	bool useVBO = g_Config.bUseVBO;
	if (useVBO) {
		//char title[64];
		//sprintf(title, "upload %i verts for sw", indexGen.VertexCount());
		//LoggingDeadline deadline(title, 5);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_[curVbo_]);
		glBufferData(GL_ARRAY_BUFFER, vertexSize * numTrans, drawBuffer, GL_STREAM_DRAW);
		drawBuffer = 0;  // so that the calls use offsets instead.
	}		
	bool doTextureProjection = gstate.getUVGenMode() == 1;
	glVertexAttribPointer(program->a_position, 4, GL_FLOAT, GL_FALSE, vertexSize, drawBuffer);
	if (program->a_texcoord != -1) glVertexAttribPointer(program->a_texcoord, doTextureProjection ? 3 : 2, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)drawBuffer) + 4 * 4);
	if (program->a_color0 != -1) glVertexAttribPointer(program->a_color0, 4, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, ((uint8_t*)drawBuffer) + 7 * 4);
	if (program->a_color1 != -1) glVertexAttribPointer(program->a_color1, 3, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, ((uint8_t*)drawBuffer) + 8 * 4);
	if (drawIndexed) {
		if (useVBO) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_[curVbo_]);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(short) * numTrans, inds, GL_STREAM_DRAW);
			inds = 0;
		}
		glDrawElements(glprim[prim], numTrans, GL_UNSIGNED_SHORT, inds);
		if (useVBO) {
			// Attempt to orphan the buffer we used so the GPU can alloc a new one.
			// glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(short) * numTrans, 0, GL_DYNAMIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
	} else {
		glDrawArrays(glprim[prim], 0, numTrans);
	}
	if (useVBO) {
		// Attempt to orphan the buffer we used so the GPU can alloc a new one.
		// glBufferData(GL_ARRAY_BUFFER, vertexSize * numTrans, 0, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		curVbo_++;
		if (curVbo_ == NUM_VBOS)
			curVbo_ = 0;
	}
}

VertexDecoder *TransformDrawEngine::GetVertexDecoder(u32 vtype) {
	auto iter = decoderMap_.find(vtype);
	if (iter != decoderMap_.end())
		return iter->second;
	VertexDecoder *dec = new VertexDecoder(); 
	dec->SetVertexType(vtype);
	decoderMap_[vtype] = dec;
	return dec;
}

void TransformDrawEngine::SetupVertexDecoder(u32 vertType) {
	// If vtype has changed, setup the vertex decoder.
	// TODO: Simply cache the setup decoders instead.
	if (vertType != lastVType_) {
		dec_ = GetVertexDecoder(vertType);
		lastVType_ = vertType;
	}
}

int TransformDrawEngine::EstimatePerVertexCost() {
	// TODO: This is transform cost, also account for rasterization cost somehow... although it probably
	// runs in parallel with transform.

	// Also, this is all pure guesswork. If we can find a way to do measurements, that would be great.

	// GTA wants a low value to run smooth, GoW wants a high value (otherwise it thinks things
	// went too fast and starts doing all the work over again).

	int cost = 20;
	if (gstate.isLightingEnabled()) {
		cost += 10;
	}

	for (int i = 0; i < 4; i++) {
		if (gstate.lightEnable[i] & 1)
			cost += 20;
	}
	if (gstate.getUVGenMode() != 0) {
		cost += 20;
	}
	if (dec_ && dec_->morphcount > 1) {
		cost += 5 * dec_->morphcount;
	}

	if (CoreTiming::GetClockFrequencyMHz() == 333) {
		// Just brutally double to make God of War happier.
		// FUDGE FACTORS! Delicious fudge factors!
		cost *= 2;
	}
	return cost;
}

void TransformDrawEngine::SubmitPrim(void *verts, void *inds, int prim, int vertexCount, u32 vertType, int forceIndexType, int *bytesRead) {
	if (vertexCount == 0)
		return;  // we ignore zero-sized draw calls.

	if (!indexGen.PrimCompatible(prevPrim_, prim) || numDrawCalls >= MAX_DEFERRED_DRAW_CALLS)
		Flush();
	prevPrim_ = prim;
	SetupVertexDecoder(vertType);

	dec_->IncrementStat(STAT_VERTSSUBMITTED, vertexCount);

	if (bytesRead)
		*bytesRead = vertexCount * dec_->VertexSize();

	gpuStats.numDrawCalls++;
	gpuStats.numVertsSubmitted += vertexCount;

	DeferredDrawCall &dc = drawCalls[numDrawCalls++];
	dc.verts = verts;
	dc.inds = inds;
	dc.vertType = vertType;
	dc.indexType = ((forceIndexType == -1) ? (vertType & GE_VTYPE_IDX_MASK) : forceIndexType) >> GE_VTYPE_IDX_SHIFT;
	dc.prim = prim;
	dc.vertexCount = vertexCount;
	if (inds) {
		GetIndexBounds(inds, vertexCount, vertType, &dc.indexLowerBound, &dc.indexUpperBound);
	} else {
		dc.indexLowerBound = 0;
		dc.indexUpperBound = vertexCount - 1;
	}
}

void TransformDrawEngine::DecodeVerts() {
	for (int i = 0; i < numDrawCalls; i++) {
		const DeferredDrawCall &dc = drawCalls[i];

		indexGen.SetIndex(collectedVerts);
		int indexLowerBound = dc.indexLowerBound, indexUpperBound = dc.indexUpperBound;

		u32 indexType = dc.indexType;
		void *inds = dc.inds;
		if (indexType == GE_VTYPE_IDX_NONE >> GE_VTYPE_IDX_SHIFT) {
			// Decode the verts and apply morphing. Simple.
			dec_->DecodeVerts(decoded + collectedVerts * (int)dec_->GetDecVtxFmt().stride,
				dc.verts, indexLowerBound, indexUpperBound);
			collectedVerts += indexUpperBound - indexLowerBound + 1;
			indexGen.AddPrim(dc.prim, dc.vertexCount);
		} else {
			// It's fairly common that games issue long sequences of PRIM calls, with differing
			// inds pointer but the same base vertex pointer. We'd like to reuse vertices between
			// these as much as possible, so we make sure here to combine as many as possible
			// into one nice big drawcall, sharing data.

			// 1. Look ahead to find the max index, only looking as "matching" drawcalls.
			//    Expand the lower and upper bounds as we go.
			int j = i + 1;
			int lastMatch = i;
			while (j < numDrawCalls) {
				if (drawCalls[j].verts != dc.verts)
					break;
				indexLowerBound = std::min(indexLowerBound, (int)drawCalls[j].indexLowerBound);
				indexUpperBound = std::max(indexUpperBound, (int)drawCalls[j].indexUpperBound);
				lastMatch = j;
				j++;
			}
			
			// 2. Loop through the drawcalls, translating indices as we go.
			for (j = i; j <= lastMatch; j++) {
				switch (indexType) {
				case GE_VTYPE_IDX_8BIT >> GE_VTYPE_IDX_SHIFT:
					indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u8 *)drawCalls[j].inds, indexLowerBound);
					break;
				case GE_VTYPE_IDX_16BIT >> GE_VTYPE_IDX_SHIFT:
					indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u16 *)drawCalls[j].inds, indexLowerBound);
					break;
				}
			}

			int vertexCount = indexUpperBound - indexLowerBound + 1;
			// 3. Decode that range of vertex data.
			dec_->DecodeVerts(decoded + collectedVerts * (int)dec_->GetDecVtxFmt().stride,
				dc.verts, indexLowerBound, indexUpperBound);
			collectedVerts += vertexCount;

			// 4. Advance indexgen vertex counter.
			indexGen.Advance(vertexCount);
			i = lastMatch;
		}
	}
}

u32 TransformDrawEngine::ComputeHash() {
	u32 fullhash = 0;
	int vertexSize = dec_->GetDecVtxFmt().stride;

	// TODO: Add some caps both for numDrawCalls and num verts to check?
	for (int i = 0; i < numDrawCalls; i++) {
		if (!drawCalls[i].inds) {
			fullhash += CityHash32((const char *)drawCalls[i].verts, vertexSize * drawCalls[i].vertexCount);
		} else {
			// This could get seriously expensive with sparse indices. Need to combine hashing ranges the same way
			// we do when drawing.
			fullhash += CityHash32((const char *)drawCalls[i].verts + vertexSize * drawCalls[i].indexLowerBound,
				vertexSize * (drawCalls[i].indexUpperBound - drawCalls[i].indexLowerBound));
			int indexSize = (dec_->VertexType() & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT ? 2 : 1;
			fullhash += CityHash32((const char *)drawCalls[i].inds, indexSize * drawCalls[i].vertexCount);
		}
	}

	return fullhash;
}

u32 TransformDrawEngine::ComputeFastDCID() {
	u32 hash = 0;
	for (int i = 0; i < numDrawCalls; i++) {
		hash ^= (u32)(uintptr_t)drawCalls[i].verts;
		hash = __rotl(hash, 13);
		hash ^= (u32)(uintptr_t)drawCalls[i].inds;
		hash = __rotl(hash, 13);
		hash ^= (u32)drawCalls[i].vertType;
		hash = __rotl(hash, 13);
		hash ^= (u32)drawCalls[i].vertexCount;
		hash = __rotl(hash, 13);
		hash ^= (u32)drawCalls[i].prim;
	}
	return hash;
}

enum { VAI_KILL_AGE = 120 };

void TransformDrawEngine::ClearTrackedVertexArrays() {
	for (auto vai = vai_.begin(); vai != vai_.end(); vai++) {
		delete vai->second;
	}
	vai_.clear();
}

void TransformDrawEngine::DecimateTrackedVertexArrays() {
	int threshold = gpuStats.numFrames - VAI_KILL_AGE;
	for (auto iter = vai_.begin(); iter != vai_.end(); ) {
		if (iter->second->lastFrame < threshold) {
			delete iter->second;
			vai_.erase(iter++);
		}
		else
			++iter;
	}

	// Enable if you want to see vertex decoders in the log output. Need a better way.
#if 0
	char buffer[16384];
	for (std::map<u32, VertexDecoder*>::iterator dec = decoderMap_.begin(); dec != decoderMap_.end(); ++dec) {
		char *ptr = buffer;
		ptr += dec->second->ToString(ptr);
//		*ptr++ = '\n';
		NOTICE_LOG(HLE, buffer);
	}
#endif
}

VertexArrayInfo::~VertexArrayInfo() {
	if (vbo)
		glDeleteBuffers(1, &vbo);
	if (ebo)
		glDeleteBuffers(1, &ebo);
}

void TransformDrawEngine::Flush() {
	if (!numDrawCalls)
		return;

	gpuStats.numFlushes++;
	
	gpuStats.numTrackedVertexArrays = (int)vai_.size();

	// TODO: This should not be done on every drawcall, we should collect vertex data
	// until critical state changes. That's when we draw (flush).

	int prim = prevPrim_;
	ApplyDrawState(prim);

	LinkedShader *program = shaderManager_->ApplyShader(prim);

	if (program->useHWTransform_) {
		GLuint vbo = 0, ebo = 0;
		int vertexCount = 0;
		bool useElements = true;
		// Cannot cache vertex data with morph enabled.
		if (g_Config.bVertexCache && !(lastVType_ & GE_VTYPE_MORPHCOUNT_MASK)) {
			u32 id = ComputeFastDCID();
			auto iter = vai_.find(id);
			VertexArrayInfo *vai;
			if (iter != vai_.end()) {
				// We've seen this before. Could have been a cached draw.
				vai = iter->second;
			} else {
				vai = new VertexArrayInfo();
				vai->decFmt = dec_->GetDecVtxFmt();
				vai_[id] = vai;
			}

			switch (vai->status) {
			case VertexArrayInfo::VAI_NEW:
				{
					// Haven't seen this one before.
					u32 dataHash = ComputeHash();
					vai->hash = dataHash;
					vai->status = VertexArrayInfo::VAI_HASHING;
					vai->drawsUntilNextFullHash = 0;
					DecodeVerts(); // writes to indexGen
					goto rotateVBO;
				}

				// Hashing - still gaining confidence about the buffer.
				// But if we get this far it's likely to be worth creating a vertex buffer.
			case VertexArrayInfo::VAI_HASHING:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFrames) {
						vai->numFrames++;
					}
					if (vai->drawsUntilNextFullHash == 0) {
						u32 newHash = ComputeHash();
						if (newHash != vai->hash) {
							vai->status = VertexArrayInfo::VAI_UNRELIABLE;
							if (vai->vbo) {
								glDeleteBuffers(1, &vai->vbo);
								vai->vbo = 0;
							}
							if (vai->ebo) {
								glDeleteBuffers(1, &vai->ebo);
								vai->ebo = 0;
							}
							DecodeVerts();
							goto rotateVBO;
						}
						if (vai->numVerts > 100) {
							// exponential backoff up to 16 draws, then every 24
							vai->drawsUntilNextFullHash = std::min(24, vai->numFrames);
						} else {
							// Lower numbers seem much more likely to change.
							vai->drawsUntilNextFullHash = 0;
						}
						// TODO: tweak
						//if (vai->numFrames > 1000) {
						//	vai->status = VertexArrayInfo::VAI_RELIABLE;
						//}
					} else {
						vai->drawsUntilNextFullHash--;
						// TODO: "mini-hashing" the first 32 bytes of the vertex/index data or something.
					}

					if (vai->vbo == 0) {
						DecodeVerts();
						vai->numVerts = indexGen.VertexCount();
						vai->prim = indexGen.Prim();
						useElements = !indexGen.SeenOnlyPurePrims();
						if (!useElements && indexGen.PureCount()) {
							vai->numVerts = indexGen.PureCount();
						}
						glGenBuffers(1, &vai->vbo);
						glBindBuffer(GL_ARRAY_BUFFER, vai->vbo);
						glBufferData(GL_ARRAY_BUFFER, dec_->GetDecVtxFmt().stride * indexGen.MaxIndex(), decoded, GL_STATIC_DRAW);
						// If there's only been one primitive type, and it's either TRIANGLES, LINES or POINTS,
						// there is no need for the index buffer we built. We can then use glDrawArrays instead
						// for a very minor speed boost.
						if (useElements) {
							glGenBuffers(1, &vai->ebo);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vai->ebo);
							glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(short) * indexGen.VertexCount(), (GLvoid *)decIndex, GL_STATIC_DRAW);
						} else {
							vai->ebo = 0;
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
						}
					} else {
						gpuStats.numCachedDrawCalls++;
						glBindBuffer(GL_ARRAY_BUFFER, vai->vbo);
						if (vai->ebo)
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vai->ebo);
						useElements = vai->ebo ? true : false;
						gpuStats.numCachedVertsDrawn += vai->numVerts;
					}
					vbo = vai->vbo;
					ebo = vai->ebo;
					vertexCount = vai->numVerts;
					prim = vai->prim;
					break;
				}

				// Reliable - we don't even bother hashing anymore. Right now we don't go here until after a very long time.
			case VertexArrayInfo::VAI_RELIABLE:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFrames) {
						vai->numFrames++;
					}
					gpuStats.numCachedDrawCalls++;
					gpuStats.numCachedVertsDrawn += vai->numVerts;
					vbo = vai->vbo;
					ebo = vai->ebo;
					glBindBuffer(GL_ARRAY_BUFFER, vbo);
					if (ebo)
						glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
					vertexCount = vai->numVerts;
					prim = vai->prim;
					break;
				}

			case VertexArrayInfo::VAI_UNRELIABLE:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFrames) {
						vai->numFrames++;
					}
					DecodeVerts();
					goto rotateVBO;
				}
			}

			vai->lastFrame = gpuStats.numFrames;
		} else {
			DecodeVerts();
rotateVBO:
			gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
			useElements = !indexGen.SeenOnlyPurePrims();
			vertexCount = indexGen.VertexCount();
			if (!useElements && indexGen.PureCount()) {
				vertexCount = indexGen.PureCount();
			}
			if (g_Config.bUseVBO) {
				// Just rotate VBO.
				vbo = vbo_[curVbo_];
				ebo = ebo_[curVbo_];
				curVbo_++;
				if (curVbo_ == NUM_VBOS)
					curVbo_ = 0;
				glBindBuffer(GL_ARRAY_BUFFER, vbo);
				glBufferData(GL_ARRAY_BUFFER, dec_->GetDecVtxFmt().stride * indexGen.MaxIndex(), decoded, GL_STREAM_DRAW);
				if (useElements) {
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
					glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(short) * vertexCount, (GLvoid *)decIndex, GL_STREAM_DRAW);
				}
			} else {
				glBindBuffer(GL_ARRAY_BUFFER, 0);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			}
			prim = indexGen.Prim();
		}
		
		DEBUG_LOG(G3D, "Flush prim %i! %i verts in one go", prim, vertexCount);

		SetupDecFmtForDraw(program, dec_->GetDecVtxFmt(), vbo ? 0 : decoded);
		if (useElements) {
			glDrawElements(glprim[prim], vertexCount, GL_UNSIGNED_SHORT, ebo ? 0 : (GLvoid*)decIndex);
			if (ebo)
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		} else {
			glDrawArrays(glprim[prim], 0, vertexCount);
		}
		if (vbo)
			glBindBuffer(GL_ARRAY_BUFFER, 0);
	} else {
		DecodeVerts();
		gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
		prim = indexGen.Prim();
		// Undo the strip optimization, not supported by the SW code yet.
		if (prim == GE_PRIM_TRIANGLE_STRIP)
			prim = GE_PRIM_TRIANGLES;
		DEBUG_LOG(G3D, "Flush prim %i SW! %i verts in one go", prim, indexGen.VertexCount());

		SoftwareTransformAndDraw(
			prim, decoded, program, indexGen.VertexCount(), 
			dec_->VertexType(), (void *)decIndex, GE_VTYPE_IDX_16BIT, dec_->GetDecVtxFmt(),
			indexGen.MaxIndex());
	}

	indexGen.Reset();
	collectedVerts = 0;
	numDrawCalls = 0;
	prevPrim_ = -1;
}
