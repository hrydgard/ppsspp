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

#include "Common/MemoryUtil.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/VertexDecoder.h"
#include "GPU/GLES/TransformPipeline.h"
#include "GPU/Common/SplineCommon.h"

#include "GPU/Software/TransformUnit.h"
#include "GPU/Software/Clipper.h"
#include "GPU/Software/Lighting.h"

static u8 buf[65536 * 48];  // yolo
static bool outside_range_flag = false;

WorldCoords TransformUnit::ModelToWorld(const ModelCoords& coords)
{
	Mat3x3<float> world_matrix(gstate.worldMatrix);
	return WorldCoords(world_matrix * coords) + Vec3<float>(gstate.worldMatrix[9], gstate.worldMatrix[10], gstate.worldMatrix[11]);
}

WorldCoords TransformUnit::ModelToWorldNormal(const ModelCoords& coords)
{
	Mat3x3<float> world_matrix(gstate.worldMatrix);
	return WorldCoords(world_matrix * coords);
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

// TODO: This is ugly
static inline ScreenCoords ClipToScreenInternal(const ClipCoords& coords, bool set_flag = true)
{
	ScreenCoords ret;
	// TODO: Check for invalid parameters (x2 < x1, etc)
	float vpx1 = getFloat24(gstate.viewportx1);
	float vpx2 = getFloat24(gstate.viewportx2);
	float vpy1 = getFloat24(gstate.viewporty1);
	float vpy2 = getFloat24(gstate.viewporty2);
	float vpz1 = getFloat24(gstate.viewportz1);
	float vpz2 = getFloat24(gstate.viewportz2);

	float retx = coords.x * vpx1 / coords.w + vpx2;
	float rety = coords.y * vpy1 / coords.w + vpy2;
	float retz = coords.z * vpz1 / coords.w + vpz2;

	if (gstate.clipEnable & 0x1) {
		if (retz < 0.f)
			retz = 0.f;
		if (retz > 65535.f)
			retz = 65535.f;
	}

	if (set_flag && (retx > 4095.9375f || rety > 4095.9375f || retx < 0 || rety < 0 || retz < 0 || retz > 65535.f))
		outside_range_flag = true;

	// 16 = 0xFFFF / 4095.9375
	return ScreenCoords(retx * 16, rety * 16, retz);
}

ScreenCoords TransformUnit::ClipToScreen(const ClipCoords& coords)
{
	return ClipToScreenInternal(coords, false);
}

DrawingCoords TransformUnit::ScreenToDrawing(const ScreenCoords& coords)
{
	DrawingCoords ret;
	// TODO: What to do when offset > coord?
	ret.x = (((u32)coords.x - gstate.getOffsetX16()) / 16) & 0x3ff;
	ret.y = (((u32)coords.y - gstate.getOffsetY16()) / 16) & 0x3ff;
	ret.z = coords.z;
	return ret;
}

ScreenCoords TransformUnit::DrawingToScreen(const DrawingCoords& coords)
{
	ScreenCoords ret;
	ret.x = (u32)coords.x * 16 + gstate.getOffsetX16();
	ret.y = (u32)coords.y * 16 + gstate.getOffsetY16();
	ret.z = coords.z;
	return ret;
}

static VertexData ReadVertex(VertexReader& vreader)
{
	VertexData vertex;

	float pos[3];
	// VertexDecoder normally scales z, but we want it unscaled.
	vreader.ReadPosZ16(pos);

	if (!gstate.isModeClear() && gstate.isTextureMapEnabled() && vreader.hasUV()) {
		float uv[2];
		vreader.ReadUV(uv);
		vertex.texturecoords = Vec2<float>(uv[0], uv[1]);
	}

	if (vreader.hasNormal()) {
		float normal[3];
		vreader.ReadNrm(normal);
		vertex.normal = Vec3<float>(normal[0], normal[1], normal[2]);

		if (gstate.areNormalsReversed())
			vertex.normal = -vertex.normal;
	}

	if (vertTypeIsSkinningEnabled(gstate.vertType) && !gstate.isModeThrough()) {
		float W[8] = { 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
		vreader.ReadWeights(W);

		Vec3<float> tmppos(0.f, 0.f, 0.f);
		Vec3<float> tmpnrm(0.f, 0.f, 0.f);

		for (int i = 0; i < vertTypeGetNumBoneWeights(gstate.vertType); ++i) {
			Mat3x3<float> bone(&gstate.boneMatrix[12*i]);
			tmppos += (bone * ModelCoords(pos[0], pos[1], pos[2]) * W[i] + Vec3<float>(gstate.boneMatrix[12*i+9], gstate.boneMatrix[12*i+10], gstate.boneMatrix[12*i+11]));
			if (vreader.hasNormal())
				tmpnrm += (bone * vertex.normal) * W[i];
		}

		pos[0] = tmppos.x;
		pos[1] = tmppos.y;
		pos[2] = tmppos.z;
		if (vreader.hasNormal())
			vertex.normal = tmpnrm;
	}

	if (vreader.hasColor0()) {
		float col[4];
		vreader.ReadColor0(col);
		vertex.color0 = Vec4<int>(col[0]*255, col[1]*255, col[2]*255, col[3]*255);
	} else {
		vertex.color0 = Vec4<int>(gstate.getMaterialAmbientR(), gstate.getMaterialAmbientG(), gstate.getMaterialAmbientB(), gstate.getMaterialAmbientA());
	}

	if (vreader.hasColor1()) {
		float col[3];
		vreader.ReadColor1(col);
		vertex.color1 = Vec3<int>(col[0]*255, col[1]*255, col[2]*255);
	} else {
		vertex.color1 = Vec3<int>(0, 0, 0);
	}

	if (!gstate.isModeThrough()) {
		vertex.modelpos = ModelCoords(pos[0], pos[1], pos[2]);
		vertex.worldpos = WorldCoords(TransformUnit::ModelToWorld(vertex.modelpos));
		vertex.clippos = ClipCoords(TransformUnit::ViewToClip(TransformUnit::WorldToView(vertex.worldpos)));
		vertex.screenpos = ClipToScreenInternal(vertex.clippos);

		if (vreader.hasNormal()) {
			vertex.worldnormal = TransformUnit::ModelToWorldNormal(vertex.normal);
			// TODO: Isn't there a flag that controls whether to normalize the normal?
			vertex.worldnormal /= vertex.worldnormal.Length();
		}

		Lighting::Process(vertex, vreader.hasColor0());
	} else {
		vertex.screenpos.x = (u32)pos[0] * 16 + gstate.getOffsetX16();
		vertex.screenpos.y = (u32)pos[1] * 16 + gstate.getOffsetY16();
		vertex.screenpos.z = pos[2];
		vertex.clippos.w = 1.f;
	}

	return vertex;
}

#define START_OPEN_U 1
#define END_OPEN_U 2
#define START_OPEN_V 4
#define END_OPEN_V 8

struct SplinePatch {
	VertexData points[16];
	int type;
	int pad[3];
};

SplinePatch *TransformUnit::patchBuffer_ = 0;
int TransformUnit::patchBufferSize_ = 0;

void TransformUnit::SubmitSpline(void* control_points, void* indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertex_type)
{
	VertexDecoder vdecoder;
	vdecoder.SetVertexType(vertex_type);
	const DecVtxFormat& vtxfmt = vdecoder.GetDecVtxFmt();

	static u8 buf[65536 * 48]; // yolo
	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertex_type & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	u8* indices8 = (u8*)indices;
	u16* indices16 = (u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertex_type, &index_lower_bound, &index_upper_bound);
	vdecoder.DecodeVerts(buf, control_points, index_lower_bound, index_upper_bound);

	VertexReader vreader(buf, vtxfmt, vertex_type);

	int num_patches_u = count_u - 3;
	int num_patches_v = count_v - 3;

	if (patchBufferSize_ < num_patches_u * num_patches_v) {
		if (patchBuffer_) {
			FreeAlignedMemory(patchBuffer_);
		}
		patchBuffer_ = (SplinePatch *)AllocateAlignedMemory(num_patches_u * num_patches_v, 16);
		patchBufferSize_ = num_patches_u * num_patches_v;
	}
	SplinePatch *patches = patchBuffer_;

	for (int patch_u = 0; patch_u < num_patches_u; ++patch_u) {
		for (int patch_v = 0; patch_v < num_patches_v; ++patch_v) {
			SplinePatch& patch = patches[patch_u + patch_v * num_patches_u];

			for (int point = 0; point < 16; ++point) {
				int idx = (patch_u + point%4) + (patch_v + point/4) * count_u;
				if (indices)
					vreader.Goto(indices_16bit ? indices16[idx] : indices8[idx]);
				else
					vreader.Goto(idx);

				patch.points[point] = ReadVertex(vreader);
			}
			patch.type = (type_u | (type_v<<2));
			if (patch_u != 0) patch.type &= ~START_OPEN_U;
			if (patch_v != 0) patch.type &= ~START_OPEN_V;
			if (patch_u != num_patches_u-1) patch.type &= ~END_OPEN_U;
			if (patch_v != num_patches_v-1) patch.type &= ~END_OPEN_V;
		}
	}

	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		SplinePatch& patch = patches[patch_idx];

		// TODO: Should do actual patch subdivision instead of just drawing the control points!
		const int tile_min_u = (patch.type & START_OPEN_U) ? 0 : 1;
		const int tile_min_v = (patch.type & START_OPEN_V) ? 0 : 1;
		const int tile_max_u = (patch.type & END_OPEN_U) ? 3 : 2;
		const int tile_max_v = (patch.type & END_OPEN_V) ? 3 : 2;
		for (int tile_u = tile_min_u; tile_u < tile_max_u; ++tile_u) {
			for (int tile_v = tile_min_v; tile_v < tile_max_v; ++tile_v) {
				int point_index = tile_u + tile_v*4;

				VertexData v0 = patch.points[point_index];
				VertexData v1 = patch.points[point_index+1];
				VertexData v2 = patch.points[point_index+4];
				VertexData v3 = patch.points[point_index+5];

				// TODO: Backface culling etc
				Clipper::ProcessTriangle(v0, v1, v2);
				Clipper::ProcessTriangle(v2, v1, v0);
				Clipper::ProcessTriangle(v2, v1, v3);
				Clipper::ProcessTriangle(v3, v1, v2);
			}
		}
	}

	host->GPUNotifyDraw();
}

void TransformUnit::SubmitPrimitive(void* vertices, void* indices, u32 prim_type, int vertex_count, u32 vertex_type, int *bytesRead)
{
	// TODO: Cache VertexDecoder objects
	VertexDecoder vdecoder;
	vdecoder.SetVertexType(vertex_type);
	const DecVtxFormat& vtxfmt = vdecoder.GetDecVtxFmt();

	if (bytesRead)
		*bytesRead = vertex_count * vdecoder.VertexSize();

	// Frame skipping.
	if (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) {
		return;
	}

	u16 index_lower_bound = 0;
	u16 index_upper_bound = vertex_count - 1;
	bool indices_16bit = (vertex_type & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	u8* indices8 = (u8*)indices;
	u16* indices16 = (u16*)indices;
	if (indices)
		GetIndexBounds(indices, vertex_count, vertex_type, &index_lower_bound, &index_upper_bound);
	vdecoder.DecodeVerts(buf, vertices, index_lower_bound, index_upper_bound);

	VertexReader vreader(buf, vtxfmt, vertex_type);

	const int max_vtcs_per_prim = 3;
	int vtcs_per_prim = 0;

	switch (prim_type) {
	case GE_PRIM_POINTS: vtcs_per_prim = 1; break;
	case GE_PRIM_LINES: vtcs_per_prim = 2; break;
	case GE_PRIM_TRIANGLES: vtcs_per_prim = 3; break;
	case GE_PRIM_RECTANGLES: vtcs_per_prim = 2; break;
	}

	VertexData data[max_vtcs_per_prim];

	// TODO: Do this in two passes - first process the vertices (before indexing/stripping),
	// then resolve the indices. This lets us avoid transforming shared vertices twice.

	switch (prim_type) {
	case GE_PRIM_POINTS:
	case GE_PRIM_LINES:
	case GE_PRIM_TRIANGLES:
	case GE_PRIM_RECTANGLES:
		{
			for (int vtx = 0; vtx < vertex_count; vtx += vtcs_per_prim) {
				for (int i = 0; i < vtcs_per_prim; ++i) {
					if (indices)
						vreader.Goto(indices_16bit ? indices16[vtx+i] : indices8[vtx+i]);
					else
						vreader.Goto(vtx+i);

					data[i] = ReadVertex(vreader);
					if (outside_range_flag)
						break;
				}
				if (outside_range_flag) {
					outside_range_flag = false;
					continue;
				}

				switch (prim_type) {
				case GE_PRIM_TRIANGLES:
				{
					if (!gstate.isCullEnabled() || gstate.isModeClear()) {
						Clipper::ProcessTriangle(data[0], data[1], data[2]);
						Clipper::ProcessTriangle(data[2], data[1], data[0]);
					} else if (!gstate.getCullMode())
						Clipper::ProcessTriangle(data[2], data[1], data[0]);
					else
						Clipper::ProcessTriangle(data[0], data[1], data[2]);
					break;
				}

				case GE_PRIM_RECTANGLES:
					Clipper::ProcessRect(data[0], data[1]);
					break;

				case GE_PRIM_LINES:
					Clipper::ProcessLine(data[0], data[1]);
					break;

				case GE_PRIM_POINTS:
					Clipper::ProcessPoint(data[0]);
					break;
				}
			}
			break;
		}

	case GE_PRIM_LINE_STRIP:
		{
			int skip_count = 1; // Don't draw a line when loading the first vertex
			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				if (indices)
					vreader.Goto(indices_16bit ? indices16[vtx] : indices8[vtx]);
				else
					vreader.Goto(vtx);

				data[vtx & 1] = ReadVertex(vreader);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
				} else {
					Clipper::ProcessLine(data[(vtx & 1) ^ 1], data[vtx & 1]);
				}
			}
			break;
		}

	case GE_PRIM_TRIANGLE_STRIP:
		{
			int skip_count = 2; // Don't draw a triangle when loading the first two vertices

			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				if (indices)
					vreader.Goto(indices_16bit ? indices16[vtx] : indices8[vtx]);
				else
					vreader.Goto(vtx);

				data[vtx % 3] = ReadVertex(vreader);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
					continue;
				}

				if (!gstate.isCullEnabled() || gstate.isModeClear()) {
					Clipper::ProcessTriangle(data[0], data[1], data[2]);
					Clipper::ProcessTriangle(data[2], data[1], data[0]);
				} else if ((!gstate.getCullMode()) ^ (vtx % 2)) {
					// We need to reverse the vertex order for each second primitive,
					// but we additionally need to do that for every primitive if CCW cullmode is used.
					Clipper::ProcessTriangle(data[2], data[1], data[0]);
				} else {
					Clipper::ProcessTriangle(data[0], data[1], data[2]);
				}
			}
			break;
		}

	case GE_PRIM_TRIANGLE_FAN:
		{
			unsigned int skip_count = 1; // Don't draw a triangle when loading the first two vertices

			if (indices)
				vreader.Goto(indices_16bit ? indices16[0] : indices8[0]);
			else
				vreader.Goto(0);
			data[0] = ReadVertex(vreader);

			for (int vtx = 1; vtx < vertex_count; ++vtx) {
				if (indices)
					vreader.Goto(indices_16bit ? indices16[vtx] : indices8[vtx]);
				else
					vreader.Goto(vtx);

				data[2 - (vtx % 2)] = ReadVertex(vreader);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
					continue;
				}

				if (!gstate.isCullEnabled() || gstate.isModeClear()) {
					Clipper::ProcessTriangle(data[0], data[1], data[2]);
					Clipper::ProcessTriangle(data[2], data[1], data[0]);
				} else if ((!gstate.getCullMode()) ^ (vtx % 2)) {
					// We need to reverse the vertex order for each second primitive,
					// but we additionally need to do that for every primitive if CCW cullmode is used.
					Clipper::ProcessTriangle(data[2], data[1], data[0]);
				} else {
					Clipper::ProcessTriangle(data[0], data[1], data[2]);
				}
			}
			break;
		}
	}

	host->GPUNotifyDraw();
}

