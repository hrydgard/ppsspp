// Copyright (c) 2012- PPSSPP Project.

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

#include <unordered_map>

#include <d3d9.h>

#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"

struct DecVtxFormat;

namespace DX9 {

class VSShader;
class ShaderManagerDX9;
class TextureCacheDX9;
class FramebufferManagerDX9;

// States transitions:
// On creation: DRAWN_NEW
// DRAWN_NEW -> DRAWN_HASHING
// DRAWN_HASHING -> DRAWN_RELIABLE
// DRAWN_HASHING -> DRAWN_UNRELIABLE
// DRAWN_ONCE -> UNRELIABLE
// DRAWN_RELIABLE -> DRAWN_SAFE
// UNRELIABLE -> death
// DRAWN_ONCE -> death
// DRAWN_RELIABLE -> death

enum {
	VAI_FLAG_VERTEXFULLALPHA = 1,
};

// Avoiding the full include of TextureDecoder.h.
#ifdef _M_X64
typedef u64 ReliableHashType;
#else
typedef u32 ReliableHashType;
#endif

// Try to keep this POD.
class VertexArrayInfoDX9 {
public:
	VertexArrayInfoDX9() {
		status = VAI_NEW;
		vbo = 0;
		ebo = 0;
		prim = GE_PRIM_INVALID;
		numDraws = 0;
		numFrames = 0;
		lastFrame = gpuStats.numFlips;
		numVerts = 0;
		drawsUntilNextFullHash = 0;
		flags = 0;
	}
	~VertexArrayInfoDX9();

	enum Status {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	ReliableHashType hash;
	u32 minihash;

	Status status;

	LPDIRECT3DVERTEXBUFFER9 vbo;
	LPDIRECT3DINDEXBUFFER9 ebo;

	// Precalculated parameter for drawRangeElements
	u16 numVerts;
	u16 maxIndex;
	s8 prim;

	// ID information
	int numDraws;
	int numFrames;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash;
	u8 flags;
};

// Handles transform, lighting and drawing.
class TransformDrawEngineDX9 : public DrawEngineCommon {
public:
	TransformDrawEngineDX9();
	virtual ~TransformDrawEngineDX9();

	void SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead);

	void SetShaderManager(ShaderManagerDX9 *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetTextureCache(TextureCacheDX9 *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManagerDX9 *fbManager) {
		framebufferManager_ = fbManager;
	}
	void InitDeviceObjects();
	void DestroyDeviceObjects();
	void GLLost() {};

	void Resized();  // TODO: Call

	void DecimateTrackedVertexArrays();
	void ClearTrackedVertexArrays();

	void SetupVertexDecoder(u32 vertType);
	void SetupVertexDecoderInternal(u32 vertType);

	// This requires a SetupVertexDecoder call first.
	int EstimatePerVertexCost() {
		// TODO: This is transform cost, also account for rasterization cost somehow... although it probably
		// runs in parallel with transform.

		// Also, this is all pure guesswork. If we can find a way to do measurements, that would be great.

		// GTA wants a low value to run smooth, GoW wants a high value (otherwise it thinks things
		// went too fast and starts doing all the work over again).

		int cost = 20;
		if (gstate.isLightingEnabled()) {
			cost += 10;

			for (int i = 0; i < 4; i++) {
				if (gstate.isLightChanEnabled(i))
					cost += 10;
			}
		}

		if (gstate.getUVGenMode() != GE_TEXMAP_TEXTURE_COORDS) {
			cost += 20;
		}
		if (dec_ && dec_->morphcount > 1) {
			cost += 5 * dec_->morphcount;
		}

		return cost;
	}

	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		DecodeVerts();
	}

	bool IsCodePtrVertexDecoder(const u8 *ptr) const;

	void DispatchFlush() override { Flush(); }
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) override {
		SubmitPrim(verts, inds, prim, vertexCount, vertType, bytesRead);
	}

private:
	void DecodeVerts();
	void DecodeVertsStep();
	void DoFlush();

	void ApplyDrawState(int prim);
	void ApplyDrawStateLate();
	void ApplyBlendState();
	void ApplyStencilReplaceAndLogicOp(ReplaceAlphaType replaceAlphaWithStencil);
	bool ApplyShaderBlending();
	inline void ResetShaderBlending();

	IDirect3DVertexDeclaration9 *SetupDecFmtForDraw(VSShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt);

	u32 ComputeMiniHash();
	ReliableHashType ComputeHash();  // Reads deferred vertex data.
	void MarkUnreliable(VertexArrayInfoDX9 *vai);

	VertexDecoder *GetVertexDecoder(u32 vtype);

	// Defer all vertex decoding to a Flush, so that we can hash and cache the
	// generated buffers without having to redecode them every time.
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

	// Vertex collector state
	IndexGenerator indexGen;
	int decodedVerts_;
	GEPrimitiveType prevPrim_;

	u32 lastVType_;
	
	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;

	std::unordered_map<u32, VertexArrayInfoDX9 *> vai_;
	std::unordered_map<u32, IDirect3DVertexDeclaration9 *> vertexDeclMap_;
	
	// Other
	ShaderManagerDX9 *shaderManager_;
	TextureCacheDX9 *textureCache_;
	FramebufferManagerDX9 *framebufferManager_;

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };

	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls;
	int vertexCountInDrawCalls;

	int decimationCounter_;
	int decodeCounter_;
	u32 dcid_;

	UVScale *uvScale;

	bool fboTexBound_;
};

}  // namespace
