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

#include "../../Core/MemMap.h"
#include "../../Core/Host.h"
#include "../../Core/System.h"
#include "../../native/gfx_es2/gl_state.h"

#include "../Math3D.h"
#include "../GPUState.h"
#include "../ge_constants.h"

#include "StateMapping.h"
#include "TextureCache.h"
#include "TransformPipeline.h"
#include "VertexDecoder.h"
#include "ShaderManager.h"
#include "DisplayListInterpreter.h"

GLuint glprim[8] =
{
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	GL_TRIANGLES,	 // With OpenGL ES we have to expand sprites into triangles, tripling the data instead of doubling. sigh. OpenGL ES, Y U NO SUPPORT GL_QUADS?
};

DecodedVertex decoded[65536];
TransformedVertex transformed[65536];
TransformedVertex transformedExpanded[65536];
uint16_t indexBuffer[65536];	// Unused

// TODO: This should really return 2 colors, one for specular and one for diffuse.

// Convenient way to do precomputation to save the parts of the lighting calculation
// that's common between the many vertices of a draw call.
class Lighter {
public:
	Lighter();
	void Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], Vec3 pos, Vec3 normal, float dots[4]);

private:
	bool disabled_;
	Color4 globalAmbient;
	Color4 materialEmissive;
	Color4 materialAmbient;
	Color4 materialDiffuse;
	Color4 materialSpecular;
	float specCoef_;
	Vec3 viewer_;
	bool doShadeMapping_;
	int materialUpdate_;
};

Lighter::Lighter() {
	disabled_ = false;
	doShadeMapping_ = (gstate.texmapmode & 0x3) == 2;
	if (!doShadeMapping_ && !(gstate.lightEnable[0]&1) && !(gstate.lightEnable[1]&1) && !(gstate.lightEnable[2]&1) && !(gstate.lightEnable[3]&1))
	{
		disabled_ = true;
	}
	materialEmissive.GetFromRGB(gstate.materialemissive);
	materialEmissive.a = 0.0f;
	globalAmbient.GetFromRGB(gstate.ambientcolor);
	globalAmbient.GetFromA(gstate.ambientalpha);
	materialAmbient.GetFromRGB(gstate.materialambient);
	materialAmbient.a = 1.0f;
	materialDiffuse.GetFromRGB(gstate.materialdiffuse);
	materialDiffuse.a = 1.0f;
	materialSpecular.GetFromRGB(gstate.materialspecular);
	materialSpecular.a = 1.0f;
	specCoef_ = getFloat24(gstate.materialspecularcoef);
	viewer_ = Vec3(-gstate.viewMatrix[9], -gstate.viewMatrix[10], -gstate.viewMatrix[11]);
	materialUpdate_ = gstate.materialupdate & 7;
}

void Lighter::Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], Vec3 pos, Vec3 normal, float dots[4])
{
	if (disabled_) {
		memcpy(colorOut0, colorIn, sizeof(float) * 4);
		memset(colorOut1, 0, sizeof(float) * 4);
		return;
	}

	Vec3 norm = normal.Normalized();
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

	// Try lights.elf - there's something wrong with the lighting

	for (int l = 0; l < 4; l++)
	{
		// can we skip this light?
		if ((gstate.lightEnable[l] & 1) == 0 && !doShadeMapping_)
			continue;

		GELightComputation comp = (GELightComputation)(gstate.ltype[l] & 3);
		GELightType type = (GELightType)((gstate.ltype[l] >> 8) & 3);
		Vec3 toLight;

		if (type == GE_LIGHTTYPE_DIRECTIONAL)
			toLight = Vec3(gstate_c.lightpos[l]);  // lightdir is for spotlights
		else
			toLight = Vec3(gstate_c.lightpos[l]) - pos;

		bool doSpecular = (comp != GE_LIGHTCOMP_ONLYDIFFUSE);
		bool poweredDiffuse = comp == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE;

		float lightScale = 1.0f;
		if (type != GE_LIGHTTYPE_DIRECTIONAL)
		{
			float distance = toLight.Normalize();
			lightScale = 1.0f / (gstate_c.lightatt[l][0] + gstate_c.lightatt[l][1]*distance + gstate_c.lightatt[l][2]*distance*distance);
			if (lightScale > 1.0f) lightScale = 1.0f;
		}

		float dot = toLight * norm;

		// Clamp dot to zero.
		if (dot < 0.0f) dot = 0.0f;

		if (poweredDiffuse)
			dot = powf(dot, specCoef_);

		Color4 diff = (gstate_c.lightColor[1][l] * *diffuse) * (dot * lightScale);

		// Real PSP specular
		Vec3 toViewer(0,0,1);
		// Better specular
		// Vec3 toViewer = (viewer - pos).Normalized();

		if (doSpecular)
		{
			Vec3 halfVec = toLight;
			halfVec += toViewer;
			halfVec.Normalize();

			dot = halfVec * norm;
			if (dot >= 0)
			{
				lightSum1 += (gstate_c.lightColor[2][l] * *specular * (powf(dot, specCoef_)*lightScale));
			}
		}
		dots[l] = dot;
		if (gstate.lightEnable[l] & 1)
		{
			lightSum0 += gstate_c.lightColor[0][l] * *ambient + diff;
		}
	}

	// 4?
	for (int i = 0; i < 4; i++) {
		colorOut0[i] = lightSum0[i] > 1.0f ? 1.0f : lightSum0[i];
		colorOut1[i] = lightSum1[i] > 1.0f ? 1.0f : lightSum1[i];
	}
}

