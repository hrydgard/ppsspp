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

#pragma once

#include <vector>
#include <unordered_map>

#include "Common/CommonTypes.h"

#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/VertexDecoderCommon.h"

class VertexDecoder;

enum {
	VERTEX_BUFFER_MAX = 65536,
	DECODED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 64,
	DECODED_INDEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 16,
	SPLINE_BUFFER_SIZE = VERTEX_BUFFER_MAX * 20,
};

class DrawEngineCommon {
public:
	DrawEngineCommon();
	virtual ~DrawEngineCommon();

	bool TestBoundingBox(void* control_points, int vertexCount, u32 vertType);

	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	static u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, VertexDecoder *dec, int lowerBound, int upperBound, u32 vertType);

	// Flush is normally non-virtual but here's a virtual way to call it, used by the shared spline code, which is expensive anyway.
	// Not really sure if these wrappers are worth it...
	virtual void DispatchFlush() = 0;
	// Same for SubmitPrim
	virtual void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) = 0;

	void SubmitSpline(const void *control_points, const void *indices, int tess_u, int tess_v, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, bool computeNormals, bool patchFacing, u32 vertType, int *bytesRead);
	void SubmitBezier(const void *control_points, const void *indices, int tess_u, int tess_v, int count_u, int count_v, GEPatchPrimType prim_type, bool computeNormals, bool patchFacing, u32 vertType, int *bytesRead);

	std::vector<std::string> DebugGetVertexLoaderIDs();
	std::string DebugGetVertexLoaderString(std::string id, DebugShaderStringType stringType);
protected:
	// Preprocessing for spline/bezier
	u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType);

	void ApplyClearToMemory(int x1, int y1, int x2, int y2, u32 clearColor);

	VertexDecoder *GetVertexDecoder(u32 vtype);

	inline int IndexSize(u32 vtype) const {
		const u32 indexType = (vtype & GE_VTYPE_IDX_MASK);
		if (indexType == GE_VTYPE_IDX_16BIT) {
			return 2;
		} else if (indexType == GE_VTYPE_IDX_32BIT) {
			return 4;
		}
		return 1;
	}

	// Vertex collector buffers
	u8 *decoded;
	u16 *decIndex;
	u8 *splineBuffer;

	// Cached vertex decoders
	std::unordered_map<u32, VertexDecoder *> decoderMap_;
	VertexDecoder *dec_;
	VertexDecoderJitCache *decJitCache_;
	VertexDecoderOptions decOptions_;

	// Fixed index buffer for easy quad generation from spline/bezier
	u16 *quadIndices_;
};
