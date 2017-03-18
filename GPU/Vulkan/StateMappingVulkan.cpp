// Copyright (c) 2012- PPSSPP Project.

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

#include "Common/Vulkan/VulkanLoader.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/GPUStateUtils.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
//#include "GPU/Vulkan/StateMappingVulkan.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
//#include "GPU/Vulkan/PixelShaderGeneratorVulkan.h"

// These tables all fit into u8s.
static const VkBlendFactor vkBlendFactorLookup[(size_t)BlendFactor::COUNT] = {
	VK_BLEND_FACTOR_ZERO,
	VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_DST_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	VK_BLEND_FACTOR_DST_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	VK_BLEND_FACTOR_CONSTANT_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
	VK_BLEND_FACTOR_CONSTANT_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
	VK_BLEND_FACTOR_SRC1_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
	VK_BLEND_FACTOR_SRC1_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
	VK_BLEND_FACTOR_MAX_ENUM,
};

static const VkBlendOp vkBlendEqLookup[(size_t)BlendEq::COUNT] = {
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
	VK_BLEND_OP_MIN,
	VK_BLEND_OP_MAX,
};

static const VkCullModeFlagBits cullingMode[] = {
	VK_CULL_MODE_BACK_BIT,
	VK_CULL_MODE_FRONT_BIT,
};

static const VkCompareOp compareOps[] = {
	VK_COMPARE_OP_NEVER,
	VK_COMPARE_OP_ALWAYS,
	VK_COMPARE_OP_EQUAL,
	VK_COMPARE_OP_NOT_EQUAL,
	VK_COMPARE_OP_LESS,
	VK_COMPARE_OP_LESS_OR_EQUAL,
	VK_COMPARE_OP_GREATER,
	VK_COMPARE_OP_GREATER_OR_EQUAL,
};

static const VkStencilOp stencilOps[] = {
	VK_STENCIL_OP_KEEP,
	VK_STENCIL_OP_ZERO,
	VK_STENCIL_OP_REPLACE,
	VK_STENCIL_OP_INVERT,
	VK_STENCIL_OP_INCREMENT_AND_CLAMP,
	VK_STENCIL_OP_DECREMENT_AND_CLAMP,
	VK_STENCIL_OP_KEEP, // reserved
	VK_STENCIL_OP_KEEP, // reserved
};

static const VkPrimitiveTopology primToVulkan[8] = {
	VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  // Vulkan doesn't do quads. We could do strips with restart-index though. We could also do RECT primitives in the geometry shader.
};

// These are actually the same exact values/order/etc. as the GE ones, but for clarity...
static const VkLogicOp logicOps[] = {
	VK_LOGIC_OP_CLEAR,
	VK_LOGIC_OP_AND,
	VK_LOGIC_OP_AND_REVERSE,
	VK_LOGIC_OP_COPY,
	VK_LOGIC_OP_AND_INVERTED,
	VK_LOGIC_OP_NO_OP,
	VK_LOGIC_OP_XOR,
	VK_LOGIC_OP_OR,
	VK_LOGIC_OP_NOR,
	VK_LOGIC_OP_EQUIVALENT,
	VK_LOGIC_OP_INVERT,
	VK_LOGIC_OP_OR_REVERSE,
	VK_LOGIC_OP_COPY_INVERTED,
	VK_LOGIC_OP_OR_INVERTED,
	VK_LOGIC_OP_NAND,
	VK_LOGIC_OP_SET,
};

void ResetShaderBlending() {
	//
}

