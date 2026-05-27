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
#include "Common/Math/CrossSIMD.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUState.h"
#include "GPU/Math3D.h"
#include "GPU/GPUStateSIMDUtil.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Software/Clipper.h"

// This is the software transform pipeline, which is necessary for supporting RECT
// primitives correctly without geometry shaders, and may be easier to use for
// debugging than the hardware transform pipeline.

// Additionally, it performs some culling, clipping and clamping which it can do more accurately than the normal
// hardware pipeline can.

// There's code here that simply expands transformed RECTANGLES into plain triangles, additionally LINEs and POINTs get expanded
// to produce more PSP-like behavior.

// We're gonna have to keep software transforming RECTANGLES, unless we use a geom shader which we can't on OpenGL ES 2.0.

// The verts are in the order:  BR BL TL TR
// 2   3       3   2        0   3          2   1
//        to           to            or
// 1   0       0   1        1   2          3   0

// Note: 0 is BR and 2 is TL.

// The PSP has a funky mechanism where the UV direction of screen-space rectangles is decided by the relative positioning
// of the two corners defining the rectangle.
static void RotateUV(TransformedVertex v[4]) {
	const float x1 = v[2].x;
	const float x2 = v[0].x;
	const float y1 = v[2].y;
	const float y2 = v[0].y;
	if ((x1 < x2 && y1 > y2) || (x1 > x2 && y1 < y2)) {
		float tempu = v[1].u;
		float tempv = v[1].v;
		v[1].u = v[3].u;
		v[1].v = v[3].v;
		v[3].u = tempu;
		v[3].v = tempv;
	}
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

	// The last vertical strip often extends outside the drawing area so we don't want an equality check.
	// But make sure it at least fully covers it.
	if (transformed[numVerts - 1].x < x2) {
		return false;
	}

	return true;
}

// At the end, this calls ProjectClipAndExpand which will expand rectangles as necessary, or apply culling.
SoftwareTransformAction SoftwareTransform::Transform(SoftwareTransformParams &params, int prim, u32 vertType, const DecVtxFormat &decVtxFormat, int &numDecodedVerts, int vertsSize, int vertexCount, u16 *&inds, int indsSize, SoftwareTransformResult *result) {
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
	// Not really sure what a sensible value might be, but let's try 64k.
	constexpr float largeFogValue = 65535.0f;
	if (my_isnanorinf(fog_end)) {
		fog_end = std::signbit(fog_end) ? -largeFogValue : largeFogValue;
	}
	if (my_isnanorinf(fog_slope)) {
		fog_slope = std::signbit(fog_slope) ? -largeFogValue : largeFogValue;
	}

	TransformedVertex *transformed = params.transformed;
	VertexReader reader(params.decoded, decVtxFormat, vertType);
	if (throughmode) {
		const u32 materialAmbientRGBA = gstate.getMaterialAmbientRGBA();
		const bool hasColor = reader.hasColor0();
		const bool hasUV = reader.hasUV();
		for (int index = 0; index < numDecodedVerts; index++) {
			// Do not touch the coordinates or the colors. No lighting.
			reader.Goto(index);
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
		if (params.allowClear && reallyAClear && gl_extensions.gpuVendor != GPU_VENDOR_IMGTEC) {
			// If alpha is not allowed to be separate, it must match for both depth/stencil and color.  Vulkan requires this.
			bool alphaMatchesColor = gstate.isClearModeColorMask() == gstate.isClearModeAlphaMask();
			bool depthMatchesStencil = gstate.isClearModeAlphaMask() == gstate.isClearModeDepthMask();
			bool matchingComponents = params.allowSeparateAlphaClear || (alphaMatchesColor && depthMatchesStencil);
			bool stencilNotMasked = !gstate.isClearModeAlphaMask() || gstate.getStencilWriteMask() == 0x00;
			if (matchingComponents && stencilNotMasked) {
				DepthScaleFactors depthScale = GetDepthScaleFactors(gstate_c.UseFlags());
				// Need to rescale from a [0, 1] float.  This is the final transformed value.
				float depth = depthScale.EncodeFromU16((float)(int)(transformed[1].z * 65535.0f));
				// Non-zero depth clears are unusual, but some drivers don't match drawn depth values to cleared values.
				// Games sometimes expect exact matches (see #12626, for example) for equal comparisons.
				if (!(params.everUsedEqualDepth && gstate.isClearModeDepthMask() && result->depth > 0.0f && result->depth < 1.0f)) {
					result->color = transformed[1].color0_32;
					result->depth = depth;
					gpuStats.perFrame.numClears++;
					return SW_CLEAR;
				}
			}
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

			float ruv[2];
			if (reader.hasUV())
				reader.ReadUV(ruv);
			else {
				ruv[0] = 0.0f;
				ruv[1] = 0.0f;
			}

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
						break;

					case GE_PROJMAP_NORMAL: // Use non-normalized normal as source!
						source = normal;
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
				break;
			}

			uv[0] = uv[0] * widthFactor;
			uv[1] = uv[1] * heightFactor;

			// Transform the coord by the view matrix.
			Vec3ByMatrix43(v, out, gstate.viewMatrix);
			fogCoef = (v[2] + fog_end) * fog_slope;

			// Then transform by the projection.
			Vec3ByMatrix44(transformed[index].pos, v, gstate.projMatrix);

			transformed[index].fog = fogCoef;
			memcpy(&transformed[index].uv, uv, 3 * sizeof(float));
			transformed[index].color0_32 = c0.ToRGBA();
			transformed[index].color1_32 = c1.ToRGBA();

			// Projection happens later in ProjectClipAndExpand.

			// Vertex depth rounding is done in the shader, to simulate the 16-bit depth buffer.
		}
	}

	// TODO: This doesn't seem to be a very good check, but let's leave it for now.

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

	return ProjectClipAndExpand(params, prim, vertexCount, vertType, inds, indsSize, numDecodedVerts, vertsSize, result);
}

