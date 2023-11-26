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

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"

class VertexDecoder;

enum {
	VERTEX_BUFFER_MAX = 65536,
	DECODED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 2 * 36,  // 36 == sizeof(SimpleVertex)
	DECODED_INDEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 6 * 6 * 2,   // * 6 for spline tessellation, then * 6 again for converting into points/lines, and * 2 for 2 bytes per index
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

inline uint32_t GetVertTypeID(uint32_t vertType, int uvGenMode, bool skinInDecode) {
	// As the decoder depends on the UVGenMode when we use UV prescale, we simply mash it
	// into the top of the verttype where there are unused bits.
	return (vertType & 0xFFFFFF) | (uvGenMode << 24) | (skinInDecode << 26);
}

struct SimpleVertex;
namespace Spline { struct Weight2D; }

class TessellationDataTransfer {
public:
	virtual ~TessellationDataTransfer() {}
	void CopyControlPoints(float *pos, float *tex, float *col, int posStride, int texStride, int colStride, const SimpleVertex *const *points, int size, u32 vertType);
	virtual void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) = 0;
};

// Culling plane.
struct Plane {
	float x, y, z, w;
	void Set(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; }
	float Test(const float f[3]) const { return x * f[0] + y * f[1] + z * f[2] + w; }
};

class DrawEngineCommon {
public:
	DrawEngineCommon();
	virtual ~DrawEngineCommon();

	void Init();
	virtual void DeviceLost() = 0;
	virtual void DeviceRestore(Draw::DrawContext *draw) = 0;

	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	static u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, VertexDecoder *dec, int lowerBound, int upperBound, u32 vertType);

	// Flush is normally non-virtual but here's a virtual way to call it, used by the shared spline code, which is expensive anyway.
	// Not really sure if these wrappers are worth it...
	virtual void DispatchFlush() = 0;

	// This would seem to be unnecessary now, but is still required for splines/beziers to work in the software backend since SubmitPrim
	// is different. Should probably refactor that.
	// Note that vertTypeID should be computed using GetVertTypeID().
	virtual void DispatchSubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, bool clockwise, int *bytesRead) {
		SubmitPrim(verts, inds, prim, vertexCount, vertTypeID, clockwise, bytesRead);
	}

	virtual void DispatchSubmitImm(GEPrimitiveType prim, TransformedVertex *buffer, int vertexCount, int cullMode, bool continuation);

	bool TestBoundingBox(const void *control_points, const void *inds, int vertexCount, u32 vertType);

	void FlushSkin() {
		bool applySkin = (lastVType_ & GE_VTYPE_WEIGHT_MASK) && decOptions_.applySkinInDecode;
		if (applySkin) {
			DecodeVerts(decoded_);
		}
	}

	int ExtendNonIndexedPrim(const uint32_t *cmd, const uint32_t *stall, u32 vertTypeID, bool clockwise, int *bytesRead, bool isTriangle);
	bool SubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, bool clockwise, int *bytesRead);
	void SkipPrim(GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int *bytesRead);

	template<class Surface>
	void SubmitCurve(const void *control_points, const void *indices, Surface &surface, u32 vertType, int *bytesRead, const char *scope);
	void ClearSplineBezierWeights();

	bool CanUseHardwareTransform(int prim);
	bool CanUseHardwareTessellation(GEPatchPrimType prim);

	std::vector<std::string> DebugGetVertexLoaderIDs();
	std::string DebugGetVertexLoaderString(std::string id, DebugShaderStringType stringType);

	virtual void NotifyConfigChanged();

	bool EverUsedExactEqualDepth() const {
		return everUsedExactEqualDepth_;
	}
	void SetEverUsedExactEqualDepth(bool v) {
		everUsedExactEqualDepth_ = v;
	}

	bool IsCodePtrVertexDecoder(const u8 *ptr) const {
		if (decJitCache_)
			return decJitCache_->IsInSpace(ptr);
		return false;
	}
	int GetNumDrawCalls() const {
		return numDrawVerts_;
	}

	VertexDecoder *GetVertexDecoder(u32 vtype);

	virtual void ClearTrackedVertexArrays() {}

