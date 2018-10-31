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
#include "Common/Hashmaps.h"

#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "GPU/Vulkan/StateMappingVulkan.h"

// PSP vertex format.
enum class PspAttributeLocation {
	POSITION = 0,
	TEXCOORD = 1,
	NORMAL = 2,
	W1 = 3,
	W2 = 4,
	COLOR0 = 5,
	COLOR1 = 6,

	COUNT
};

struct VulkanPipelineKey {
	VulkanPipelineRasterStateKey raster;  // prim is included here
	VkRenderPass renderPass;
	VkShaderModule vShader;
	VkShaderModule fShader;
	uint32_t vtxFmtId;
	bool useHWTransform;

	void ToString(std::string *str) const {
		str->resize(sizeof(*this));
		memcpy(&(*str)[0], this, sizeof(*this));
	}
	void FromString(const std::string &str) {
		memcpy(this, &str[0], sizeof(*this));
	}
	std::string GetDescription(DebugShaderStringType stringType) const;
};

enum PipelineFlags {
	PIPELINE_FLAG_USES_LINES = (1 << 2),
	PIPELINE_FLAG_USES_BLEND_CONSTANT = (1 << 3),
};

// Simply wraps a Vulkan pipeline, providing some metadata.
struct VulkanPipeline {
	VkPipeline pipeline;
	int flags;  // PipelineFlags enum above.

	// Convenience.
	bool UsesBlendConstant() const { return (flags & PIPELINE_FLAG_USES_BLEND_CONSTANT) != 0; }
	bool UsesLines() const { return (flags & PIPELINE_FLAG_USES_LINES) != 0; }
};

class VulkanContext;
class VulkanVertexShader;
class VulkanFragmentShader;
class ShaderManagerVulkan;
class DrawEngineCommon;

class PipelineManagerVulkan {
public:
	PipelineManagerVulkan(VulkanContext *ctx);
	~PipelineManagerVulkan();

	VulkanPipeline *GetOrCreatePipeline(VkPipelineLayout layout, VkRenderPass renderPass, const VulkanPipelineRasterStateKey &rasterKey, const DecVtxFormat *decFmt, VulkanVertexShader *vs, VulkanFragmentShader *fs, bool useHwTransform);
	int GetNumPipelines() const { return (int)pipelines_.size(); }

	void Clear();

	void SetLineWidth(float lw);

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);

	std::string DebugGetObjectString(std::string id, DebugShaderType type, DebugShaderStringType stringType);
	std::vector<std::string> DebugGetObjectIDs(DebugShaderType type);

	// Saves data for faster creation next time.
	void SaveCache(FILE *file, bool saveRawPipelineCache, ShaderManagerVulkan *shaderManager, Draw::DrawContext *drawContext);
	bool LoadCache(FILE *file, bool loadRawPipelineCache, ShaderManagerVulkan *shaderManager, Draw::DrawContext *drawContext, VkPipelineLayout layout);
	void CancelCache();

private:
	DenseHashMap<VulkanPipelineKey, VulkanPipeline *, nullptr> pipelines_;
	VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
	VulkanContext *vulkan_;
	float lineWidth_ = 1.0f;
	bool cancelCache_ = false;
};