// Modifies the vertices in-place. Applies viewport and projection.
// TODO: SIMD.
void SoftwareTransform::ProjectVertices(TransformedVertex *transformed, int vertexCount) {
#if 0
	Lin::Vec3 vpOffset(gstate.getViewportXCenter(), gstate.getViewportYCenter(), gstate.getViewportZCenter());
	Lin::Vec3 vpScale(gstate.getViewportXScale(), gstate.getViewportYScale(), gstate.getViewportZScale());

	for (int i = 0; i < vertexCount; i++) {
		// Here we also need to do the perspective device and apply the viewport.
		// TODO: Move this to ProjectClipAndExpand.
		const float w = transformed[i].pos_w;
		const float recip = 1.0f / w;
		Lin::Vec3 xyz = vpOffset + vpScale.scaledBy(Lin::Vec3(transformed[i].x * recip, transformed[i].y * recip, transformed[i].z * recip));
		transformed[i].x = xyz.x;
		transformed[i].y = xyz.y;
		transformed[i].z = xyz.z;
	}
#else
	const Vec4F32 vpOffset = GetViewportOffsetVec(gstate);
	const Vec4F32 vpScale = GetViewportScaleVec(gstate);
	// CrossSIMD implementation.
	for (int i = 0; i < vertexCount; i++) {
		Vec4F32 xyzw = Vec4F32::Load(&transformed[i].x);
		Vec4F32 wRecip = Vec4F32::Splat(1.0f / transformed[i].pos_w);
		Vec4F32 projected = xyzw * wRecip * vpScale + vpOffset;
		// Now, we need to restore the W value as we'll still need it later.
		projected.WithLane3From(xyzw).Store(&transformed[i].x);
	}
#endif
}

// Helper to check if a vertex is inside the near plane (z >= -w)
// We add a tiny epsilon to prevent floating-point precision issues at the exact boundary
inline bool IsInsideNearPlane(const TransformedVertex& v) {
	return v.z >= -v.pos_w;
}

