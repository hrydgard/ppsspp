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

#include <algorithm>
#include <cmath>
#include "Common/CPUDetect.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/Math3D.h"
#include "GPU/Common/FramebufferManagerCommon.h"
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

static void RotateUV(TransformedVertex v[4], bool flippedY) {
	// We use the transformed tl/br coordinates to figure out whether they're flipped or not.
	float ySign = flippedY ? -1.0 : 1.0;

	const float x1 = v[2].x;
	const float x2 = v[0].x;
	const float y1 = v[2].y * ySign;
	const float y2 = v[0].y * ySign;

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

void SoftwareTransform::SetProjMatrix(float mtx[14], bool invertedX, bool invertedY, const Lin::Vec3 &trans, const Lin::Vec3 &scale) {
	memcpy(&projMatrix_.m, mtx, 16 * sizeof(float));

	if (invertedY) {
		projMatrix_.xy = -projMatrix_.xy;
		projMatrix_.yy = -projMatrix_.yy;
		projMatrix_.zy = -projMatrix_.zy;
		projMatrix_.wy = -projMatrix_.wy;
	}
	if (invertedX) {
		projMatrix_.xx = -projMatrix_.xx;
		projMatrix_.yx = -projMatrix_.yx;
		projMatrix_.zx = -projMatrix_.zx;
		projMatrix_.wx = -projMatrix_.wx;
	}

	projMatrix_.translateAndScale(trans, scale);
}

void SoftwareTransform::Decode(int prim, u32 vertType, const DecVtxFormat &decVtxFormat, int maxIndex, SoftwareTransformResult *result) {
	u8 *decoded = params_.decoded;
	TransformedVertex *transformed = params_.transformed;
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
	// Same fixup as in ShaderManagerGLES.cpp
	if (my_isnanorinf(fog_end)) {
		// Not really sure what a sensible value might be, but let's try 64k.
		fog_end = std::signbit(fog_end) ? -65535.0f : 65535.0f;
	}
	if (my_isnanorinf(fog_slope)) {
		fog_slope = std::signbit(fog_slope) ? -65535.0f : 65535.0f;
	}

	int provokeIndOffset = 0;
	if (params_.provokeFlatFirst) {
		provokeIndOffset = ColorIndexOffset(prim, gstate.getShadeMode(), gstate.isModeClear());
	}

	VertexReader reader(decoded, decVtxFormat, vertType);
	if (throughmode) {
		for (int index = 0; index < maxIndex; index++) {
			// Do not touch the coordinates or the colors. No lighting.
			reader.Goto(index);
			// TODO: Write to a flexible buffer, we don't always need all four components.
			TransformedVertex &vert = transformed[index];
			reader.ReadPos(vert.pos);
			vert.pos_w = 1.0f;

			if (reader.hasColor0()) {
				if (provokeIndOffset != 0 && index + provokeIndOffset < maxIndex) {
					reader.Goto(index + provokeIndOffset);
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
			vert.uv_w = 1.0f;

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

			float ruv[2] = { 0.0f, 0.0f };
			if (reader.hasUV())
				reader.ReadUV(ruv);

			// Read all the provoking vertex values here.
			Vec4f unlitColor;
			if (provokeIndOffset != 0 && index + provokeIndOffset < maxIndex)
				reader.Goto(index + provokeIndOffset);
			if (reader.hasColor0())
				reader.ReadColor0(unlitColor.AsArray());
			else
				unlitColor = Vec4f::FromRGBA(gstate.getMaterialAmbientRGBA());
			if (reader.hasNormal())
				reader.ReadNrm(normal.AsArray());

			if (!skinningEnabled) {
				Vec3ByMatrix43(out, pos, gstate.worldMatrix);
				if (reader.hasNormal()) {
					if (gstate.areNormalsReversed()) {
						normal = -normal;
					}
					Norm3ByMatrix43(worldnormal.AsArray(), normal.AsArray(), gstate.worldMatrix);
					worldnormal = worldnormal.NormalizedOr001(cpu_info.bSSE4_1);
				}
			} else {
				float weights[8];
				// TODO: For flat, are weights from the provoking used for color/normal?
				reader.Goto(index);
				reader.ReadWeights(weights);

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
					worldnormal = worldnormal.NormalizedOr001(cpu_info.bSSE4_1);
				}
			}

			// Perform lighting here if enabled.
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
				for (int j = 0; j < 4; j++) {
					c0[j] = unlitColor[j];
				}
				if (lmode) {
					// c1 is already 0.
				}
			}

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
					// TODO: What's the correct behavior with flat shading?  Provoked normal or real normal?

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
						source = normal.NormalizedOr001(cpu_info.bSSE4_1);
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
					auto getLPosFloat = [&](int l, int i) {
						return getFloat24(gstate.lpos[l * 3 + i]);
					};
					auto getLPos = [&](int l) {
						return Vec3f(getLPosFloat(l, 0), getLPosFloat(l, 1), getLPosFloat(l, 2));
					};
					auto calcShadingLPos = [&](int l) {
						Vec3f pos = getLPos(l);
						return pos.NormalizedOr001(cpu_info.bSSE4_1);
					};
					// Might not have lighting enabled, so don't use lighter.
					Vec3f lightpos0 = calcShadingLPos(gstate.getUVLS0());
					Vec3f lightpos1 = calcShadingLPos(gstate.getUVLS1());

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
			Vec3ByMatrix44(transformed[index].pos, v, projMatrix_.m);
			transformed[index].fog = fogCoef;
			memcpy(&transformed[index].uv, uv, 3 * sizeof(float));
			transformed[index].color0_32 = c0.ToRGBA();
			transformed[index].color1_32 = c1.ToRGBA();

			// Vertex depth rounding is done in the shader, to simulate the 16-bit depth buffer.
		}
	}

	// Here's the best opportunity to try to detect rectangles used to clear the screen, and
	// replace them with real clears. This can provide a speedup on certain mobile chips.
	//
	// An alternative option is to simply ditch all the verts except the first and last to create a single
	// rectangle out of many. Quite a small optimization though.
	// TODO: This bleeds outside the play area in non-buffered mode. Big deal? Probably not.
	// TODO: Allow creating a depth clear and a color draw.
	bool reallyAClear = false;
	if (maxIndex > 1 && prim == GE_PRIM_RECTANGLES && gstate.isModeClear() && throughmode) {
		int scissorX2 = gstate.getScissorX2() + 1;
		int scissorY2 = gstate.getScissorY2() + 1;
		reallyAClear = IsReallyAClear(transformed, maxIndex, scissorX2, scissorY2);
		if (reallyAClear && gstate.getColorMask() != 0xFFFFFFFF && (gstate.isClearModeColorMask() || gstate.isClearModeAlphaMask())) {
			result->setSafeSize = true;
			result->safeWidth = scissorX2;
			result->safeHeight = scissorY2;
		}
	}
	if (params_.allowClear && reallyAClear && gl_extensions.gpuVendor != GPU_VENDOR_IMGTEC) {
		// If alpha is not allowed to be separate, it must match for both depth/stencil and color.  Vulkan requires this.
		bool alphaMatchesColor = gstate.isClearModeColorMask() == gstate.isClearModeAlphaMask();
		bool depthMatchesStencil = gstate.isClearModeAlphaMask() == gstate.isClearModeDepthMask();
		bool matchingComponents = params_.allowSeparateAlphaClear || (alphaMatchesColor && depthMatchesStencil);
		bool stencilNotMasked = !gstate.isClearModeAlphaMask() || gstate.getStencilWriteMask() == 0x00;
		if (matchingComponents && stencilNotMasked) {
			result->color = transformed[1].color0_32;
			// Need to rescale from a [0, 1] float.  This is the final transformed value.
			result->depth = ToScaledDepthFromIntegerScale((int)(transformed[1].z * 65535.0f));
			result->action = SW_CLEAR;
			gpuStats.numClears++;
			return;
		}
	}

	// Detect full screen "clears" that might not be so obvious, to set the safe size if possible.
	if (!result->setSafeSize && prim == GE_PRIM_RECTANGLES && maxIndex == 2 && throughmode) {
		bool clearingColor = gstate.isModeClear() && (gstate.isClearModeColorMask() || gstate.isClearModeAlphaMask());
		bool writingColor = gstate.getColorMask() != 0xFFFFFFFF;
		bool startsZeroX = transformed[0].x <= 0.0f && transformed[1].x > 0.0f && transformed[1].x > transformed[0].x;
		bool startsZeroY = transformed[0].y <= 0.0f && transformed[1].y > 0.0f && transformed[1].y > transformed[0].y;

		if (startsZeroX && startsZeroY && (clearingColor || writingColor)) {
			int scissorX2 = gstate.getScissorX2() + 1;
			int scissorY2 = gstate.getScissorY2() + 1;
			result->setSafeSize = true;
			result->safeWidth = std::min(scissorX2, (int)transformed[1].x);
			result->safeHeight = std::min(scissorY2, (int)transformed[1].y);
		}
	}
}

// Also, this assumes SetTexture() has already figured out the actual texture height.
void SoftwareTransform::DetectOffsetTexture(int maxIndex) {
	TransformedVertex *transformed = params_.transformed;

	const int w = gstate.getTextureWidth(0);
	const int h = gstate.getTextureHeight(0);
	float widthFactor = (float)w / (float)gstate_c.curTextureWidth;
	float heightFactor = (float)h / (float)gstate_c.curTextureHeight;

	// Breath of Fire 3 does some interesting rendering here, probably from being a port.
	// It draws at 384x240 to two buffers in VRAM, one right after the other.
	// We end up creating separate framebuffers, and rendering to each.
	// But the game then stretches this to the screen - and reads from a single 512 tall texture.
	// We initially use the first framebuffer.  This code detects the read from the second.
	//
	// First Vs: 12, 228 - second Vs: 252, 468 - estimated fb height: 272

	// If curTextureHeight is < h, it must be a framebuffer that wasn't full height.
	if (gstate_c.curTextureHeight < (u32)h && maxIndex >= 2) {
		// This is the max V that will still land within the framebuffer (since it's shorter.)
		// We already adjusted V to the framebuffer above.
		const float maxAvailableV = 1.0f;
		// This is the max V that would've been inside the original texture size.
		const float maxValidV = heightFactor;

		// Apparently, Assassin's Creed: Bloodlines accesses just outside.
		const float invTexH = 1.0f / gstate_c.curTextureHeight; // size of one texel.

		// Are either TL or BR inside the texture but outside the framebuffer?
		const bool tlOutside = transformed[0].v > maxAvailableV + invTexH && transformed[0].v <= maxValidV;
		const bool brOutside = transformed[1].v > maxAvailableV + invTexH && transformed[1].v <= maxValidV;

		// If TL isn't outside, is it at least near the end?
		// We check this because some games do 0-512 from a 272 tall framebuf.
		const bool tlAlmostOutside = transformed[0].v > maxAvailableV * 0.5f && transformed[0].v <= maxValidV;

		if (tlOutside || (brOutside && tlAlmostOutside)) {
			const u32 prevXOffset = gstate_c.curTextureXOffset;
			const u32 prevYOffset = gstate_c.curTextureYOffset;

			// This is how far the nearest coord is, so that's where we'll look for the next framebuf.
			const u32 yOffset = (int)(gstate_c.curTextureHeight * std::min(transformed[0].v, transformed[1].v));
			if (params_.texCache->SetOffsetTexture(yOffset)) {
				const float oldWidthFactor = widthFactor;
				const float oldHeightFactor = heightFactor;
				widthFactor = (float)w / (float)gstate_c.curTextureWidth;
				heightFactor = (float)h / (float)gstate_c.curTextureHeight;

				// We need to subtract this offset from the UVs to address the new framebuf.
				const float adjustedYOffset = yOffset + prevYOffset - gstate_c.curTextureYOffset;
				const float yDiff = (float)adjustedYOffset / (float)h;
				const float adjustedXOffset = prevXOffset - gstate_c.curTextureXOffset;
				const float xDiff = (float)adjustedXOffset / (float)w;

				for (int index = 0; index < maxIndex; ++index) {
					transformed[index].u = (transformed[index].u / oldWidthFactor - xDiff) * widthFactor;
					transformed[index].v = (transformed[index].v / oldHeightFactor - yDiff) * heightFactor;
				}

				// We undid the offset, so reset.  This avoids a different shader.
				gstate_c.curTextureXOffset = prevXOffset;
				gstate_c.curTextureYOffset = prevYOffset;
			}
		}
	}
}

// NOTE: The viewport must be up to date!
void SoftwareTransform::BuildDrawingParams(int prim, int vertexCount, u32 vertType, u16 *&inds, int &maxIndex, SoftwareTransformResult *result) {
	TransformedVertex *transformed = params_.transformed;
	TransformedVertex *transformedExpanded = params_.transformedExpanded;
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;

	// Step 2: expand and process primitives.
	result->drawBuffer = transformed;
	int numTrans = 0;

	FramebufferManagerCommon *fbman = params_.fbman;
	bool useBufferedRendering = fbman->UseBufferedRendering();

	if (prim == GE_PRIM_RECTANGLES) {
		ExpandRectangles(vertexCount, maxIndex, inds, transformed, transformedExpanded, numTrans, throughmode);
		result->drawBuffer = transformedExpanded;
		result->drawIndexed = true;

		// We don't know the color until here, so we have to do it now, instead of in StateMapping.
		// Might want to reconsider the order of things later...
		if (gstate.isModeClear() && gstate.isClearModeAlphaMask()) {
			result->setStencil = true;
			if (vertexCount > 1) {
				// Take the bottom right alpha value of the first rect as the stencil value.
				// Technically, each rect could individually fill its stencil, but most of the
				// time they use the same one.
				result->stencilValue = transformed[inds[1]].color0[3];
			} else {
				result->stencilValue = 0;
			}
		}
	} else if (prim == GE_PRIM_POINTS) {
		ExpandPoints(vertexCount, maxIndex, inds, transformed, transformedExpanded, numTrans, throughmode);
		result->drawBuffer = transformedExpanded;
		result->drawIndexed = true;
	} else if (prim == GE_PRIM_LINES) {
		ExpandLines(vertexCount, maxIndex, inds, transformed, transformedExpanded, numTrans, throughmode);
		result->drawBuffer = transformedExpanded;
		result->drawIndexed = true;
	} else {
		// We can simply draw the unexpanded buffer.
		numTrans = vertexCount;
		result->drawIndexed = true;

		// If we don't support custom cull in the shader, process it here.
		if (!gstate_c.Supports(GPU_SUPPORTS_CULL_DISTANCE) && vertexCount > 0 && !throughmode) {
			const u16 *indsIn = (const u16 *)inds;
			u16 *newInds = inds + vertexCount;
			u16 *indsOut = newInds;

			float minZValue, maxZValue;
			CalcCullParams(minZValue, maxZValue);

			std::vector<int> outsideZ;
			outsideZ.resize(vertexCount);

			// First, check inside/outside directions for each index.
			for (int i = 0; i < vertexCount; ++i) {
				float z = transformed[indsIn[i]].z / transformed[indsIn[i]].pos_w;
				if (z >= maxZValue)
					outsideZ[i] = 1;
				else if (z <= minZValue)
					outsideZ[i] = -1;
				else
					outsideZ[i] = 0;
			}

			// Now, for each primitive type, throw away the indices if:
			//  - Depth clamp on, and ALL verts are outside *in the same direction*.
			//  - Depth clamp off, and ANY vert is outside.
			if (prim == GE_PRIM_TRIANGLES && gstate.isDepthClampEnabled()) {
				numTrans = 0;
				for (int i = 0; i < vertexCount - 2; i += 3) {
					if (outsideZ[i + 0] != 0 && outsideZ[i + 0] == outsideZ[i + 1] && outsideZ[i + 0] == outsideZ[i + 2]) {
						// All outside, and all the same direction.  Nuke this triangle.
						continue;
					}

					memcpy(indsOut, indsIn + i, 3 * sizeof(uint16_t));
					indsOut += 3;
					numTrans += 3;
				}

				inds = newInds;
			} else if (prim == GE_PRIM_TRIANGLES) {
				numTrans = 0;
				for (int i = 0; i < vertexCount - 2; i += 3) {
					if (outsideZ[i + 0] != 0 || outsideZ[i + 1] != 0 || outsideZ[i + 2] != 0) {
						// Even one outside, and we cull.
						continue;
					}

					memcpy(indsOut, indsIn + i, 3 * sizeof(uint16_t));
					indsOut += 3;
					numTrans += 3;
				}

				inds = newInds;
			}
		}
	}

	if (gstate.isModeClear()) {
		gpuStats.numClears++;
	}

	result->action = SW_DRAW_PRIMITIVES;
	result->drawNumTrans = numTrans;
}

void SoftwareTransform::CalcCullParams(float &minZValue, float &maxZValue) {
	// The projected Z can be up to 0x3F8000FF, which is where this constant is from.
	// It seems like it may only maintain 15 mantissa bits (excluding implied.)
	maxZValue = 1.000030517578125f * gstate_c.vpDepthScale;
	minZValue = -maxZValue;
	// Scale and offset the Z appropriately, since we baked that into a projection transform.
	if (params_.usesHalfZ) {
		maxZValue = maxZValue * 0.5f + 0.5f + gstate_c.vpZOffset * 0.5f;
		minZValue = minZValue * 0.5f + 0.5f + gstate_c.vpZOffset * 0.5f;
	} else {
		maxZValue += gstate_c.vpZOffset;
		minZValue += gstate_c.vpZOffset;
	}
	// In case scale was negative, flip.
	if (minZValue > maxZValue)
		std::swap(minZValue, maxZValue);
}

void SoftwareTransform::ExpandRectangles(int vertexCount, int &maxIndex, u16 *&inds, TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode) {
	// Rectangles always need 2 vertices, disregard the last one if there's an odd number.
	vertexCount = vertexCount & ~1;
	numTrans = 0;
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
		if (throughmode) {
			RotateUVThrough(trans);
		} else {
			RotateUV(trans, params_.flippedY);
		}

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
}

void SoftwareTransform::ExpandLines(int vertexCount, int &maxIndex, u16 *&inds, TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode) {
	// Lines always need 2 vertices, disregard the last one if there's an odd number.
	vertexCount = vertexCount & ~1;
	numTrans = 0;
	TransformedVertex *trans = &transformedExpanded[0];

	const u16 *indsIn = (const u16 *)inds;
	u16 *newInds = inds + vertexCount;
	u16 *indsOut = newInds;

	float dx = 1.0f * gstate_c.vpWidthScale * (1.0f / fabsf(gstate.getViewportXScale()));
	float dy = 1.0f * gstate_c.vpHeightScale * (1.0f / fabsf(gstate.getViewportYScale()));
	float du = 1.0f / gstate_c.curTextureWidth;
	float dv = 1.0f / gstate_c.curTextureHeight;

	if (throughmode) {
		dx = 1.0f;
		dy = 1.0f;
	}

	maxIndex = 4 * (vertexCount / 2);

	if (PSP_CoreParameter().compat.flags().CenteredLines) {
		// Lines meant to be pretty in 3D like in Echochrome.

		// We expand them in both directions for symmetry, so we need to halve the expansion.
		dx *= 0.5f;
		dy *= 0.5f;

		for (int i = 0; i < vertexCount; i += 2) {
			const TransformedVertex &transVtx1 = transformed[indsIn[i + 0]];
			const TransformedVertex &transVtx2 = transformed[indsIn[i + 1]];

			// Okay, let's calculate the perpendicular.
			float horizontal = transVtx2.x - transVtx1.x;
			float vertical = transVtx2.y - transVtx1.y;
			Vec2f addWidth = Vec2f(-vertical, horizontal).Normalized();

			float xoff = addWidth.x * dx;
			float yoff = addWidth.y * dy;

			// bottom right
			trans[0].CopyFromWithOffset(transVtx2, xoff, yoff);
			// top right
			trans[1].CopyFromWithOffset(transVtx1, xoff, yoff);
			// top left
			trans[2].CopyFromWithOffset(transVtx1, -xoff, -yoff);
			// bottom left
			trans[3].CopyFromWithOffset(transVtx2, -xoff, -yoff);

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
	} else {
		// Lines meant to be as closely compatible with upscaled 2D drawing as possible.
		// We use this as default.

		for (int i = 0; i < vertexCount; i += 2) {
			const TransformedVertex &transVtx1 = transformed[indsIn[i + 0]];
			const TransformedVertex &transVtx2 = transformed[indsIn[i + 1]];

			const TransformedVertex &transVtxT = transVtx1.y <= transVtx2.y ? transVtx1 : transVtx2;
			const TransformedVertex &transVtxB = transVtx1.y <= transVtx2.y ? transVtx2 : transVtx1;
			const TransformedVertex &transVtxL = transVtx1.x <= transVtx2.x ? transVtx1 : transVtx2;
			const TransformedVertex &transVtxR = transVtx1.x <= transVtx2.x ? transVtx2 : transVtx1;

			// Sort the points so our perpendicular will bias the right direction.
			const TransformedVertex &transVtxTL = (transVtxT.y != transVtxB.y || transVtxT.x > transVtxB.x) ? transVtxT : transVtxB;
			const TransformedVertex &transVtxBL = (transVtxT.y != transVtxB.y || transVtxT.x > transVtxB.x) ? transVtxB : transVtxT;

			// Okay, let's calculate the perpendicular.
			float horizontal = transVtxTL.x - transVtxBL.x;
			float vertical = transVtxTL.y - transVtxBL.y;
			Vec2f addWidth = Vec2f(-vertical, horizontal).Normalized();

			// bottom right
			trans[0] = transVtxBL;
			trans[0].x += addWidth.x * dx;
			trans[0].y += addWidth.y * dy;
			trans[0].u += addWidth.x * du;
			trans[0].v += addWidth.y * dv;

			// top right
			trans[1] = transVtxTL;
			trans[1].x += addWidth.x * dx;
			trans[1].y += addWidth.y * dy;
			trans[1].u += addWidth.x * du;
			trans[1].v += addWidth.y * dv;

			// top left
			trans[2] = transVtxTL;

			// bottom left
			trans[3] = transVtxBL;

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
	}

	inds = newInds;
}

void SoftwareTransform::ExpandPoints(int vertexCount, int &maxIndex, u16 *&inds, TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode) {
	numTrans = 0;
	TransformedVertex *trans = &transformedExpanded[0];

	const u16 *indsIn = (const u16 *)inds;
	u16 *newInds = inds + vertexCount;
	u16 *indsOut = newInds;

	float dx = 1.0f * gstate_c.vpWidthScale * (1.0f / gstate.getViewportXScale());
	float dy = 1.0f * gstate_c.vpHeightScale * (1.0f / gstate.getViewportYScale());
	float du = 1.0f / gstate_c.curTextureWidth;
	float dv = 1.0f / gstate_c.curTextureHeight;

	if (throughmode) {
		dx = 1.0f;
		dy = 1.0f;
	}

	maxIndex = 4 * vertexCount;
	for (int i = 0; i < vertexCount; ++i) {
		const TransformedVertex &transVtxTL = transformed[indsIn[i]];

		// Create the bottom right version.
		TransformedVertex transVtxBR = transVtxTL;
		transVtxBR.x += dx * transVtxTL.pos_w;
		transVtxBR.y += dy * transVtxTL.pos_w;
		transVtxBR.u += du * transVtxTL.uv_w;
		transVtxBR.v += dv * transVtxTL.uv_w;

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

		// Triangle: BR-TR-TL
		indsOut[0] = i * 4 + 0;
		indsOut[1] = i * 4 + 1;
		indsOut[2] = i * 4 + 2;
		// Triangle: BL-BR-TL
		indsOut[3] = i * 4 + 3;
		indsOut[4] = i * 4 + 0;
		indsOut[5] = i * 4 + 2;
		trans += 4;
		indsOut += 6;

		numTrans += 6;
	}
	inds = newInds;
}
