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

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "GPU/Directx9/TransformPipelineDX9.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"

namespace DX9 {

// This normalizes a set of vertices in any format to SimpleVertex format, by processing away morphing AND skinning.
// The rest of the transform pipeline like lighting will go as normal, either hardware or software.
// The implementation is initially a bit inefficient but shouldn't be a big deal.
// An intermediate buffer of not-easy-to-predict size is stored at bufPtr.
u32 TransformDrawEngineDX9::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, VertexDecoderDX9 *dec, int lowerBound, int upperBound, u32 vertType) {
	// First, decode the vertices into a GPU compatible format. This step can be eliminated but will need a separate
	// implementation of the vertex decoder.
	dec->DecodeVerts(bufPtr, inPtr, lowerBound, upperBound);

	// OK, morphing eliminated but bones still remain to be taken care of.
	// Let's do a partial software transform where we only do skinning.

	VertexReader reader(bufPtr, dec->GetDecVtxFmt(), vertType);

	SimpleVertex *sverts = (SimpleVertex *)outPtr;

	const u8 defaultColor[4] = {
		(u8)gstate.getMaterialAmbientR(),
		(u8)gstate.getMaterialAmbientG(),
		(u8)gstate.getMaterialAmbientB(),
		(u8)gstate.getMaterialAmbientA(),
	};

	// Let's have two separate loops, one for non skinning and one for skinning.
	if (!g_Config.bSoftwareSkinning && (vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
		int numBoneWeights = vertTypeGetNumBoneWeights(vertType);
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i);
			SimpleVertex &sv = sverts[i];
			if (vertType & GE_VTYPE_TC_MASK) {
				reader.ReadUV(sv.uv);
			}

			if (vertType & GE_VTYPE_COL_MASK) {
				reader.ReadColor0_8888(sv.color);
			} else {
				memcpy(sv.color, defaultColor, 4);
			}

			float nrm[3], pos[3];
			float bnrm[3], bpos[3];

			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tesselation anyway, not sure if any need to supply
				reader.ReadNrm(nrm);
			} else {
				nrm[0] = 0;
				nrm[1] = 0;
				nrm[2] = 1.0f;
			}
			reader.ReadPos(pos);

			// Apply skinning transform directly
			float weights[8];
			reader.ReadWeights(weights);
			// Skinning
			Vec3Packedf psum(0,0,0);
			Vec3Packedf nsum(0,0,0);
			for (int w = 0; w < numBoneWeights; w++) {
				if (weights[w] != 0.0f) {
					Vec3ByMatrix43(bpos, pos, gstate.boneMatrix+w*12);
					Vec3Packedf tpos(bpos);
					psum += tpos * weights[w];

					Norm3ByMatrix43(bnrm, nrm, gstate.boneMatrix+w*12);
					Vec3Packedf tnorm(bnrm);
					nsum += tnorm * weights[w];
				}
			}
			sv.pos = psum;
			sv.nrm = nsum;
		}
	} else {
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i);
			SimpleVertex &sv = sverts[i];
			if (vertType & GE_VTYPE_TC_MASK) {
				reader.ReadUV(sv.uv);
			} else {
				sv.uv[0] = 0;  // This will get filled in during tesselation
				sv.uv[1] = 0;
			}
			if (vertType & GE_VTYPE_COL_MASK) {
				reader.ReadColor0_8888(sv.color);
			} else {
				memcpy(sv.color, defaultColor, 4);
			}
			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tesselation anyway, not sure if any need to supply
				reader.ReadNrm((float *)&sv.nrm);
			} else {
				sv.nrm.x = 0;
				sv.nrm.y = 0;
				sv.nrm.z = 1.0f;
			}
			reader.ReadPos((float *)&sv.pos);
		}
	}

	// Okay, there we are! Return the new type (but keep the index bits)
	return GE_VTYPE_TC_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_NRM_FLOAT | GE_VTYPE_POS_FLOAT | (vertType & (GE_VTYPE_IDX_MASK | GE_VTYPE_THROUGH));
}

u32 TransformDrawEngineDX9::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType) {
	const u32 vertTypeID = (vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24);
	VertexDecoderDX9 *dec = GetVertexDecoder(vertTypeID);
	return NormalizeVertices(outPtr, bufPtr, inPtr, dec, lowerBound, upperBound, vertType);
}

// Spline implementation copied and modified from neobrain's softgpu (orphis code?)

#define START_OPEN_U 1
#define END_OPEN_U 2
#define START_OPEN_V 4
#define END_OPEN_V 8

