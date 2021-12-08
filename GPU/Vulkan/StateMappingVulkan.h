#pragma once

#include "Common/GPU/Vulkan/VulkanLoader.h"

#include <cstring>

class FramebufferManagerVulkan;

struct ScissorRect {
	int x, y;
	int width, height;
};

struct VulkanDynamicState {
	VkViewport viewport;
	ScissorRect scissor;
	bool useBlendColor;
	uint32_t blendColor;
	bool useStencil;
	uint8_t stencilRef;
	uint8_t stencilWriteMask;
	uint8_t stencilCompareMask;
};

// Let's pack this tight using bitfields.
// If an enable flag is set to 0, all the data fields for that section should
// also be set to 0.
// ~64 bits.
// Can't use enums unfortunately, they end up signed and breaking values above half their ranges.
struct VulkanPipelineRasterStateKey {
	// Blend
	unsigned int blendEnable : 1;
	unsigned int srcColor : 5;  // VkBlendFactor
	unsigned int destColor : 5;  // VkBlendFactor
	unsigned int srcAlpha : 5;  // VkBlendFactor
	unsigned int destAlpha : 5;  // VkBlendFactor
	// bool useBlendConstant : 1;  // sacrifice a bit to cheaply check if we need to update the blend color
	unsigned int blendOpColor : 3;  // VkBlendOp
	unsigned int blendOpAlpha : 3;  // VkBlendOp
	unsigned int logicOpEnable : 1;
	unsigned int logicOp : 4;  // VkLogicOp
	unsigned int colorWriteMask : 4;

	// Depth/Stencil
	unsigned int depthClampEnable : 1;
	unsigned int depthTestEnable : 1;
	unsigned int depthWriteEnable : 1;
	unsigned int depthCompareOp : 3;  // VkCompareOp 
	unsigned int stencilTestEnable : 1;
	unsigned int stencilCompareOp : 3;  // VkCompareOp
	unsigned int stencilPassOp : 4; // VkStencilOp
	unsigned int stencilFailOp : 4; // VkStencilOp 
	unsigned int stencilDepthFailOp : 4;  // VkStencilOp 

	// We'll use dynamic state for writemask, reference and comparemask to start with,
	// and viewport/scissor.

	// Rasterizer
	unsigned int cullMode : 2;  // VkCullModeFlagBits 
	unsigned int topology : 4;  // VkPrimitiveTopology 

	bool operator < (const VulkanPipelineRasterStateKey &other) const {
		size_t size = sizeof(VulkanPipelineRasterStateKey);
		return memcmp(this, &other, size) < 0;
	}
};
