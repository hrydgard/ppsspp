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

#include "math/math_util.h"
#include "gfx_es2/gpu_features.h"

#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/Math3D.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"

// This is the software transform pipeline, which is necessary for supporting RECT
// primitives correctly without geometry shaders, and may be easier to use for
// debugging than the hardware transform pipeline.

// There's code here that simply expands transformed RECTANGLES into plain triangles.

// We're gonna have to keep software transforming RECTANGLES, unless we use a geom shader which we can't on OpenGL ES 2.0 or DX9.
// Usually, though, these primitives don't use lighting etc so it's no biggie performance wise, but it would be nice to get rid of
// this code.

// Actually, if we find the camera-relative right and down vectors, it might even be possible to add the extra points in pre-transformed
// space and thus make decent use of hardware transform.

// Actually again, single quads could be drawn more efficiently using GL_TRIANGLE_STRIP, no need to duplicate verts as for
// GL_TRIANGLES. Still need to sw transform to compute the extra two corners though.
//

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

// Note: 0 is BR and 2 is TL.

static void RotateUV(TransformedVertex v[4], float flippedMatrix[16], float ySign) {
	// Transform these two coordinates to figure out whether they're flipped or not.
	Vec4f tl;
	Vec3ByMatrix44(tl.AsArray(), v[2].pos, flippedMatrix);

	Vec4f br;
	Vec3ByMatrix44(br.AsArray(), v[0].pos, flippedMatrix);

	const float invtlw = 1.0f / tl.w;
	const float invbrw = 1.0f / br.w;
	const float x1 = tl.x * invtlw;
	const float x2 = br.x * invbrw;
	const float y1 = tl.y * invtlw * ySign;
	const float y2 = br.y * invbrw * ySign;

	if ((x1 < x2 && y1 < y2) || (x1 > x2 && y1 > y2))
		SwapUVs(v[1], v[3]);
}

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
static bool IsReallyAClear(const TransformedVertex *transformed, int numVerts, float x2, float y2) {
	if (transformed[0].x != 0.0f || transformed[0].y != 0.0f)
		return false;

	// Color and Z are decided by the second vertex, so only need to check those for matching color.
	u32 matchcolor = transformed[1].color0_32;
	float matchz = transformed[1].z;

	for (int i = 1; i < numVerts; i++) {
		if ((i & 1) == 0) {
			// Top left of a rectangle
			if (transformed[i].y != 0.0f)
				return false;
			if (i > 0 && transformed[i].x != transformed[i - 1].x)
				return false;
		} else {
			if (transformed[i].color0_32 != matchcolor || transformed[i].z != matchz)
				return false;
			// Bottom right
			if (transformed[i].y < y2)
				return false;
			if (transformed[i].x <= transformed[i - 1].x)
				return false;
		}
	}

	// The last vertical strip often extends outside the drawing area.
	if (transformed[numVerts - 1].x < x2)
		return false;

	return true;
}

static int ColorIndexOffset(int prim, GEShadeMode shadeMode, bool clearMode) {
	if (shadeMode != GE_SHADE_FLAT || clearMode) {
		return 0;
	}

	switch (prim) {
	case GE_PRIM_LINES:
	case GE_PRIM_LINE_STRIP:
		return 1;

	case GE_PRIM_TRIANGLES:
	case GE_PRIM_TRIANGLE_STRIP:
		return 2;

	case GE_PRIM_TRIANGLE_FAN:
		return 1;

	case GE_PRIM_RECTANGLES:
		// We already use BR color when expanding, so no need to offset.
		return 0;

	default:
		break;
	}
	return 0;
}

