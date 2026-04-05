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
#include "GPU/GPUDefinitions.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"

class VertexDecoder;
struct DepthDraw;

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

struct SimpleVertex;
namespace Spline { struct Weight2D; }

class TessellationDataTransfer {
public:
	virtual ~TessellationDataTransfer() {}
	static void CopyControlPoints(float *pos, float *tex, float *col, int posStride, int texStride, int colStride, const SimpleVertex *const *points, int size, u32 vertType);
	virtual void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) = 0;
};

// Culling plane, group of 8.
struct alignas(16) Plane8 {
	float x[8], y[8], z[8], w[8];
	void Set(int i, float _x, float _y, float _z, float _w) { x[i] = _x; y[i] = _y; z[i] = _z; w[i] = _w; }
	float Test(int i, const float f[3]) const { return x[i] * f[0] + y[i] * f[1] + z[i] * f[2] + w[i]; }
};

class DrawEngineCommon {
public:
	DrawEngineCommon();
	virtual ~DrawEngineCommon();

	void Init();

	virtual void BeginFrame();

	void SetGPUCommon(GPUCommon *gpuCommon) {
		gpuCommon_ = gpuCommon;
	}

	virtual void DeviceLost() = 0;
	virtual void DeviceRestore(Draw::DrawContext *draw) = 0;

	// Dispatches the queued-up draws.
	virtual void Flush() = 0;

	// This would seem to be unnecessary now, but is still required for splines/beziers to work in the software backend since SubmitPrim
	// is different. Should probably refactor that.
	// Note that vertTypeID should be computed using GetVertTypeID().
	virtual void DispatchSubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, bool clockwise, int *bytesRead) {
		VertexDecoder *dec = GetVertexDecoder(vertTypeID);
		SubmitPrim(verts, inds, prim, vertexCount, dec, vertTypeID, clockwise, bytesRead);
	}

	virtual void DispatchSubmitImm(GEPrimitiveType prim, TransformedVertex *buffer, int vertexCount, int cullMode, bool continuation);

	bool TestBoundingBox(const void *control_points, const void *inds, int vertexCount, const VertexDecoder *dec, u32 vertType);

	// This is a less accurate version of TestBoundingBox, but faster. Can have more false positives.
	// Doesn't support indexing.
	bool TestBoundingBoxFast(const void *control_points, int vertexCount, const VertexDecoder *dec, u32 vertType);
	bool TestBoundingBoxThrough(const void *vdata, int vertexCount, const VertexDecoder *dec, u32 vertType, int *bytesRead);

	void FlushPartialDecode() {
		DecodeVerts(dec_, decoded_);
	}

	void FlushSkin() {
		if (dec_ && dec_->skinInDecode) {
			FlushPartialDecode();
		}
	}

	int ExtendNonIndexedPrim(const uint32_t *cmd, const uint32_t *stall, const VertexDecoder *dec, u32 vertTypeID, bool clockwise, int *bytesRead, bool isTriangle);
	bool SubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, const VertexDecoder *dec, u32 vertTypeID, bool clockwise, int *bytesRead);
	void SkipPrim(GEPrimitiveType prim, int vertexCount, const VertexDecoder *dec, u32 vertTypeID, int *bytesRead);

	template<class Surface>
	void SubmitCurve(const void *control_points, const void *indices, Surface &surface, u32 vertType, int *bytesRead, const char *scope);
	static void ClearSplineBezierWeights();

	bool CanUseHardwareTransform(int prim) const;
	bool CanUseHardwareTessellation(GEPatchPrimType prim) const;

	std::vector<std::string> DebugGetVertexLoaderIDs();
	std::string DebugGetVertexLoaderString(std::string_view id, DebugShaderStringType stringType);

	virtual void NotifyConfigChanged();

	bool EverUsedExactEqualDepth() const {
		return everUsedExactEqualDepth_;
	}
	void SetEverUsedExactEqualDepth(bool v) {
		everUsedExactEqualDepth_ = v;
	}

	bool DescribeCodePtr(const u8 *ptr, std::string &name) const;
	int GetNumDrawCalls() const {
		return numDrawVerts_;
	}

	VertexDecoder *GetVertexDecoder(u32 vertTypeID) {
		VertexDecoder *dec;
		if (decoderMap_.Get(vertTypeID, &dec))
			return dec;
		dec = new VertexDecoder();
		_assert_(dec);
		dec->SetVertexType(vertTypeID, decOptions_, decJitCache_);
		decoderMap_.Insert(vertTypeID, dec);
		return dec;
	}

	void AssertEmpty() {
		_dbg_assert_(numDrawVerts_ == 0 && numDrawInds_ == 0);
	}

	// temporary hack
	uint8_t *GetTempSpace() {
		return decoded_ + 12 * 65536;
	}

	void FlushQueuedDepth();

