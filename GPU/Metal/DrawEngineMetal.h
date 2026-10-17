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

// Objective-C++ only.

#include <unordered_map>

#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/Metal/MetalRenderManager.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/ShaderId.h"

class ShaderManagerMetal;
class TextureCacheMetal;
class FramebufferManagerMetal;

// What goes into a MTLRenderPipelineState, other than the shaders.
struct MetalBlendKey {
	union {
		uint32_t value;
		struct {
			uint32_t blendEnable : 1;
			uint32_t srcColor : 5;  // BlendFactor
			uint32_t dstColor : 5;
			uint32_t srcAlpha : 5;
			uint32_t dstAlpha : 5;
			uint32_t eqColor : 3;  // BlendEq
			uint32_t eqAlpha : 3;
			uint32_t colorWriteMask : 4;  // R=1, G=2, B=4, A=8
		};
	};
};

struct MetalDepthStencilKey {
	union {
		uint64_t value;
		struct {
			uint64_t depthTestEnable : 1;
			uint64_t depthWriteEnable : 1;
			uint64_t depthCompare : 3;  // GEComparison
			uint64_t stencilTestEnable : 1;
			uint64_t stencilCompare : 3;  // GEComparison
			uint64_t stencilPass : 3;  // GEStencilOp
			uint64_t stencilFail : 3;
			uint64_t stencilDepthFail : 3;
			uint64_t writeMask : 8;
			uint64_t compareMask : 8;
		};
	};
};

struct MetalPipelineKey {
	VShaderID vsid;
	FShaderID fsid;
	uint32_t vtxFmtId;  // The decoded format ID, or SOFTWARE_TRANSFORM_VTXFMT.
	uint32_t blend;  // MetalBlendKey
	uint32_t targetFormat;  // MetalRenderTargetFormat::Key()
	uint32_t pad = 0;
};

struct MetalDynamicState {
	MTLViewport viewport;
	int scissor[4];
	bool useBlendColor;
	float blendColor[4];
	uint8_t stencilRef;
	MTLCullMode cull;
	bool depthClamp;
};

class DrawEngineMetal : public DrawEngineCommon {
public:
	DrawEngineMetal(Draw::DrawContext *draw);
	~DrawEngineMetal();

	void SetShaderManager(ShaderManagerMetal *shaderManager) { shaderManager_ = shaderManager; }
	void SetTextureCache(TextureCacheMetal *textureCache) { textureCache_ = textureCache; }
	void SetFramebufferManager(FramebufferManagerMetal *fbManager) { framebufferManager_ = fbManager; }

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

	void BeginFrame() override;
	void Flush() override;
	void FinishDeferred() {
		// The indices are read from PSP memory at flush time, and the game may change them (#10095).
		Flush();
	}

	void ClearPipelines();

	// The shader cache: the pipelines this game has used, and the formats of their targets.
	bool SavePipelines(FILE *f);
	static uint32_t PipelineRecordSize();
	// Creates the pipelines, which compile in the background. waitForCompiles waits for all of them.
	bool LoadPipelines(FILE *f, int count, bool waitForCompiles);
	int GetNumPipelines() const { return (int)pipelines_.size(); }

	void SetDepalTexture(id<MTLTexture> depal, bool smooth) {
		if (boundDepal_ != depal) {
			boundDepal_ = depal;
			boundDepalSmoothed_ = smooth;
			gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}
	}

private:
	void InitDeviceObjects();
	void Invalidate(InvalidationCallbackFlags flags);
	void ConvertStateToKeys(int prim);
	void BindShaderBlendTex();
	struct Pipeline {
		MetalPipelineState *state;  // May still be compiling.
		MetalRenderTargetFormat format;
		uint32_t fragmentArrayTextureMask;
		uint32_t fragmentTextureMask;
	};

	Pipeline *GetPipeline(const VShaderID &vsid, const FShaderID &fsid, bool useHWTransform);
	Pipeline *GetOrCreatePipeline(const MetalPipelineKey &key, const DecVtxFormat *decFmt, const MetalRenderTargetFormat &format);
	id<MTLDepthStencilState> GetDepthStencilState(const MetalDepthStencilKey &key);
	bool ApplyState(const Pipeline *pipeline, bool overrideStencilRef, uint8_t stencilRef);
	void BindResources(const Pipeline *pipeline);
	void ResetAfterSkippedDraw();
	void DrawPrimitives(MTLPrimitiveType prim, __unsafe_unretained id<MTLBuffer> vbuf, uint32_t vbOffset, int count, const uint16_t *inds);

	Draw::DrawContext *draw_;
	MetalRenderManager *rm_ = nullptr;

	ShaderManagerMetal *shaderManager_ = nullptr;
	TextureCacheMetal *textureCache_ = nullptr;
	FramebufferManagerMetal *framebufferManager_ = nullptr;

	DenseHashMap<MetalPipelineKey, Pipeline *> pipelines_;
	std::unordered_map<uint64_t, id<MTLDepthStencilState>> depthStencilCache_;
	std::unordered_map<uint32_t, MTLVertexDescriptor *> vertexDescriptors_;

	// The current state.
	MetalBlendKey blendKey_{};
	MetalDepthStencilKey depthStencilKey_{};
	MetalDynamicState dynState_{};
	GEPrimitiveType lastPrim_ = GE_PRIM_INVALID;
	Pipeline *lastPipeline_ = nullptr;

	id<MTLTexture> boundSecondary_;
	id<MTLTexture> boundDepal_;
	bool boundDepalSmoothed_ = false;
	id<MTLTexture> nullTexture_;
	id<MTLSamplerState> samplerNearest_;
	id<MTLSamplerState> samplerLinear_;

	FBOTexState fboTexBindState_ = FBO_TEX_NONE;

	MetalPushAlloc baseUBO_;
	MetalPushAlloc lightUBO_;
	uint64_t uboFrame_ = (uint64_t)-1;
};
