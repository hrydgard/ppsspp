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

// The Descriptor Set used for the majority of PSP rendering looks like this:
//
// * binding 0: Texture/Sampler (the PSP texture)
// * binding 1: Secondary texture sampler for shader blending or depal palettes
// * binding 2: Base Uniform Buffer (includes fragment state)
// * binding 3: Light uniform buffer
// * binding 4: Bone uniform buffer
//
// All shaders conform to this layout, so they are all compatible with the same descriptor set.
// The format of the various uniform buffers may vary though - vertex shaders that don't skin
// won't get any bone data, etc.

#include <map>
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
class CachedTextureVulkan;

// Avoiding the full include of TextureDecoder.h.
#if (defined(_M_SSE) && defined(_M_X64)) || defined(ARM64)
typedef u64 ReliableHashType;
#else
typedef u32 ReliableHashType;
#endif

class VulkanContext;
class VulkanPushBuffer;

struct DrawEngineVulkanStats {
	int pushUBOSpaceUsed;
	int pushVertexSpaceUsed;
	int pushIndexSpaceUsed;
};

// Handles transform, lighting and drawing.
class DrawEngineVulkan : public DrawEngineCommon {
public:
	DrawEngineVulkan(VulkanContext *vulkan);
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

	bool IsCodePtrVertexDecoder(const u8 *ptr) const;

	void DispatchFlush() override { Flush(cmd_); }
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) override {
		SubmitPrim(verts, inds, prim, vertexCount, vertType, bytesRead);
	}

	void SetCmdBuffer(VkCommandBuffer cmd) {
		cmd_ = cmd;
	}

	VkPipelineLayout GetPipelineLayout() const {
		return pipelineLayout_;
	}

	void BeginFrame();
	void EndFrame();

	void DirtyAllUBOs();

	VulkanPushBuffer *GetPushBufferForTextureData() {
		return frame_[curFrame_].pushUBO;
	}

	const DrawEngineVulkanStats &GetStats() const {
		return stats_;
	}

private:
	struct FrameData;

	void DecodeVerts(VulkanPushBuffer *push, uint32_t *bindOffset, VkBuffer *vkbuf);
	void DecodeVertsStep(u8 *dest, int &i, int &decodedVerts);

	void DoFlush(VkCommandBuffer cmd);
	void UpdateUBOs(FrameData *frame);

	VkDescriptorSet GetDescriptorSet(VkImageView imageView, VkSampler sampler, VkBuffer base, VkBuffer light, VkBuffer bone);

	VulkanContext *vulkan_;

	// We use a single descriptor set layout for all PSP draws.
	VkDescriptorSetLayout descriptorSetLayout_;
	VkPipelineLayout pipelineLayout_;

	struct DescriptorSetKey {
		VkImageView imageView_;
		VkImageView secondaryImageView_;
		VkSampler sampler_;
		VkBuffer base_, light_, bone_;  // All three UBO slots will be set to this. This will usually be identical
		// for all draws in a frame, except when the buffer has to grow.

		bool operator < (const DescriptorSetKey &other) const {
			if (imageView_ < other.imageView_) return true; else if (imageView_ > other.imageView_) return false;
			if (sampler_ < other.sampler_) return true; else if (sampler_ > other.sampler_) return false;
			if (secondaryImageView_ < other.secondaryImageView_) return true; else if (secondaryImageView_ > other.secondaryImageView_) return false;
			if (base_ < other.base_) return true; else if (base_ > other.base_) return false;
			if (light_ < other.light_) return true; else if (light_ > other.light_) return false;
			if (bone_ < other.bone_) return true; else if (bone_ > other.bone_) return false;
			return false;
		}
	};

	// We alternate between these.
	struct FrameData {
		VkDescriptorPool descPool;
		VulkanPushBuffer *pushUBO;
		VulkanPushBuffer *pushVertex;
		VulkanPushBuffer *pushIndex;
		// We do rolling allocation and reset instead of caching across frames. That we might do later.
		std::map<DescriptorSetKey, VkDescriptorSet> descSets;
	};

	int curFrame_;
	FrameData frame_[2];

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

	// This is always set to the current main command buffer of the VulkanContext.
	// In the future, we may support flushing mid-frame and more fine grained command buffer usage,
	// but for now, let's just submit a whole frame at a time. This is not compatible with some games
	// that do mid frame read-backs.
	VkCommandBuffer cmd_;

	// Vertex collector state
	IndexGenerator indexGen;
	GEPrimitiveType prevPrim_;

	u32 lastVTypeID_;

	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;

	// Other
	ShaderManagerVulkan *shaderManager_;
	PipelineManagerVulkan *pipelineManager_;
	TextureCacheVulkan *textureCache_;
	FramebufferManagerVulkan *framebufferManager_;

	VkSampler depalSampler_;

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };

	// State cache
	uint32_t dirtyUniforms_;
	uint32_t baseUBOOffset;
	uint32_t lightUBOOffset;
	uint32_t boneUBOOffset;
	VkBuffer baseBuf, lightBuf, boneBuf;
	VkImageView imageView;
	VkSampler sampler;

	// Null texture
	VulkanTexture *nullTexture_;
	VkSampler nullSampler_;

	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls;
	int vertexCountInDrawCalls;
	UVScale *uvScale;

	bool fboTexNeedBind_;
	bool fboTexBound_;

	DrawEngineVulkanStats stats_;
};
