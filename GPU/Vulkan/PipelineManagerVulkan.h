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

#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Vulkan/VulkanUtil.h"

// The Descriptor Set used for the majority of PSP rendering looks like this:
//
// * binding 0: Vertex Data (up to 7 locations as defined in PspAttributeLocation)
// * binding 1: Texture Sampler (the PSP texture)
// * binding 2: Secondary texture sampler for shader blending or depal palettes
// * binding 3: Vertex Uniform Buffer
// * binding 4: Fragment Uniform Buffer
//
// All shaders conform to this layout, so they are all compatible with the same descriptor set.
// The format of the various uniform buffers may vary though - vertex shaders that don't skin
// won't get any bone data, etc.

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
	// We'll use dynamic state for writemask, reference and comparemask to start with.

	// Rasterizer
	VkCullModeFlagBits cullMode : 2;
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

};