// TODO: Do this more progressively. No need to compute the entire state if the entire state hasn't changed.
// In Vulkan, we simply collect all the state together into a "pipeline key" - we don't actually set any state here
// (the caller is responsible for setting the little dynamic state that is supported, dynState).
void DrawEngineVulkan::ConvertStateToVulkanKey(FramebufferManagerVulkan &fbManager, ShaderManagerVulkan *shaderManager, int prim, VulkanPipelineRasterStateKey &key, VulkanDynamicState &dynState) {
	memset(&key, 0, sizeof(key));
	memset(&dynState, 0, sizeof(dynState));
	// Unfortunately, this isn't implemented yet.
	gstate_c.allowShaderBlend = false;

	// Set blend - unless we need to do it in the shader.
	GenericBlendState blendState;
	ConvertBlendState(blendState, gstate_c.allowShaderBlend);

	if (blendState.applyShaderBlending) {
		if (ApplyShaderBlending()) {
			// We may still want to do something about stencil -> alpha.
			ApplyStencilReplaceAndLogicOp(blendState.replaceAlphaWithStencil, blendState);
		}
		else {
			// Until next time, force it off.
			ResetShaderBlending();
			gstate_c.allowShaderBlend = false;
		}
	}
	else if (blendState.resetShaderBlending) {
		ResetShaderBlending();
	}

	if (blendState.enabled) {
		key.blendEnable = true;
		key.blendOpColor = vkBlendEqLookup[(size_t)blendState.eqColor];
		key.blendOpAlpha = vkBlendEqLookup[(size_t)blendState.eqAlpha];
		key.srcColor = vkBlendFactorLookup[(size_t)blendState.srcColor];
		key.srcAlpha = vkBlendFactorLookup[(size_t)blendState.srcAlpha];
		key.destColor = vkBlendFactorLookup[(size_t)blendState.dstColor];
		key.destAlpha = vkBlendFactorLookup[(size_t)blendState.dstAlpha];
		if (blendState.dirtyShaderBlend) {
			gstate_c.Dirty(DIRTY_SHADERBLEND);
		}
		dynState.useBlendColor = blendState.useBlendColor;
		if (blendState.useBlendColor) {
			dynState.blendColor = blendState.blendColor;
		}
	}
	else {
		key.blendEnable = false;
		dynState.useBlendColor = false;
	}

	dynState.useStencil = false;

	// Set ColorMask/Stencil/Depth
	if (gstate.isModeClear()) {
		key.logicOpEnable = false;
		key.cullMode = VK_CULL_MODE_NONE;

		key.depthTestEnable = true;
		key.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		key.depthWriteEnable = gstate.isClearModeDepthMask();
		if (gstate.isClearModeDepthMask()) {
			fbManager.SetDepthUpdated();
		}

		// Color Test
		bool colorMask = gstate.isClearModeColorMask();
		bool alphaMask = gstate.isClearModeAlphaMask();
		key.colorWriteMask = (colorMask ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT) : 0) | (alphaMask ? VK_COLOR_COMPONENT_A_BIT : 0);

		// Stencil Test
		if (alphaMask) {
			key.stencilTestEnable = true;
			key.stencilCompareOp = VK_COMPARE_OP_ALWAYS;
			key.stencilPassOp = VK_STENCIL_OP_REPLACE;
			key.stencilFailOp = VK_STENCIL_OP_REPLACE;
			key.stencilDepthFailOp = VK_STENCIL_OP_REPLACE;
			dynState.useStencil = true;
			// In clear mode, the stencil value is set to the alpha value of the vertex.
			// A normal clear will be 2 points, the second point has the color.
			// We override this value in the pipeline from software transform for clear rectangles.
			dynState.stencilRef = 0xFF;
			dynState.stencilWriteMask = 0xFF;
		}
		else {
			key.stencilTestEnable = false;
			dynState.useStencil = false;
		}
	}
	else {
		if (gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP)) {
			// Logic Ops
			if (gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY) {
				key.logicOpEnable = true;
				key.logicOp = logicOps[gstate.getLogicOp()];
			}
			else {
				key.logicOpEnable = false;
			}
		}

		// Set cull
		bool wantCull = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		key.cullMode = wantCull ? (gstate.getCullMode() ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT) : VK_CULL_MODE_NONE;

		// Depth Test
		if (gstate.isDepthTestEnabled()) {
			key.depthTestEnable = true;
			key.depthCompareOp = compareOps[gstate.getDepthTestFunction()];
			key.depthWriteEnable = gstate.isDepthWriteEnabled();
			if (gstate.isDepthWriteEnabled()) {
				fbManager.SetDepthUpdated();
			}
		}
		else {
			key.depthTestEnable = false;
			key.depthWriteEnable = false;
			key.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		}

		// PSP color/alpha mask is per bit but we can only support per byte.
		// But let's do that, at least. And let's try a threshold.
		bool rmask = (gstate.pmskc & 0xFF) < 128;
		bool gmask = ((gstate.pmskc >> 8) & 0xFF) < 128;
		bool bmask = ((gstate.pmskc >> 16) & 0xFF) < 128;
		bool amask = (gstate.pmska & 0xFF) < 128;

