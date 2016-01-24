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

#include <map>

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
	bool useHWTransform;
	const VertexDecoder *vtxDec;
	VkShaderModule vShader;
	VkShaderModule fShader;

	bool operator < (const VulkanPipelineKey &other) const {
		if (raster < other.raster) return true; else if (other.raster < raster) return false;
		if (useHWTransform < other.useHWTransform) return true; else if (other.useHWTransform < useHWTransform) return false;
		if (vtxDec < other.vtxDec) return true; else if (other.vtxDec < vtxDec) return false;
		if (vShader < other.vShader) return true; else if (other.vShader < vShader) return false;
		if (fShader < other.fShader) return true; else if (other.fShader < fShader) return false;
		return false;
	}
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
};

class VulkanContext;
class VulkanVertexShader;
class VulkanFragmentShader;

class PipelineManagerVulkan {
public:
	PipelineManagerVulkan(VulkanContext *ctx);
	~PipelineManagerVulkan();

	VulkanPipeline *GetOrCreatePipeline(VkPipelineLayout layout, const VulkanPipelineRasterStateKey &rasterKey, const VertexDecoder *vtxDec, VulkanVertexShader *vs, VulkanFragmentShader *fs, bool useHwTransform);
	int GetNumPipelines() const { return (int)pipelines_.size(); }

	void Clear();

	std::string DebugGetObjectString(std::string id, DebugShaderType type, DebugShaderStringType stringType);
	std::vector<std::string> DebugGetObjectIDs(DebugShaderType type);

private:
	std::map<VulkanPipelineKey, VulkanPipeline *> pipelines_;
	VkPipelineCache pipelineCache_;
	VulkanContext *vulkan_;
};