// This is the software transform pipeline, which is necessary for supporting RECT
// primitives correctly. Other primitives are possible to transform and light in hardware
// using vertex shader, which will be way, way faster, especially on mobile. This has
// not yet been implemented though.
void GLES_GPU::TransformAndDrawPrim(void *verts, void *inds, int prim, int vertexCount, float *customUV, int forceIndexType, int *bytesRead)
{
	int indexLowerBound, indexUpperBound;
	// First, decode the verts and apply morphing
	VertexDecoder dec;
	dec.SetVertexType(gstate.vertType);
	dec.DecodeVerts(decoded, verts, inds, prim, vertexCount, &indexLowerBound, &indexUpperBound);
	if (bytesRead)
		*bytesRead = vertexCount * dec.VertexSize();

	// And here we should return, having collected the morphed but untransformed vertices.
	// Note that DecodeVerts should convert strips into indexed lists etc, adding to our
	// current vertex buffer and index buffer.

	// The rest below here should only execute on Flush.

#if 0
	for (int i = indexLowerBound; i <= indexUpperBound; i++) {
		PrintDecodedVertex(decoded[i], gstate.vertType);
	}
#endif
	bool useTexCoord = false;

	// Check if anything needs updating
	if (gstate_c.textureChanged)
	{
		if ((gstate.textureMapEnable & 1) && !gstate.isModeClear())
		{
			PSPSetTexture();
			useTexCoord = true;
		}
	}
	gpuStats.numDrawCalls++;
	gpuStats.numVertsTransformed += vertexCount;

	bool throughmode = (gstate.vertType & GE_VTYPE_THROUGH_MASK) != 0;

	/*
	DEBUG_LOG(G3D, "View matrix:");
	const float *m = &gstate.viewMatrix[0];
	DEBUG_LOG(G3D, "%f %f %f", m[0], m[1], m[2]);
	DEBUG_LOG(G3D, "%f %f %f", m[3], m[4], m[5]);
	DEBUG_LOG(G3D, "%f %f %f", m[6], m[7], m[8]);
	DEBUG_LOG(G3D, "%f %f %f", m[9], m[10], m[11]);
	*/

	// Then, transform and draw in one big swoop (urgh!)
	// need to move this to the shader.

	// We're gonna have to keep software transforming RECTANGLES, unless we use a geom shader which we can't on OpenGL ES 2.0.
	// Usually, though, these primitives don't use lighting etc so it's no biggie performance wise, but it would be nice to get rid of
	// this code.

	// Actually, if we find the camera-relative right and down vectors, it might even be possible to add the extra points in pre-transformed
	// space and thus make decent use of hardware transform.

	// Actually again, single quads could be drawn more efficiently using GL_TRIANGLE_STRIP, no need to duplicate verts as for
	// GL_TRIANGLES. Still need to sw transform to compute the extra two corners though.
	
	// Temporary storage for RECTANGLES emulation
	float v2[3] = {0};
	float uv2[2] = {0};

	// TODO: Could use glDrawElements in some cases, see below.

	// TODO: Split up into multiple draw calls for GLES 2.0 where you can't guarantee support for more than 0x10000 verts.

#if defined(USING_GLES2)
	if (vertexCount > 0x10000/3)
		vertexCount = 0x10000/3;
#endif

	Lighter lighter;

	for (int index = indexLowerBound; index <= indexUpperBound; index++)
	{	
		float v[3] = {0, 0, 0};
		float c0[4] = {1, 1, 1, 1};
		float c1[4] = {0, 0, 0, 0};
		float uv[2] = {0, 0};

		if (throughmode)
		{
			// Do not touch the coordinates or the colors. No lighting.
			for (int j=0; j<3; j++)
				v[j] = decoded[index].pos[j];
			if(dec.hasColor()) {
				for (int j=0; j<4; j++) {
					c0[j] = decoded[index].color[j] / 255.0f;
					c1[j] = 0.0f;
				}
			}
			else
			{
				c0[0] = (gstate.materialambient & 0xFF) / 255.f;
				c0[1] = ((gstate.materialambient >> 8) & 0xFF) / 255.f;
				c0[2] = ((gstate.materialambient >> 16) & 0xFF) / 255.f;
				c0[3] = (gstate.materialalpha & 0xFF) / 255.f;
			}

			// TODO : check if has uv
			for (int j=0; j<2; j++)
				uv[j] = decoded[index].uv[j];
			// Rescale UV?
		}
		else
		{
			// We do software T&L for now
			float out[3], norm[3];
			if ((gstate.vertType & GE_VTYPE_WEIGHT_MASK) == GE_VTYPE_WEIGHT_NONE)
			{
				Vec3ByMatrix43(out, decoded[index].pos, gstate.worldMatrix);
				Norm3ByMatrix43(norm, decoded[index].normal, gstate.worldMatrix);
			}
			else
			{
				// Skinning
				Vec3 psum(0,0,0);
				Vec3 nsum(0,0,0);
				int nweights = ((gstate.vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT) + 1;
				for (int i = 0; i < nweights; i++)
				{
					if (decoded[index].weights[i] != 0.0f) {
						Vec3ByMatrix43(out, decoded[index].pos, gstate.boneMatrix+i*12);
						Norm3ByMatrix43(norm, decoded[index].normal, gstate.boneMatrix+i*12);
						Vec3 tpos(out), tnorm(norm);
						psum += tpos*decoded[index].weights[i];
						nsum += tnorm*decoded[index].weights[i];
					}
				}

				nsum.Normalize();

				Vec3ByMatrix43(out, psum.v, gstate.worldMatrix);
				Norm3ByMatrix43(norm, nsum.v, gstate.worldMatrix);
			}

			// Perform lighting here if enabled. don't need to check through, it's checked above.
			float dots[4] = {0,0,0,0};
			float unlitColor[4];
			for (int j = 0; j < 4; j++) {
				unlitColor[j] = decoded[index].color[j] / 255.0f;
			}
			float litColor0[4];
			float litColor1[4];
			lighter.Light(litColor0, litColor1, unlitColor, out, norm, dots);

			if (gstate.lightingEnable & 1)
			{
				// TODO: don't ignore gstate.lmode - we should send two colors in that case
				if (gstate.lmode & 1) {
					// Separate colors
					for (int j = 0; j < 4; j++) {
						c0[j] = litColor0[j];
						c1[j] = litColor1[j];
					}
				} else {
					// Summed color into c0
					for (int j = 0; j < 4; j++) {
						c0[j] = litColor0[j] + litColor1[j];
						c1[j] = 0.0f;
					}
				}
			}
			else
			{
				if(dec.hasColor()) {
					for (int j = 0; j < 4; j++) {
						c0[j] = unlitColor[j];
						c1[j] = 0.0f;
					}
				} else {
					c0[0] = (gstate.materialambient & 0xFF) / 255.f;
					c0[1] = ((gstate.materialambient >> 8) & 0xFF) / 255.f;
					c0[2] = ((gstate.materialambient >> 16) & 0xFF) / 255.f;
					c0[3] = (gstate.materialalpha & 0xFF) / 255.f;
				}
			}

			if (customUV) {
				uv[0] = customUV[index * 2 + 0]*gstate_c.uScale + gstate_c.uOff;
				uv[1] = customUV[index * 2 + 1]*gstate_c.vScale + gstate_c.vOff;
			} else {
				// Perform texture coordinate generation after the transform and lighting - one style of UV depends on lights.
				switch (gstate.texmapmode & 0x3)
				{
				case 0:	// UV mapping
					// Texture scale/offset is only performed in this mode.
					uv[0] = decoded[index].uv[0]*gstate_c.uScale + gstate_c.uOff;
					uv[1] = decoded[index].uv[1]*gstate_c.vScale + gstate_c.vOff;
					break;
				case 1:
					{
						// Projection mapping
						Vec3 source;
						switch ((gstate.texmapmode >> 8) & 0x3)
						{
						case 0: // Use model space XYZ as source
							source = decoded[index].pos;
							break;
						case 1: // Use unscaled UV as source
							source = Vec3(decoded[index].uv[0], decoded[index].uv[1], 0.0f);
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
					}
					break;
				case 2:
					// Shade mapping
					{
						int lightsource1 = gstate.texshade & 0x3;
						int lightsource2 = (gstate.texshade >> 8) & 0x3;
						uv[0] = dots[lightsource1];
						uv[1] = dots[lightsource2];
					}
					break;
				case 3:
					// Illegal
					break;
				}
			}

			// Transform the coord by the view matrix.
			// We only really need to do it here for RECTANGLES drawing. However,
			// there's no point in optimizing it out because all other primitives
			// will be moved to hardware transform anyway.
			Vec3ByMatrix43(v, out, gstate.viewMatrix);
		}
		memcpy(&transformed[index].x, v, 3 * sizeof(float));
		memcpy(&transformed[index].uv, uv, 2 * sizeof(float));
		memcpy(&transformed[index].color0, c0, 4 * sizeof(float));
		memcpy(&transformed[index].color1, c1, 4 * sizeof(float));
	}

	// Step 2: Expand using the index buffer, and expand rectangles.

	const TransformedVertex *drawBuffer = transformed;
	int numTrans = 0;

	int indexType = (gstate.vertType & GE_VTYPE_IDX_MASK);
	if (forceIndexType != -1) {
		indexType = forceIndexType;
	}

	bool drawIndexed = false;
	GLuint glIndexType = 0;

	if (prim != GE_PRIM_RECTANGLES) {
		// We can simply draw the unexpanded buffer.
		numTrans = vertexCount;
		switch (indexType) {
		case GE_VTYPE_IDX_8BIT:
			drawIndexed = true;
			glIndexType = GL_UNSIGNED_BYTE;
			break;
		case GE_VTYPE_IDX_16BIT:
			drawIndexed = true;
			glIndexType = GL_UNSIGNED_SHORT;
			break;
		default:
			drawIndexed = false;
			break;
		}
	} else {
		numTrans = 0;
		drawBuffer = transformedExpanded;
		TransformedVertex *trans = &transformedExpanded[0];
		TransformedVertex saved;
		for (int i = 0; i < vertexCount; i++) {
			int index;
			if (indexType == GE_VTYPE_IDX_8BIT)
			{
				index = ((u8*)inds)[i];
			}
			else if (indexType == GE_VTYPE_IDX_16BIT)
			{
				index = ((u16*)inds)[i];
			}
			else
			{
				index = i;
			}

			TransformedVertex &transVtx = transformed[index];
			if ((i & 1) == 0)
			{
				// Save this vertex so we can generate when we get the next one. Color is taken from the last vertex.
				saved = transVtx;
			}
			else
			{
				// We have to turn the rectangle into two triangles, so 6 points. Sigh.

				// TODO: there's supposed to be extra magic here to rotate the UV coordinates depending on if upside down etc.

				// bottom right
				*trans = transVtx;
				trans++;

				// top left
				*trans = transVtx;
				trans->x = saved.x;
				trans->uv[0] = saved.uv[0];
				trans->y = saved.y;
				trans->uv[1] = saved.uv[1];
				trans++;

				// top right
				*trans = transVtx;
				trans->x = saved.x;
				trans->uv[0] = saved.uv[0];
				trans++;

				// bottom left
				*trans = transVtx;
				trans->y = saved.y;
				trans->uv[1] = saved.uv[1];
				trans++;

				// bottom right
				*trans = transVtx;
				trans->x = saved.x;
				trans->uv[0] = saved.uv[0];
				trans->y = saved.y;
				trans->uv[1] = saved.uv[1];
				trans++;

				// top left
				*trans = transVtx;
				trans++;

				numTrans += 6;
			}
		}
	}

	// TODO: This should not be done on every drawcall, we should collect vertex data
	// until critical state changes. That's when we draw (flush).

	ApplyDrawState();
	UpdateViewportAndProjection();

	LinkedShader *program = shaderManager_->ApplyShader(prim);

	// TODO: Make a cache for glEnableVertexAttribArray and glVertexAttribPtr states, these spam the gDebugger log.
	glEnableVertexAttribArray(program->a_position);
	if (useTexCoord && program->a_texcoord != -1) glEnableVertexAttribArray(program->a_texcoord);
	if (program->a_color0 != -1) glEnableVertexAttribArray(program->a_color0);
	if (program->a_color1 != -1) glEnableVertexAttribArray(program->a_color1);
	const int vertexSize = sizeof(transformed[0]);
	glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, vertexSize, drawBuffer);
	if (useTexCoord && program->a_texcoord != -1) glVertexAttribPointer(program->a_texcoord, 2, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)drawBuffer) + 3 * 4);
	if (program->a_color0 != -1) glVertexAttribPointer(program->a_color0, 4, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)drawBuffer) + 5 * 4);
	if (program->a_color1 != -1) glVertexAttribPointer(program->a_color1, 4, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)drawBuffer) + 9 * 4);
	// NOTICE_LOG(G3D,"DrawPrimitive: %i", numTrans);
	if (drawIndexed) {
		glDrawElements(glprim[prim], numTrans, glIndexType, (GLvoid *)inds);
	} else {
		glDrawArrays(glprim[prim], 0, numTrans);
	}
	glDisableVertexAttribArray(program->a_position);
	if (useTexCoord && program->a_texcoord != -1) glDisableVertexAttribArray(program->a_texcoord);
	if (program->a_color0 != -1) glDisableVertexAttribArray(program->a_color0);
	if (program->a_color1 != -1) glDisableVertexAttribArray(program->a_color1);
}

