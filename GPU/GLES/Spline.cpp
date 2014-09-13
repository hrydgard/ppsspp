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

#include "GPU/GLES/TransformPipeline.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "GPU/Math3D.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"

// Here's how to evaluate them fast:
// http://and-what-happened.blogspot.se/2012/07/evaluating-b-splines-aka-basis-splines.html

// This normalizes a set of vertices in any format to SimpleVertex format, by processing away morphing AND skinning.
// The rest of the transform pipeline like lighting will go as normal, either hardware or software.
// The implementation is initially a bit inefficient but shouldn't be a big deal.
// An intermediate buffer of not-easy-to-predict size is stored at bufPtr.
u32 TransformDrawEngine::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, VertexDecoder *dec, int lowerBound, int upperBound, u32 vertType) {
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

u32 TransformDrawEngine::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType) {
	const u32 vertTypeID = (vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24);
	VertexDecoder *dec = GetVertexDecoder(vertTypeID);
	return NormalizeVertices(outPtr, bufPtr, inPtr, dec, lowerBound, upperBound, vertType);
}

void TransformDrawEngine::SubmitSpline(void* control_points, void* indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertType) {
	Flush();

	if (prim_type != GE_PATCHPRIM_TRIANGLES) {
		// Only triangles supported!
		return;
	}

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 24;
	
	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, (int)sizeof(SimpleVertex));
	}
	const DecVtxFormat& vtxfmt = vdecoder->GetDecVtxFmt();

	// TODO: Do something less idiotic to manage this buffer
	SimpleVertex **points = new SimpleVertex *[count_u * count_v];

	// Make an array of pointers to the control points, to get rid of indices.
	for (int idx = 0; idx < count_u * count_v; idx++) {
		if (indices)
			points[idx] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
		else
			points[idx] = simplified_control_points + idx;
	}

	u8 *decoded2 = decoded + 65536 * 36;

	int count = 0;
	u8 *dest = decoded2;

	SplinePatchLocal patch;
	patch.type_u = type_u;
	patch.type_v = type_v;
	patch.count_u = count_u;
	patch.count_v = count_v;
	patch.points = points;

	TesselateSplinePatch(dest, count, patch, origVertType);

	delete[] points;

	u32 vertTypeWithIndex16 = (vertType & ~GE_VTYPE_IDX_MASK) | GE_VTYPE_IDX_16BIT;

	UVScale prevUVScale;
	if (g_Config.bPrescaleUV) {
		// We scaled during Normalize already so let's turn it off when drawing.
		prevUVScale = gstate_c.uv;
		gstate_c.uv.uScale = 1.0f;
		gstate_c.uv.vScale = 1.0f;
		gstate_c.uv.uOff = 0;
		gstate_c.uv.vOff = 0;
	}
	SubmitPrim(decoded2, quadIndices_, GE_PRIM_TRIANGLES, count, vertTypeWithIndex16, 0);

	Flush();

	if (g_Config.bPrescaleUV) {
		gstate_c.uv = prevUVScale;
	}
}

void TransformDrawEngine::SubmitBezier(void* control_points, void* indices, int count_u, int count_v, GEPatchPrimType prim_type, u32 vertType) {
	Flush();

	if (prim_type != GE_PATCHPRIM_TRIANGLES) {
		// Only triangles supported!
		return;
	}

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 24;

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, (int)sizeof(SimpleVertex));
	}
	const DecVtxFormat& vtxfmt = vdecoder->GetDecVtxFmt();

	// Bezier patches share less control points than spline patches. Otherwise they are pretty much the same (except bezier don't support the open/close thing)
	int num_patches_u = (count_u - 1) / 3;
	int num_patches_v = (count_v - 1) / 3;
	BezierPatch* patches = new BezierPatch[num_patches_u * num_patches_v];
	for (int patch_u = 0; patch_u < num_patches_u; patch_u++) {
		for (int patch_v = 0; patch_v < num_patches_v; patch_v++) {
			BezierPatch& patch = patches[patch_u + patch_v * num_patches_u];
			for (int point = 0; point < 16; ++point) {
				int idx = (patch_u * 3 + point%4) + (patch_v * 3 + point/4) * count_u;
				if (indices)
					patch.points[point] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
				else
					patch.points[point] = simplified_control_points + idx;
			}
			patch.u_index = patch_u * 3;
			patch.v_index = patch_v * 3;
		}
	}

	u8 *decoded2 = decoded + 65536 * 36;

	int count = 0;
	u8 *dest = decoded2;

	// Simple approximation of the real tesselation factor.
	// We shouldn't really split up into separate 4x4 patches, instead we should do something that works
	// like the splines, so we subdivide across the whole "mega-patch".
	if (num_patches_u == 0) num_patches_u = 1;
	if (num_patches_v == 0) num_patches_v = 1;
	int tess_u = gstate.getPatchDivisionU() / num_patches_u;
	int tess_v = gstate.getPatchDivisionV() / num_patches_v;
	if (tess_u < 4) tess_u = 4;
	if (tess_v < 4) tess_v = 4;

	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		BezierPatch& patch = patches[patch_idx];
		TesselateBezierPatch(dest, count, tess_u, tess_v, patch, origVertType);
	}
	delete[] patches;

	u32 vertTypeWithIndex16 = (vertType & ~GE_VTYPE_IDX_MASK) | GE_VTYPE_IDX_16BIT;

	UVScale prevUVScale;
	if (g_Config.bPrescaleUV) {
		// We scaled during Normalize already so let's turn it off when drawing.
		prevUVScale = gstate_c.uv;
		gstate_c.uv.uScale = 1.0f;
		gstate_c.uv.vScale = 1.0f;
		gstate_c.uv.uOff = 0;
		gstate_c.uv.vOff = 0;
	}

	SubmitPrim(decoded2, quadIndices_, GE_PRIM_TRIANGLES, count, vertTypeWithIndex16, 0);
	Flush();

	if (g_Config.bPrescaleUV) {
		gstate_c.uv = prevUVScale;
	}
}
