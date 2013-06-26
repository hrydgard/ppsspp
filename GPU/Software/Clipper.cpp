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

void ProcessQuad(VertexData* data)
{
	// TODO: Clipping

	VertexData verts[6] = { data[0], data[0], data[1], data[1], data[0], data[0] };
	verts[1].drawpos.x = data[1].drawpos.x;
	verts[4].drawpos.x = data[0].drawpos.x;
	Rasterizer::DrawTriangle(data);
	Rasterizer::DrawTriangle(data+3);
}

void ProcessTriangle(VertexData* data)
{
	enum { NUM_CLIPPED_VERTICES = 33, NUM_INDICES = NUM_CLIPPED_VERTICES + 3 };

	VertexData* Vertices[NUM_CLIPPED_VERTICES];
	VertexData ClippedVertices[NUM_CLIPPED_VERTICES];
	for (int i = 0; i < NUM_CLIPPED_VERTICES; ++i)
		Vertices[i+3] = &ClippedVertices[i];

	// TODO: Change logic when it's a backface
	Vertices[0] = &data[0];
	Vertices[1] = &data[1];
	Vertices[2] = &data[2];

	int indices[NUM_INDICES] = { 0, 1, 2, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG,
									SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG,
									SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG };
	int numIndices = 3;

	int mask = 0;
	mask |= CalcClipMask(data[0].clippos);
	mask |= CalcClipMask(data[1].clippos);
	mask |= CalcClipMask(data[2].clippos);

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
			Rasterizer::DrawTriangle(data);
		}
	}
}

} // namespace