void SoftwareTransform(
	int prim, int vertexCount, u32 vertType, u16 *&inds, int indexType,
	const DecVtxFormat &decVtxFormat, int &maxIndex, TransformedVertex *&drawBuffer, int &numTrans, bool &drawIndexed, const SoftwareTransformParams *params, SoftwareTransformResult *result) {
	u8 *decoded = params->decoded;
	FramebufferManagerCommon *fbman = params->fbman;
	TextureCacheCommon *texCache = params->texCache;
	TransformedVertex *transformed = params->transformed;
	TransformedVertex *transformedExpanded = params->transformedExpanded;
	float ySign = 1.0f;
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();

	float uscale = 1.0f;
	float vscale = 1.0f;
	if (throughmode) {
		uscale /= gstate_c.curTextureWidth;
		vscale /= gstate_c.curTextureHeight;
	}

	bool skinningEnabled = vertTypeIsSkinningEnabled(vertType);

	const int w = gstate.getTextureWidth(0);
	const int h = gstate.getTextureHeight(0);
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
	if (my_isnan(fog_slope)) {
		// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
		// Just put the fog far away at a large finite distance.
		// Infinities and NaNs are rather unpredictable in shaders on many GPUs
		// so it's best to just make it a sane calculation.
		fog_end = 100000.0f;
		fog_slope = 1.0f;
	}

	int colorIndOffset = 0;
	if (params->provokeFlatFirst) {
		colorIndOffset = ColorIndexOffset(prim, gstate.getShadeMode(), gstate.isModeClear());
	}

	VertexReader reader(decoded, decVtxFormat, vertType);
	if (throughmode) {
		for (int index = 0; index < maxIndex; index++) {
			// Do not touch the coordinates or the colors. No lighting.
			reader.Goto(index);
			// TODO: Write to a flexible buffer, we don't always need all four components.
			TransformedVertex &vert = transformed[index];
			reader.ReadPos(vert.pos);

			if (reader.hasColor0()) {
				if (colorIndOffset != 0 && index + colorIndOffset < maxIndex) {
					reader.Goto(index + colorIndOffset);
					reader.ReadColor0_8888(vert.color0);
					reader.Goto(index);
				} else {
					reader.ReadColor0_8888(vert.color0);
				}
			} else {
				vert.color0_32 = gstate.getMaterialAmbientRGBA();
			}

			if (reader.hasUV()) {
				reader.ReadUV(vert.uv);

				vert.u *= uscale;
				vert.v *= vscale;
			} else {
				vert.u = 0.0f;
				vert.v = 0.0f;
			}

			// Ignore color1 and fog, never used in throughmode anyway.
			// The w of uv is also never used (hardcoded to 1.0.)
		}
	} else {
		// Okay, need to actually perform the full transform.
		for (int index = 0; index < maxIndex; index++) {
			reader.Goto(index);

			float v[3] = {0, 0, 0};
			Vec4f c0 = Vec4f(1, 1, 1, 1);
			Vec4f c1 = Vec4f(0, 0, 0, 0);
			float uv[3] = {0, 0, 1};
			float fogCoef = 1.0f;

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
				if (colorIndOffset != 0 && index + colorIndOffset < maxIndex) {
					reader.Goto(index + colorIndOffset);
					reader.ReadColor0(&unlitColor.x);
					reader.Goto(index);
				} else {
					reader.ReadColor0(&unlitColor.x);
				}
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
					// Summed color into c0 (will clamp in ToRGBA().)
					for (int j = 0; j < 4; j++) {
						c0[j] += litColor1[j];
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
				// We always prescale in the vertex decoder now.
				uv[0] = ruv[0];
				uv[1] = ruv[1];
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

			// TODO: Write to a flexible buffer, we don't always need all four components.
			memcpy(&transformed[index].x, v, 3 * sizeof(float));
			transformed[index].fog = fogCoef;
			memcpy(&transformed[index].u, uv, 3 * sizeof(float));
			transformed[index].color0_32 = c0.ToRGBA();
			transformed[index].color1_32 = c1.ToRGBA();

			// The multiplication by the projection matrix is still performed in the vertex shader.
			// So is vertex depth rounding, to simulate the 16-bit depth buffer.
		}
	}

	// Here's the best opportunity to try to detect rectangles used to clear the screen, and
	// replace them with real clears. This can provide a speedup on certain mobile chips.
	//
	// An alternative option is to simply ditch all the verts except the first and last to create a single
	// rectangle out of many. Quite a small optimization though.
	// Experiment: Disable on PowerVR (see issue #6290)
	// TODO: This bleeds outside the play area in non-buffered mode. Big deal? Probably not.
	// TODO: Allow creating a depth clear and a color draw.
	bool reallyAClear = false;
	if (maxIndex > 1 && prim == GE_PRIM_RECTANGLES && gstate.isModeClear() && params->allowClear) {
		int scissorX2 = gstate.getScissorX2() + 1;
		int scissorY2 = gstate.getScissorY2() + 1;
		reallyAClear = IsReallyAClear(transformed, maxIndex, scissorX2, scissorY2);
	}
	if (reallyAClear && gl_extensions.gpuVendor != GPU_VENDOR_IMGTEC) {
		// If alpha is not allowed to be separate, it must match for both depth/stencil and color.  Vulkan requires this.
		bool alphaMatchesColor = gstate.isClearModeColorMask() == gstate.isClearModeAlphaMask();
		bool depthMatchesStencil = gstate.isClearModeAlphaMask() == gstate.isClearModeDepthMask();
		if (params->allowSeparateAlphaClear || (alphaMatchesColor && depthMatchesStencil)) {
			result->color = transformed[1].color0_32;
			// Need to rescale from a [0, 1] float.  This is the final transformed value.
			result->depth = ToScaledDepth((s16)(int)(transformed[1].z * 65535.0f));
			result->action = SW_CLEAR;
			gpuStats.numClears++;
			return;
		}
	}

	// This means we're using a framebuffer (and one that isn't big enough.)
	if (gstate_c.curTextureHeight < (u32)h && maxIndex >= 2) {
		// Even if not rectangles, this will detect if either of the first two are outside the framebuffer.
		// HACK: Adding one pixel margin to this detection fixes issues in Assassin's Creed : Bloodlines,
		// while still keeping BOF working (see below).
		const float invTexH = 1.0f / gstate_c.curTextureHeight; // size of one texel.
		bool tlOutside;
		bool tlAlmostOutside;
		bool brOutside;
		// If we're outside heightFactor, then v must be wrapping or clamping.  Avoid this workaround.
		// If we're <= 1.0f, we're inside the framebuffer (workaround not needed.)
		// We buffer that 1.0f a little more with a texel to avoid some false positives.
		tlOutside = transformed[0].v <= heightFactor && transformed[0].v > 1.0f + invTexH;
		brOutside = transformed[1].v <= heightFactor && transformed[1].v > 1.0f + invTexH;
		// Careful: if br is outside, but tl is well inside, this workaround still doesn't make sense.
		// We go with halfway, since we overestimate framebuffer heights sometimes but not by much.
		tlAlmostOutside = transformed[0].v <= heightFactor && transformed[0].v >= 0.5f;
		if (tlOutside || (brOutside && tlAlmostOutside)) {
			// Okay, so we're texturing from outside the framebuffer, but inside the texture height.
			// Breath of Fire 3 does this to access a render surface at an offset.
			const u32 bpp = fbman->GetTargetFormat() == GE_FORMAT_8888 ? 4 : 2;
			const u32 prevH = texCache->AttachedDrawingHeight();
			const u32 fb_size = bpp * fbman->GetTargetStride() * prevH;
			const u32 prevYOffset = gstate_c.curTextureYOffset;
			if (texCache->SetOffsetTexture(fb_size)) {
				const float oldWidthFactor = widthFactor;
				const float oldHeightFactor = heightFactor;
				widthFactor = (float) w / (float) gstate_c.curTextureWidth;
				heightFactor = (float) h / (float) gstate_c.curTextureHeight;

				// We've already baked in the old gstate_c.curTextureYOffset, so correct.
				const float yDiff = (float) (prevH + prevYOffset - gstate_c.curTextureYOffset) / (float) h;
				for (int index = 0; index < maxIndex; ++index) {
					transformed[index].u *= widthFactor / oldWidthFactor;
					// Inverse it back to scale to the new FBO, and add 1.0f to account for old FBO.
					transformed[index].v = (transformed[index].v / oldHeightFactor - yDiff) * heightFactor;
				}
			}
		}
	}

	// Step 2: expand rectangles.
	drawBuffer = transformed;
	numTrans = 0;
	drawIndexed = false;

	if (prim != GE_PRIM_RECTANGLES) {
		// We can simply draw the unexpanded buffer.
		numTrans = vertexCount;
		drawIndexed = true;
	} else {
		bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
		if (useBufferedRendering)
			ySign = -ySign;

		float flippedMatrix[16];
		if (!throughmode) {
			memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));

			const bool invertedY = useBufferedRendering ? (gstate_c.vpHeight < 0) : (gstate_c.vpHeight > 0);
			if (invertedY) {
				flippedMatrix[1] = -flippedMatrix[1];
				flippedMatrix[5] = -flippedMatrix[5];
				flippedMatrix[9] = -flippedMatrix[9];
				flippedMatrix[13] = -flippedMatrix[13];
			}
			const bool invertedX = gstate_c.vpWidth < 0;
			if (invertedX) {
				flippedMatrix[0] = -flippedMatrix[0];
				flippedMatrix[4] = -flippedMatrix[4];
				flippedMatrix[8] = -flippedMatrix[8];
				flippedMatrix[12] = -flippedMatrix[12];
			}
		}

		//rectangles always need 2 vertices, disregard the last one if there's an odd number
		vertexCount = vertexCount & ~1;
		numTrans = 0;
		drawBuffer = transformedExpanded;
		TransformedVertex *trans = &transformedExpanded[0];
		const u16 *indsIn = (const u16 *)inds;
		u16 *newInds = inds + vertexCount;
		u16 *indsOut = newInds;
		maxIndex = 4 * (vertexCount / 2);
		for (int i = 0; i < vertexCount; i += 2) {
			const TransformedVertex &transVtxTL = transformed[indsIn[i + 0]];
			const TransformedVertex &transVtxBR = transformed[indsIn[i + 1]];

			// We have to turn the rectangle into two triangles, so 6 points.
			// This is 4 verts + 6 indices.

			// bottom right
			trans[0] = transVtxBR;

			// top right
			trans[1] = transVtxBR;
			trans[1].y = transVtxTL.y;
			trans[1].v = transVtxTL.v;

			// top left
			trans[2] = transVtxBR;
			trans[2].x = transVtxTL.x;
			trans[2].y = transVtxTL.y;
			trans[2].u = transVtxTL.u;
			trans[2].v = transVtxTL.v;

			// bottom left
			trans[3] = transVtxBR;
			trans[3].x = transVtxTL.x;
			trans[3].u = transVtxTL.u;

			// That's the four corners. Now process UV rotation.
			if (throughmode)
				RotateUVThrough(trans);
			else
				RotateUV(trans, flippedMatrix, ySign);

			// Triangle: BR-TR-TL
			indsOut[0] = i * 2 + 0;
			indsOut[1] = i * 2 + 1;
			indsOut[2] = i * 2 + 2;
			// Triangle: BL-BR-TL
			indsOut[3] = i * 2 + 3;
			indsOut[4] = i * 2 + 0;
			indsOut[5] = i * 2 + 2;
			trans += 4;
			indsOut += 6;

			numTrans += 6;
		}
		inds = newInds;
		drawIndexed = true;

		// We don't know the color until here, so we have to do it now, instead of in StateMapping.
		// Might want to reconsider the order of things later...
		if (gstate.isModeClear() && gstate.isClearModeAlphaMask()) {
			result->setStencil = true;
			if (vertexCount > 1) {
				// Take the bottom right alpha value of the first rect as the stencil value.
				// Technically, each rect could individually fill its stencil, but most of the
				// time they use the same one.
				result->stencilValue = transformed[indsIn[1]].color0[3];
			} else {
				result->stencilValue = 0;
			}
		}
	}

	if (gstate.isModeClear()) {
		gpuStats.numClears++;
	}

	result->action = SW_DRAW_PRIMITIVES;
}
