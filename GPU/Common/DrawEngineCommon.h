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
#include "Common/Hashmaps.h"

#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"

class VertexDecoder;

enum {
	VERTEX_BUFFER_MAX = 65536,
	DECODED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 64,
	DECODED_INDEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 16,
	SPLINE_BUFFER_SIZE = VERTEX_BUFFER_MAX * 26, // At least, this buffer needs greater than 1679616 bytes for Mist Dragon morphing in FF4CC.
};

// Avoiding the full include of TextureDecoder.h.
#if (defined(_M_SSE) && defined(_M_X64)) || defined(ARM64)
typedef u64 ReliableHashType;
#else
typedef u32 ReliableHashType;
#endif

class DrawEngineCommon {
public:
	DrawEngineCommon();
	virtual ~DrawEngineCommon();

	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	static u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, VertexDecoder *dec, int lowerBound, int upperBound, u32 vertType);

	// Flush is normally non-virtual but here's a virtual way to call it, used by the shared spline code, which is expensive anyway.
	// Not really sure if these wrappers are worth it...
	virtual void DispatchFlush() = 0;
	// Same for SubmitPrim
	virtual void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) = 0;

	bool TestBoundingBox(void* control_points, int vertexCount, u32 vertType, int *bytesRead);
	void SubmitSpline(const void *control_points, const void *indices, int tess_u, int tess_v, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, bool computeNormals, bool patchFacing, u32 vertType, int *bytesRead);
	void SubmitBezier(const void *control_points, const void *indices, int tess_u, int tess_v, int count_u, int count_v, GEPatchPrimType prim_type, bool computeNormals, bool patchFacing, u32 vertType, int *bytesRead);

	std::vector<std::string> DebugGetVertexLoaderIDs();
	std::string DebugGetVertexLoaderString(std::string id, DebugShaderStringType stringType);

	virtual void Resized();

	void SetupVertexDecoder(u32 vertType);

	bool IsCodePtrVertexDecoder(const u8 *ptr) const {
		return decJitCache_->IsInSpace(ptr);
	}

protected:
	virtual void ClearTrackedVertexArrays() {}

	int ComputeNumVertsToDecode() const;
	void DecodeVerts(u8 *dest);

	// Preprocessing for spline/bezier
	u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType, int *vertexSize = nullptr);

	// Utility for vertex caching
	u32 ComputeMiniHash();
	ReliableHashType ComputeHash();

	// Vertex decoding
	void DecodeVertsStep(u8 *dest, int &i, int &decodedVerts);

	bool ApplyShaderBlending();

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
	u8 *decoded = nullptr;
	u16 *decIndex = nullptr;
	u8 *splineBuffer = nullptr;

	// Cached vertex decoders
	u32 lastVType_ = -1;
	DenseHashMap<u32, VertexDecoder *, nullptr> decoderMap_;
	VertexDecoder *dec_ = nullptr;
	VertexDecoderJitCache *decJitCache_ = nullptr;
	VertexDecoderOptions decOptions_{};

	TransformedVertex *transformed = nullptr;
	TransformedVertex *transformedExpanded = nullptr;

	// Defer all vertex decoding to a "Flush" (except when software skinning)
	struct DeferredDrawCall {
		void *verts;
		void *inds;
		u32 vertType;
		u8 indexType;
		s8 prim;
		u32 vertexCount;
		u16 indexLowerBound;
		u16 indexUpperBound;
	};

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };
	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls = 0;
	int vertexCountInDrawCalls_ = 0;
	UVScale uvScale[MAX_DEFERRED_DRAW_CALLS];

	int decimationCounter_ = 0;
	int decodeCounter_ = 0;
	u32 dcid_ = 0;

	// Vertex collector state
	IndexGenerator indexGen;
	int decodedVerts_ = 0;
	GEPrimitiveType prevPrim_ = GE_PRIM_INVALID;

	// Fixed index buffer for easy quad generation from spline/bezier
	u16 *quadIndices_ = nullptr;

	// Shader blending state
	bool fboTexNeedBind_ = false;
	bool fboTexBound_ = false;

	// Hardware tessellation
	int numPatches;
	class TessellationDataTransfer {
	protected:
		// TODO: These aren't used by all backends.
		int prevSize;
		int prevSizeTex;
		int prevSizeCol;
	public:
		virtual ~TessellationDataTransfer() {}
		// Send spline/bezier's control points to vertex shader through floating point texture.
		virtual void PrepareBuffers(float *&pos, float *&tex, float *&col, int &posStride, int &texStride, int &colStride, int size, bool hasColor, bool hasTexCoords) {
			posStride = 4;
			texStride = 4;
			colStride = 4;
		}
		virtual void SendDataToShader(const float *pos, const float *tex, const float *col, int size, bool hasColor, bool hasTexCoords) = 0;
		virtual void EndFrame() {}
	};
	TessellationDataTransfer *tessDataTransfer;
};
