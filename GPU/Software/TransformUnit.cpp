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
#include "../GLES/VertexDecoder.h"

#include "TransformUnit.h"
#include "Rasterizer.h"

WorldCoords TransformUnit::ModelToWorld(const ModelCoords& coords)
{
	Mat3x3<float> world_matrix(gstate.worldMatrix);
	return WorldCoords(world_matrix * coords) + Vec3<float>(gstate.worldMatrix[9], gstate.worldMatrix[10], gstate.worldMatrix[11]);
}

ViewCoords TransformUnit::WorldToView(const WorldCoords& coords)
{
	Mat3x3<float> view_matrix(gstate.viewMatrix);
	return ViewCoords(view_matrix * coords) + Vec3<float>(gstate.viewMatrix[9], gstate.viewMatrix[10], gstate.viewMatrix[11]);
}

ClipCoords TransformUnit::ViewToClip(const ViewCoords& coords)
{
	Vec4<float> coords4(coords.x, coords.y, coords.z, 1.0f);
	Mat4x4<float> projection_matrix(gstate.projMatrix);
	return ClipCoords(projection_matrix * coords4);
}

ScreenCoords TransformUnit::ClipToScreen(const ClipCoords& coords)
{
	ScreenCoords ret;
	float vpx1 = getFloat24(gstate.viewportx1);
	float vpx2 = getFloat24(gstate.viewportx2);
	float vpy1 = getFloat24(gstate.viewporty1);
	float vpy2 = getFloat24(gstate.viewporty2);
	float vpz1 = getFloat24(gstate.viewportz1);
	float vpz2 = getFloat24(gstate.viewportz2);
	// TODO: Check for invalid parameters (x2 < x1, etc)
	ret.x = (coords.x * vpx1 / coords.w + vpx2) * 16; // 16 = 0xFFFF / 4095.9375;
	ret.y = (coords.y * vpy1 / coords.w + vpy2) * 16; // 16 = 0xFFFF / 4095.9375;
	ret.z = (coords.z * vpz1 / coords.w + vpz2) * 16; // 16 = 0xFFFF / 4095.9375;
	return ret;
}

DrawingCoords TransformUnit::ScreenToDrawing(const ScreenCoords& coords)
{
	DrawingCoords ret;
	// TODO: What to do when offset > coord?
	// TODO: Mask can be re-enabled now, I guess.
	ret.x = (((u32)coords.x - (gstate.offsetx&0xffff))/16) & 0x3ff;
	ret.y = (((u32)coords.y - (gstate.offsety&0xffff))/16) & 0x3ff;
	return ret;
}

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
	// TODO: Do we need to include the equal sign here, too?
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

void TransformUnit::SubmitPrimitive(void* vertices, u32 prim_type, int vertex_count, u32 vertex_type)
{
	// TODO: Cache VertexDecoder objects
	VertexDecoder vdecoder;
	vdecoder.SetVertexType(vertex_type);
	const DecVtxFormat& vtxfmt = vdecoder.GetDecVtxFmt();

	static u8 buf[102400]; // yolo
	vdecoder.DecodeVerts(buf, vertices, 0, vertex_count - 1);

	VertexReader vreader(buf, vtxfmt, vertex_type);

	// We only support triangle lists, for now.
	for (int vtx = 0; vtx < vertex_count; vtx+=3)
	{
		enum { NUM_CLIPPED_VERTICES = 33, NUM_INDICES = NUM_CLIPPED_VERTICES + 3 };
		VertexData* Vertices[NUM_CLIPPED_VERTICES];
		VertexData ClippedVertices[NUM_CLIPPED_VERTICES];
		VertexData data[3];

		for (int i = 0; i < NUM_CLIPPED_VERTICES; ++i)
			Vertices[i+3] = &ClippedVertices[i];

		// TODO: Change logic when it's a backface
		Vertices[0] = &data[0];
		Vertices[1] = &data[1];
		Vertices[2] = &data[2];

		for (unsigned int i = 0; i < 3; ++i)
		{
			float pos[3];
			vreader.Goto(vtx+i);
			vreader.ReadPos(pos);

			if (gstate.textureMapEnable && vreader.hasUV())
			{
				float uv[2];
				vreader.ReadUV(uv);
				data[i].texturecoords = Vec2<float>(uv[0], uv[1]);
			}

			ModelCoords mcoords(pos[0], pos[1], pos[2]);
			data[i].clippos = ClipCoords(ClipCoords(TransformUnit::ViewToClip(TransformUnit::WorldToView(TransformUnit::ModelToWorld(mcoords)))));
			data[i].drawpos = DrawingCoords(TransformUnit::ScreenToDrawing(TransformUnit::ClipToScreen(data[i].clippos)));
		}

		// TODO: Should do lighting here!

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
skip:;
	}
}