struct GlTypeInfo {
	GLuint type;
	int count;
	GLboolean normalized;
};

const GlTypeInfo GLComp[8] = {
	{0}, // 	DEC_NONE,
	{GL_FLOAT, 1, GL_FALSE}, // 	DEC_FLOAT_1,
	{GL_FLOAT, 2, GL_FALSE}, // 	DEC_FLOAT_2,
	{GL_FLOAT, 3, GL_FALSE}, // 	DEC_FLOAT_3,
	{GL_FLOAT, 4, GL_FALSE}, // 	DEC_FLOAT_4,
	{GL_BYTE, 3, GL_TRUE}, // 	DEC_S8_3,
	{GL_SHORT, 3, GL_TRUE},// 	DEC_S16_3,
	{GL_BYTE, 4, GL_TRUE},// 	DEC_U8_4,
};

static inline void VertexAttribSetup(int attrib, int fmt, int stride, u8 *ptr) {
	if (attrib != -1 && fmt) {
		const GlTypeInfo &type = GLComp[fmt];
		glEnableVertexAttribArray(attrib);
		glVertexAttribPointer(attrib, type.count, type.type, type.normalized, stride, ptr);
	}
}
static inline void VertexAttribDisable(int attrib, int fmt) {
	if (attrib != -1 && fmt) {
		glDisableVertexAttribArray(attrib);
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

static void DesetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt) {
	VertexAttribDisable(program->a_weight0123, decFmt.w0fmt);
	VertexAttribDisable(program->a_weight4567, decFmt.w1fmt);
	VertexAttribDisable(program->a_texcoord, decFmt.uvfmt);
	VertexAttribDisable(program->a_color0, decFmt.c0fmt);
	VertexAttribDisable(program->a_color1, decFmt.c1fmt);
	VertexAttribDisable(program->a_normal, decFmt.nrmfmt);
	VertexAttribDisable(program->a_position, decFmt.posfmt);
}

void GLES_GPU::Flush(int prim) {
	// TODO
}

void GLES_GPU::ApplyDrawState()
{

	// TODO: All this setup is soon so expensive that we'll need dirty flags, or simply do it in the command writes where we detect dirty by xoring. Silly to do all this work on every drawcall.

	// TODO: The top bit of the alpha channel should be written to the stencil bit somehow. This appears to require very expensive multipass rendering :( Alternatively, one could do a
	// single fullscreen pass that converts alpha to stencil (or 2 passes, to set both the 0 and 1 values) very easily.

	// Set cull
	bool wantCull = !gstate.isModeClear() && !gstate.isModeThrough() && gstate.isCullEnabled();
	glstate.cullFace.set(wantCull);

	if(wantCull) {
		u8 cullMode = gstate.getCullMode();
		glstate.cullFaceMode.set(cullingMode[cullMode]);
	}

	// Set blend
	bool wantBlend = !gstate.isModeClear() && (gstate.alphaBlendEnable & 1);
	glstate.blend.set(wantBlend);
	if(wantBlend) {
		// This can't be done exactly as there are several PSP blend modes that are impossible to do on OpenGL ES 2.0, and some even on regular OpenGL for desktop.
		// HOWEVER - we should be able to approximate the 2x modes in the shader, although they will clip wrongly.
		int blendFuncA  = gstate.getBlendFuncA();
		int blendFuncB  = gstate.getBlendFuncB();
		int blendFuncEq = gstate.getBlendEq();

		glstate.blendEquation.set(eqLookup[blendFuncEq]);

		if (blendFuncA != GE_SRCBLEND_FIXA && blendFuncB != GE_DSTBLEND_FIXB) {
			// All is valid, no blendcolor needed
			glstate.blendFunc.set(aLookup[blendFuncA], bLookup[blendFuncB]);
		} else {
			GLuint glBlendFuncA = blendFuncA == GE_SRCBLEND_FIXA ? GL_INVALID_ENUM : aLookup[blendFuncA];
			GLuint glBlendFuncB = blendFuncB == GE_DSTBLEND_FIXB ? GL_INVALID_ENUM : bLookup[blendFuncB];
			u32 fixA = gstate.getFixA();
			u32 fixB = gstate.getFixB();
			// Shortcut by using GL_ONE where possible, no need to set blendcolor
			if (glBlendFuncA == GL_INVALID_ENUM && blendFuncA == GE_SRCBLEND_FIXA) {
				if (fixA == 0xFFFFFF)
					glBlendFuncA = GL_ONE;
				else if (fixA == 0)
					glBlendFuncA = GL_ZERO;
			} 
			if (glBlendFuncB == GL_INVALID_ENUM && blendFuncB == GE_DSTBLEND_FIXB) {
				if (fixB == 0xFFFFFF)
					glBlendFuncB = GL_ONE;
				else if (fixB == 0)
					glBlendFuncB = GL_ZERO;
			}
			if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB != GL_INVALID_ENUM) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {(fixA & 0xFF)/255.0f, ((fixA >> 8) & 0xFF)/255.0f, ((fixA >> 16) & 0xFF)/255.0f, 1.0f};
				glstate.blendColor.set(blendColor);
				glBlendFuncA = GL_CONSTANT_COLOR;
			} else if (glBlendFuncA != GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {(fixB & 0xFF)/255.0f, ((fixB >> 8) & 0xFF)/255.0f, ((fixB >> 16) & 0xFF)/255.0f, 1.0f};
				glstate.blendColor.set(blendColor);
				glBlendFuncB = GL_CONSTANT_COLOR;
			} else if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {  // Should also check for approximate equality
				if (fixA == (fixB ^ 0xFFFFFF)) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = GL_ONE_MINUS_CONSTANT_COLOR;
					const float blendColor[4] = {(fixA & 0xFF)/255.0f, ((fixA >> 8) & 0xFF)/255.0f, ((fixA >> 16) & 0xFF)/255.0f, 1.0f};
					glstate.blendColor.set(blendColor);
				} else if (fixA == fixB) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = GL_CONSTANT_COLOR;
					const float blendColor[4] = {(fixA & 0xFF)/255.0f, ((fixA >> 8) & 0xFF)/255.0f, ((fixA >> 16) & 0xFF)/255.0f, 1.0f};
					glstate.blendColor.set(blendColor);
				} else {
					NOTICE_LOG(HLE, "ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
					glBlendFuncA = GL_ONE;
					glBlendFuncB = GL_ONE;
				}
			}
			// At this point, through all paths above, glBlendFuncA and glBlendFuncB will be set somehow.

			glstate.blendFunc.set(glBlendFuncA, glBlendFuncB);
		}
	}

	bool wantDepthTest = gstate.isModeClear() || gstate.isDepthTestEnabled();
	glstate.depthTest.set(wantDepthTest);
	if(wantDepthTest) {
		// Force GL_ALWAYS if mode clear
		int depthTestFunc = gstate.isModeClear() ? 1 : gstate.getDepthTestFunc();
		glstate.depthFunc.set(ztests[depthTestFunc]);
	}

	bool wantDepthWrite = gstate.isModeClear() || gstate.isDepthWriteEnabled();
	glstate.depthWrite.set(wantDepthWrite ? GL_TRUE : GL_FALSE);

	float depthRangeMin = gstate_c.zOff - gstate_c.zScale;
	float depthRangeMax = gstate_c.zOff + gstate_c.zScale;
	glstate.depthRange.set(depthRangeMin, depthRangeMax);
}

