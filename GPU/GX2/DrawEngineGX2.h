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

#include <map>
#include <wiiu/gx2.h>

#include "Common/Data/Collections/Hashmaps.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GX2/FragmentShaderGeneratorGX2.h"
#include "GPU/GX2/StateMappingGX2.h"
#include "GPU/GX2/GX2Util.h"

struct DecVtxFormat;
struct UVScale;

class GX2VertexShader;
class ShaderManagerGX2;
class TextureCacheGX2;
class FramebufferManagerGX2;

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
	VAI11_FLAG_VERTEXFULLALPHA = 1,
};

// Try to keep this POD.
class VertexArrayInfoGX2 {
public:
	VertexArrayInfoGX2() {
		status = VAI_NEW;
		vbo = nullptr;
		ebo = nullptr;
		prim = GE_PRIM_INVALID;
		numDraws = 0;
		numFrames = 0;
		lastFrame = gpuStats.numFlips;
		numVerts = 0;
		drawsUntilNextFullHash = 0;
		flags = 0;
	}
	~VertexArrayInfoGX2();

	enum Status : u8 {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,   // cache, don't hash
		VAI_UNRELIABLE, // never cache
	};

	uint64_t hash;
	u32 minihash;

	void *vbo;
	void *ebo;

	// Precalculated parameter for drawRangeElements
	u16 numVerts;
	u16 maxIndex;
	s8 prim;
	Status status;

	// ID information
	int numDraws;
	int numFrames;
	int lastFrame; // So that we can forget.
	u16 drawsUntilNextFullHash;
	u8 flags;
};

// Handles transform, lighting and drawing.
class DrawEngineGX2 : public DrawEngineCommon {
public:
	DrawEngineGX2(Draw::DrawContext *draw, GX2ContextState *context);
	virtual ~DrawEngineGX2();

	void SetShaderManager(ShaderManagerGX2 *shaderManager) { shaderManager_ = shaderManager; }
	void SetTextureCache(TextureCacheGX2 *textureCache) { textureCache_ = textureCache; }
	void SetFramebufferManager(FramebufferManagerGX2 *fbManager) { framebufferManager_ = fbManager; }
	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void BeginFrame();

	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		DecodeVerts(decoded);
	}

	void DispatchFlush() override { Flush(); }

	void ClearTrackedVertexArrays() override;

	void Resized() override;

	void ClearInputLayoutMap();

private:
	void DoFlush();

	void ApplyDrawState(int prim);
	void ApplyDrawStateLate(bool applyStencilRef, u8 stencilRef);
	void ResetShaderBlending();

	GX2FetchShader *SetupFetchShaderForDraw(GX2VertexShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt);

	void MarkUnreliable(VertexArrayInfoGX2 *vai);

	Draw::DrawContext *draw_; // Used for framebuffer related things exclusively.
	GX2ContextState *context_;

	PrehashMap<VertexArrayInfoGX2 *, nullptr> vai_;

	struct FetchShaderKey {
		GX2VertexShader *vshader;
		u32 decFmtId;
		bool operator<(const FetchShaderKey &other) const {
			if (decFmtId < other.decFmtId)
				return true;
			if (decFmtId > other.decFmtId)
				return false;
			return vshader < other.vshader;
		}
	};

	DenseHashMap<FetchShaderKey, GX2FetchShader *, nullptr> fetchShaderMap_;

	// Other
	ShaderManagerGX2 *shaderManager_ = nullptr;
	TextureCacheGX2 *textureCache_ = nullptr;
	FramebufferManagerGX2 *framebufferManager_ = nullptr;

	// Pushbuffers
	PushBufferGX2 *pushVerts_;
	PushBufferGX2 *pushInds_;
	PushBufferGX2 *pushUBO_;

	// GX2 state object caches.

	struct GX2BlendState {
		GX2ColorControlReg color;
		GX2BlendControlReg blend;
		GX2BlendConstantColorReg constant;
		GX2TargetChannelMaskReg mask;
	};

	struct GX2RasterizerState {
		GX2FrontFace frontFace_;
		BOOL cullFront_;
		BOOL cullBack_;
	};

	DenseHashMap<u64, GX2BlendState*, nullptr> blendCache_;
	DenseHashMap<u64, GX2DepthStencilControlReg*, nullptr> depthStencilCache_;
	DenseHashMap<u32, GX2RasterizerState*, nullptr> rasterCache_;

	// Keep the depth state between ApplyDrawState and ApplyDrawStateLate
	GX2BlendState* blendState_ = nullptr;
	GX2DepthStencilControlReg* depthStencilState_ = nullptr;
	GX2RasterizerState* rasterState_ = nullptr;

	// State keys
	GX2StateKeys keys_{};
	GX2DynamicState dynState_{};

	// Hardware tessellation
	class TessellationDataTransferGX2 : public TessellationDataTransfer {
	private:
		GX2ContextState *context_;
		GX2Texture data_tex[3];

	public:
		TessellationDataTransferGX2(GX2ContextState *context_) : TessellationDataTransfer(), context_(context_), data_tex{} {}
		~TessellationDataTransferGX2() {
			for (int i = 0; i < 3; i++) {
				MEM2_free(data_tex[i].surface.image);
			}
		}
		void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) override;
	};

	int lastRenderStepId_ = -1;
};
