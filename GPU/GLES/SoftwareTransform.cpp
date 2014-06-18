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

#include "gfx_es2/gl_state.h"
#include "math/math_util.h"

#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/Math3D.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/TransformPipeline.h"

// This is the software transform pipeline, which is necessary for supporting RECT
// primitives correctly without geometry shaders, and may be easier to use for
// debugging than the hardware transform pipeline.

// There's code here that simply expands transformed RECTANGLES into plain triangles.

// We're gonna have to keep software transforming RECTANGLES, unless we use a geom shader which we can't on OpenGL ES 2.0.
// Usually, though, these primitives don't use lighting etc so it's no biggie performance wise, but it would be nice to get rid of
// this code.

// Actually, if we find the camera-relative right and down vectors, it might even be possible to add the extra points in pre-transformed
// space and thus make decent use of hardware transform.

// Actually again, single quads could be drawn more efficiently using GL_TRIANGLE_STRIP, no need to duplicate verts as for
// GL_TRIANGLES. Still need to sw transform to compute the extra two corners though.
//

extern const GLuint glprim[8];

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

	u32 matchcolor = transformed[0].color0_32;
	float matchz = transformed[0].z;

	int bufW = gstate_c.curRTWidth;
	int bufH = gstate_c.curRTHeight;

	float prevX = 0.0f;
	for (int i = 1; i < numVerts; i++) {
		if (transformed[i].color0_32 != matchcolor || transformed[i].z != matchz)
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


void TransformDrawEngine::SoftwareTransformAndDraw(
		int prim, u8 *decoded, LinkedShader *program, int vertexCount, u32 vertType, void *inds, int indexType, const DecVtxFormat &decVtxFormat, int maxIndex) {

	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();

	// TODO: Split up into multiple draw calls for GLES 2.0 where you can't guarantee support for more than 0x10000 verts.

#if defined(MOBILE_DEVICE)
	if (vertexCount > 0x10000/3)
		vertexCount = 0x10000/3;
#endif

	float uscale = 1.0f;
	float vscale = 1.0f;
	bool scaleUV = false;
	if (throughmode) {
		uscale /= gstate_c.curTextureWidth;
		vscale /= gstate_c.curTextureHeight;
	} else {
		scaleUV = !g_Config.bPrescaleUV;
	}

	bool skinningEnabled = vertTypeIsSkinningEnabled(vertType);

	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	float widthFactor = (float) w / (float) gstate_c.curTextureWidth;
	float heightFactor = (float) h / (float) gstate_c.curTextureHeight;

	Lighter lighter(vertType);
	float fog_end = getFloat24(gstate.fog1);
	float fog_slope = getFloat24(gstate.fog2);
	// Same fixup as in ShaderManager.cpp
	if (my_isinf(fog_slope)) {
		// not really sure what a sensible value might be.
		fog_slope = fog_slope < 0.0f ? -10000.0f : 10000.0f;
	}
	if (my_isnan(fog_slope))	{
		// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
		// Just put the fog far away at a large finite distance.
		// Infinities and NaNs are rather unpredictable in shaders on many GPUs
		// so it's best to just make it a sane calculation.
		fog_end = 100000.0f;
		fog_slope = 1.0f;
	}

	VertexReader reader(decoded, decVtxFormat, vertType);
	// We flip in the fragment shader for GE_TEXMAP_TEXTURE_MATRIX.
	const bool flipV = gstate_c.flipTexture && gstate.getUVGenMode() != GE_TEXMAP_TEXTURE_MATRIX;
	for (int index = 0; index < maxIndex; index++) {
		reader.Goto(index);

		float v[3] = {0, 0, 0};
		Vec4f c0 = Vec4f(1, 1, 1, 1);
		Vec4f c1 = Vec4f(0, 0, 0, 0);
		float uv[3] = {0, 0, 1};
		float fogCoef = 1.0f;

		if (throughmode) {
			// Do not touch the coordinates or the colors. No lighting.
			reader.ReadPos(v);
			if (reader.hasColor0()) {
				reader.ReadColor0(&c0.x);
				// c1 is already 0.
			} else {
				c0 = Vec4f::FromRGBA(gstate.getMaterialAmbientRGBA());
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
			float out[3];
			float pos[3];
			Vec3f normal(0, 0, 1);
			Vec3f worldnormal(0, 0, 1);
			reader.ReadPos(pos);

			if (!skinningEnabled) {
				Vec3ByMatrix43(out, pos, gstate.worldMatrix);
				if (reader.hasNormal()) {
					reader.ReadNrm(normal.AsArray());
					if (gstate.areNormalsReversed()) {
						normal = -normal;
					}
					Norm3ByMatrix43(worldnormal.AsArray(), normal.AsArray(), gstate.worldMatrix);
					worldnormal = worldnormal.Normalized();
				}
			} else {
				float weights[8];
				reader.ReadWeights(weights);
				if (reader.hasNormal())
					reader.ReadNrm(normal.AsArray());

				// Skinning
				Vec3f psum(0, 0, 0);
				Vec3f nsum(0, 0, 0);
				for (int i = 0; i < vertTypeGetNumBoneWeights(vertType); i++) {
					if (weights[i] != 0.0f) {
						Vec3ByMatrix43(out, pos, gstate.boneMatrix+i*12);
						Vec3f tpos(out);
						psum += tpos * weights[i];
						if (reader.hasNormal()) {
							Vec3f norm;
							Norm3ByMatrix43(norm.AsArray(), normal.AsArray(), gstate.boneMatrix+i*12);
							nsum += norm * weights[i];
						}
					}
				}

				// Yes, we really must multiply by the world matrix too.
				Vec3ByMatrix43(out, psum.AsArray(), gstate.worldMatrix);
				if (reader.hasNormal()) {
					normal = nsum;
					if (gstate.areNormalsReversed()) {
						normal = -normal;
					}
					Norm3ByMatrix43(worldnormal.AsArray(), normal.AsArray(), gstate.worldMatrix);
					worldnormal = worldnormal.Normalized();
				}
			}

			// Perform lighting here if enabled. don't need to check through, it's checked above.
			Vec4f unlitColor = Vec4f(1, 1, 1, 1);
			if (reader.hasColor0()) {
				reader.ReadColor0(&unlitColor.x);
			} else {
				unlitColor = Vec4f::FromRGBA(gstate.getMaterialAmbientRGBA());
			}

			if (gstate.isLightingEnabled()) {
				float litColor0[4];
				float litColor1[4];
				lighter.Light(litColor0, litColor1, unlitColor.AsArray(), out, worldnormal);

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
					c0 = Vec4f::FromRGBA(gstate.getMaterialAmbientRGBA());
				}
				if (lmode) {
					// c1 is already 0.
				}
			}

			float ruv[2] = {0.0f, 0.0f};
			if (reader.hasUV())
				reader.ReadUV(ruv);

			// Perform texture coordinate generation after the transform and lighting - one style of UV depends on lights.
			switch (gstate.getUVGenMode()) {
			case GE_TEXMAP_TEXTURE_COORDS:	// UV mapping
			case GE_TEXMAP_UNKNOWN: // Seen in Riviera.  Unsure of meaning, but this works.
				// Texture scale/offset is only performed in this mode.
				if (scaleUV) {
					uv[0] = ruv[0]*gstate_c.uv.uScale + gstate_c.uv.uOff;
					uv[1] = ruv[1]*gstate_c.uv.vScale + gstate_c.uv.vOff;
				} else {
					uv[0] = ruv[0];
					uv[1] = ruv[1];
				}
				uv[2] = 1.0f;
				break;

			case GE_TEXMAP_TEXTURE_MATRIX:
				{
					// Projection mapping
					Vec3f source;
					switch (gstate.getUVProjMode())	{
					case GE_PROJMAP_POSITION: // Use model space XYZ as source
						source = pos;
						break;

					case GE_PROJMAP_UV: // Use unscaled UV as source
						source = Vec3f(ruv[0], ruv[1], 0.0f);
						break;

					case GE_PROJMAP_NORMALIZED_NORMAL: // Use normalized normal as source
						source = normal.Normalized();
						if (!reader.hasNormal()) {
							ERROR_LOG_REPORT(G3D, "Normal projection mapping without normal?");
						}
						break;

					case GE_PROJMAP_NORMAL: // Use non-normalized normal as source!
						source = normal;
						if (!reader.hasNormal()) {
							ERROR_LOG_REPORT(G3D, "Normal projection mapping without normal?");
						}
						break;
					}

					float uvw[3];
					Vec3ByMatrix43(uvw, &source.x, gstate.tgenMatrix);
					uv[0] = uvw[0];
					uv[1] = uvw[1];
					uv[2] = uvw[2];
				}
				break;

			case GE_TEXMAP_ENVIRONMENT_MAP:
				// Shade mapping - use two light sources to generate U and V.
				{
					Vec3f lightpos0 = Vec3f(&lighter.lpos[gstate.getUVLS0() * 3]).Normalized();
					Vec3f lightpos1 = Vec3f(&lighter.lpos[gstate.getUVLS1() * 3]).Normalized();

					uv[0] = (1.0f + Dot(lightpos0, worldnormal))/2.0f;
					uv[1] = (1.0f + Dot(lightpos1, worldnormal))/2.0f;
					uv[2] = 1.0f;
				}
				break;

			default:
				// Illegal
				ERROR_LOG_REPORT(G3D, "Impossible UV gen mode? %d", gstate.getUVGenMode());
				break;
			}

			uv[0] = uv[0] * widthFactor;
			uv[1] = uv[1] * heightFactor;

			// Transform the coord by the view matrix.
			Vec3ByMatrix43(v, out, gstate.viewMatrix);
			fogCoef = (v[2] + fog_end) * fog_slope;
		}

		// TODO: Write to a flexible buffer, we don't always need all four components.
		memcpy(&transformed[index].x, v, 3 * sizeof(float));
		transformed[index].fog = fogCoef;
		memcpy(&transformed[index].u, uv, 3 * sizeof(float));
		if (flipV) {
			transformed[index].v = 1.0f - transformed[index].v;
		}
		transformed[index].color0_32 = c0.ToRGBA();
		transformed[index].color1_32 = c1.ToRGBA();
	}

	// Here's the best opportunity to try to detect rectangles used to clear the screen, and
	// replace them with real OpenGL clears. This can provide a speedup on certain mobile chips.
	//
	// An alternative option is to simply ditch all the verts except the first and last to create a single
	// rectangle out of many. Quite a small optimization though.
	if (maxIndex > 1 && gstate.isModeClear() && prim == GE_PRIM_RECTANGLES && IsReallyAClear(maxIndex)) {
		u32 clearColor = transformed[0].color0_32;
		float clearDepth = transformed[0].z;
		const float col[4] = {
			((clearColor & 0xFF)) / 255.0f,
			((clearColor & 0xFF00) >> 8) / 255.0f,
			((clearColor & 0xFF0000) >> 16) / 255.0f,
			((clearColor & 0xFF000000) >> 24) / 255.0f,
		};

		bool colorMask = gstate.isClearModeColorMask();
		bool alphaMask = gstate.isClearModeAlphaMask();
		bool depthMask = gstate.isClearModeDepthMask();
		if (depthMask) {
			framebufferManager_->SetDepthUpdated();
		}

		// Note that scissor may still apply while clearing.  Turn off other tests for the clear.
		glstate.stencilTest.disable();
		glstate.depthTest.disable();

		GLbitfield target = 0;
		if (colorMask || alphaMask) target |= GL_COLOR_BUFFER_BIT;
		if (alphaMask) target |= GL_STENCIL_BUFFER_BIT;
		if (depthMask) target |= GL_DEPTH_BUFFER_BIT;

		glClearColor(col[0], col[1], col[2], col[3]);
#ifdef USING_GLES2
		glClearDepthf(clearDepth);
#else
		glClearDepth(clearDepth);
#endif
		// Stencil takes alpha.
		glClearStencil(clearColor >> 24);
		glClear(target);
		framebufferManager_->SetColorUpdated();
		return;
	}

	if (gstate_c.flipTexture && transformed[0].v < 0.0f && transformed[0].v > 1.0f - heightFactor) {
		// Okay, so we're texturing from outside the framebuffer, but inside the texture height.
		// Breath of Fire 3 does this to access a render surface at +curTextureHeight.
		const u32 bpp = framebufferManager_->GetTargetFormat() == GE_FORMAT_8888 ? 4 : 2;
		const u32 fb_size = bpp * framebufferManager_->GetTargetStride() * gstate_c.curTextureHeight;
		if (textureCache_->SetOffsetTexture(fb_size)) {
			const float oldWidthFactor = widthFactor;
			const float oldHeightFactor = heightFactor;
			widthFactor = (float) w / (float) gstate_c.curTextureWidth;
			heightFactor = (float) h / (float) gstate_c.curTextureHeight;

			for (int index = 0; index < maxIndex; ++index) {
				transformed[index].u *= widthFactor / oldWidthFactor;
				// Inverse it back to scale to the new FBO, and add 1.0f to account for old FBO.
				transformed[index].v = 1.0f - transformed[index].v - 1.0f;
				transformed[index].v *= heightFactor / oldHeightFactor;
				transformed[index].v = 1.0f - transformed[index].v;
			}
		}
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
		u32 stencilValue = 0;
		for (int i = 0; i < vertexCount; i += 2) {
			int index = ((const u16*)inds)[i];
			saved = transformed[index];
			int index2 = ((const u16*)inds)[i + 1];
			TransformedVertex &transVtx = transformed[index2];
			if (i == 0)
				stencilValue = transVtx.color0[3];
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
			// One game might be one of the Metal Gear ones, can't find the issue right now though.
			// else
			//	RotateUV(trans);

			// bottom right
			trans[4] = trans[0];

			// top left
			trans[5] = trans[2];
			trans += 6;

			numTrans += 6;
		}

		// We don't know the color until here, so we have to do it now, instead of in StateMapping.
		// Might want to reconsider the order of things later...
		if (gstate.isModeClear() && gstate.isClearModeAlphaMask()) {
			glstate.stencilFunc.set(GL_ALWAYS, stencilValue, 255);
		}
	}

	// TODO: Add a post-transform cache here for multi-RECTANGLES only.
	// Might help for text drawing.

	// these spam the gDebugger log.
	const int vertexSize = sizeof(transformed[0]);

	bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribPointer(ATTR_POSITION, 4, GL_FLOAT, GL_FALSE, vertexSize, drawBuffer);
	int attrMask = program->attrMask;
	if (attrMask & (1 << ATTR_TEXCOORD)) glVertexAttribPointer(ATTR_TEXCOORD, doTextureProjection ? 3 : 2, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)drawBuffer) + 4 * 4);
	if (attrMask & (1 << ATTR_COLOR0)) glVertexAttribPointer(ATTR_COLOR0, 4, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, ((uint8_t*)drawBuffer) + 7 * 4);
	if (attrMask & (1 << ATTR_COLOR1)) glVertexAttribPointer(ATTR_COLOR1, 3, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, ((uint8_t*)drawBuffer) + 8 * 4);
	if (drawIndexed) {
#if 1  // USING_GLES2
		glDrawElements(glprim[prim], numTrans, GL_UNSIGNED_SHORT, inds);
#else
		glDrawRangeElements(glprim[prim], 0, indexGen.MaxIndex(), numTrans, GL_UNSIGNED_SHORT, inds);
#endif
	} else {
		glDrawArrays(glprim[prim], 0, numTrans);
	}
}
