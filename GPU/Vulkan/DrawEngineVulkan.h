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

#include "Common/Hashmaps.h"

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
struct VulkanPipeline;

struct DrawEngineVulkanStats {
	int pushUBOSpaceUsed;
	int pushVertexSpaceUsed;
	int pushIndexSpaceUsed;
};

enum {
	VAIVULKAN_FLAG_VERTEXFULLALPHA = 1,
};

// Try to keep this POD.
class VertexArrayInfoVulkan {
public:
	VertexArrayInfoVulkan() {
		lastFrame = gpuStats.numFlips;
	}
	// No destructor needed - we always fully wipe.

	enum Status : uint8_t {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	ReliableHashType hash;
	u32 minihash;

	// These will probably always be the same, but whatever.
	VkBuffer vb = VK_NULL_HANDLE;
	VkBuffer ib = VK_NULL_HANDLE;
	// Offsets into the cache buffer.
	uint32_t vbOffset = 0;
	uint32_t ibOffset = 0;

	// Precalculated parameter for vkDrawIndexed
	u16 numVerts = 0;
	u16 maxIndex = 0;
	s8 prim = GE_PRIM_INVALID;
	Status status = VAI_NEW;

	// ID information
	int numDraws = 0;
	int numFrames = 0;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash = 0;
	u8 flags = 0;
};

class VulkanRenderManager;

// Handles transform, lighting and drawing.
class DrawEngineVulkan : public DrawEngineCommon {
public:
	DrawEngineVulkan(VulkanContext *vulkan, Draw::DrawContext *draw);
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
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void DispatchFlush() override { Flush(); }
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) override {
		SubmitPrim(verts, inds, prim, vertexCount, vertType, bytesRead);
	}

	VkPipelineLayout GetPipelineLayout() const {
		return pipelineLayout_;
	}

	void BeginFrame();
	void EndFrame();

	void DirtyAllUBOs();

	void DirtyPipeline() {
		lastPipeline_ = nullptr;
	}

	VulkanPushBuffer *GetPushBufferForTextureData() {
		return frame_[curFrame_].pushUBO;
	}

	const DrawEngineVulkanStats &GetStats() const {
		return stats_;
	}

private:
	struct FrameData;
	void ApplyDrawStateLate(VulkanRenderManager *renderManager, bool applyStencilRef, uint8_t stencilRef, bool useBlendConstant);
	void ConvertStateToVulkanKey(FramebufferManagerVulkan &fbManager, ShaderManagerVulkan *shaderManager, int prim, VulkanPipelineRasterStateKey &key, VulkanDynamicState &dynState);

	void InitDeviceObjects();
	void DestroyDeviceObjects();

	int ComputeNumVertsToDecode() const;
	void DecodeVerts(VulkanPushBuffer *push, uint32_t *bindOffset, VkBuffer *vkbuf);

	void DoFlush();
	void UpdateUBOs(FrameData *frame);

	VkDescriptorSet GetOrCreateDescriptorSet(VkImageView imageView, VkSampler sampler, VkBuffer base, VkBuffer light, VkBuffer bone);

	VulkanContext *vulkan_;
	Draw::DrawContext *draw_;

	// We use a single descriptor set layout for all PSP draws.
	VkDescriptorSetLayout descriptorSetLayout_;
	VkPipelineLayout pipelineLayout_;
	VulkanPipeline *lastPipeline_;
	VkDescriptorSet lastDs_ = VK_NULL_HANDLE;

	PrehashMap<VertexArrayInfoVulkan *, nullptr> vai_;
	VulkanPushBuffer *vertexCache_;
	int decimationCounter_ = 0;
	int descDecimationCounter_ = 0;

	struct DescriptorSetKey {
		VkImageView imageView_;
		VkImageView secondaryImageView_;
		VkSampler sampler_;
		VkBuffer base_, light_, bone_;  // All three UBO slots will be set to this. This will usually be identical
		// for all draws in a frame, except when the buffer has to grow.
	};

	// We alternate between these.
	struct FrameData {
		FrameData() : descSets(1024) {}

		VkDescriptorPool descPool;
		VulkanPushBuffer *pushUBO;
		VulkanPushBuffer *pushVertex;
		VulkanPushBuffer *pushIndex;
		// We do rolling allocation and reset instead of caching across frames. That we might do later.
		DenseHashMap<DescriptorSetKey, VkDescriptorSet, (VkDescriptorSet)VK_NULL_HANDLE> descSets;

		void Destroy(VulkanContext *vulkan);
	};

	GEPrimitiveType lastPrim_ = GE_PRIM_INVALID;
	int curFrame_;
	FrameData frame_[VulkanContext::MAX_INFLIGHT_FRAMES];

	// Other
	ShaderManagerVulkan *shaderManager_ = nullptr;
	PipelineManagerVulkan *pipelineManager_ = nullptr;
	TextureCacheVulkan *textureCache_ = nullptr;
	FramebufferManagerVulkan *framebufferManager_ = nullptr;

	VkSampler depalSampler_;

	// State cache
	uint64_t dirtyUniforms_;
	uint32_t baseUBOOffset;
	uint32_t lightUBOOffset;
	uint32_t boneUBOOffset;
	VkBuffer baseBuf, lightBuf, boneBuf;
	VkImageView imageView;
	VkSampler sampler;

	// Null texture
	VulkanTexture *nullTexture_ = nullptr;
	VkSampler nullSampler_ = VK_NULL_HANDLE;

	DrawEngineVulkanStats stats_;

	VulkanPipelineRasterStateKey pipelineKey_{};
	VulkanDynamicState dynState_{};

	// Hardware tessellation
	class TessellationDataTransferVulkan : public TessellationDataTransfer {
	private:
		VulkanContext *vulkan_;
		Draw::DrawContext *draw_;
		VulkanTexture *data_tex[3];
		VkSampler sampler;
	public:
		TessellationDataTransferVulkan(VulkanContext *vulkan, Draw::DrawContext *draw) 
			: TessellationDataTransfer(), vulkan_(vulkan), draw_(draw), data_tex(), sampler() {
			for (int i = 0; i < 3; i++)
				data_tex[i] = new VulkanTexture(vulkan_);

			CreateSampler();
		}
		~TessellationDataTransferVulkan() {
			for (int i = 0; i < 3; i++)
				delete data_tex[i];

			vulkan_->Delete().QueueDeleteSampler(sampler);
		}
		void SendDataToShader(const float *pos, const float *tex, const float *col, int size, bool hasColor, bool hasTexCoords) override;
		void PrepareBuffers(float *&pos, float *&tex, float *&col, int size, bool hasColor, bool hasTexCoords) override;
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

			VkResult res = vkCreateSampler(vulkan_->GetDevice(), &samp, nullptr, &sampler);
			assert(res == VK_SUCCESS);
		}
	};
};