#ifndef MOBILE_DEVICE
		u8 abits = (gstate.pmska >> 0) & 0xFF;
		u8 rbits = (gstate.pmskc >> 0) & 0xFF;
		u8 gbits = (gstate.pmskc >> 8) & 0xFF;
		u8 bbits = (gstate.pmskc >> 16) & 0xFF;
		if ((rbits != 0 && rbits != 0xFF) || (gbits != 0 && gbits != 0xFF) || (bbits != 0 && bbits != 0xFF)) {
			WARN_LOG_REPORT_ONCE(rgbmask, G3D, "Unsupported RGB mask: r=%02x g=%02x b=%02x", rbits, gbits, bbits);
		}
		if (abits != 0 && abits != 0xFF) {
			// The stencil part of the mask is supported.
			WARN_LOG_REPORT_ONCE(amask, G3D, "Unsupported alpha/stencil mask: %02x", abits);
		}
#endif

		// Let's not write to alpha if stencil isn't enabled.
		if (!gstate.isStencilTestEnabled()) {
			amask = false;
		}
		else {
			// If the stencil type is set to KEEP, we shouldn't write to the stencil/alpha channel.
			if (ReplaceAlphaWithStencilType() == STENCIL_VALUE_KEEP) {
				amask = false;
			}
		}

		key.colorWriteMask = (rmask ? VK_COLOR_COMPONENT_R_BIT : 0) | (gmask ? VK_COLOR_COMPONENT_G_BIT : 0) | (bmask ? VK_COLOR_COMPONENT_B_BIT : 0) | (amask ? VK_COLOR_COMPONENT_A_BIT : 0);

		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);

		// Stencil Test
		if (stencilState.enabled) {
			key.stencilTestEnable = true;
			key.stencilCompareOp = compareOps[stencilState.testFunc];
			key.stencilPassOp = stencilOps[stencilState.zPass];
			key.stencilFailOp = stencilOps[stencilState.sFail];
			key.stencilDepthFailOp = stencilOps[stencilState.zFail];
			dynState.useStencil = true;
			dynState.stencilRef = stencilState.testRef;
			dynState.stencilCompareMask = stencilState.testMask;
			dynState.stencilWriteMask = stencilState.writeMask;
		}
		else {
			key.stencilTestEnable = false;
			dynState.useStencil = false;
		}
	}

	key.topology = primToVulkan[prim];
}

void DrawEngineVulkan::ApplyStateLate() {
	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		gstate_c.Clean(DIRTY_VIEWPORTSCISSOR_STATE);

		ViewportAndScissor vpAndScissor;
		bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
		ConvertViewportAndScissor(useBufferedRendering,
			framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
			framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
			vpAndScissor);

		VkViewport vp;
		vp.x = vpAndScissor.viewportX;
		vp.y = vpAndScissor.viewportY;
		vp.width = vpAndScissor.viewportW;
		vp.height = vpAndScissor.viewportH;
		vp.minDepth = vpAndScissor.depthRangeMin;
		vp.maxDepth = vpAndScissor.depthRangeMax;
		if (vpAndScissor.dirtyProj) {
			gstate_c.Dirty(DIRTY_PROJMATRIX);
		}

		VkRect2D scissor;
		scissor.offset.x = vpAndScissor.scissorX;
		scissor.offset.y = vpAndScissor.scissorY;
		scissor.extent.width = vpAndScissor.scissorW;
		scissor.extent.height = vpAndScissor.scissorH;

		float depthMin = vpAndScissor.depthRangeMin;
		float depthMax = vpAndScissor.depthRangeMax;

		if (depthMin < 0.0f) depthMin = 0.0f;
		if (depthMax > 1.0f) depthMax = 1.0f;
		if (vpAndScissor.dirtyDepth) {
			gstate_c.Dirty(DIRTY_DEPTHRANGE);
		}

		// TODO: Dirty-flag these.
		vkCmdSetScissor(cmd_, 0, 1, &scissor);
		vkCmdSetViewport(cmd_, 0, 1, &vp);
	}
}
