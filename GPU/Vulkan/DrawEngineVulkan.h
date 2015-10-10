// Copyright (c) 2015- PPSSPP Project.

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

#include "GPU/Common/DrawEngineCommon.h"
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

#include "GPU/Vulkan/VulkanUtil.h"

#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"

struct DecVtxFormat;
struct UVScale;

class ShaderManagerVulkan;
class PipelineManagerVulkan;
class TextureCacheVulkan;
class FramebufferManagerVulkan;

// Avoiding the full include of TextureDecoder.h.
#if (defined(_M_SSE) && defined(_M_X64)) || defined(ARM64)
typedef u64 ReliableHashType;
#else
typedef u32 ReliableHashType;
#endif

// Handles transform, lighting and drawing.
class DrawEngineVulkan : public DrawEngineCommon {
public:
	DrawEngineVulkan();
	virtual ~DrawEngineVulkan();

	void SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead);

	void SetShaderManager(ShaderManagerVulkan *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetPipelineManager(PipelineManagerVulkan *pipelineManager) {
		pipelineManager_ = pipelineManager;
	}
	void SetTextureCache(TextureCacheVulkan *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManagerVulkan *fbManager) {
		framebufferManager_ = fbManager;
	}

	void Resized();  // TODO: Call

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
	void Flush(VkCommandBuffer cmd) {
		if (!numDrawCalls)
			return;
		DoFlush(cmd);
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		DecodeVerts();
	}

	bool IsCodePtrVertexDecoder(const u8 *ptr) const;

	void DispatchFlush() override { Flush(cmd_); }
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) override {
		SubmitPrim(verts, inds, prim, vertexCount, vertType, bytesRead);
	}

	void SetCmdBuffer(VkCommandBuffer cmd) {
		cmd_ = cmd;
	}

private:
	void DecodeVerts();
	void DecodeVertsStep();
	void DoFlush(VkCommandBuffer cmd);

	VertexDecoder *GetVertexDecoder(u32 vtype);

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

	VkCommandBuffer cmd_;

	// Vertex collector state
	IndexGenerator indexGen;
	int decodedVerts_;
	GEPrimitiveType prevPrim_;

	u32 lastVType_;

	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;

	// Other
	ShaderManagerVulkan *shaderManager_;
	PipelineManagerVulkan *pipelineManager_;
	TextureCacheVulkan *textureCache_;
	FramebufferManagerVulkan *framebufferManager_;

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };

	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls;
	int vertexCountInDrawCalls;

	int decodeCounter_;
	u32 dcid_;

	bool fboTexNeedBind_;
	bool fboTexBound_;
};