inline bool IsInsideFarPlane(const TransformedVertex& v) {
	return v.z <= v.pos_w;
}

// Generated by Gemini, and adapted to fit.
void ClipTrianglesAgainstNearPlane(
	TransformedVertex *transformed, int &transformedCount, int maxTransformed,
	u16 *indicesIn, int numIndicesIn,
	u16 *indicesOut, int &numIndicesOut, int maxIndicesOut
) {
	int clipCount = 0;
	// Process one triangle (3 indices) at a time
	for (size_t i = 0; i < numIndicesIn; i += 3) {
		u16 idx0 = indicesIn[i];
		u16 idx1 = indicesIn[i + 1];
		u16 idx2 = indicesIn[i + 2];

		TransformedVertex& v0 = transformed[idx0];
		TransformedVertex& v1 = transformed[idx1];
		TransformedVertex& v2 = transformed[idx2];

		bool in0 = IsInsideNearPlane(v0);
		bool in1 = IsInsideNearPlane(v1);
		bool in2 = IsInsideNearPlane(v2);

		bool inFar0 = IsInsideFarPlane(v0);
		bool inFar1 = IsInsideFarPlane(v1);
		bool inFar2 = IsInsideFarPlane(v2);

		int insideCount = (in0 ? 1 : 0) + (in1 ? 1 : 0) + (in2 ? 1 : 0);
		int insideFarCount = (inFar0 ? 1 : 0) + (inFar1 ? 1 : 0) + (inFar2 ? 1 : 0);

		// Case 1: Entirely visible
		if (insideCount == 3) {
			indicesOut[numIndicesOut++] = idx0;
			indicesOut[numIndicesOut++] = idx1;
			indicesOut[numIndicesOut++] = idx2;
		}
		// Case 2: Entirely clipped / behind near plane
		else if (insideCount == 0) {
			// Cull, no clipping needed.
			continue;
		}
		// Case 3: Entirely beyond far plane
		else if (insideFarCount == 0) {
			// All are beyond the far plane. Cull.
			continue;
		}
		// Case 3: Partially clipped
		else {
			clipCount++;

			// We need to organize vertices cleanly to calculate intersections.
			// We will create a local polygon array of the inside/outside states.
			u16 triIdx[3] = {idx0, idx1, idx2};
			bool triIn[3] = {in0, in1, in2};

			// Output generated vertex indices for this clipped polygon
			u16 polyIndices[4];
			int polyLength = 0;

			for (int j = 0; j < 3; ++j) {
				int next = (j + 1) % 3;

				u16 currIdx = triIdx[j];
				u16 nextIdx = triIdx[next];

				// If current vertex is inside, it stays a part of the output polygon
				if (triIn[j]) {
					polyIndices[polyLength++] = currIdx;
				}

				// If we cross the clipping plane line (inside->outside or outside->inside)
				if (triIn[j] != triIn[next]) {
					/* const */ TransformedVertex& a = transformed[currIdx];
					/* const */ TransformedVertex& b = transformed[nextIdx];

					// Find interpolation factor 't' where: z_interpolated = -w_interpolated
					// Lerp formulation:
					// z = a.z + t*(b.z - a.z)
					// w = a.w + t*(b.w - a.w)
					// Set z = -w => a.z + t*(b.z - a.z) = -(a.pos_w + t*(b.pos_w - a.pos_w))
					// Solve for t:
					float denominator = (b.z - a.z) + (b.pos_w - a.pos_w);
					float t = 0.0f;
					if (fabsf(denominator) > 0.000001f) {
						t = (-a.z - a.pos_w) / denominator;
					}
					// Clamp safely due to float precision
					if (t < 0.0f) t = 0.0f;
					if (t > 1.0f) t = 1.0f;

					// Generate new vertex at the intersection point
					TransformedVertex newVertex;
					TransformedVertex::Lerp(&newVertex, const_cast<TransformedVertex&>(a), const_cast<TransformedVertex&>(b), t);

					// Force exact intersection to eliminate precision creeping down the pipeline
					newVertex.z = -newVertex.pos_w;

					// These can be used for debugging.
					// newVertex.color0_32 = 0xFFFF00FF;
					// a.color0_32 = 0xFFFF00FF;
					// b.color0_32 = 0xFFFF00FF;

					// Append to global vertex buffer
					transformed[transformedCount++] = newVertex;
					u16 newIdx = static_cast<u16>(transformedCount - 1);
					polyIndices[polyLength++] = newIdx;
				}
			}

			// Triangulate the resulting polygon array (will be either 3 or 4 vertices)
			if (polyLength == 3) {
				indicesOut[numIndicesOut++] = polyIndices[0];
				indicesOut[numIndicesOut++] = polyIndices[1];
				indicesOut[numIndicesOut++] = polyIndices[2];
			} else if (polyLength == 4) {
				// Triangle 1
				indicesOut[numIndicesOut++] = polyIndices[0];
				indicesOut[numIndicesOut++] = polyIndices[1];
				indicesOut[numIndicesOut++] = polyIndices[2];
				// Triangle 2
				indicesOut[numIndicesOut++] = polyIndices[0];
				indicesOut[numIndicesOut++] = polyIndices[2];
				indicesOut[numIndicesOut++] = polyIndices[3];
			}
		}
	}

	gpuStats.perFrame.numSoftClippedTriangles++;
}

