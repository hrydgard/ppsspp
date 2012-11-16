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

#if defined(ANDROID) || defined(BLACKBERRY)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include "../../Core/MemMap.h"
#include "../../Core/Host.h"
#include "../../Core/System.h"

#include "../Math3D.h"
#include "../GPUState.h"
#include "../ge_constants.h"

#include "TextureCache.h"
#include "TransformPipeline.h"
#include "VertexDecoder.h"
#include "ShaderManager.h"

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
	Color4 lightSum1(0,0,0,0);

	// Try lights.elf - there's something wrong with the lighting

	for (int l = 0; l < 4; l++)
	{
		// can we skip this light?
		if ((gstate.lightEnable[l] & 1) == 0 && !doShadeMapping_)
			continue;

		GELightComputation comp = (GELightComputation)(gstate.ltype[l]&3);
		GELightType type = (GELightType)((gstate.ltype[l]>>8)&3);
		Vec3 toLight;

		if (type == GE_LIGHTTYPE_DIRECTIONAL)
			toLight = Vec3(gstate.lightpos[l]);  // lightdir is for spotlights
		else
			toLight = Vec3(gstate.lightpos[l]) - pos;

		bool doSpecular = (comp != GE_LIGHTCOMP_ONLYDIFFUSE);
		bool poweredDiffuse = comp == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE;

		float lightScale = 1.0f;
		if (type != GE_LIGHTTYPE_DIRECTIONAL)
		{
			float distance = toLight.Normalize(); 
			lightScale = 1.0f / (gstate.lightatt[l][0] + gstate.lightatt[l][1]*distance + gstate.lightatt[l][2]*distance*distance);
			if (lightScale > 1.0f) lightScale = 1.0f;
		}

		float dot = toLight * norm;

		// Clamp dot to zero.
		if (dot < 0.0f) dot = 0.0f;

		if (poweredDiffuse)
			dot = powf(dot, specCoef_);

		Color4 diff = (gstate.lightColor[1][l] * *diffuse) * (dot * lightScale);	

		// Real PSP specular
		Vec3 toViewer(0,0,1);
		// Better specular
		//Vec3 toViewer = (viewer - pos).Normalized();

		if (doSpecular)
		{
			Vec3 halfVec = toLight;
			halfVec += toViewer;
			halfVec.Normalize();

			dot = halfVec * norm;
			if (dot >= 0)
			{
				lightSum1 += (gstate.lightColor[2][l] * *specular * (powf(dot, specCoef_)*lightScale));
			}	
		}
		dots[l] = dot;
		if (gstate.lightEnable[l] & 1)
		{
			lightSum0 += gstate.lightColor[0][l] * *ambient + diff;
		}
	}

	// 4?
	for (int i = 0; i < 4; i++) {
		colorOut0[i] = lightSum0[i];
		colorOut1[i] = lightSum1[i];
	}
}

// This is the software transform pipeline, which is necessary for supporting RECT
// primitives correctly. Other primitives are possible to transform and light in hardware
// using vertex shader, which will be way, way faster, especially on mobile. This has
// not yet been implemented though.
void TransformAndDrawPrim(void *verts, void *inds, int prim, int vertexCount, LinkedShader *program, float *customUV, int forceIndexType)
{
	int indexLowerBound, indexUpperBound;
	// First, decode the verts and apply morphing
	VertexDecoder dec;
	dec.SetVertexType(gstate.vertType);
	dec.DecodeVerts(decoded, verts, inds, prim, vertexCount, &indexLowerBound, &indexUpperBound);

	bool useTexCoord = false;

	// Check if anything needs updating
	if (gstate.textureChanged)
	{
		if (gstate.textureMapEnable && !(gstate.clearmode & 1))
		{
			PSPSetTexture();
			useTexCoord = true;
		}
	}

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

	// TODO: Split up into multiple draw calls for Android where you can't guarantee support for more than 0x10000 verts.

#if defined(ANDROID) || defined(BLACKBERRY)
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

		if (gstate.vertType & GE_VTYPE_THROUGH_MASK)
		{
			// Do not touch the coordinates or the colors. No lighting.
			for (int j=0; j<3; j++)
				v[j] = decoded[index].pos[j];
			// TODO : check if has color
			for (int j=0; j<4; j++) {
				c0[j] = decoded[index].color[j] / 255.0f;
				c1[j] = 0.0f;
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
			if (program->a_color0 != -1)
			{
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
					// no lighting? copy the color.
					for (int j = 0; j < 4; j++) {
						c0[j] = unlitColor[j];
						c1[j] = 0.0f;
					}
				}
			}
			else
			{
				// no color in the fragment program???
				for (int j = 0; j < 4; j++) {
					c0[j] = decoded[index].color[j] / 255.0f;
					c1[j] = 0.0f;
				}
			}

			if (customUV) {
				uv[0] = customUV[index * 2 + 0]*gstate.uScale + gstate.uOff;
				uv[1] = customUV[index * 2 + 1]*gstate.vScale + gstate.vOff;
			} else {
				// Perform texture coordinate generation after the transform and lighting - one style of UV depends on lights.
				switch (gstate.texmapmode & 0x3)
				{
				case 0:	// UV mapping
					// Texture scale/offset is only performed in this mode.
					uv[0] = decoded[index].uv[0]*gstate.uScale + gstate.uOff;
					uv[1] = decoded[index].uv[1]*gstate.vScale + gstate.vOff;
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

				// top left
				*trans = transVtx;
				trans++;

				// bottom right
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

	/*
	if (((gstate.vertType ) & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_8BIT)
	{
		glDrawElements(glprim, vertexCount, GL_UNSIGNED_BYTE, inds);
	} 
	else if (((gstate.vertType ) & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT)
	{
		glDrawElements(glprim, vertexCount, GL_UNSIGNED_SHORT, inds);
	}
	else
	{*/

}
