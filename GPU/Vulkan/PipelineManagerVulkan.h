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
	uint32_t vtxDecId;
	bool useHWTransform;

	void ToString(std::string *str) const {
		str->resize(sizeof(*this));
		memcpy(&(*str)[0], this, sizeof(*this));
	}
	void FromString(const std::string &str) {
		memcpy(this, &str[0], sizeof(*this));
	}
};

enum {
	UB_VS_FS_BASE = (1 << 0),
	UB_VS_BONES = (1 << 1),
	UB_VS_LIGHTS = (1 << 2),
};

// Simply wraps a Vulkan pipeline, providing some metadata.
struct VulkanPipeline {
	VkPipeline pipeline;
	int uniformBlocks;  // UB_ enum above.
	bool useBlendConstant;
	bool usesLines;
};

class VulkanContext;
class VulkanVertexShader;
class VulkanFragmentShader;

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

private:
	DenseHashMap<VulkanPipelineKey, VulkanPipeline *, nullptr> pipelines_;
	VkPipelineCache pipelineCache_;
	VulkanContext *vulkan_;
	float lineWidth_ = 1.0f;
};
