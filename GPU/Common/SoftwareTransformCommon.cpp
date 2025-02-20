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
#include "Core/System.h"
#include "GPU/GPUState.h"
#include "GPU/Math3D.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"

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
	if (transformed[0].x < 0.0f || transformed[0].y < 0.0f || transformed[0].x > 0.5f || transformed[0].y > 0.5f)
		return false;

	const float originY = transformed[0].y;

	// Color and Z are decided by the second vertex, so only need to check those for matching color.
	const u32 matchcolor = transformed[1].color0_32;
	const float matchz = transformed[1].z;

	for (int i = 1; i < numVerts; i++) {
		if ((i & 1) == 0) {
			// Top left of a rectangle
			if (transformed[i].y != originY)
				return false;
			float gap = fabsf(transformed[i].x - transformed[i - 1].x);  // Should probably do some smarter check.
			if (i > 0 && gap > 0.0625)
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

void SoftwareTransform::SetProjMatrix(const float mtx[14], bool invertedX, bool invertedY, const Lin::Vec3 &trans, const Lin::Vec3 &scale) {
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

void SoftwareTransform::Transform(int prim, u32 vertType, const DecVtxFormat &decVtxFormat, int numDecodedVerts, SoftwareTransformResult *result) {
	u8 *decoded = params_.decoded;
	TransformedVertex *transformed = params_.transformed;
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();

	float uscale = 1.0f;
	float vscale = 1.0f;
	if (throughmode && prim != GE_PRIM_RECTANGLES) {
		// For through rectangles, we do this scaling in Expand.
		uscale /= gstate_c.curTextureWidth;
		vscale /= gstate_c.curTextureHeight;
	}

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

	VertexReader reader(decoded, decVtxFormat, vertType);
	if (throughmode) {
		const u32 materialAmbientRGBA = gstate.getMaterialAmbientRGBA();
		const bool hasColor = reader.hasColor0();
		const bool hasUV = reader.hasUV();
		for (int index = 0; index < numDecodedVerts; index++) {
			// Do not touch the coordinates or the colors. No lighting.
			reader.Goto(index);
			// TODO: Write to a flexible buffer, we don't always need all four components.
			TransformedVertex &vert = transformed[index];
			reader.ReadPosThrough(vert.pos);
			vert.pos_w = 1.0f;

			if (hasColor) {
				vert.color0_32 = reader.ReadColor0_8888();
			} else {
				vert.color0_32 = materialAmbientRGBA;
			}

			if (hasUV) {
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
		const Vec4f materialAmbientRGBA = Vec4f::FromRGBA(gstate.getMaterialAmbientRGBA());
		// Okay, need to actually perform the full transform.
		for (int index = 0; index < numDecodedVerts; index++) {
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
			reader.ReadPosNonThrough(pos);

			float ruv[2] = { 0.0f, 0.0f };
			if (reader.hasUV())
				reader.ReadUV(ruv);

			Vec4f unlitColor;
			if (reader.hasColor0())
				reader.ReadColor0(unlitColor.AsArray());
			else
				unlitColor = materialAmbientRGBA;
			if (reader.hasNormal())
				reader.ReadNrm(normal.AsArray());

			Vec3ByMatrix43(out, pos, gstate.worldMatrix);
			if (reader.hasNormal()) {
				if (gstate.areNormalsReversed()) {
					normal = -normal;
				}
				Norm3ByMatrix43(worldnormal.AsArray(), normal.AsArray(), gstate.worldMatrix);
				worldnormal = worldnormal.NormalizedOr001(cpu_info.bSSE4_1);
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
					// Projection mapping
					Vec3f source(0.0f, 0.0f, 1.0f);
					switch (gstate.getUVProjMode())	{
					case GE_PROJMAP_POSITION: // Use model space XYZ as source
						source = pos;
						break;

					case GE_PROJMAP_UV: // Use unscaled UV as source
						source = Vec3f(ruv[0], ruv[1], 0.0f);
						break;

					case GE_PROJMAP_NORMALIZED_NORMAL: // Use normalized normal as source
						source = normal.Normalized(cpu_info.bSSE4_1);
						if (!reader.hasNormal()) {
							ERROR_LOG_REPORT(Log::G3D, "Normal projection mapping without normal?");
						}
						break;

					case GE_PROJMAP_NORMAL: // Use non-normalized normal as source!
						source = normal;
						if (!reader.hasNormal()) {
							ERROR_LOG_REPORT(Log::G3D, "Normal projection mapping without normal?");
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
				ERROR_LOG_REPORT(Log::G3D, "Impossible UV gen mode? %d", gstate.getUVGenMode());
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
	if (numDecodedVerts > 1 && prim == GE_PRIM_RECTANGLES && gstate.isModeClear() && throughmode) {
		int scissorX2 = gstate.getScissorX2() + 1;
		int scissorY2 = gstate.getScissorY2() + 1;
		reallyAClear = IsReallyAClear(transformed, numDecodedVerts, scissorX2, scissorY2);
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
			DepthScaleFactors depthScale = GetDepthScaleFactors(gstate_c.UseFlags());
			result->color = transformed[1].color0_32;
			// Need to rescale from a [0, 1] float.  This is the final transformed value.
			result->depth = depthScale.EncodeFromU16((float)(int)(transformed[1].z * 65535.0f));
			result->action = SW_CLEAR;
			gpuStats.numClears++;
			return;
		}
	}

	// Detect full screen "clears" that might not be so obvious, to set the safe size if possible.
	if (!result->setSafeSize && prim == GE_PRIM_RECTANGLES && numDecodedVerts == 2 && throughmode) {
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

void SoftwareTransform::BuildDrawingParams(int prim, int vertexCount, u32 vertType, u16 *&inds, int indsSize, int &numDecodedVerts, int vertsSize, SoftwareTransformResult *result) {
	TransformedVertex *transformed = params_.transformed;
	TransformedVertex *transformedExpanded = params_.transformedExpanded;
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;

	// Step 2: expand and process primitives.
	result->drawBuffer = transformed;
	int numTrans = 0;

	FramebufferManagerCommon *fbman = params_.fbman;
	bool useBufferedRendering = fbman->UseBufferedRendering();

	if (prim == GE_PRIM_RECTANGLES) {
		if (!ExpandRectangles(vertexCount, numDecodedVerts, vertsSize, inds, indsSize, transformed, transformedExpanded, numTrans, throughmode, &result->pixelMapped)) {
			result->drawNumTrans = 0;
			result->pixelMapped = false;
			return;
		}
		result->drawBuffer = transformedExpanded;

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
		result->pixelMapped = false;
		if (!ExpandPoints(vertexCount, numDecodedVerts, vertsSize, inds, indsSize, transformed, transformedExpanded, numTrans, throughmode)) {
			result->drawNumTrans = 0;
			return;
		}
		result->drawBuffer = transformedExpanded;
	} else if (prim == GE_PRIM_LINES) {
		result->pixelMapped = false;
		if (!ExpandLines(vertexCount, numDecodedVerts, vertsSize, inds, indsSize, transformed, transformedExpanded, numTrans, throughmode)) {
			result->drawNumTrans = 0;
			return;
		}
		result->drawBuffer = transformedExpanded;
	} else {
		// We can simply draw the unexpanded buffer.
		numTrans = vertexCount;
		result->pixelMapped = false;

		// If we don't support custom cull in the shader, process it here.
		if (!gstate_c.Use(GPU_USE_CULL_DISTANCE) && vertexCount > 0 && !throughmode) {
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
				if (z > maxZValue)
					outsideZ[i] = 1;
				else if (z < minZValue)
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
		} else if (throughmode && g_Config.bSmart2DTexFiltering && !gstate_c.textureIsVideo) {
			// We check some common cases for pixel mapping.
			// TODO: It's not really optimal that some previous step has removed the triangle strip.
			if (vertexCount <= 6 && prim == GE_PRIM_TRIANGLES) {
				// It's enough to check UV deltas vs pos deltas between vertex pairs:
				// 0-1 1-3 3-2 2-0. Maybe can even skip the last one. Probably some simple math can get us that sequence.
				// Unfortunately we need to reverse the previous UV scaling operation. Fortunately these are powers of two
				// so the operations are exact.
				bool pixelMapped = true;
				const u16 *indsIn = (const u16 *)inds;
				const float uscale = gstate_c.curTextureWidth;
				const float vscale = gstate_c.curTextureHeight;
				for (int t = 0; t < vertexCount; t += 3) {
					struct { int a; int b; } pairs[] = { {0, 1}, {1, 2}, {2, 0} };
					for (int i = 0; i < ARRAY_SIZE(pairs); i++) {
						int a = indsIn[t + pairs[i].a];
						int b = indsIn[t + pairs[i].b];
						float du = fabsf((transformed[a].u - transformed[b].u) * uscale);
						float dv = fabsf((transformed[a].v - transformed[b].v) * vscale);
						float dx = fabsf(transformed[a].x - transformed[b].x);
						float dy = fabsf(transformed[a].y - transformed[b].y);
						if (du != dx || dv != dy) {
							pixelMapped = false;
						}
					}
					if (!pixelMapped) {
						break;
					}
				}
				result->pixelMapped = pixelMapped;
			}
		}
	}

	if (gstate.isModeClear()) {
		gpuStats.numClears++;
	}

	result->action = SW_DRAW_INDEXED;
	result->drawNumTrans = numTrans;
}

void SoftwareTransform::CalcCullParams(float &minZValue, float &maxZValue) const {
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

bool SoftwareTransform::ExpandRectangles(int vertexCount, int &numDecodedVerts, int vertsSize, u16 *&inds, int indsSize, const TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode, bool *pixelMappedExactly) const {
	// Before we start, do a sanity check - does the output fit?
	if ((vertexCount / 2) * 6 > indsSize) {
		// Won't fit, kill the draw.
		return false;
	}
	if ((vertexCount / 2) * 4 > vertsSize) {
		// Won't fit, kill the draw.
		return false;
	}

	// Rectangles always need 2 vertices, disregard the last one if there's an odd number.
	vertexCount = vertexCount & ~1;
	numTrans = 0;
	TransformedVertex *trans = &transformedExpanded[0];

	const u16 *indsIn = (const u16 *)inds;
	u16 *newInds = inds + vertexCount;
	u16 *indsOut = newInds;

	numDecodedVerts = 4 * (vertexCount / 2);

	float uscale = 1.0f;
	float vscale = 1.0f;
	if (throughmode) {
		uscale /= gstate_c.curTextureWidth;
		vscale /= gstate_c.curTextureHeight;
	}

	bool pixelMapped = g_Config.bSmart2DTexFiltering && !gstate_c.textureIsVideo;

	for (int i = 0; i < vertexCount; i += 2) {
		const TransformedVertex &transVtxTL = transformed[indsIn[i + 0]];
		const TransformedVertex &transVtxBR = transformed[indsIn[i + 1]];

		if (pixelMapped) {
			float dx = transVtxBR.x - transVtxTL.x;
			float dy = transVtxBR.y - transVtxTL.y;
			float du = transVtxBR.u - transVtxTL.u;
			float dv = transVtxBR.v - transVtxTL.v;

			// NOTE: We will accept it as pixel mapped if only one dimension is stretched. This fixes dialog frames in FFI.
			// Though, there could be false positives in other games due to this. Let's see if it is a problem...
			if (dx <= 0 || dy <= 0 || (dx != du && dy != dv)) {
				pixelMapped = false;
			}
		}

		// We have to turn the rectangle into two triangles, so 6 points.
		// This is 4 verts + 6 indices.

		// bottom right
		trans[0] = transVtxBR;
		trans[0].u = transVtxBR.u * uscale;
		trans[0].v = transVtxBR.v * vscale;

		// top right
		trans[1] = transVtxBR;
		trans[1].y = transVtxTL.y;
		trans[1].u = transVtxBR.u * uscale;
		trans[1].v = transVtxTL.v * vscale;

		// top left
		trans[2] = transVtxBR;
		trans[2].x = transVtxTL.x;
		trans[2].y = transVtxTL.y;
		trans[2].u = transVtxTL.u * uscale;
		trans[2].v = transVtxTL.v * vscale;

		// bottom left
		trans[3] = transVtxBR;
		trans[3].x = transVtxTL.x;
		trans[3].u = transVtxTL.u * uscale;
		trans[3].v = transVtxBR.v * vscale;

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
	*pixelMappedExactly = pixelMapped;
	return true;
}

// In-place. So, better not be doing this on GPU memory!
void IndexBufferProvokingLastToFirst(int prim, u16 *inds, int indsSize) {
	switch (prim) {
	case GE_PRIM_LINES:
		// Swap every two indices.
		for (int i = 0; i < indsSize - 1; i += 2) {
			u16 temp = inds[i];
			inds[i] = inds[i + 1];
			inds[i + 1] = temp;
		}
		break;
	case GE_PRIM_TRIANGLES:
		// Rotate the triangle so the last becomes the first, without changing the winding order.
		// This could be done with a series of pshufb.
		for (int i = 0; i < indsSize - 2; i += 3) {
			u16 temp = inds[i + 2];
			inds[i + 2] = inds[i + 1];
			inds[i + 1] = inds[i];
			inds[i] = temp;
		}
		break;
	case GE_PRIM_POINTS:
		// Nothing to do,
		break;
	case GE_PRIM_RECTANGLES:
		// Nothing to do, already using the 2nd vertex.
		break;
	default:
		_dbg_assert_msg_(false, "IndexBufferProvokingFirstToLast: Only works with plain indexed primitives, no strips or fans")
	}
}

bool SoftwareTransform::ExpandLines(int vertexCount, int &numDecodedVerts, int vertsSize, u16 *&inds, int indsSize, const TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode) {
	// Before we start, do a sanity check - does the output fit?
	if ((vertexCount / 2) * 6 > indsSize) {
		// Won't fit, kill the draw.
		return false;
	}
	if ((vertexCount / 2) * 4 > vertsSize) {
		return false;
	}

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

	numDecodedVerts = 4 * (vertexCount / 2);

	if (PSP_CoreParameter().compat.flags().CenteredLines) {
		// Lines meant to be pretty in 3D like in Echochrome.

		// We expand them in both directions for symmetry, so we need to halve the expansion.
		dx *= 0.5f;
		dy *= 0.5f;

		for (int i = 0; i < vertexCount; i += 2) {
			const TransformedVertex &transVtx1 = transformed[indsIn[i + 0]];
			const TransformedVertex &transVtx2 = transformed[indsIn[i + 1]];

			// Okay, let's calculate the perpendicular.
			float horizontal = transVtx2.x * transVtx2.pos_w - transVtx1.x * transVtx1.pos_w;
			float vertical = transVtx2.y * transVtx2.pos_w - transVtx1.y * transVtx1.pos_w;

			Vec2f addWidth = Vec2f(-vertical, horizontal).Normalized();

			float xoff = addWidth.x * dx;
			float yoff = addWidth.y * dy;

			// bottom right
			trans[0].CopyFromWithOffset(transVtx2, xoff * transVtx2.pos_w, yoff * transVtx2.pos_w);
			// top right
			trans[1].CopyFromWithOffset(transVtx1, xoff * transVtx1.pos_w, yoff * transVtx1.pos_w);
			// top left
			trans[2].CopyFromWithOffset(transVtx1, -xoff * transVtx1.pos_w, -yoff * transVtx1.pos_w);
			// bottom left
			trans[3].CopyFromWithOffset(transVtx2, -xoff * transVtx2.pos_w, -yoff * transVtx2.pos_w);

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
			float horizontal = transVtxTL.x * transVtxTL.pos_w - transVtxBL.x * transVtxBL.pos_w;
			float vertical = transVtxTL.y * transVtxTL.pos_w - transVtxBL.y * transVtxBL.pos_w;
			Vec2f addWidth = Vec2f(-vertical, horizontal).Normalized();

			// bottom right
			trans[0] = transVtxBL;
			trans[0].x += addWidth.x * dx * trans[0].pos_w;
			trans[0].y += addWidth.y * dy * trans[0].pos_w;
			trans[0].u += addWidth.x * du * trans[0].uv_w;
			trans[0].v += addWidth.y * dv * trans[0].uv_w;

			// top right
			trans[1] = transVtxTL;
			trans[1].x += addWidth.x * dx * trans[1].pos_w;
			trans[1].y += addWidth.y * dy * trans[1].pos_w;
			trans[1].u += addWidth.x * du * trans[1].uv_w;
			trans[1].v += addWidth.y * dv * trans[1].uv_w;

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
	return true;
}

bool SoftwareTransform::ExpandPoints(int vertexCount, int &maxIndex, int vertsSize, u16 *&inds, int indsSize, const TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode) {
	// Before we start, do a sanity check - does the output fit?
	if (vertexCount * 6 > indsSize) {
		// Won't fit, kill the draw.
		return false;
	}
	if (vertexCount * 4 > vertsSize) {
		// Won't fit, kill the draw.
		return false;
	}

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
	return true;
}

// This normalizes a set of vertices in any format to SimpleVertex format, by processing away morphing AND skinning.
// The rest of the transform pipeline like lighting will go as normal, either hardware or software.
// The implementation is initially a bit inefficient but shouldn't be a big deal.
// An intermediate buffer of not-easy-to-predict size is stored at bufPtr.
u32 NormalizeVertices(SimpleVertex *sverts, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, VertexDecoder *dec, u32 vertType) {
	// First, decode the vertices into a GPU compatible format. This step can be eliminated but will need a separate
	// implementation of the vertex decoder.
	dec->DecodeVerts(bufPtr, inPtr, &gstate_c.uv, lowerBound, upperBound);

	// OK, morphing eliminated but bones still remain to be taken care of.
	// Let's do a partial software transform where we only do skinning.

	VertexReader reader(bufPtr, dec->GetDecVtxFmt(), vertType);

	const u8 defaultColor[4] = {
		(u8)gstate.getMaterialAmbientR(),
		(u8)gstate.getMaterialAmbientG(),
		(u8)gstate.getMaterialAmbientB(),
		(u8)gstate.getMaterialAmbientA(),
	};

	// Let's have two separate loops, one for non skinning and one for skinning.
	if (!dec->skinInDecode && (vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
		int numBoneWeights = vertTypeGetNumBoneWeights(vertType);
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i - lowerBound);
			SimpleVertex &sv = sverts[i];
			if (vertType & GE_VTYPE_TC_MASK) {
				reader.ReadUV(sv.uv);
			}

			if (vertType & GE_VTYPE_COL_MASK) {
				sv.color_32 = reader.ReadColor0_8888();
			} else {
				memcpy(sv.color, defaultColor, 4);
			}

			float nrm[3], pos[3];
			float bnrm[3], bpos[3];

			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tessellation anyway, not sure if any need to supply
				reader.ReadNrm(nrm);
			} else {
				nrm[0] = 0;
				nrm[1] = 0;
				nrm[2] = 1.0f;
			}
			reader.ReadPosAuto(pos);

			// Apply skinning transform directly
			float weights[8];
			reader.ReadWeights(weights);
			// Skinning
			Vec3Packedf psum(0, 0, 0);
			Vec3Packedf nsum(0, 0, 0);
			for (int w = 0; w < numBoneWeights; w++) {
				if (weights[w] != 0.0f) {
					Vec3ByMatrix43(bpos, pos, gstate.boneMatrix + w * 12);
					Vec3Packedf tpos(bpos);
					psum += tpos * weights[w];

					Norm3ByMatrix43(bnrm, nrm, gstate.boneMatrix + w * 12);
					Vec3Packedf tnorm(bnrm);
					nsum += tnorm * weights[w];
				}
			}
			sv.pos = psum;
			sv.nrm = nsum;
		}
	} else {
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i - lowerBound);
			SimpleVertex &sv = sverts[i];
			if (vertType & GE_VTYPE_TC_MASK) {
				reader.ReadUV(sv.uv);
			} else {
				sv.uv[0] = 0.0f;  // This will get filled in during tessellation
				sv.uv[1] = 0.0f;
			}
			if (vertType & GE_VTYPE_COL_MASK) {
				sv.color_32 = reader.ReadColor0_8888();
			} else {
				memcpy(sv.color, defaultColor, 4);
			}
			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tessellation anyway, not sure if any need to supply
				reader.ReadNrm((float *)&sv.nrm);
			} else {
				sv.nrm.x = 0.0f;
				sv.nrm.y = 0.0f;
				sv.nrm.z = 1.0f;
			}
			reader.ReadPosAuto((float *)&sv.pos);
		}
	}

	// Okay, there we are! Return the new type (but keep the index bits)
	return GE_VTYPE_TC_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_NRM_FLOAT | GE_VTYPE_POS_FLOAT | (vertType & (GE_VTYPE_IDX_MASK | GE_VTYPE_THROUGH));
}

// clip space to screen space
Vec3f ClipToScreen(const Vec4f& coords) {
	float xScale = gstate.getViewportXScale();
	float xCenter = gstate.getViewportXCenter();
	float yScale = gstate.getViewportYScale();
	float yCenter = gstate.getViewportYCenter();
	float zScale = gstate.getViewportZScale();
	float zCenter = gstate.getViewportZCenter();

	float x = coords.x * xScale / coords.w + xCenter;
	float y = coords.y * yScale / coords.w + yCenter;
	float z = coords.z * zScale / coords.w + zCenter;

	// 16 = 0xFFFF / 4095.9375
	return Vec3f(x * 16 - gstate.getOffsetX16(), y * 16 - gstate.getOffsetY16(), z);
}

static Vec3f ScreenToDrawing(const Vec3f& coords) {
	Vec3f ret;
	ret.x = coords.x * (1.0f / 16.0f);
	ret.y = coords.y * (1.0f / 16.0f);
	ret.z = coords.z;
	return ret;
}

// TODO: This probably is not the best interface.
// drawEngine is just for the vertex decoder lookup.
// This is really just for vertex preview in the debugger, not for actual rendering!
bool GetCurrentDrawAsDebugVertices(DrawEngineCommon *drawEngine, int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	// This is always for the current vertices.
	u16 indexLowerBound = 0;
	u16 indexUpperBound = count - 1;

	if (!Memory::IsValidAddress(gstate_c.vertexAddr) || count == 0)
		return false;

	bool savedVertexFullAlpha = gstate_c.vertexFullAlpha;

	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		const u8 *inds = Memory::GetPointer(gstate_c.indexAddr);
		const u16_le *inds16 = (const u16_le *)inds;
		const u32_le *inds32 = (const u32_le *)inds;

		if (inds) {
			GetIndexBounds(inds, count, gstate.vertType, &indexLowerBound, &indexUpperBound);
			indices.resize(count);
			switch (gstate.vertType & GE_VTYPE_IDX_MASK) {
			case GE_VTYPE_IDX_8BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds[i];
				}
				break;
			case GE_VTYPE_IDX_16BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds16[i];
				}
				break;
			case GE_VTYPE_IDX_32BIT:
				WARN_LOG_REPORT_ONCE(simpleIndexes32, Log::G3D, "SimpleVertices: Decoding 32-bit indexes");
				for (int i = 0; i < count; ++i) {
					// These aren't documented and should be rare.  Let's bounds check each one.
					if (inds32[i] != (u16)inds32[i]) {
						ERROR_LOG_REPORT_ONCE(simpleIndexes32Bounds, Log::G3D, "SimpleVertices: Index outside 16-bit range");
					}
					indices[i] = (u16)inds32[i];
				}
				break;
			}
		} else {
			indices.clear();
		}
	} else {
		indices.clear();
	}

	static std::vector<u32> temp_buffer;
	static std::vector<SimpleVertex> simpleVertices;
	temp_buffer.resize(std::max((int)indexUpperBound, 8192) * 128 / sizeof(u32));
	simpleVertices.resize(indexUpperBound + 1);

	// We always want "applyskinindecode" here, faster than letting NormalizeVertices handle it.
	const u32 vertTypeID = GetVertTypeID(gstate.vertType, gstate.getUVGenMode(), true);
	VertexDecoder *dec = drawEngine->GetVertexDecoder(vertTypeID);
	NormalizeVertices(&simpleVertices[0], (u8 *)(&temp_buffer[0]), Memory::GetPointerUnchecked(gstate_c.vertexAddr), indexLowerBound, indexUpperBound, dec, gstate.vertType);

	float world[16];
	float view[16];
	float worldview[16];
	float worldviewproj[16];
	ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
	Matrix4ByMatrix4(worldview, world, view);
	Matrix4ByMatrix4(worldviewproj, worldview, gstate.projMatrix);

	// This transforms the vertices.
	// NOTE: We really should just run the full software transform?

	vertices.resize(indexUpperBound + 1);
	uint32_t vertType = gstate.vertType;
	for (int i = indexLowerBound; i <= indexUpperBound; ++i) {
		const SimpleVertex &vert = simpleVertices[i];

		if ((vertType & GE_VTYPE_THROUGH) != 0) {
			if (vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0];
				vertices[i].v = vert.uv[1];
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = vert.pos.x;
			vertices[i].y = vert.pos.y;
			vertices[i].z = vert.pos.z;
			if (vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
			vertices[i].nx = 0.0f;  // No meaningful normals in through mode
			vertices[i].ny = 0.0f;
			vertices[i].nz = 1.0f;
		} else {
			float clipPos[4];
			Vec3ByMatrix44(clipPos, vert.pos.AsArray(), worldviewproj);
			Vec3f screenPos = ClipToScreen(clipPos);
			Vec3f drawPos = ScreenToDrawing(screenPos);

			if (vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0] * (float)gstate.getTextureWidth(0);
				vertices[i].v = vert.uv[1] * (float)gstate.getTextureHeight(0);
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			// Should really have separate coordinates for before and after transform.
			vertices[i].x = drawPos.x;
			vertices[i].y = drawPos.y;
			vertices[i].z = drawPos.z;
			if (vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
			vertices[i].nx = vert.nrm.x;
			vertices[i].ny = vert.nrm.y;
			vertices[i].nz = vert.nrm.z;
		}
	}

	gstate_c.vertexFullAlpha = savedVertexFullAlpha;

	return true;
}
