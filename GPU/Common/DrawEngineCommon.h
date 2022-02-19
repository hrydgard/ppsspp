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

#include "Common/CommonTypes.h"
#include "Common/Data/Collections/Hashmaps.h"

#include "GPU/GPUState.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"

class VertexDecoder;

enum {
	VERTEX_BUFFER_MAX = 65536,
	DECODED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 64,
	DECODED_INDEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 16,
};

enum {
	TEX_SLOT_PSP_TEXTURE = 0,
	TEX_SLOT_SHADERBLEND_SRC = 1,
	TEX_SLOT_ALPHATEST = 2,
	TEX_SLOT_CLUT = 3,
	TEX_SLOT_SPLINE_POINTS = 4,
	TEX_SLOT_SPLINE_WEIGHTS_U = 5,
	TEX_SLOT_SPLINE_WEIGHTS_V = 6,
};

enum FBOTexState {
	FBO_TEX_NONE,
	FBO_TEX_COPY_BIND_TEX,
	FBO_TEX_READ_FRAMEBUFFER,
};

inline uint32_t GetVertTypeID(uint32_t vertType, int uvGenMode) {
	// As the decoder depends on the UVGenMode when we use UV prescale, we simply mash it
	// into the top of the verttype where there are unused bits.
	return (vertType & 0xFFFFFF) | (uvGenMode << 24);
}

struct SimpleVertex;
namespace Spline { struct Weight2D; }

class TessellationDataTransfer {
public:
	virtual ~TessellationDataTransfer() {}
	void CopyControlPoints(float *pos, float *tex, float *col, int posStride, int texStride, int colStride, const SimpleVertex *const *points, int size, u32 vertType);
	virtual void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) = 0;
};

class DrawEngineCommon {
public:
	DrawEngineCommon();
	virtual ~DrawEngineCommon();

	void Init();

	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	static u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, VertexDecoder *dec, int lowerBound, int upperBound, u32 vertType);

	// Flush is normally non-virtual but here's a virtual way to call it, used by the shared spline code, which is expensive anyway.
	// Not really sure if these wrappers are worth it...
	virtual void DispatchFlush() = 0;

	// This would seem to be unnecessary now, but is still required for splines/beziers to work in the software backend since SubmitPrim
	// is different. Should probably refactor that.
	// Note that vertTypeID should be computed using GetVertTypeID().
	virtual void DispatchSubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int cullMode, int *bytesRead) {
		SubmitPrim(verts, inds, prim, vertexCount, vertTypeID, cullMode, bytesRead);
	}

	virtual void DispatchSubmitImm(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int cullMode, int *bytesRead) {
		SubmitPrim(verts, inds, prim, vertexCount, vertTypeID, cullMode, bytesRead);
		DispatchFlush();
	}

	bool TestBoundingBox(const void* control_points, int vertexCount, u32 vertType, int *bytesRead);

	void SubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int cullMode, int *bytesRead);
	template<class Surface>
	void SubmitCurve(const void *control_points, const void *indices, Surface &surface, u32 vertType, int *bytesRead, const char *scope);
	void ClearSplineBezierWeights();

	bool CanUseHardwareTransform(int prim);
	bool CanUseHardwareTessellation(GEPatchPrimType prim);

	std::vector<std::string> DebugGetVertexLoaderIDs();
	std::string DebugGetVertexLoaderString(std::string id, DebugShaderStringType stringType);

	virtual void Resized();

	bool IsCodePtrVertexDecoder(const u8 *ptr) const {
		return decJitCache_->IsInSpace(ptr);
	}
	int GetNumDrawCalls() const {
		return numDrawCalls;
	}

	VertexDecoder *GetVertexDecoder(u32 vtype);

protected:
	virtual bool UpdateUseHWTessellation(bool enabled) { return enabled; }
	virtual void ClearTrackedVertexArrays() {}

	int ComputeNumVertsToDecode() const;
	void DecodeVerts(u8 *dest);

	// Preprocessing for spline/bezier
	u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType, int *vertexSize = nullptr);

	// Utility for vertex caching
	u32 ComputeMiniHash();
	uint64_t ComputeHash();

	// Vertex decoding
	void DecodeVertsStep(u8 *dest, int &i, int &decodedVerts);

	void ApplyFramebufferRead(FBOTexState *fboTexState);

	inline int IndexSize(u32 vtype) const {
		const u32 indexType = (vtype & GE_VTYPE_IDX_MASK);
		if (indexType == GE_VTYPE_IDX_16BIT) {
			return 2;
		} else if (indexType == GE_VTYPE_IDX_32BIT) {
			return 4;
		}
		return 1;
	}

	bool useHWTransform_ = false;
	bool useHWTessellation_ = false;

	// Vertex collector buffers
	u8 *decoded = nullptr;
	u16 *decIndex = nullptr;

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
		const void *verts;
		const void *inds;
		u32 vertexCount;
		u8 indexType;
		s8 prim;
		u8 cullMode;
		u16 indexLowerBound;
		u16 indexUpperBound;
		UVScale uvScale;
	};

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };
	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls = 0;
	int vertexCountInDrawCalls_ = 0;

	int decimationCounter_ = 0;
	int decodeCounter_ = 0;
	u32 dcid_ = 0;

	// Vertex collector state
	IndexGenerator indexGen;
	int decodedVerts_ = 0;
	GEPrimitiveType prevPrim_ = GE_PRIM_INVALID;

	// Shader blending state
	bool fboTexBound_ = false;

	// Sometimes, unusual situations mean we need to reset dirty flags after state calc finishes.
	uint64_t dirtyRequiresRecheck_ = 0;

	ComputedPipelineState pipelineState_;

	// Hardware tessellation
	TessellationDataTransfer *tessDataTransfer;
};
