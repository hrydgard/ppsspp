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

// Let's pack this tight using bitfields.
// If an enable flag is set to 0, all the data fields for that section should
// also be set to 0.
// ~54 bits.
struct VulkanPipelineRasterStateKey {
	// Blend
	bool blendEnable : 1;
	VkBlendFactor srcColor : 5;
	VkBlendFactor destColor : 5;
	VkBlendFactor srcAlpha : 5;
	VkBlendFactor destAlpha : 5;
	VkBlendOp blendOpColor : 3;
	VkBlendOp blendOpAlpha : 3;
	bool logicOpEnable : 1;
	VkLogicOp logicOp : 4;
	int colorWriteMask : 4;

	// Depth/Stencil
	bool depthTestEnable : 1;
	bool depthWriteEnable : 1;
	VkCompareOp depthCompareOp : 3;
	bool stencilTestEnable : 1;
	VkCompareOp stencilCompareOp : 4;
	VkStencilOp stencilPassOp : 4;
	VkStencilOp stencilFailOp : 4;
	VkStencilOp stencilDepthFailOp : 4;
	// We'll use dynamic state for writemask, reference and comparemask to start with,
	// and viewport/scissor.

	// Rasterizer
	VkCullModeFlagBits cullMode : 2;
	VkPrimitiveTopology topology : 4;

	bool operator < (const VulkanPipelineRasterStateKey &other) const {
		return memcmp(this, &other, sizeof(*this)) < 0;
	}
};

// All the information needed. All PSP rendering (except full screen clears?) will make use of a single
// render pass definition.
struct VulkanPipelineKey {
	VulkanPipelineRasterStateKey raster;
	int prim;
	bool pretransformed;
	uint32_t vertType;
	ShaderID vShaderId;
	ShaderID fShaderId;

	bool operator < (const VulkanPipelineKey &other) const {
		if (raster < other.raster) return true; else if (other.raster < raster) return false;
		if (prim < other.prim) return true; else if (other.prim < prim) return false;
		if (pretransformed < other.pretransformed) return true; else if (other.pretransformed < pretransformed) return false;
		if (vertType < other.vertType) return true; else if (other.vertType < vertType) return false;
		if (vShaderId < other.vShaderId) return true; else if (other.vShaderId < vShaderId) return false;
		if (fShaderId < other.fShaderId) return true; else if (other.fShaderId < fShaderId) return false;
		return false;
	}
};

enum {
	UB_VS_BASICTRANSFORM = (1 << 0),
	UB_VS_BONES = (1 << 1),
	UB_VS_LIGHTS = (1 << 2),
};

// Simply wraps a Vulkan pipeline, providing some metadata.
struct VulkanPipeline {
	VkPipeline pipeline;
	int uniformBlocks;  // UB_ enum above.
};

class PipelineManagerVulkan {
public:
	PipelineManagerVulkan(VulkanContext *ctx);
	~PipelineManagerVulkan();

private:
	std::map<VulkanPipelineKey, VkPipeline> pipelines_;
	VkPipelineCache pipelineCache_;
	VulkanContext *vulkan_;
};
