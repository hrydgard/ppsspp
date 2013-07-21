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

#include "../GPUState.h"

#include "Clipper.h"
#include "Rasterizer.h"

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

void ProcessQuad(VertexData* data)
{
	if (!gstate.isModeThrough()) {
		// TODO: Not sure if the clipping code works...
/*		// TODO: Color of second vertex should be preserved
		int mask0 = CalcClipMask(data[0].clippos);
		int mask1 = CalcClipMask(data[1].clippos);
		int mask = mask0 | mask1;

		if ((mask0&mask1) & CLIP_NEG_X_BIT) return;
		if ((mask0&mask1) & CLIP_POS_X_BIT) return;
		if ((mask0&mask1) & CLIP_NEG_Y_BIT) return;
		if ((mask0&mask1) & CLIP_POS_Y_BIT) return;
		if ((mask0&mask1) & CLIP_NEG_Z_BIT) return;
		if ((mask0&mask1) & CLIP_POS_Z_BIT) return;

		VertexData* Vertices[2] = { &data[0], &data[1] };

		CLIP_LINE(CLIP_POS_X_BIT, -1,  0,  0, 1);
		CLIP_LINE(CLIP_NEG_X_BIT,  1,  0,  0, 1);
		CLIP_LINE(CLIP_POS_Y_BIT,  0, -1,  0, 1);
		CLIP_LINE(CLIP_NEG_Y_BIT,  0,  1,  0, 1);
		CLIP_LINE(CLIP_POS_Z_BIT,  0,  0,  0, 1);
		CLIP_LINE(CLIP_NEG_Z_BIT,  0,  0,  1, 1);

		data[0].drawpos = TransformUnit::ScreenToDrawing(TransformUnit::ClipToScreen(data[0].clippos));
		data[1].drawpos = TransformUnit::ScreenToDrawing(TransformUnit::ClipToScreen(data[1].clippos));*/


		VertexData buf[4];
		buf[0].clippos = ClipCoords(data[0].clippos.x, data[0].clippos.y, data[1].clippos.z, data[1].clippos.w);
		buf[0].texturecoords = data[0].texturecoords;

		buf[1].clippos = ClipCoords(data[0].clippos.x, data[1].clippos.y, data[1].clippos.z, data[1].clippos.w);
		buf[1].texturecoords = Vec2<float>(data[0].texturecoords.x, data[1].texturecoords.y);

		buf[2].clippos = ClipCoords(data[1].clippos.x, data[0].clippos.y, data[1].clippos.z, data[1].clippos.w);
		buf[2].texturecoords = Vec2<float>(data[1].texturecoords.x, data[0].texturecoords.y);

		buf[3] = data[1];

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

		Rasterizer::DrawTriangle(*topleft, *topright, *bottomright);
		Rasterizer::DrawTriangle(*bottomright, *bottomleft, *topleft);
	}

	// through mode handling
	VertexData buf[4];
	buf[0].drawpos = DrawingCoords(data[0].drawpos.x, data[0].drawpos.y, data[1].drawpos.z);
	buf[0].texturecoords = data[0].texturecoords;

	buf[1].drawpos = DrawingCoords(data[0].drawpos.x, data[1].drawpos.y, data[1].drawpos.z);
	buf[1].texturecoords = Vec2<float>(data[0].texturecoords.x, data[1].texturecoords.y);

	buf[2].drawpos = DrawingCoords(data[1].drawpos.x, data[0].drawpos.y, data[1].drawpos.z);
	buf[2].texturecoords = Vec2<float>(data[1].texturecoords.x, data[0].texturecoords.y);

	buf[3] = data[1];

	// Color and depth values of second vertex are used for the whole rectangle
	buf[0].color0 = buf[1].color0 = buf[2].color0 = buf[3].color0;
	buf[0].color1 = buf[1].color1 = buf[2].color1 = buf[3].color1;
	buf[0].clippos.w = buf[1].clippos.w = buf[2].clippos.w = buf[3].clippos.w = 1.0f;

	VertexData* topleft = &buf[0];
	VertexData* topright = &buf[1];
	VertexData* bottomleft = &buf[2];
	VertexData* bottomright = &buf[3];

	for (int i = 0; i < 4; ++i) {
		if (buf[i].drawpos.x < topleft->drawpos.x && buf[i].drawpos.y < topleft->drawpos.y)
			topleft = &buf[i];
		if (buf[i].drawpos.x > topright->drawpos.x && buf[i].drawpos.y < topright->drawpos.y)
			topright = &buf[i];
		if (buf[i].drawpos.x < bottomleft->drawpos.x && buf[i].drawpos.y > bottomleft->drawpos.y)
			bottomleft = &buf[i];
		if (buf[i].drawpos.x > bottomright->drawpos.x && buf[i].drawpos.y > bottomright->drawpos.y)
			bottomright = &buf[i];
	}

	Rasterizer::DrawTriangle(*topleft, *topright, *bottomright);
	Rasterizer::DrawTriangle(*bottomright, *bottomleft, *topleft);
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

	// TODO: Change logic when it's a backface
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

	if (mask) {
		for(int i = 0; i < 3; i += 3) {
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
	}

	for(int i = 0; i+3 <= numIndices; i+=3)
	{
		if(indices[i] != SKIP_FLAG)
		{
			VertexData data[3] = { *Vertices[indices[i]], *Vertices[indices[i+1]], *Vertices[indices[i+2]] };
			data[0].drawpos = DrawingCoords(TransformUnit::ScreenToDrawing(TransformUnit::ClipToScreen(data[0].clippos)));
			data[1].drawpos = DrawingCoords(TransformUnit::ScreenToDrawing(TransformUnit::ClipToScreen(data[1].clippos)));
			data[2].drawpos = DrawingCoords(TransformUnit::ScreenToDrawing(TransformUnit::ClipToScreen(data[2].clippos)));
			Rasterizer::DrawTriangle(data[0], data[1], data[2]);
		}
	}
}

} // namespace
