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

#include "GPU/Vulkan/VulkanUtil.h"

#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Vulkan/StateMappingVulkan.h"
#include "GPU/Vulkan/VulkanRenderManager.h"


// TODO: Move to some appropriate header.
#ifdef _MSC_VER
#define NO_INLINE __declspec(noinline)
#else
#define NO_INLINE __attribute__((noinline))
#endif

struct DecVtxFormat;
struct UVScale;

class ShaderManagerVulkan;
class PipelineManagerVulkan;
class TextureCacheVulkan;
class FramebufferManagerVulkan;

class VulkanContext;
class VulkanPushPool;
struct VulkanPipeline;

struct DrawEngineVulkanStats {
	int pushVertexSpaceUsed;
	int pushIndexSpaceUsed;
};

class VulkanRenderManager;

class TessellationDataTransferVulkan : public TessellationDataTransfer  {
public:
	TessellationDataTransferVulkan(VulkanContext *vulkan) : vulkan_(vulkan) {}

	void SetPushPool(VulkanPushPool *push) { push_ = push; }
	// Send spline/bezier's control points and weights to vertex shader through structured shader buffer.
	void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) override;
	const VkDescriptorBufferInfo *GetBufferInfo() { return bufInfo_; }
private:
	VulkanContext *vulkan_;
	VulkanPushPool *push_;  // Updated each frame.
	VkDescriptorBufferInfo bufInfo_[3]{};
};

enum {
	DRAW_BINDING_TEXTURE = 0,
	DRAW_BINDING_2ND_TEXTURE = 1,
	DRAW_BINDING_DEPAL_TEXTURE = 2,
	DRAW_BINDING_DYNUBO_BASE = 3,
	DRAW_BINDING_DYNUBO_LIGHT = 4,
	DRAW_BINDING_DYNUBO_BONE = 5,
	DRAW_BINDING_TESS_STORAGE_BUF = 6,
	DRAW_BINDING_TESS_STORAGE_BUF_WU = 7,
	DRAW_BINDING_TESS_STORAGE_BUF_WV = 8,
	DRAW_BINDING_COUNT = 9,
};

// Handles transform, lighting and drawing.
class DrawEngineVulkan : public DrawEngineCommon {
public:
	DrawEngineVulkan(Draw::DrawContext *draw);
	~DrawEngineVulkan();

	// We reference feature flags, so this is called after construction.
	void InitDeviceObjects();

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

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

	// So that this can be inlined
	void Flush() {
		if (!numDrawInds_)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawInds_)
			return;
		// Decode any pending vertices. And also flush while we're at it, for simplicity.
		// It might be possible to only decode like in the other backends, but meh, it can't matter.
		// Issue #10095 has a nice example of where this is required.
		DoFlush();
	}

	void DispatchFlush() override {
		if (!numDrawInds_)
			return;
		DoFlush();
	}

	VKRPipelineLayout *GetPipelineLayout() const {
		return pipelineLayout_;
	}

	void BeginFrame();
	void EndFrame();

	void DirtyAllUBOs();

	void DirtyPipeline() {
		lastPipeline_ = nullptr;
	}

	VulkanPushPool *GetPushBufferForTextureData() {
		return pushUBO_;
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
	void Invalidate(InvalidationCallbackFlags flags);

	void ApplyDrawStateLate(VulkanRenderManager *renderManager, bool applyStencilRef, uint8_t stencilRef, bool useBlendConstant);
	void ConvertStateToVulkanKey(FramebufferManagerVulkan &fbManager, ShaderManagerVulkan *shaderManager, int prim, VulkanPipelineRasterStateKey &key, VulkanDynamicState &dynState);
	void BindShaderBlendTex();

	void DestroyDeviceObjects();

	void DoFlush();
	void UpdateUBOs();

	NO_INLINE void ResetAfterDraw();

	Draw::DrawContext *draw_;

	// We use a shared descriptor set layouts for all PSP draws.
	VKRPipelineLayout *pipelineLayout_ = nullptr;
	VulkanPipeline *lastPipeline_ = nullptr;
	VkDescriptorSet lastDs_ = VK_NULL_HANDLE;

	// Secondary texture for shader blending
	VkImageView boundSecondary_ = VK_NULL_HANDLE;

	// CLUT texture for shader depal
	VkImageView boundDepal_ = VK_NULL_HANDLE;
	bool boundDepalSmoothed_ = false;
	VkSampler samplerSecondaryLinear_ = VK_NULL_HANDLE;
	VkSampler samplerSecondaryNearest_ = VK_NULL_HANDLE;

	struct DescriptorSetKey {
		VkImageView imageView_;
		VkImageView secondaryImageView_;
		VkImageView depalImageView_;
		VkSampler sampler_;
		VkBuffer base_, light_, bone_;  // All three UBO slots will be set to this. This will usually be identical
		// for all draws in a frame, except when the buffer has to grow.
	};

	GEPrimitiveType lastPrim_ = GE_PRIM_INVALID;

	// This one's not accurately named, it's used for all kinds of stuff that's not vertices or indices.
	VulkanPushPool *pushUBO_ = nullptr;

	VulkanPushPool *pushVertex_ = nullptr;
	VulkanPushPool *pushIndex_ = nullptr;

	// Other
	ShaderManagerVulkan *shaderManager_ = nullptr;
	PipelineManagerVulkan *pipelineManager_ = nullptr;
	TextureCacheVulkan *textureCache_ = nullptr;
	FramebufferManagerVulkan *framebufferManager_ = nullptr;

	// State cache
	uint64_t dirtyUniforms_ = 0;
	uint32_t baseUBOOffset = 0;
	uint32_t lightUBOOffset = 0;
	uint32_t boneUBOOffset = 0;
	VkBuffer baseBuf = VK_NULL_HANDLE;
	VkBuffer lightBuf = VK_NULL_HANDLE;
	VkBuffer boneBuf = VK_NULL_HANDLE;
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
};