SoftwareTransformAction SoftwareTransform::ProjectClipAndExpand(SoftwareTransformParams &params, int prim, int vertexCount, u32 vertType, u16 *&inds, int indsSize, int &numDecodedVerts, int vertsSize, SoftwareTransformResult *result) {
	TransformedVertex *transformed = params.transformed;
	TransformedVertex *transformedExpanded = params.transformedExpanded;
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;

	// Step 2: expand and process primitives.
	result->drawBuffer = transformed;
	int numTrans = 0;

	FramebufferManagerCommon *fbman = params.fbman;
	bool useBufferedRendering = fbman->UseBufferedRendering();

	// NOTE: ExpandRectanges/lines/etc should do clipping while they're at it.
	if (prim == GE_PRIM_RECTANGLES) {
		// TODO: We should cull rectangles outzide -W<Z<W here if *both* points are outside in the same direction,
		// like we do with triangles.

		if (!throughmode) {
			ProjectVertices(transformed, numDecodedVerts);
		}

		if (!ExpandRectangles(vertexCount, numDecodedVerts, vertsSize, inds, indsSize, transformed, transformedExpanded, numTrans, throughmode, &result->pixelMapped)) {
			result->drawNumTrans = 0;
			result->pixelMapped = false;
			return SW_CULLED;
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
				result->stencilValue = (u8)(transformed[inds[1]].color0_32 >> 24);
			} else {
				result->stencilValue = 0;
			}
		}
	} else if (prim == GE_PRIM_POINTS) {
		// TODO: We should cull points here if they are outside -W<Z<W.

		if (!throughmode) {
			ProjectVertices(transformed, numDecodedVerts);
		}

		result->pixelMapped = false;
		if (!ExpandPoints(vertexCount, numDecodedVerts, vertsSize, inds, indsSize, transformed, transformedExpanded, numTrans, throughmode)) {
			result->drawNumTrans = 0;
			return SW_CULLED;
		}
		result->drawBuffer = transformedExpanded;
	} else if (prim == GE_PRIM_LINES) {
		// TODO: We should cull rectangles outzide -W<Z<W here if *both* points are outside in the same direction,
		// like we do with triangles.

		if (!throughmode) {
			ProjectVertices(transformed, numDecodedVerts);
		}

		result->pixelMapped = false;
		if (!ExpandLines(vertexCount, numDecodedVerts, vertsSize, inds, indsSize, transformed, transformedExpanded, numTrans, throughmode)) {
			result->drawNumTrans = 0;
			return SW_CULLED;
		}
		result->drawBuffer = transformedExpanded;
	} else if (prim == GE_PRIM_TRIANGLES) {
		// Triangles. We can simply draw the unexpanded buffer, although we do also take the opportunity to perform culling.
		numTrans = vertexCount;
		result->pixelMapped = false;

		if (throughmode) {
			// Nothing to do, pass the vertices right through as-is. Well, we can go look for pixel mapping, but we don't do any culling or clipping.
			if (g_Config.bSmart2DTexFiltering && !gstate_c.textureIsVideo) {
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
						struct { int a; int b; } pairs[] = {{0, 1}, {1, 2}, {2, 0}};
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
		} else {
			// Culling and clipping needs to be done here, it doesn't happen in the shader in the case of software transform.
			// However, fast culling should already have taken care of the Z<-W and Z>W culling, but we check for it on a per-triangle
			// basis here anyway.
			const u16 *indsIn = (const u16 *)inds;
			u16 *origInds = inds;

			// TODO: We should either merge the two loops, or avoid the second loop if no culling is needed.

			// Now, for each triangle, throw away the indices if:
			//  - Depth clip/clamp on, and ALL verts are outside *in the same direction*.
			//  - Depth clip/clamp off, and ANY vert is outside.

			u16 *newInds = inds + vertexCount;
			u16 *indsOut = newInds;

			if (gstate.isDepthClipEnabled()) {
				const u16 *indsIn = (const u16 *)inds;
				int newIndexCount = 0;
				ClipTrianglesAgainstNearPlane(transformed, numDecodedVerts, 65536, inds, vertexCount, indsOut, newIndexCount, 65336);
				numTrans = newIndexCount;
			} else {
				std::vector<int> outsideZ;
				outsideZ.resize(vertexCount);

				// First, check inside/outside directions for each index.
				// We are still in clip space here, so we can cull aggressively in Z.
				// TODO: This is so cheap now that we can probably avoid the buffer and just do the work below.
				for (int i = 0; i < vertexCount; ++i) {
					float z = transformed[indsIn[i]].z;
					float w = transformed[indsIn[i]].pos_w;
					if (z > w) {
						outsideZ[i] = 1;
					} else if (z < -w) {
						outsideZ[i] = -1;
					} else {
						outsideZ[i] = 0;
					}
				}

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
			}

			inds = newInds;

			// Now that we're done culling and generating clipped vertices if needed (not yet implemented), we go ahead and project.
			ProjectVertices(transformed, numDecodedVerts);

			// Alright! Now, we can approximate Z-clamping, if the hardware lacks support for doing it for us.
			// Now, this can only be done exactly if all vertices in a triangle are beyond the far plane.
			// If not we technically need to cut it in two parts to clamp accurately.
			// However, in most cases that matter (such as missing skies, etc), this is fine.
			const int maxZInt = gstate.getDepthRangeMax();
			// float maxZ = maxZInt / 65535.0f;
			const int minZInt = gstate.getDepthRangeMin();
			// float minZ = minZInt / 65535.0f;
			// We only need to clamp if minZ and maxZ aren't at the extreme in each direction, as otherwise
			// minZ and maxZ will cut things off.

			// If gstate_c.Use(GPU_USE_DEPTH_CLAMP), we can theoretically skip this. However, to keep behavior the same regardless of the flag, we won't.
			if (gstate.isDepthClipEnabled() && (minZInt == 0 || maxZInt == 65535)) {
				for (int i = 0; i < numTrans - 2; i += 3) {
					TransformedVertex &v0 = transformed[newInds[i]];
					TransformedVertex &v1 = transformed[newInds[i + 1]];
					TransformedVertex &v2 = transformed[newInds[i + 2]];
					if (v0.x < 0.0f || v0.x > 4096.0f ||
						v1.x < 0.0f || v1.y > 4096.0f ||
						v2.x < 0.0f || v2.y > 4096.0f) {
						// If it's outside the viewport, we might as well skip the clamping, as it won't be visible anyway.
						// continue;
					}

					if (minZInt == 0) {
						bool v0InFront = v0.z < 0.0f;
						bool v1InFront = v1.z < 0.0f;
						bool v2InFront = v2.z < 0.0f;
						if (v0InFront && v1InFront && v2InFront) {
							v0.z = 0.0f;
							v1.z = 0.0f;
							v2.z = 0.0f;
						}
					}

					if (maxZInt == 65535) {
						bool v0Beyond = v0.z >= 65535.0f;
						bool v1Beyond = v1.z >= 65535.0f;
						bool v2Beyond = v2.z >= 65535.0f;

						if (v0Beyond && v1Beyond && v2Beyond) {
							v0.z = 65535.0f;
							v1.z = 65535.0f;
							v2.z = 65535.0f;
						}
					}
				}
			}
		}
	} else {
		_dbg_assert_(false);
	}

	if (gstate.isModeClear()) {
		gpuStats.perFrame.numClears++;
	}

	result->drawNumTrans = numTrans;
	return SW_DRAW_INDEXED;
}

