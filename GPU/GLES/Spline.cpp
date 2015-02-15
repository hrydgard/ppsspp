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
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"

// Here's how to evaluate them fast:
// http://and-what-happened.blogspot.se/2012/07/evaluating-b-splines-aka-basis-splines.html

u32 TransformDrawEngine::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType) {
	const u32 vertTypeID = (vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24);
	VertexDecoder *dec = GetVertexDecoder(vertTypeID);
	return DrawEngineCommon::NormalizeVertices(outPtr, bufPtr, inPtr, dec, lowerBound, upperBound, vertType);
}

const GEPrimitiveType primType[] = { GE_PRIM_TRIANGLES, GE_PRIM_LINES, GE_PRIM_POINTS };

void TransformDrawEngine::SubmitSpline(const void *control_points, const void *indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertType) {
	Flush();

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 18;
	
	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, (int)sizeof(SimpleVertex));
	}

	// TODO: Do something less idiotic to manage this buffer
	SimpleVertex **points = new SimpleVertex *[count_u * count_v];

	// Make an array of pointers to the control points, to get rid of indices.
	for (int idx = 0; idx < count_u * count_v; idx++) {
		if (indices)
			points[idx] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
		else
			points[idx] = simplified_control_points + idx;
	}

	int count = 0;

	u8 *dest = splineBuffer;

	SplinePatchLocal patch;
	patch.type_u = type_u;
	patch.type_v = type_v;
	patch.count_u = count_u;
	patch.count_v = count_v;
	patch.points = points;

	int maxVertexCount = SPLINE_BUFFER_SIZE / vertexSize;
	TesselateSplinePatch(dest, quadIndices_, count, patch, origVertType, maxVertexCount);

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

	int bytesRead;
	SubmitPrim(splineBuffer, quadIndices_, primType[prim_type], count, vertTypeWithIndex16, &bytesRead);

	Flush();

	if (g_Config.bPrescaleUV) {
		gstate_c.uv = prevUVScale;
	}
}

void TransformDrawEngine::SubmitBezier(const void *control_points, const void *indices, int count_u, int count_v, GEPatchPrimType prim_type, u32 vertType) {
	Flush();

	// TODO: Verify correct functionality with < 4.
	if (count_u < 4 || count_v < 4)
		return;

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	// There are normally not a lot of control points so just splitting decoded should be reasonably safe, although not great.
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 18;

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, (int)sizeof(SimpleVertex));
	}

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
			patch.index = patch_v * num_patches_u + patch_u;
		}
	}

	int count = 0;
	u8 *dest = splineBuffer;

	// Simple approximation of the real tesselation factor.
	// We shouldn't really split up into separate 4x4 patches, instead we should do something that works
	// like the splines, so we subdivide across the whole "mega-patch".
	if (num_patches_u == 0) num_patches_u = 1;
	if (num_patches_v == 0) num_patches_v = 1;
	int tess_u = gstate.getPatchDivisionU();
	int tess_v = gstate.getPatchDivisionV();
	if (tess_u < 4) tess_u = 4;
	if (tess_v < 4) tess_v = 4;

	u16 *inds = quadIndices_;
	int maxVertices = SPLINE_BUFFER_SIZE / vertexSize;
	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		BezierPatch& patch = patches[patch_idx];
		TesselateBezierPatch(dest, inds, count, tess_u, tess_v, patch, origVertType, maxVertices);
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

	int bytesRead;
	SubmitPrim(splineBuffer, quadIndices_, primType[prim_type], count, vertTypeWithIndex16, &bytesRead);

	Flush();

	if (g_Config.bPrescaleUV) {
		gstate_c.uv = prevUVScale;
	}
}