// TODO: This probably is not the best interface.
bool TransformUnit::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	// This is always for the current vertices.
	u16 indexLowerBound = 0;
	u16 indexUpperBound = count - 1;

	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		const u8 *inds = Memory::GetPointer(gstate_c.indexAddr);
		const u16 *inds16 = (const u16 *)inds;

		if (inds) {
			GetIndexBounds(inds, count, gstate.vertType, &indexLowerBound, &indexUpperBound);
			indices.resize(count);
			switch (gstate.vertType & GE_VTYPE_IDX_MASK) {
			case GE_VTYPE_IDX_16BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds16[i];
				}
				break;
			case GE_VTYPE_IDX_8BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds[i];
				}
				break;
			default:
				return false;
			}
		} else {
			indices.clear();
		}
	} else {
		indices.clear();
	}

	static std::vector<u32> temp_buffer;
	static std::vector<SimpleVertex> simpleVertices;
	temp_buffer.resize(65536 * 24 / sizeof(u32));
	simpleVertices.resize(indexUpperBound + 1);

	VertexDecoder vdecoder;
	vdecoder.SetVertexType(gstate.vertType);
	TransformDrawEngine::NormalizeVertices((u8 *)(&simpleVertices[0]), (u8 *)(&temp_buffer[0]), Memory::GetPointer(gstate_c.vertexAddr), &vdecoder, indexLowerBound, indexUpperBound, gstate.vertType);

	float world[16];
	float view[16];
	float worldview[16];
	float worldviewproj[16];
	ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
	Matrix4ByMatrix4(worldview, world, view);
	Matrix4ByMatrix4(worldviewproj, worldview, gstate.projMatrix);

	vertices.resize(indexUpperBound + 1);
	for (int i = indexLowerBound; i <= indexUpperBound; ++i) {
		const SimpleVertex &vert = simpleVertices[i];

		if (gstate.isModeThrough()) {
			if (gstate.vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0];
				vertices[i].v = vert.uv[1];
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = vert.pos.x;
			vertices[i].y = vert.pos.y;
			vertices[i].z = vert.pos.z;
			if (gstate.vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
		} else {
			float clipPos[4];
			Vec3ByMatrix44(clipPos, vert.pos.AsArray(), worldviewproj);
			ScreenCoords screenPos = ClipToScreen(clipPos);
			DrawingCoords drawPos = ScreenToDrawing(screenPos);

			if (gstate.vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0];
				vertices[i].v = vert.uv[1];
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = drawPos.x;
			vertices[i].y = drawPos.y;
			vertices[i].z = 1.0;
			if (gstate.vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
		}
	}

	return true;
}
