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

#include <cstring>

#include "Common/Data/Collections/Hashmaps.h"
#include "Common/Thread/Promise.h"

#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "GPU/Vulkan/StateMappingVulkan.h"
#include "GPU/Vulkan/VulkanQueueRunner.h"
#include "GPU/Vulkan/VulkanRenderManager.h"

struct VKRGraphicsPipeline;
class VulkanRenderManager;
class VulkanContext;
class VulkanVertexShader;
class VulkanFragmentShader;
class VulkanGeometryShader;
class ShaderManagerVulkan;
class DrawEngineCommon;

struct VulkanPipelineKey {
	VulkanPipelineRasterStateKey raster;  // prim is included here
	VKRRenderPass *renderPass;
	Promise<VkShaderModule> *vShader;
	Promise<VkShaderModule> *fShader;
	Promise<VkShaderModule> *gShader;
	uint32_t vtxFmtId;
	bool useHWTransform;

	void ToString(std::string *str) const {
		str->resize(sizeof(*this));
		memcpy(&(*str)[0], this, sizeof(*this));
	}
	void FromString(const std::string &str) {
		memcpy(this, &str[0], sizeof(*this));
	}
	std::string GetDescription(DebugShaderStringType stringType, ShaderManagerVulkan *shaderManager) const;

private:
	std::string GetRasterStateDesc(bool lineBreaks) const;
};

// Simply wraps a Vulkan pipeline, providing some metadata.
struct VulkanPipeline {
	~VulkanPipeline() {
		desc->Release();
	}

	VKRGraphicsPipeline *pipeline;
	VKRGraphicsPipelineDesc *desc;
	PipelineFlags pipelineFlags;  // PipelineFlags enum above.

	bool UsesBlendConstant() const { return (pipelineFlags & PipelineFlags::USES_BLEND_CONSTANT) != 0; }
	bool UsesDepthStencil() const { return (pipelineFlags & PipelineFlags::USES_DEPTH_STENCIL) != 0; }
	bool UsesGeometryShader() const { return (pipelineFlags & PipelineFlags::USES_GEOMETRY_SHADER) != 0; }
	bool UsesDiscard() const { return (pipelineFlags & PipelineFlags::USES_DISCARD) != 0; }
	bool UsesFlatShading() const { return (pipelineFlags & PipelineFlags::USES_FLAT_SHADING) != 0; }

	u32 GetVariantsBitmask() const;
};

class PipelineManagerVulkan {
public:
	PipelineManagerVulkan(VulkanContext *ctx);
	~PipelineManagerVulkan();

	// variantMask is only used when loading pipelines from cache.
	VulkanPipeline *GetOrCreatePipeline(VulkanRenderManager *renderManager, VKRPipelineLayout *layout, const VulkanPipelineRasterStateKey &rasterKey, const DecVtxFormat *decFmt, VulkanVertexShader *vs, VulkanFragmentShader *fs, VulkanGeometryShader *gs, bool useHwTransform, u32 variantMask, int multiSampleLevel, bool cacheLoad);
	int GetNumPipelines() const { return (int)pipelines_.size(); }

	void Clear();

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);

	void InvalidateMSAAPipelines();

	std::string DebugGetObjectString(const std::string &id, DebugShaderType type, DebugShaderStringType stringType, ShaderManagerVulkan *shaderManager);
	std::vector<std::string> DebugGetObjectIDs(DebugShaderType type) const;

	// Saves data for faster creation next time.
	void SavePipelineCache(FILE *file, bool saveRawPipelineCache, ShaderManagerVulkan *shaderManager, Draw::DrawContext *drawContext);
	bool LoadPipelineCache(FILE *file, bool loadRawPipelineCache, ShaderManagerVulkan *shaderManager, Draw::DrawContext *drawContext, VKRPipelineLayout *layout, int multiSampleLevel);

private:
	DenseHashMap<VulkanPipelineKey, VulkanPipeline *> pipelines_;
	VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
	VulkanContext *vulkan_;
};
