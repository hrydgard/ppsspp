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
uint16_t indexBuffer[65536];	// Unused

// TODO: This should really return 2 colors, one for specular and one for diffuse.

void Light(float colorOut[4], const float colorIn[4], Vec3 pos, Vec3 normal, float dots[4])
{
	// could cache a lot of stuff, such as ambient, across vertices...

	bool doShadeMapping = (gstate.texmapmode & 0x3) == 2;
	if (!doShadeMapping && !(gstate.lightEnable[0]&1) && !(gstate.lightEnable[1]&1) && !(gstate.lightEnable[2]&1) && !(gstate.lightEnable[3]&1))
	{
		memcpy(colorOut, colorIn, sizeof(float) * 4);
		return;
	}

	Color4 emissive;
	emissive.GetFromRGB(gstate.materialemissive);
	Color4 globalAmbient;
	globalAmbient.GetFromRGB(gstate.ambientcolor);
	globalAmbient.GetFromA(gstate.ambientalpha);

	Vec3 norm = normal.Normalized();
	Color4 in(colorIn);

	Color4 ambient;
	if (gstate.materialupdate & 1)
	{
		ambient = in;
	}
	else
	{
		ambient.GetFromRGB(gstate.materialambient);
		ambient.a=1.0f;
	}

	Color4 diffuse;
	if (gstate.materialupdate & 2)
	{
		diffuse = in;
	}
	else
	{
		diffuse.GetFromRGB(gstate.materialdiffuse);
		diffuse.a=1.0f;
	}

	Color4 specular;
	if (gstate.materialupdate & 4)
	{
		specular = in;
	}
	else
	{
		specular.GetFromRGB(gstate.materialspecular);
		specular.a=1.0f;
	}

	float specCoef = getFloat24(gstate.materialspecularcoef);
	
	Vec3 viewer(-gstate.viewMatrix[9], -gstate.viewMatrix[10], -gstate.viewMatrix[11]);

	Color4 lightSum = globalAmbient * ambient + emissive;


	// Try lights.elf - there's something wrong with the lighting

	for (int l = 0; l < 4; l++)
	{
		// can we skip this light?
		//if ((gstate.lightEnable[l] & 1) == 0) // && !doShadeMapping)
		//	continue;

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
			dot = powf(dot, specCoef);

		Color4 diff = (gstate.lightColor[1][l] * diffuse) * (dot * lightScale);	
		Color4 spec(0,0,0,0);

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
				spec += (gstate.lightColor[2][l] * specular * (powf(dot, specCoef)*lightScale));
			}	
		}
		dots[l] = dot;
		if (gstate.lightEnable[l] & 1)
		{
			lightSum += gstate.lightColor[0][l]*ambient + diff + spec;
		}
	}

	for (int i = 0; i < 3; i++)
		colorOut[i] = lightSum[i];
}

