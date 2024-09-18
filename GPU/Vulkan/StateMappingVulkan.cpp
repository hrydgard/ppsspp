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

#include <algorithm>

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"

#include "Common/Data/Convert/SmallDataConvert.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/GPUStateUtils.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"

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
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, // We convert points to triangles.
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, // We convert lines to triangles.
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, // We convert line strips to triangles.
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

// In Vulkan, we simply collect all the state together into a "pipeline key" - we don't actually set any state here
// (the caller is responsible for setting the little dynamic state that is supported, dynState).
void DrawEngineVulkan::ConvertStateToVulkanKey(FramebufferManagerVulkan &fbManager, ShaderManagerVulkan *shaderManager, int prim, VulkanPipelineRasterStateKey &key, VulkanDynamicState &dynState) {
	key.topology = primToVulkan[prim];

	bool useBufferedRendering = framebufferManager_->UseBufferedRendering();

	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		if (gstate.isModeClear()) {
			key.logicOpEnable = false;
			key.logicOp = VK_LOGIC_OP_CLEAR;
			key.blendEnable = false;
			key.blendOpColor = VK_BLEND_OP_ADD;
			key.blendOpAlpha = VK_BLEND_OP_ADD;
			key.srcColor = VK_BLEND_FACTOR_ONE;
			key.srcAlpha = VK_BLEND_FACTOR_ONE;
			key.destColor = VK_BLEND_FACTOR_ZERO;
			key.destAlpha = VK_BLEND_FACTOR_ZERO;
			dynState.useBlendColor = false;

			// Color Mask
			bool colorMask = gstate.isClearModeColorMask();
			bool alphaMask = gstate.isClearModeAlphaMask();
			key.colorWriteMask = (colorMask ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT) : 0) | (alphaMask ? VK_COLOR_COMPONENT_A_BIT : 0);
		} else {
			pipelineState_.Convert(draw_->GetShaderLanguageDesc().bitwiseOps);
			GenericMaskState &maskState = pipelineState_.maskState;
			GenericBlendState &blendState = pipelineState_.blendState;
			GenericLogicState &logicState = pipelineState_.logicState;

			if (pipelineState_.FramebufferRead() && useBufferedRendering) {
				ApplyFramebufferRead(&fboTexBindState_);
				// The shader takes over the responsibility for blending, so recompute.
				// We might still end up using blend to write something to alpha.
				ApplyStencilReplaceAndLogicOpIgnoreBlend(blendState.replaceAlphaWithStencil, blendState);
				dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
				gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
			} else {
				if (fboTexBound_) {
					boundSecondary_ = VK_NULL_HANDLE;
					fboTexBound_ = false;
					dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
					gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
				}
			}

			if (blendState.blendEnabled) {
				key.blendEnable = true;
				key.blendOpColor = vkBlendEqLookup[(size_t)blendState.eqColor];
				key.blendOpAlpha = vkBlendEqLookup[(size_t)blendState.eqAlpha];
				key.srcColor = vkBlendFactorLookup[(size_t)blendState.srcColor];
				key.srcAlpha = vkBlendFactorLookup[(size_t)blendState.srcAlpha];
				key.destColor = vkBlendFactorLookup[(size_t)blendState.dstColor];
				key.destAlpha = vkBlendFactorLookup[(size_t)blendState.dstAlpha];
				if (blendState.dirtyShaderBlendFixValues) {
					dirtyRequiresRecheck_ |= DIRTY_SHADERBLEND;
					gstate_c.Dirty(DIRTY_SHADERBLEND);
				}
				dynState.useBlendColor = blendState.useBlendColor;
				if (blendState.useBlendColor) {
					dynState.blendColor = blendState.blendColor;
				}
			} else {
				key.blendEnable = false;
				key.blendOpColor = VK_BLEND_OP_ADD;
				key.blendOpAlpha = VK_BLEND_OP_ADD;
				key.srcColor = VK_BLEND_FACTOR_ONE;
				key.srcAlpha = VK_BLEND_FACTOR_ONE;
				key.destColor = VK_BLEND_FACTOR_ZERO;
				key.destAlpha = VK_BLEND_FACTOR_ZERO;
				dynState.useBlendColor = false;
			}

			key.colorWriteMask = maskState.channelMask;  // flags match

			if (logicState.logicOpEnabled) {
				key.logicOpEnable = true;
				key.logicOp = logicOps[(int)logicState.logicOp];
			} else {
				key.logicOpEnable = false;
				key.logicOp = VK_LOGIC_OP_COPY;
			}

			// Workaround proposed in #10421, for bug where the color write mask is not applied correctly on Adreno.
			if ((gstate.pmskc & 0x00FFFFFF) == 0x00FFFFFF && g_Config.bVendorBugChecksEnabled && draw_->GetBugs().Has(Draw::Bugs::COLORWRITEMASK_BROKEN_WITH_DEPTHTEST)) {
				key.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				if (!key.blendEnable) {
					bool writeAlpha = maskState.channelMask & 8;
					key.blendEnable = true;
					key.blendOpAlpha = VK_BLEND_OP_ADD;
					key.srcAlpha = writeAlpha ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
					key.destAlpha = writeAlpha ? VK_BLEND_FACTOR_ZERO : VK_BLEND_FACTOR_ONE;
				}
				key.blendOpColor = VK_BLEND_OP_ADD;
				key.srcColor = VK_BLEND_FACTOR_ZERO;
				key.destColor = VK_BLEND_FACTOR_ONE;
			}
		}
	}

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		bool wantCull = !gstate.isModeClear() && prim != GE_PRIM_RECTANGLES && prim > GE_PRIM_LINE_STRIP && gstate.isCullEnabled();
		key.cullMode = wantCull ? (gstate.getCullMode() ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT) : VK_CULL_MODE_NONE;

		if (gstate.isModeClear() || gstate.isModeThrough()) {
			// TODO: Might happen in clear mode if not through...
			key.depthClampEnable = false;
		} else {
			if (gstate.getDepthRangeMin() == 0 || gstate.getDepthRangeMax() == 65535) {
				// TODO: Still has a bug where we clamp to depth range if one is not the full range.
				// But the alternate is not clamping in either direction...
				key.depthClampEnable = gstate.isDepthClampEnabled() && gstate_c.Use(GPU_USE_DEPTH_CLAMP);
			} else {
				// We just want to clip in this case, the clamp would be clipped anyway.
				key.depthClampEnable = false;
			}
		}
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);

		if (gstate.isModeClear()) {
			key.depthTestEnable = true;
			key.depthCompareOp = VK_COMPARE_OP_ALWAYS;
			key.depthWriteEnable = gstate.isClearModeDepthMask();

			// Stencil Test
			bool alphaMask = gstate.isClearModeAlphaMask();
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
				// But we still apply the stencil write mask.
				dynState.stencilWriteMask = stencilState.writeMask;
			} else {
				key.stencilTestEnable = false;
				key.stencilCompareOp = VK_COMPARE_OP_ALWAYS;
				key.stencilPassOp = VK_STENCIL_OP_REPLACE;
				key.stencilFailOp = VK_STENCIL_OP_REPLACE;
				key.stencilDepthFailOp = VK_STENCIL_OP_REPLACE;
				dynState.useStencil = false;
			}
		} else {
			// Depth Test
			if (!IsDepthTestEffectivelyDisabled()) {
				key.depthTestEnable = true;
				key.depthCompareOp = compareOps[gstate.getDepthTestFunction()];
				key.depthWriteEnable = gstate.isDepthWriteEnabled();
				UpdateEverUsedEqualDepth(gstate.getDepthTestFunction());
			} else {
				key.depthTestEnable = false;
				key.depthWriteEnable = false;
				key.depthCompareOp = VK_COMPARE_OP_ALWAYS;
			}

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

				// Nasty special case for Spongebob and similar where it tries to write zeros to alpha/stencil during
				// depth-fail. We can't write to alpha then because the pixel is killed. However, we can invert the depth
				// test and modify the alpha function...
				if (SpongebobDepthInverseConditions(stencilState)) {
					key.blendEnable = true;
					key.blendOpAlpha = VK_BLEND_OP_ADD;
					key.blendOpColor = VK_BLEND_OP_ADD;
					key.srcColor = VK_BLEND_FACTOR_ZERO;
					key.destColor = VK_BLEND_FACTOR_ZERO;
					key.logicOpEnable = false;
					key.srcAlpha = VK_BLEND_FACTOR_ZERO;
					key.destAlpha = VK_BLEND_FACTOR_ZERO;
					key.colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
					key.depthCompareOp = VK_COMPARE_OP_LESS;  // Inverse of GREATER_EQUAL
					key.stencilCompareOp = VK_COMPARE_OP_ALWAYS;
					// Invert
					key.stencilPassOp = VK_STENCIL_OP_ZERO;
					key.stencilFailOp = VK_STENCIL_OP_ZERO;
					key.stencilDepthFailOp = VK_STENCIL_OP_KEEP;

					dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE;
					gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
				}
			} else {
				key.stencilTestEnable = false;
				key.stencilCompareOp = VK_COMPARE_OP_ALWAYS;
				key.stencilPassOp = VK_STENCIL_OP_REPLACE;
				key.stencilFailOp = VK_STENCIL_OP_REPLACE;
				key.stencilDepthFailOp = VK_STENCIL_OP_REPLACE;
				dynState.useStencil = false;
			}
		}
	}

	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		ViewportAndScissor vpAndScissor;
		ConvertViewportAndScissor(useBufferedRendering,
			fbManager.GetRenderWidth(), fbManager.GetRenderHeight(),
			fbManager.GetTargetBufferWidth(), fbManager.GetTargetBufferHeight(),
			vpAndScissor);
		UpdateCachedViewportState(vpAndScissor);

		float depthMin = vpAndScissor.depthRangeMin;
		float depthMax = vpAndScissor.depthRangeMax;

		if (depthMin < 0.0f) depthMin = 0.0f;
		if (depthMax > 1.0f) depthMax = 1.0f;

		VkViewport &vp = dynState.viewport;
		vp.x = vpAndScissor.viewportX;
		vp.y = vpAndScissor.viewportY;
		vp.width = vpAndScissor.viewportW;
		vp.height = vpAndScissor.viewportH;
		vp.minDepth = vpAndScissor.depthRangeMin;
		vp.maxDepth = vpAndScissor.depthRangeMax;

		ScissorRect &scissor = dynState.scissor;
		scissor.x = vpAndScissor.scissorX;
		scissor.y = vpAndScissor.scissorY;
		scissor.width = std::max(0, vpAndScissor.scissorW);
		scissor.height = std::max(0, vpAndScissor.scissorH);
	}
}

