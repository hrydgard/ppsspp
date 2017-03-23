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
#include "GPU/Vulkan/StateMappingVulkan.h"

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

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);

	void SetupVertexDecoder(u32 vertType);
	void SetupVertexDecoderInternal(u32 vertType);

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
		return frame_[curFrame_ & 1].pushUBO;
	}

	const DrawEngineVulkanStats &GetStats() const {
		return stats_;
	}

private:
	struct FrameData;

	void ConvertStateToVulkanKey(FramebufferManagerVulkan &fbManager, ShaderManagerVulkan *shaderManager, int prim, VulkanDynamicState &dynState, bool overrideStencilRef, uint8_t stencilRef);
	void ApplyStateLate();

	void InitDeviceObjects();
	void DestroyDeviceObjects();

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

		void Destroy(VulkanContext *vulkan);
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
	ShaderManagerVulkan *shaderManager_ = nullptr;
	PipelineManagerVulkan *pipelineManager_ = nullptr;
	TextureCacheVulkan *textureCache_ = nullptr;
	FramebufferManagerVulkan *framebufferManager_ = nullptr;

	// Current pipeline key. Updated progressively.
	VulkanPipelineRasterStateKey key_{};

	VkSampler depalSampler_;

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };

	// State cache
	uint64_t dirtyUniforms_;
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
	UVScale uvScale[MAX_DEFERRED_DRAW_CALLS];

	DrawEngineVulkanStats stats_;

	// Hardware tessellation
	class TessellationDataTransferVulkan : public TessellationDataTransfer {
	private:
		VulkanContext *vulkan;
		VulkanTexture *data_tex[3];
		VkSampler sampler;
	public:
		TessellationDataTransferVulkan(VulkanContext *vulkan) 
			: TessellationDataTransfer(), vulkan(vulkan), data_tex(), sampler() {
			for (int i = 0; i < 3; i++)
				data_tex[i] = new VulkanTexture(vulkan);

			CreateSampler();
		}
		~TessellationDataTransferVulkan() {
			for (int i = 0; i < 3; i++)
				delete data_tex[i];

			vulkan->Delete().QueueDeleteSampler(sampler);
		}
		void SendDataToShader(const float *pos, const float *tex, const float *col, int size, bool hasColor, bool hasTexCoords) override;
		VulkanTexture *GetTexture(int i) const { return data_tex[i]; }
		VkSampler GetSampler() const { return sampler; }
		void CreateSampler() {
			VkSamplerCreateInfo samp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samp.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			samp.compareOp = VK_COMPARE_OP_NEVER;
			samp.flags = 0;
			samp.magFilter =VK_FILTER_NEAREST;
			samp.minFilter = VK_FILTER_NEAREST;
			samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

			if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY) && g_Config.iAnisotropyLevel > 0) {
				// Docs say the min of this value and the supported max are used.
				samp.maxAnisotropy = 1 << g_Config.iAnisotropyLevel;
				samp.anisotropyEnable = true;
			} else {
				samp.maxAnisotropy = 1.0f;
				samp.anisotropyEnable = false;
			}

			samp.maxLod = 1.0f;
			samp.minLod = 0.0f;
			samp.mipLodBias = 0.0f;

			VkResult res = vkCreateSampler(vulkan->GetDevice(), &samp, nullptr, &sampler);
			assert(res == VK_SUCCESS);
		}
	};
};
