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

#include "GPU/Software/Clipper.h"
#include "GPU/Software/Rasterizer.h"

#include "profiler/profiler.h"

namespace Clipper {

enum {
	SKIP_FLAG = -1,
	CLIP_POS_X_BIT = 0x01,
	CLIP_NEG_X_BIT = 0x02,
	CLIP_POS_Y_BIT = 0x04,
	CLIP_NEG_Y_BIT = 0x08,
	CLIP_POS_Z_BIT = 0x10,
	CLIP_NEG_Z_BIT = 0x20,
};

static inline int CalcClipMask(const ClipCoords& v)
{
	int mask = 0;
	if (v.x > v.w) mask |= CLIP_POS_X_BIT;
	if (v.x < -v.w) mask |= CLIP_NEG_X_BIT;
	if (v.y > v.w) mask |= CLIP_POS_Y_BIT;
	if (v.y < -v.w) mask |= CLIP_NEG_Y_BIT;
	if (v.z > v.w) mask |= CLIP_POS_Z_BIT;
	if (v.z < -v.w) mask |= CLIP_NEG_Z_BIT;
	return mask;
}

#define AddInterpolatedVertex(t, out, in, numVertices) \
{ \
	Vertices[numVertices]->Lerp(t, *Vertices[out], *Vertices[in]); \
	numVertices++; \
}

#define DIFFERENT_SIGNS(x,y) ((x <= 0 && y > 0) || (x > 0 && y <= 0))

#define CLIP_DOTPROD(I, A, B, C, D) \
	(Vertices[I]->clippos.x * A + Vertices[I]->clippos.y * B + Vertices[I]->clippos.z * C + Vertices[I]->clippos.w * D)

#define POLY_CLIP( PLANE_BIT, A, B, C, D )							\
{																	\
	if (mask & PLANE_BIT) {											\
		int idxPrev = inlist[0];									\
		float dpPrev = CLIP_DOTPROD(idxPrev, A, B, C, D );			\
		int outcount = 0;											\
																	\
		inlist[n] = inlist[0];										\
		for (int j = 1; j <= n; j++) { 								\
			int idx = inlist[j];									\
			float dp = CLIP_DOTPROD(idx, A, B, C, D );				\
			if (dpPrev >= 0) {										\
				outlist[outcount++] = idxPrev;						\
			}														\
																	\
			if (DIFFERENT_SIGNS(dp, dpPrev)) {						\
				if (dp < 0) {										\
					float t = dp / (dp - dpPrev);					\
					AddInterpolatedVertex(t, idx, idxPrev, numVertices);		\
				} else {											\
					float t = dpPrev / (dpPrev - dp);				\
					AddInterpolatedVertex(t, idxPrev, idx, numVertices);		\
				}													\
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
	if (mask & PLANE_BIT) {											\
		float dp0 = CLIP_DOTPROD(0, A, B, C, D );				\
		float dp1 = CLIP_DOTPROD(1, A, B, C, D );				\
		int i = 0;												\
																\
		if (mask0 & PLANE_BIT) {								\
			if (dp0 < 0) {										\
				float t = dp1 / (dp1 - dp0);					\
				i = 0;											\
				AddInterpolatedVertex(t, 1, 0, i);				\
			}													\
		}														\
		dp0 = CLIP_DOTPROD(0, A, B, C, D );						\
																\
		if (mask1 & PLANE_BIT) {								\
			if (dp1 < 0) {										\
				float t = dp1 / (dp1- dp0);						\
				i = 1;											\
				AddInterpolatedVertex(t, 1, 0, i);				\
			}													\
		}														\
	}															\
}

static void RotateUVThrough(const VertexData &tl, const VertexData &br, VertexData &tr, VertexData &bl) {
	const fixed16 x1 = tl.screenpos.x;
	const fixed16 x2 = br.screenpos.x;
	const fixed16 y1 = tl.screenpos.y;
	const fixed16 y2 = br.screenpos.y;

	if ((x1 < x2 && y1 > y2) || (x1 > x2 && y1 < y2)) {
		std::swap(bl.texturecoords, tr.texturecoords);
	}
}

void ProcessRect(const VertexData& v0, const VertexData& v1)
{
	if (!gstate.isModeThrough()) {
		VertexData buf[4];
		buf[0].clippos = ClipCoords(v0.clippos.x, v0.clippos.y, v1.clippos.z, v1.clippos.w);
		buf[0].texturecoords = v0.texturecoords;

		buf[1].clippos = ClipCoords(v0.clippos.x, v1.clippos.y, v1.clippos.z, v1.clippos.w);
		buf[1].texturecoords = Vec2<float>(v0.texturecoords.x, v1.texturecoords.y);

		buf[2].clippos = ClipCoords(v1.clippos.x, v0.clippos.y, v1.clippos.z, v1.clippos.w);
		buf[2].texturecoords = Vec2<float>(v1.texturecoords.x, v0.texturecoords.y);

		buf[3] = v1;

		// Color and depth values of second vertex are used for the whole rectangle
		buf[0].color0 = buf[1].color0 = buf[2].color0 = buf[3].color0;
		buf[0].color1 = buf[1].color1 = buf[2].color1 = buf[3].color1;

		VertexData* topleft = &buf[0];
		VertexData* topright = &buf[1];
		VertexData* bottomleft = &buf[2];
		VertexData* bottomright = &buf[3];

		for (int i = 0; i < 4; ++i) {
			if (buf[i].clippos.x < topleft->clippos.x && buf[i].clippos.y < topleft->clippos.y)
				topleft = &buf[i];
			if (buf[i].clippos.x > topright->clippos.x && buf[i].clippos.y < topright->clippos.y)
				topright = &buf[i];
			if (buf[i].clippos.x < bottomleft->clippos.x && buf[i].clippos.y > bottomleft->clippos.y)
				bottomleft = &buf[i];
			if (buf[i].clippos.x > bottomright->clippos.x && buf[i].clippos.y > bottomright->clippos.y)
				bottomright = &buf[i];
		}

		// Four triangles to do backfaces as well. Two of them will get backface culled.
		ProcessTriangle(*topleft, *topright, *bottomright);
		ProcessTriangle(*bottomright, *topright, *topleft);
		ProcessTriangle(*bottomright, *bottomleft, *topleft);
		ProcessTriangle(*topleft, *bottomleft, *bottomright);
	} else {
		// through mode handling
		VertexData buf[4];
		buf[0].screenpos = ScreenCoords(v0.screenpos.x, v0.screenpos.y, v1.screenpos.z);
		buf[0].texturecoords = v0.texturecoords;

		buf[1].screenpos = ScreenCoords(v0.screenpos.x, v1.screenpos.y, v1.screenpos.z);
		buf[1].texturecoords = Vec2<float>(v0.texturecoords.x, v1.texturecoords.y);

		buf[2].screenpos = ScreenCoords(v1.screenpos.x, v0.screenpos.y, v1.screenpos.z);
		buf[2].texturecoords = Vec2<float>(v1.texturecoords.x, v0.texturecoords.y);

		buf[3] = v1;

		// Color and depth values of second vertex are used for the whole rectangle
		buf[0].color0 = buf[1].color0 = buf[2].color0 = buf[3].color0;
		buf[0].color1 = buf[1].color1 = buf[2].color1 = buf[3].color1;
		buf[0].clippos.w = buf[1].clippos.w = buf[2].clippos.w = buf[3].clippos.w = 1.0f;
		buf[0].fogdepth = buf[1].fogdepth = buf[2].fogdepth = buf[3].fogdepth = 1.0f;

		VertexData* topleft = &buf[0];
		VertexData* topright = &buf[1];
		VertexData* bottomleft = &buf[2];
		VertexData* bottomright = &buf[3];

		// Um. Why is this stuff needed?
		for (int i = 0; i < 4; ++i) {
			if (buf[i].screenpos.x < topleft->screenpos.x && buf[i].screenpos.y < topleft->screenpos.y)
				topleft = &buf[i];
			if (buf[i].screenpos.x > topright->screenpos.x && buf[i].screenpos.y < topright->screenpos.y)
				topright = &buf[i];
			if (buf[i].screenpos.x < bottomleft->screenpos.x && buf[i].screenpos.y > bottomleft->screenpos.y)
				bottomleft = &buf[i];
			if (buf[i].screenpos.x > bottomright->screenpos.x && buf[i].screenpos.y > bottomright->screenpos.y)
				bottomright = &buf[i];
		}

		RotateUVThrough(v0, v1, *topright, *bottomleft);

		// Four triangles to do backfaces as well. Two of them will get backface culled.
		Rasterizer::DrawTriangle(*topleft, *topright, *bottomright);
		Rasterizer::DrawTriangle(*bottomright, *topright, *topleft);
		Rasterizer::DrawTriangle(*bottomright, *bottomleft, *topleft);
		Rasterizer::DrawTriangle(*topleft, *bottomleft, *bottomright);
	}
}

void ProcessPoint(VertexData& v0)
{
	// Points need no clipping. Will be bounds checked in the rasterizer (which seems backwards?)
	Rasterizer::DrawPoint(v0);
}

void ProcessLine(VertexData& v0, VertexData& v1)
{
	if (gstate.isModeThrough()) {
		// Actually, should clip this one too so we don't need to do bounds checks in the rasterizer.
		Rasterizer::DrawLine(v0, v1);
		return;
	}

	// TODO: 3D lines
}

void ProcessTriangle(VertexData& v0, VertexData& v1, VertexData& v2)
{
	if (gstate.isModeThrough()) {
		Rasterizer::DrawTriangle(v0, v1, v2);
		return;
	}

	enum { NUM_CLIPPED_VERTICES = 33, NUM_INDICES = NUM_CLIPPED_VERTICES + 3 };

	VertexData* Vertices[NUM_INDICES];
	VertexData ClippedVertices[NUM_CLIPPED_VERTICES];
	for (int i = 0; i < NUM_CLIPPED_VERTICES; ++i)
		Vertices[i+3] = &ClippedVertices[i];

	// TODO: Change logic when it's a backface (why? In what way?)
	Vertices[0] = &v0;
	Vertices[1] = &v1;
	Vertices[2] = &v2;

	int indices[NUM_INDICES] = { 0, 1, 2, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG,
									SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG,
									SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG };
	int numIndices = 3;

	int mask = 0;
	mask |= CalcClipMask(v0.clippos);
	mask |= CalcClipMask(v1.clippos);
	mask |= CalcClipMask(v2.clippos);

	if (mask && (gstate.clipEnable & 0x1)) {
		// discard if any vertex is outside the near clipping plane
		if (mask & CLIP_NEG_Z_BIT)
			return;

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

			POLY_CLIP(CLIP_POS_X_BIT, -1,  0,  0, 1);
			POLY_CLIP(CLIP_NEG_X_BIT,  1,  0,  0, 1);
			POLY_CLIP(CLIP_POS_Y_BIT,  0, -1,  0, 1);
			POLY_CLIP(CLIP_NEG_Y_BIT,  0,  1,  0, 1);
			POLY_CLIP(CLIP_POS_Z_BIT,  0,  0,  0, 1);
			POLY_CLIP(CLIP_NEG_Z_BIT,  0,  0,  1, 1);

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
	} else if (CalcClipMask(v0.clippos) & CalcClipMask(v1.clippos) & CalcClipMask(v2.clippos))  {
		// If clipping is disabled, only discard the current primitive
		// if all three vertices lie outside one of the clipping planes
		return;
	}

	for (int i = 0; i+3 <= numIndices; i+=3)
	{
		if(indices[i] != SKIP_FLAG)
		{
			VertexData data[3] = { *Vertices[indices[i]], *Vertices[indices[i+1]], *Vertices[indices[i+2]] };
			data[0].screenpos = TransformUnit::ClipToScreen(data[0].clippos);
			data[1].screenpos = TransformUnit::ClipToScreen(data[1].clippos);
			data[2].screenpos = TransformUnit::ClipToScreen(data[2].clippos);
			Rasterizer::DrawTriangle(data[0], data[1], data[2]);
		}
	}
}

} // namespace