void DrawEngineVulkan::BindShaderBlendTex() {
	// TODO: At this point, we know if the vertices are full alpha or not.
	// Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
	if (!gstate.isModeClear()) {
		if (fboTexBindState_ == FBO_TEX_COPY_BIND_TEX) {
			VirtualFramebuffer *curRenderVfb = framebufferManager_->GetCurrentRenderVFB();
			bool bindResult = framebufferManager_->BindFramebufferAsColorTexture(1, curRenderVfb, BINDFBCOLOR_MAY_COPY | BINDFBCOLOR_UNCACHED, Draw::ALL_LAYERS);
			_dbg_assert_(bindResult);
			boundSecondary_ = (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE1_IMAGEVIEW);
			fboTexBound_ = true;
			fboTexBindState_ = FBO_TEX_NONE;

			// Must dirty blend state here so we re-copy next time.  Example: Lunar's spell effects.
			dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE;
		} else {
			boundSecondary_ = VK_NULL_HANDLE;
		}
	} else {
		boundSecondary_ = VK_NULL_HANDLE;
	}
}

void DrawEngineVulkan::ApplyDrawStateLate(VulkanRenderManager *renderManager, bool applyStencilRef, uint8_t stencilRef, bool useBlendConstant) {
	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		renderManager->SetScissor(dynState_.scissor.x, dynState_.scissor.y, dynState_.scissor.width, dynState_.scissor.height);
		renderManager->SetViewport(dynState_.viewport);
	}
	if ((gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE) && dynState_.useStencil) || applyStencilRef) {
		renderManager->SetStencilParams(dynState_.stencilWriteMask, dynState_.stencilCompareMask, applyStencilRef ? stencilRef : dynState_.stencilRef);
	}
	if (gstate_c.IsDirty(DIRTY_BLEND_STATE) && useBlendConstant) {
		renderManager->SetBlendFactor(dynState_.blendColor);
	}
}
