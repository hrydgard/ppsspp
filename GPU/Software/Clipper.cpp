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

#include "GPU/GPUState.h"

#include "GPU/Software/BinManager.h"
#include "GPU/Software/Clipper.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRectangle.h"
#include "GPU/Software/TransformUnit.h"

#include "Common/Profiler/Profiler.h"

namespace Clipper {

enum {
	SKIP_FLAG = -1,
	CLIP_NEG_Z_BIT = 0x20,
};

static inline int CalcClipMask(const ClipCoords &v) {
	// This checks `x / w` compared to 1 or -1, skipping the division.
	if (v.z < -v.w)
		return -1;
	return 0;
}

inline bool different_signs(float x, float y) {
	return ((x <= 0 && y > 0) || (x > 0 && y <= 0));
}

inline float clip_dotprod(const VertexData &vert, float A, float B, float C, float D) {
	return (vert.clippos.x * A + vert.clippos.y * B + vert.clippos.z * C + vert.clippos.w * D);
}

#define POLY_CLIP( PLANE_BIT, A, B, C, D )							\
{																	\
	if (mask & PLANE_BIT) {											\
		int idxPrev = inlist[0];									\
		float dpPrev = clip_dotprod(*Vertices[idxPrev], A, B, C, D );\
		int outcount = 0;											\
																	\
		inlist[n] = inlist[0];										\
		for (int j = 1; j <= n; j++) { 								\
			int idx = inlist[j];									\
			float dp = clip_dotprod(*Vertices[idx], A, B, C, D );	\
			if (dpPrev >= 0) {										\
				outlist[outcount++] = idxPrev;						\
			}														\
																	\
			if (different_signs(dp, dpPrev)) {						\
				if (dp < 0) {										\
					float t = dp / (dp - dpPrev);					\
					Vertices[numVertices++]->Lerp(t, *Vertices[idx], *Vertices[idxPrev]);		\
				} else {											\
					float t = dpPrev / (dpPrev - dp);				\
					Vertices[numVertices++]->Lerp(t, *Vertices[idxPrev], *Vertices[idx]);		\
				}													\
				clipped = true;										\
				outlist[outcount++] = numVertices - 1;				\
			}														\
																	\
			idxPrev = idx;											\
			dpPrev = dp;											\
		}															\
																	\
		if (outcount < 3)											\
			continue;												\
																	\
		{															\
			int *tmp = inlist;										\
			inlist = outlist;										\
			outlist = tmp;											\
			n = outcount;											\
		}															\
	}																\
}

#define CLIP_LINE(PLANE_BIT, A, B, C, D)						\
{																\
	if (mask & PLANE_BIT) {										\
		float dp0 = clip_dotprod(*Vertices[0], A, B, C, D );	\
		float dp1 = clip_dotprod(*Vertices[1], A, B, C, D );	\
																\
		if (mask0 & PLANE_BIT) {								\
			if (dp0 < 0) {										\
				float t = dp1 / (dp1 - dp0);					\
				Vertices[0]->Lerp(t, *Vertices[1], *Vertices[0]); \
				clipped = true;									\
			}													\
		}														\
		dp0 = clip_dotprod(*Vertices[0], A, B, C, D );			\
																\
		if (mask1 & PLANE_BIT) {								\
			if (dp1 < 0) {										\
				float t = dp1 / (dp1- dp0);						\
				Vertices[1]->Lerp(t, *Vertices[1], *Vertices[0]);	\
				clipped = true;									\
			}													\
		}														\
	}															\
}

static inline bool CheckOutsideZ(ClipCoords p, int &pos, int &neg) {
	constexpr float outsideValue = 1.000030517578125f;
	float z = p.z / p.w;
	if (z >= outsideValue) {
		pos++;
		return true;
	}
	if (-z >= outsideValue) {
		neg++;
		return true;
	}
	return false;
}

void ProcessRect(const VertexData &v0, const VertexData &v1, BinManager &binner) {
	if (!gstate.isModeThrough()) {
		// We may discard the entire rect based on depth values.
		int outsidePos = 0, outsideNeg = 0;
		CheckOutsideZ(v0.clippos, outsidePos, outsideNeg);
		CheckOutsideZ(v1.clippos, outsidePos, outsideNeg);

		// With depth clamp off, we discard the rectangle if even one vert is outside.
		if (outsidePos + outsideNeg > 0 && !gstate.isDepthClampEnabled())
			return;
		// With it on, both must be outside in the same direction.
		else if (outsidePos >= 2 || outsideNeg >= 2)
			return;

		if (v0.fogdepth != v1.fogdepth) {
			// Rectangles seem to always use nearest along X for fog depth, but reversed.
			// TODO: Check exactness of middle.
			VertexData vhalf0 = v1;
			vhalf0.screenpos.x = v0.screenpos.x + (v1.screenpos.x - v0.screenpos.x) / 2;

			VertexData vhalf1 = v1;
			vhalf1.screenpos.x = v0.screenpos.x + (v1.screenpos.x - v0.screenpos.x) / 2;
			vhalf1.screenpos.y = v0.screenpos.y;

			VertexData vrev1 = v1;
			vrev1.fogdepth = v0.fogdepth;

			binner.AddRect(v0, vhalf0);
			binner.AddRect(vhalf1, vrev1);
		} else {
			binner.AddRect(v0, v1);
		}
	} else {
		// through mode handling
		if (Rasterizer::RectangleFastPath(v0, v1, binner)) {
			return;
		} else if (gstate.isModeClear() && !gstate.isDitherEnabled()) {
			binner.AddClearRect(v0, v1);
		} else {
			binner.AddRect(v0, v1);
		}
	}
}

void ProcessPoint(const VertexData &v0, BinManager &binner) {
	// Points need no clipping. Will be bounds checked in the rasterizer (which seems backwards?)
	binner.AddPoint(v0);
}

void ProcessLine(const VertexData &v0, const VertexData &v1, BinManager &binner) {
	if (gstate.isModeThrough()) {
		// Actually, should clip this one too so we don't need to do bounds checks in the rasterizer.
		binner.AddLine(v0, v1);
		return;
	}

	int outsidePos = 0, outsideNeg = 0;
	CheckOutsideZ(v0.clippos, outsidePos, outsideNeg);
	CheckOutsideZ(v1.clippos, outsidePos, outsideNeg);

	// With depth clamp off, we discard the line if even one vert is outside.
	if (outsidePos + outsideNeg > 0 && !gstate.isDepthClampEnabled())
		return;
	// With it on, both must be outside in the same direction.
	else if (outsidePos >= 2 || outsideNeg >= 2)
		return;

	int mask0 = CalcClipMask(v0.clippos);
	int mask1 = CalcClipMask(v1.clippos);
	int mask = mask0 | mask1;
	if ((mask & CLIP_NEG_Z_BIT) == 0) {
		binner.AddLine(v0, v1);
		return;
	}

	VertexData ClippedVertices[2] = { v0, v1 };
	VertexData *Vertices[2] = { &ClippedVertices[0], &ClippedVertices[1] };
	bool clipped = false;
	CLIP_LINE(CLIP_NEG_Z_BIT,  0,  0,  1, 1);

	VertexData data[2] = { *Vertices[0], *Vertices[1] };
	if (clipped) {
		data[0].screenpos = TransformUnit::ClipToScreen(data[0].clippos);
		data[1].screenpos = TransformUnit::ClipToScreen(data[1].clippos);
	}
	binner.AddLine(data[0], data[1]);
}

void ProcessTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2, const VertexData &provoking, BinManager &binner) {
	int mask = 0;
	if (!gstate.isModeThrough()) {
		mask |= CalcClipMask(v0.clippos);
		mask |= CalcClipMask(v1.clippos);
		mask |= CalcClipMask(v2.clippos);

		// We may discard the entire triangle based on depth values.  First check what's outside.
		int outsidePos = 0, outsideNeg = 0;
		CheckOutsideZ(v0.clippos, outsidePos, outsideNeg);
		CheckOutsideZ(v1.clippos, outsidePos, outsideNeg);
		CheckOutsideZ(v2.clippos, outsidePos, outsideNeg);

		// With depth clamp off, we discard the triangle if even one vert is outside.
		if (outsidePos + outsideNeg > 0 && !gstate.isDepthClampEnabled())
			return;
		// With it on, all three must be outside in the same direction.
		else if (outsidePos >= 3 || outsideNeg >= 3)
			return;
	}

	// No clipping is common, let's skip processing if we can.
	if ((mask & CLIP_NEG_Z_BIT) == 0) {
		if (gstate.getShadeMode() == GE_SHADE_FLAT) {
			// So that the order of clipping doesn't matter...
			VertexData corrected2 = v2;
			corrected2.color0 = provoking.color0;
			corrected2.color1 = provoking.color1;
			binner.AddTriangle(v0, v1, corrected2);
		} else {
			binner.AddTriangle(v0, v1, v2);
		}
		return;
	}

	enum { NUM_CLIPPED_VERTICES = 3, NUM_INDICES = NUM_CLIPPED_VERTICES + 3 };

	VertexData* Vertices[NUM_INDICES];
	VertexData ClippedVertices[NUM_INDICES];
	for (int i = 0; i < NUM_INDICES; ++i)
		Vertices[i] = &ClippedVertices[i];

	// TODO: Change logic when it's a backface (why? In what way?)
	ClippedVertices[0] = v0;
	ClippedVertices[1] = v1;
	ClippedVertices[2] = v2;

	int indices[NUM_INDICES] = { 0, 1, 2, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG };
	int numIndices = 3;
	bool clipped = false;

	for (int i = 0; i < 3; i += 3) {
		int vlist[2][2*6+1];
		int *inlist = vlist[0], *outlist = vlist[1];
		int n = 3;
		int numVertices = 3;

		inlist[0] = 0;
		inlist[1] = 1;
		inlist[2] = 2;

		// mark this triangle as unused in case it should be completely clipped
		indices[0] = SKIP_FLAG;
		indices[1] = SKIP_FLAG;
		indices[2] = SKIP_FLAG;

		// The PSP only clips on negative Z (importantly, regardless of viewport.)
		POLY_CLIP(CLIP_NEG_Z_BIT, 0, 0, 1, 1);

		// transform the poly in inlist into triangles
		indices[0] = inlist[0];
		indices[1] = inlist[1];
		indices[2] = inlist[2];
		for (int j = 3; j < n; ++j) {
			indices[numIndices++] = inlist[0];
			indices[numIndices++] = inlist[j - 1];
			indices[numIndices++] = inlist[j];
		}
	}

	for (int i = 0; i + 3 <= numIndices; i += 3) {
		if (indices[i] != SKIP_FLAG) {
			VertexData &subv0 = *Vertices[indices[i + 0]];
			VertexData &subv1 = *Vertices[indices[i + 1]];
			VertexData &subv2 = *Vertices[indices[i + 2]];
			if (clipped) {
				subv0.screenpos = TransformUnit::ClipToScreen(subv0.clippos);
				subv1.screenpos = TransformUnit::ClipToScreen(subv1.clippos);
				subv2.screenpos = TransformUnit::ClipToScreen(subv2.clippos);
			}

			if (gstate.getShadeMode() == GE_SHADE_FLAT) {
				// So that the order of clipping doesn't matter...
				subv2.color0 = provoking.color0;
				subv2.color1 = provoking.color1;
			}

			binner.AddTriangle(subv0, subv1, subv2);
		}
	}
}

} // namespace