void TransformAndDrawPrim(void *verts, void *inds, int prim, int vertexCount, LinkedShader *program, float *customUV, int forceIndexType)
{
	// First, decode the verts and apply morphing
	VertexDecoder dec;
	dec.SetVertexType(gstate.vertType);
	dec.DecodeVerts(decoded, verts, inds, prim, vertexCount);

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

	int numTrans = 0;
	TransformedVertex *trans = &transformed[0];

	// TODO: Could use glDrawElements in some cases, see below.


	// TODO: Split up into multiple draw calls for Android where you can't guarantee support for more than 0x10000 verts.
	int i = 0;

#if defined(ANDROID) || defined(BLACKBERRY)
	if (vertexCount > 0x10000/3)
		vertexCount = 0x10000/3;
#endif

	for (int i = 0; i < vertexCount; i++)
	{	
		int indexType = (gstate.vertType & GE_VTYPE_IDX_MASK);
		if (forceIndexType != -1) {
			indexType = forceIndexType;
		}

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

		float v[3] = {0,0,0};
		float c[4] = {1,1,1,1};
		float uv[2] = {0,0};

		if (gstate.vertType & GE_VTYPE_THROUGH_MASK)
		{
			// Do not touch the coordinates or the colors. No lighting.
			for (int j=0; j<3; j++)
				v[j] = decoded[index].pos[j];
			// TODO : check if has color
			for (int j=0; j<4; j++)
				c[j] = decoded[index].color[j];
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
				Vec3 psum(0,0,0);
				Vec3 nsum(0,0,0);
				int nweights = (gstate.vertType & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT;
				for (int i = 0; i < nweights; i++)
				{
					Vec3ByMatrix43(out, decoded[index].pos, gstate.boneMatrix+i*12);
					Norm3ByMatrix43(norm, decoded[index].normal, gstate.boneMatrix+i*12);
					Vec3 tpos(out), tnorm(norm);
					psum += tpos*decoded[index].weights[i];
					nsum += tnorm*decoded[index].weights[i];
				}
				nsum.Normalize();
				psum.Write(out);
				nsum.Write(norm);
			}

			// Perform lighting here if enabled. don't need to check through, it's checked above.
			float dots[4] = {0,0,0,0};
			if (program->a_color0 != -1)
			{
				//c[1] = norm[1];
				float litColor[4] = {0,0,0,0};
				Light(litColor, decoded[index].color, out, norm, dots);
				if (gstate.lightingEnable & 1)
				{
					memcpy(c, litColor, sizeof(litColor));
				}
				else
				{
					// no lighting? copy the color.
					for (int j = 0; j < 4; j++)
						c[j] = decoded[index].color[j];
				}
			}
			else
			{
				// no color in the fragment program???
				for (int j = 0; j < 4; j++)
					c[j] = decoded[index].color[j];
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
			// Transform the coord by the view matrix. Should this be done before or after texcoord generation?
			Vec3ByMatrix43(v, out, gstate.viewMatrix);
		}


		// We need to tesselate axis-aligned rectangles, as they're only specified by two coordinates.
		if (prim == GE_PRIM_RECTANGLES)
		{
			if ((i & 1) == 0)
			{
				// Save this vertex so we can generate when we get the next one. Color is taken from the last vertex.
				memcpy(v2, v, sizeof(float)*3);
				memcpy(uv2,uv,sizeof(float)*2);
			}
			else
			{
				// We have to turn the rectangle into two triangles, so 6 points. Sigh.

				// top left
				trans->x = v[0]; trans->y = v[1];
				trans->z = v[2]; 
				trans->uv[0] = uv[0]; trans->uv[1] = uv[1];
				memcpy(trans->color, c, 4*sizeof(float));
				trans++;

				// top right
				trans->x = v2[0]; trans->y = v[1];
				trans->z = v[2]; 
				trans->uv[0] = uv2[0]; trans->uv[1] = uv[1];
				memcpy(trans->color, c, 4*sizeof(float));
				trans++;

				// bottom right
				trans->x = v2[0]; trans->y = v2[1];
				trans->z = v[2]; 
				trans->uv[0] = uv2[0]; trans->uv[1] = uv2[1];
				memcpy(trans->color, c, 4*sizeof(float));
				trans++;

				// bottom left
				trans->x = v[0]; trans->y = v2[1];
				trans->z = v[2]; 
				trans->uv[0] = uv[0]; trans->uv[1] = uv2[1];
				memcpy(trans->color, c, 4*sizeof(float));
				trans++;

				// top left
				trans->x = v[0]; trans->y = v[1];
				trans->z = v[2]; 
				trans->uv[0] = uv[0]; trans->uv[1] = uv[1];
				memcpy(trans->color, c, 4*sizeof(float));
				trans++;

				// bottom right
				trans->x = v2[0]; trans->y = v2[1];
				trans->z = v[2]; 
				trans->uv[0] = uv2[0]; trans->uv[1] = uv2[1];
				memcpy(trans->color, c, 4*sizeof(float));
				trans++;

				numTrans += 6;
			}
		}
		else
		{
			memcpy(&trans->x, v, 3*sizeof(float));
			memcpy(trans->color, c, 4*sizeof(float));
			memcpy(trans->uv, uv, 2*sizeof(float));
			trans++;
			numTrans++;
		}
	}

	glEnableVertexAttribArray(program->a_position);
	if (useTexCoord && program->a_texcoord != -1) glEnableVertexAttribArray(program->a_texcoord);
	if (program->a_color0 != -1) glEnableVertexAttribArray(program->a_color0);
	const int vertexSize = sizeof(*trans);
	glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, vertexSize, transformed);
	if (useTexCoord && program->a_texcoord != -1) glVertexAttribPointer(program->a_texcoord, 2, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)transformed) + 3 * 4);	
	if (program->a_color0 != -1) glVertexAttribPointer(program->a_color0, 4, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)transformed) + 5 * 4);
	// NOTICE_LOG(G3D,"DrawPrimitive: %i", numTrans);
	glDrawArrays(glprim[prim], 0, numTrans);
	glDisableVertexAttribArray(program->a_position);
	if (useTexCoord && program->a_texcoord != -1) glDisableVertexAttribArray(program->a_texcoord);
	if (program->a_color0 != -1) glDisableVertexAttribArray(program->a_color0);

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