protected:
	virtual bool UpdateUseHWTessellation(bool enabled) const { return enabled; }
	void UpdatePlanes();

	void DecodeVerts(const VertexDecoder *dec, u8 *dest);
	int DecodeInds();

	int ComputeNumVertsToDecode() const;

	void ApplyFramebufferRead(FBOTexState *fboTexState);

	void InitDepthRaster();
	void ShutdownDepthRaster();
	void DepthRasterSubmitRaw(GEPrimitiveType prim, const VertexDecoder *dec, uint32_t vertTypeID, int vertexCount);
	void DepthRasterPredecoded(GEPrimitiveType prim, const void *inVerts, int numDecoded, const VertexDecoder *dec, int vertexCount);
	bool CalculateDepthDraw(DepthDraw *draw, GEPrimitiveType prim, int vertexCount);

	static inline int IndexSize(u32 vtype) {
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
		gpuStats.numVertsDecoded += numDecodedVerts_;

		indexGen.Reset();
		numDecodedVerts_ = 0;
		numDrawVerts_ = 0;
		numDrawInds_ = 0;
		vertexCountInDrawCalls_ = 0;
		decodeIndsCounter_ = 0;
		decodeVertsCounter_ = 0;
		seenPrims_ = 0;
		anyCCWOrIndexed_ = false;
		gstate_c.vertexFullAlpha = true;

		// Now seems as good a time as any to reset the min/max coords, which we may examine later.
		gstate_c.vertBounds.minU = 512;
		gstate_c.vertBounds.minV = 512;
		gstate_c.vertBounds.maxU = 0;
		gstate_c.vertBounds.maxV = 0;
	}

	inline bool CollectedPureDraw() const {
		// TODO: Do something faster.
		if (useDepthRaster_) {
			return false;
		}

		switch (seenPrims_) {
		case 1 << GE_PRIM_TRIANGLE_STRIP:
			return !anyCCWOrIndexed_ && numDrawInds_ == 1;
		case 1 << GE_PRIM_LINES:
		case 1 << GE_PRIM_POINTS:
		case 1 << GE_PRIM_TRIANGLES:
			return !anyCCWOrIndexed_;
		default:
			return false;
		}
	}

	inline void DecodeIndsAndGetData(GEPrimitiveType *prim, int *numVerts, int *maxIndex, bool *useElements, bool forceIndexed) {
		if (!forceIndexed && CollectedPureDraw()) {
			*prim = drawInds_[0].prim;
			*numVerts = numDecodedVerts_;
			*maxIndex = numDecodedVerts_;
			*useElements = false;
		} else {
			int vertexCount = DecodeInds();
			*numVerts = vertexCount;
			*maxIndex = numDecodedVerts_;
			*prim = IndexGenerator::GeneralPrim((GEPrimitiveType)drawInds_[0].prim);
			*useElements = true;
		}
	}

	inline int RemainingIndices(const uint16_t *inds) const {
		return DECODED_INDEX_BUFFER_SIZE / sizeof(uint16_t) - (inds - decIndex_);
	}

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
	DenseHashMap<u32, VertexDecoder *> decoderMap_;
	VertexDecoderJitCache *decJitCache_ = nullptr;
	VertexDecoderOptions decOptions_{};

	TransformedVertex *transformed_ = nullptr;
	TransformedVertex *transformedExpanded_ = nullptr;

	// Defer all vertex decoding to a "Flush" (except when software skinning)
	struct DeferredVerts {
		const void *verts;
		UVScale uvScale;
		u32 vertexCount;
		u16 indexLowerBound;
		u16 indexUpperBound;
	};

	struct DeferredInds {
		const void *inds;
		u32 vertexCount;
		u8 vertDecodeIndex;  // index into the drawVerts_ array to look up the vertexOffset.
		u8 indexType;
		GEPrimitiveType prim;
		bool clockwise;
		u16 offset;
	};

	enum { MAX_DEFERRED_DRAW_VERTS = 128 };  // If you change this to more than 256, change type of DeferredInds::vertDecodeIndex.
	enum { MAX_DEFERRED_DRAW_INDS = 512 };  // Monster Hunter spams indexed calls that we end up merging.
	DeferredVerts drawVerts_[MAX_DEFERRED_DRAW_VERTS];
	uint32_t drawVertexOffsets_[MAX_DEFERRED_DRAW_VERTS];
	DeferredInds drawInds_[MAX_DEFERRED_DRAW_INDS];

	const VertexDecoder *dec_ = nullptr;
	u32 lastVType_ = -1;  // corresponds to dec_.  Could really just pick it out of dec_...
	int numDrawVerts_ = 0;
	int numDrawInds_ = 0;
	int vertexCountInDrawCalls_ = 0;

	int decodeVertsCounter_ = 0;
	int decodeIndsCounter_ = 0;

	int seenPrims_ = 0;
	bool anyCCWOrIndexed_ = 0;
	bool anyIndexed_ = 0;

	bool applySkinInDecode_ = false;

	// Vertex collector state
	IndexGenerator indexGen;
	int numDecodedVerts_ = 0;
	GEPrimitiveType prevPrim_ = GE_PRIM_INVALID;

	// Shader blending state
	bool fboTexBound_ = false;

	// Sometimes, unusual situations mean we need to reset dirty flags after state calc finishes.
	uint64_t dirtyRequiresRecheck_ = 0;

	ComputedPipelineState pipelineState_;

	// Hardware tessellation
	TessellationDataTransfer *tessDataTransfer;

	// Culling
	Plane8 planes_;
	Vec2f minOffset_;
	Vec2f maxOffset_;
	bool offsetOutsideEdge_;

	GPUCommon *gpuCommon_;

	// Software depth raster
	bool useDepthRaster_ = false;

	float *depthTransformed_ = nullptr;
	int *depthScreenVerts_ = nullptr;
	uint16_t *depthIndices_ = nullptr;

	// Queue
	int depthVertexCount_ = 0;
	int depthIndexCount_ = 0;
	std::vector<DepthDraw> depthDraws_;

	double rasterTimeStart_ = 0.0;
};