// We decode all vertices into a common format for easy interpolation and stuff.
// Not fast but can be optimized later.
struct HWSplinePatch {
	u8 *points[16];
	int type;

	// We need to generate both UVs and normals later...
	// float u0, v0, u1, v1;
};

static void CopyTriangle(u8 *&dest, u8 *v1, u8 *v2, u8 * v3, int vertexSize) {
	memcpy(dest, v1, vertexSize);
	dest += vertexSize;
	memcpy(dest, v2, vertexSize);
	dest += vertexSize;
	memcpy(dest, v3, vertexSize);
	dest += vertexSize;
}

void TransformDrawEngineDX9::SubmitSpline(void* control_points, void* indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertex_type) {
	Flush();

	if (prim_type != GE_PATCHPRIM_TRIANGLES) {
		// Only triangles supported!
		return;
	}

	// We're not actually going to decode, only reshuffle.
	VertexDecoderDX9 *vdecoder = GetVertexDecoder(vertex_type);

	int undecodedVertexSize = vdecoder->VertexSize();

	const DecVtxFormat & vtxfmt = vdecoder->GetDecVtxFmt();

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertex_type & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	u8* indices8 = (u8*)indices;
	u16* indices16 = (u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertex_type, &index_lower_bound, &index_upper_bound);

	int num_patches_u = count_u - 3;
	int num_patches_v = count_v - 3;

	// TODO: Do something less idiotic to manage this buffer
	HWSplinePatch* patches = new HWSplinePatch[num_patches_u * num_patches_v];
	for (int patch_u = 0; patch_u < num_patches_u; ++patch_u) {
		for (int patch_v = 0; patch_v < num_patches_v; ++patch_v) {
			HWSplinePatch& patch = patches[patch_u + patch_v * num_patches_u];

			for (int point = 0; point < 16; ++point) {
				int idx = (patch_u + point%4) + (patch_v + point/4) * count_u;
				if (indices)
					patch.points[point] = (u8 *)control_points + undecodedVertexSize * (indices_16bit ? indices16[idx] : indices8[idx]);
				else
					patch.points[point] = (u8 *)control_points + undecodedVertexSize * idx;
			}
			patch.type = (type_u | (type_v<<2));
			if (patch_u != 0) patch.type &= ~START_OPEN_U;
			if (patch_v != 0) patch.type &= ~START_OPEN_V;
			if (patch_u != num_patches_u-1) patch.type &= ~END_OPEN_U;
			if (patch_v != num_patches_v-1) patch.type &= ~END_OPEN_V;
		}
	}

	u8 *decoded2 = decoded + 65536 * 24;

	int count = 0;
	u8 *dest = decoded2;

	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		HWSplinePatch& patch = patches[patch_idx];

		// TODO: Should do actual patch subdivision instead of just drawing the control points!
		const int tile_min_u = (patch.type & START_OPEN_U) ? 0 : 1;
		const int tile_min_v = (patch.type & START_OPEN_V) ? 0 : 1;
		const int tile_max_u = (patch.type & END_OPEN_U) ? 3 : 2;
		const int tile_max_v = (patch.type & END_OPEN_V) ? 3 : 2;
		for (int tile_u = tile_min_u; tile_u < tile_max_u; ++tile_u) {
			for (int tile_v = tile_min_v; tile_v < tile_max_v; ++tile_v) {
				int point_index = tile_u + tile_v*4;

				u8 *v0 = patch.points[point_index];
				u8 *v1 = patch.points[point_index+1];
				u8 *v2 = patch.points[point_index+4];
				u8 *v3 = patch.points[point_index+5];

				// TODO: Insert UVs and normals if not present.
				CopyTriangle(dest, v0, v2, v1, undecodedVertexSize);
				CopyTriangle(dest, v1, v2, v3, undecodedVertexSize);
				count += 6;
			}
		}
	}
	delete[] patches;

	u32 vertTypeWithoutIndex = vertex_type & ~GE_VTYPE_IDX_MASK;

	SubmitPrim(decoded2, 0, GE_PRIM_TRIANGLES, count, vertTypeWithoutIndex, GE_VTYPE_IDX_NONE, 0);
	Flush();
}

// TODO
void TransformDrawEngineDX9::SubmitBezier(void* control_points, void* indices, int count_u, int count_v, GEPatchPrimType prim_type, u32 vertex_type) {
	if (prim_type != GE_PATCHPRIM_TRIANGLES) {
		// Only triangles supported!
		return;
	}

	// We're not actually going to decode, only reshuffle.
	VertexDecoderDX9 vdecoder;
	vdecoder.SetVertexType(vertex_type);

	Flush();
}

};