bool SoftwareTransform::ExpandRectangles(int vertexCount, int &numDecodedVerts, int vertsSize, u16 *&inds, int indsSize, const TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode, bool *pixelMappedExactly) {
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

		float z = transVtxBR.z;
		// Apply Z clamping. It appears clipping does not affect rectangles, see #12058.
		if (z > 65535.0f) {
			z = 65535.0f;
		} else if (z < 0.0f) {
			z = 0.0f;
		}

		// We have to turn the rectangle into two triangles, so 6 points.
		// This is 4 verts + 6 indices.

		// bottom right
		trans[0] = transVtxBR;
		trans[0].u = transVtxBR.u * uscale;
		trans[0].v = transVtxBR.v * vscale;
		trans[0].z = z;

		// top right
		trans[1] = transVtxBR;
		trans[1].y = transVtxTL.y;
		trans[1].u = transVtxBR.u * uscale;
		trans[1].v = transVtxTL.v * vscale;
		trans[1].z = z;

		// top left
		trans[2] = transVtxBR;
		trans[2].x = transVtxTL.x;
		trans[2].y = transVtxTL.y;
		trans[2].u = transVtxTL.u * uscale;
		trans[2].v = transVtxTL.v * vscale;
		trans[2].z = z;

		// bottom left
		trans[3] = transVtxBR;
		trans[3].x = transVtxTL.x;
		trans[3].u = transVtxTL.u * uscale;
		trans[3].v = transVtxBR.v * vscale;
		trans[3].z = z;

		// That's the four corners. Now process UV rotation.
		RotateUV(trans);

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
		// This could be done with a series of pshufb, although with some "interesting"
		// boundary conditions since 16 is not divisible by 3.
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

	float dx = 1.0f;
	float dy = 1.0f;
	float du = 1.0f;
	float dv = 1.0f;

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
			trans[0].u += addWidth.x * du * trans[0].uv_w;
			trans[0].v += addWidth.y * dv * trans[0].uv_w;

			// top right
			trans[1] = transVtxTL;
			trans[1].x += addWidth.x * dx;
			trans[1].y += addWidth.y * dy;
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
		transVtxBR.x += dx;
		transVtxBR.y += dy;
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
u32 NormalizeVertices(SimpleVertex *sverts, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, const VertexDecoder *dec, u32 vertType) {
	// First, decode the vertices into a GPU compatible format. This step can be eliminated but will need a separate
	// implementation of the vertex decoder.
	// Actually if software transform is off, we could enforce it in the vertex decoder lookup before calling this,
	// avoiding having to implement it again below.
	const int count = upperBound + 1 - lowerBound;
	dec->DecodeVerts(bufPtr, inPtr + lowerBound * dec->VertexSize(), &gstate_c.uv, count);

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
static Vec3f ClipToScreen(const Vec4f& coords) {
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
				for (int i = 0; i < count; ++i) {
					// These are rare. Only the bottom 16 bits are used.
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