protected:
	virtual bool UpdateUseHWTessellation(bool enabled) const { return enabled; }
	void UpdatePlanes();

	void DecodeVerts(u8 *dest);
	void DecodeInds();

	int MaxIndex() const {
		return decodedVerts_;
	}

	// Preprocessing for spline/bezier
	u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType, int *vertexSize = nullptr);

	int ComputeNumVertsToDecode() const;

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

	inline void UpdateEverUsedEqualDepth(GEComparison comp) {
		switch (comp) {
		case GE_COMP_EQUAL:
			everUsedExactEqualDepth_ = true;
			everUsedEqualDepth_ = true;
			break;

		case GE_COMP_NOTEQUAL:
		case GE_COMP_LEQUAL:
		case GE_COMP_GEQUAL:
			everUsedEqualDepth_ = true;
			break;

		default:
			break;
		}
	}

	inline void ResetAfterDrawInline() {
		gpuStats.numFlushes++;
		gpuStats.numDrawCalls += numDrawInds_;
		gpuStats.numVertexDecodes += numDrawVerts_;
		gpuStats.numVertsSubmitted += vertexCountInDrawCalls_;

		indexGen.Reset();
		decodedVerts_ = 0;
		numDrawVerts_ = 0;
		numDrawInds_ = 0;
		vertexCountInDrawCalls_ = 0;
		decodeIndsCounter_ = 0;
		decodeVertsCounter_ = 0;
		gstate_c.vertexFullAlpha = true;

		// Now seems as good a time as any to reset the min/max coords, which we may examine later.
		gstate_c.vertBounds.minU = 512;
		gstate_c.vertBounds.minV = 512;
		gstate_c.vertBounds.maxU = 0;
		gstate_c.vertBounds.maxV = 0;
	}

	uint32_t ComputeDrawcallsHash() const;

	bool useHWTransform_ = false;
	bool useHWTessellation_ = false;
	// Used to prevent unnecessary flushing in softgpu.
	bool flushOnParams_ = true;

	// Set once a equal depth test is encountered.
	bool everUsedEqualDepth_ = false;
	bool everUsedExactEqualDepth_ = false;

	// Vertex collector buffers
	u8 *decoded_ = nullptr;
	u16 *decIndex_ = nullptr;

	// Cached vertex decoders
	u32 lastVType_ = -1;  // corresponds to dec_.  Could really just pick it out of dec_...
	DenseHashMap<u32, VertexDecoder *> decoderMap_;
	VertexDecoder *dec_ = nullptr;
	VertexDecoderJitCache *decJitCache_ = nullptr;
	VertexDecoderOptions decOptions_{};

	TransformedVertex *transformed_ = nullptr;
	TransformedVertex *transformedExpanded_ = nullptr;

	// Defer all vertex decoding to a "Flush" (except when software skinning)
	struct DeferredVerts {
		const void *verts;
		u32 vertexCount;
		u16 indexLowerBound;
		u16 indexUpperBound;
		UVScale uvScale;
	};

	struct DeferredInds {
		const void *inds;
		u32 vertexCount;
		u8 vertDecodeIndex;  // index into the drawVerts_ array to look up the vertexOffset.
		u8 indexType;
		s8 prim;
		bool clockwise;
		u16 offset;
	};

	enum { MAX_DEFERRED_DRAW_VERTS = 128 };  // If you change this to more than 256, change type of DeferredInds::vertDecodeIndex.
	enum { MAX_DEFERRED_DRAW_INDS = 512 };  // Monster Hunter spams indexed calls that we end up merging.
	DeferredVerts drawVerts_[MAX_DEFERRED_DRAW_VERTS];
	uint32_t drawVertexOffsets_[MAX_DEFERRED_DRAW_VERTS];
	DeferredInds drawInds_[MAX_DEFERRED_DRAW_INDS];

	int numDrawVerts_ = 0;
	int numDrawInds_ = 0;
	int vertexCountInDrawCalls_ = 0;

	int decodeVertsCounter_ = 0;
	int decodeIndsCounter_ = 0;

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

	// Culling
	Plane planes_[6];
	Vec2f minOffset_;
	Vec2f maxOffset_;
};
