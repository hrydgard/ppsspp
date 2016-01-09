#pragma once

#define VK_PROTOTYPES
#include "ext/vulkan/vulkan.h"

#include <cstring>

class FramebufferManagerVulkan;

struct VulkanDynamicState {
	VkViewport viewport;
	VkRect2D scissor;
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
// ~54 bits.
// Can't use enums unfortunately, they end up signed and breaking values above half their ranges.
struct VulkanPipelineRasterStateKey {
	// Blend
	bool blendEnable : 1;
	unsigned int srcColor : 5;  // VkBlendFactor
	unsigned int destColor : 5;  // VkBlendFactor
	unsigned int srcAlpha : 5;  // VkBlendFactor
	unsigned int destAlpha : 5;  // VkBlendFactor
	unsigned int blendOpColor : 3;  // VkBlendOp
	unsigned int blendOpAlpha : 3;  // VkBlendOp
	bool logicOpEnable : 1;
	unsigned int logicOp : 4;  // VkLogicOp
	int colorWriteMask : 4;

	// Depth/Stencil
	bool depthTestEnable : 1;
	bool depthWriteEnable : 1;
	unsigned int depthCompareOp : 3;  // VkCompareOp 
	bool stencilTestEnable : 1;
	unsigned int stencilCompareOp : 4;  // VkCompareOp
	unsigned int stencilPassOp : 4; // VkStencilOp
	unsigned int stencilFailOp : 4; //VkStencilOp 
	unsigned int stencilDepthFailOp : 4;  // VkStencilOp 

	// We'll use dynamic state for writemask, reference and comparemask to start with,
	// and viewport/scissor.

	// Rasterizer
	unsigned int cullMode : 2;  // VkCullModeFlagBits 
	unsigned int topology : 4;  // VkPrimitiveTopology 

	bool operator < (const VulkanPipelineRasterStateKey &other) const {
		return memcmp(this, &other, sizeof(*this)) < 0;
	}
};


void ConvertStateToVulkanKey(FramebufferManagerVulkan &fbManager, int prim, VulkanPipelineRasterStateKey &key, VulkanDynamicState &dynState);