void GLES_GPU::UpdateViewportAndProjection()
{
	bool throughmode = (gstate.vertType & GE_VTYPE_THROUGH_MASK) != 0;

	// We can probably use these to simply set scissors? Maybe we need to offset by regionX1/Y1
	int regionX1 = gstate.region1 & 0x3FF;
	int regionY1 = (gstate.region1 >> 10) & 0x3FF;
	int regionX2 = (gstate.region2 & 0x3FF) + 1;
	int regionY2 = ((gstate.region2 >> 10) & 0x3FF) + 1;

	float offsetX = (float)(gstate.offsetx & 0xFFFF) / 16.0f;
	float offsetY = (float)(gstate.offsety & 0xFFFF) / 16.0f;

	if (throughmode) {
		// No viewport transform here. Let's experiment with using region.
		return;
		glViewport((0 + regionX1) * renderWidthFactor_, (0 - regionY1) * renderHeightFactor_, (regionX2 - regionX1) * renderWidthFactor_, (regionY2 - regionY1) * renderHeightFactor_);
	} else {
		// These we can turn into a glViewport call, offset by offsetX and offsetY. Math after.
		float vpXa = getFloat24(gstate.viewportx1);
		float vpXb = getFloat24(gstate.viewportx2);
		float vpYa = getFloat24(gstate.viewporty1);
		float vpYb = getFloat24(gstate.viewporty2);
		float vpZa = getFloat24(gstate.viewportz1);  //  / 65536.0f   should map it to OpenGL's 0.0-1.0 Z range
		float vpZb = getFloat24(gstate.viewportz2);  //  / 65536.0f

		// The viewport transform appears to go like this: 
		// Xscreen = -offsetX + vpXb + vpXa * Xview
		// Yscreen = -offsetY + vpYb + vpYa * Yview
		// Zscreen = vpZb + vpZa * Zview

		// This means that to get the analogue glViewport we must:
		float vpX0 = vpXb - offsetX - vpXa;
		float vpY0 = vpYb - offsetY + vpYa;   // Need to account for sign of Y
		gstate_c.vpWidth = vpXa * 2;
		gstate_c.vpHeight = -vpYa * 2;

		return;

		float vpWidth = fabsf(gstate_c.vpWidth);
		float vpHeight = fabsf(gstate_c.vpHeight);

		// TODO: These two should feed into glDepthRange somehow.
		float vpZ0 = (vpZb - vpZa) / 65536.0f;
		float vpZ1 = (vpZa * 2) / 65536.0f;

		vpX0 *= renderWidthFactor_;
		vpY0 *= renderHeightFactor_;
		vpWidth *= renderWidthFactor_;
		vpHeight *= renderHeightFactor_;

		// Flip vpY0 to match the OpenGL coordinate system.
		vpY0 = renderHeight_ - (vpY0 + vpHeight);
		glViewport(vpX0, vpY0, vpWidth, vpHeight);
		// Sadly, as glViewport takes integers, we will not be able to support sub pixel offsets this way. But meh.
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}
}
