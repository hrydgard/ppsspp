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
// * binding 1: Secondary texture sampler for shader blending
// * binding 2: Depal palette
// * binding 3: Base Uniform Buffer (includes fragment state)
// * binding 4: Light uniform buffer
// * binding 5: Bone uniform buffer
// * binding 6: Tess data storage buffer
//
// All shaders conform to this layout, so they are all compatible with the same descriptor set.
// The format of the various uniform buffers may vary though - vertex shaders that don't skin
// won't get any bone data, etc.

#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"

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

	enum VAIStatus : uint8_t {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	uint64_t hash;
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
	VAIStatus status = VAI_NEW;

	// ID information
	int numDraws = 0;
	int numFrames = 0;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash = 0;
	u8 flags = 0;
};

class VulkanRenderManager;

class TessellationDataTransferVulkan : public TessellationDataTransfer  {
public:
	TessellationDataTransferVulkan(VulkanContext *vulkan) : vulkan_(vulkan) {}

	void SetPushBuffer(VulkanPushBuffer *push) { push_ = push; }
	// Send spline/bezier's control points and weights to vertex shader through structured shader buffer.
	void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) override;
	const VkDescriptorBufferInfo *GetBufferInfo() { return bufInfo_; }
private:
	VulkanContext *vulkan_;
	VulkanPushBuffer *push_;  // Updated each frame.
	VkDescriptorBufferInfo bufInfo_[3]{};
};

// Handles transform, lighting and drawing.
class DrawEngineVulkan : public DrawEngineCommon {
public:
	DrawEngineVulkan(Draw::DrawContext *draw);
	virtual ~DrawEngineVulkan();

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
	void DeviceRestore(Draw::DrawContext *draw);

	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		// Decode any pending vertices. And also flush while we're at it, for simplicity.
		// It might be possible to only decode like in the other backends, but meh, it can't matter.
		// Issue #10095 has a nice example of where this is required.
		DoFlush();
	}

	void DispatchFlush() override { Flush(); }

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
		return GetCurFrame().pushUBO;
	}

	const DrawEngineVulkanStats &GetStats() const {
		return stats_;
	}

	void SetDepalTexture(VkImageView depal, bool smooth) {
		if (boundDepal_ != depal) {
			boundDepal_ = depal;
			boundDepalSmoothed_ = smooth;
			gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}
	}

private:
	struct FrameData;
	void ApplyDrawStateLate(VulkanRenderManager *renderManager, bool applyStencilRef, uint8_t stencilRef, bool useBlendConstant);
	void ConvertStateToVulkanKey(FramebufferManagerVulkan &fbManager, ShaderManagerVulkan *shaderManager, int prim, VulkanPipelineRasterStateKey &key, VulkanDynamicState &dynState);
	void BindShaderBlendTex();

	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void DecodeVertsToPushBuffer(VulkanPushBuffer *push, uint32_t *bindOffset, VkBuffer *vkbuf);

	void DoFlush();
	void UpdateUBOs(FrameData *frame);
	FrameData &GetCurFrame();

	VkDescriptorSet GetOrCreateDescriptorSet(VkImageView imageView, VkSampler sampler, VkBuffer base, VkBuffer light, VkBuffer bone, bool tess);

	Draw::DrawContext *draw_;

	// We use a single descriptor set layout for all PSP draws.
	VkDescriptorSetLayout descriptorSetLayout_;
	VkPipelineLayout pipelineLayout_;
	VulkanPipeline *lastPipeline_;
	VkDescriptorSet lastDs_ = VK_NULL_HANDLE;

	// Secondary texture for shader blending
	VkImageView boundSecondary_ = VK_NULL_HANDLE;
	// CLUT texture for shader depal
	VkImageView boundDepal_ = VK_NULL_HANDLE;
	bool boundDepalSmoothed_ = false;
	VkSampler samplerSecondaryLinear_ = VK_NULL_HANDLE;
	VkSampler samplerSecondaryNearest_ = VK_NULL_HANDLE;

	PrehashMap<VertexArrayInfoVulkan *, nullptr> vai_;
	VulkanPushBuffer *vertexCache_;
	int descDecimationCounter_ = 0;

	struct DescriptorSetKey {
		VkImageView imageView_;
		VkImageView secondaryImageView_;
		VkImageView depalImageView_;
		VkSampler sampler_;
		VkBuffer base_, light_, bone_;  // All three UBO slots will be set to this. This will usually be identical
		// for all draws in a frame, except when the buffer has to grow.
		bool secondaryIsInputAttachment;
	};

	// We alternate between these.
	struct FrameData {
		FrameData() : descSets(512), descPool("DrawEngine", true) {
			descPool.Setup([this] { descSets.Clear(); });
		}

		VulkanDescSetPool descPool;

		VulkanPushBuffer *pushUBO = nullptr;
		VulkanPushBuffer *pushVertex = nullptr;
		VulkanPushBuffer *pushIndex = nullptr;

		// We do rolling allocation and reset instead of caching across frames. That we might do later.
		DenseHashMap<DescriptorSetKey, VkDescriptorSet, (VkDescriptorSet)VK_NULL_HANDLE> descSets;

		void Destroy(VulkanContext *vulkan);
	};

	GEPrimitiveType lastPrim_ = GE_PRIM_INVALID;
	FrameData frame_[VulkanContext::MAX_INFLIGHT_FRAMES];

	// Other
	ShaderManagerVulkan *shaderManager_ = nullptr;
	PipelineManagerVulkan *pipelineManager_ = nullptr;
	TextureCacheVulkan *textureCache_ = nullptr;
	FramebufferManagerVulkan *framebufferManager_ = nullptr;

	// State cache
	uint64_t dirtyUniforms_;
	uint32_t baseUBOOffset;
	uint32_t lightUBOOffset;
	uint32_t boneUBOOffset;
	VkBuffer baseBuf, lightBuf, boneBuf;
	VkImageView imageView = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;

	// For null texture
	VkSampler nullSampler_ = VK_NULL_HANDLE;

	DrawEngineVulkanStats stats_{};

	VulkanPipelineRasterStateKey pipelineKey_{};
	VulkanDynamicState dynState_{};

	int tessOffset_ = 0;
	FBOTexState fboTexBindState_ = FBO_TEX_NONE;

	// Hardware tessellation
	TessellationDataTransferVulkan *tessDataTransferVulkan;

	int lastRenderStepId_ = -1;
};
