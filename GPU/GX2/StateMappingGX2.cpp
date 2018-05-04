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

#include "Common/Data/Convert/SmallDataConvert.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/GPUStateUtils.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"

#include "Common/Profiler/Profiler.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/GX2/DrawEngineGX2.h"
#include "GPU/GX2/StateMappingGX2.h"
#include "GPU/GX2/FramebufferManagerGX2.h"
#include "GPU/GX2/TextureCacheGX2.h"

#include <wiiu/gx2.h>

// clang-format off
// These tables all fit into u8s.
static const GX2BlendMode GX2BlendFactorLookup[(size_t)BlendFactor::COUNT] = {
	GX2_BLEND_MODE_ZERO,
	GX2_BLEND_MODE_ONE,
	GX2_BLEND_MODE_SRC_COLOR,
	GX2_BLEND_MODE_INV_SRC_COLOR,
	GX2_BLEND_MODE_DST_COLOR,
	GX2_BLEND_MODE_INV_DST_COLOR,
	GX2_BLEND_MODE_SRC_ALPHA,
	GX2_BLEND_MODE_INV_SRC_ALPHA,
	GX2_BLEND_MODE_DST_ALPHA,
	GX2_BLEND_MODE_INV_DST_ALPHA,
	GX2_BLEND_MODE_BLEND_FACTOR,
	GX2_BLEND_MODE_INV_BLEND_FACTOR,
	GX2_BLEND_MODE_BLEND_ALPHA,
	GX2_BLEND_MODE_INV_BLEND_ALPHA,
	GX2_BLEND_MODE_SRC1_COLOR,
	GX2_BLEND_MODE_INV_SRC1_COLOR,
	GX2_BLEND_MODE_SRC1_ALPHA,
	GX2_BLEND_MODE_INV_SRC1_ALPHA,
};

static const GX2BlendCombineMode GX2BlendEqLookup[(size_t)BlendEq::COUNT] = {
	GX2_BLEND_COMBINE_MODE_ADD,
	GX2_BLEND_COMBINE_MODE_SUB,
	GX2_BLEND_COMBINE_MODE_REV_SUB,
	GX2_BLEND_COMBINE_MODE_MIN,
	GX2_BLEND_COMBINE_MODE_MAX,
};

static const GX2CompareFunction compareOps[] = {
	GX2_COMPARE_FUNC_NEVER,
	GX2_COMPARE_FUNC_ALWAYS,
	GX2_COMPARE_FUNC_EQUAL,
	GX2_COMPARE_FUNC_NOT_EQUAL,
	GX2_COMPARE_FUNC_LESS,
	GX2_COMPARE_FUNC_LEQUAL,
	GX2_COMPARE_FUNC_GREATER,
	GX2_COMPARE_FUNC_GEQUAL,
};

static const GX2StencilFunction stencilOps[] = {
	GX2_STENCIL_FUNCTION_KEEP,
	GX2_STENCIL_FUNCTION_ZERO,
	GX2_STENCIL_FUNCTION_REPLACE,
	GX2_STENCIL_FUNCTION_INV,
	GX2_STENCIL_FUNCTION_INCR_CLAMP,
	GX2_STENCIL_FUNCTION_DECR_CLAMP,
	GX2_STENCIL_FUNCTION_KEEP, // reserved
	GX2_STENCIL_FUNCTION_KEEP, // reserved
};

static const GX2PrimitiveMode primToGX2[8] = {
	GX2_PRIMITIVE_MODE_POINTS,
	GX2_PRIMITIVE_MODE_LINES,
	GX2_PRIMITIVE_MODE_LINE_STRIP,
	GX2_PRIMITIVE_MODE_TRIANGLES,
	GX2_PRIMITIVE_MODE_TRIANGLE_STRIP,
	GX2_PRIMITIVE_MODE_TRIANGLE_FAN,
	GX2_PRIMITIVE_MODE_TRIANGLES,
};

static const GX2LogicOp logicOps[] = {
	GX2_LOGIC_OP_CLEAR,
	GX2_LOGIC_OP_AND,
	GX2_LOGIC_OP_REV_AND,
	GX2_LOGIC_OP_COPY,
	GX2_LOGIC_OP_INV_AND,
	GX2_LOGIC_OP_NOP,
	GX2_LOGIC_OP_XOR,
	GX2_LOGIC_OP_OR,
	GX2_LOGIC_OP_NOR,
	GX2_LOGIC_OP_EQUIV,
	GX2_LOGIC_OP_INV,
	GX2_LOGIC_OP_REV_OR,
	GX2_LOGIC_OP_INV_COPY,
	GX2_LOGIC_OP_INV_OR,
	GX2_LOGIC_OP_NOT_AND,
	GX2_LOGIC_OP_SET,
};
// clang-format on

void DrawEngineGX2::ResetShaderBlending() {
	if (fboTexBound_) {
		//		GX2SetPixelTexture(nullptr, 0);
		fboTexBound_ = false;
	}
}

class FramebufferManagerGX2;
class ShaderManagerGX2;

void DrawEngineGX2::ApplyDrawState(int prim) {
	PROFILE_THIS_SCOPE("drawState");
	dynState_.topology = primToGX2[prim];

	if (!gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE)) {
		// nothing to do
		return;
	}

	bool useBufferedRendering = framebufferManager_->UseBufferedRendering();
	// Blend
	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		gstate_c.SetAllowShaderBlend(!g_Config.bDisableSlowFramebufEffects);
		if (gstate.isModeClear()) {
			keys_.blend.value = 0; // full wipe
			keys_.blend.blendEnable = false;
			dynState_.useBlendColor = false;
			// Color Test
			bool alphaMask = gstate.isClearModeAlphaMask();
			bool colorMask = gstate.isClearModeColorMask();
			keys_.blend.colorWriteMask = (GX2ChannelMask)((colorMask ? (1 | 2 | 4) : 0) | (alphaMask ? 8 : 0));
		} else {
			keys_.blend.value = 0;
			// Set blend - unless we need to do it in the shader.
			GenericBlendState blendState;
			ConvertBlendState(blendState, gstate_c.allowShaderBlend);
			if (blendState.applyShaderBlending) {
				if (ApplyShaderBlending()) {
					// We may still want to do something about stencil -> alpha.
					ApplyStencilReplaceAndLogicOp(blendState.replaceAlphaWithStencil, blendState);
				} else {
					// Until next time, force it off.
					ResetShaderBlending();
					gstate_c.SetAllowShaderBlend(false);
				}
			} else if (blendState.resetShaderBlending) {
				ResetShaderBlending();
			}

			if (blendState.enabled) {
				keys_.blend.blendEnable = true;
				keys_.blend.logicOpEnable = false;
				keys_.blend.blendOpColor = GX2BlendEqLookup[(size_t)blendState.eqColor];
				keys_.blend.blendOpAlpha = GX2BlendEqLookup[(size_t)blendState.eqAlpha];
				keys_.blend.srcColor = GX2BlendFactorLookup[(size_t)blendState.srcColor];
				keys_.blend.srcAlpha = GX2BlendFactorLookup[(size_t)blendState.srcAlpha];
				keys_.blend.destColor = GX2BlendFactorLookup[(size_t)blendState.dstColor];
				keys_.blend.destAlpha = GX2BlendFactorLookup[(size_t)blendState.dstAlpha];
				if (blendState.dirtyShaderBlend) {
					gstate_c.Dirty(DIRTY_SHADERBLEND);
				}
				dynState_.useBlendColor = blendState.useBlendColor;
				if (blendState.useBlendColor) {
					dynState_.blendColor = blendState.blendColor;
				}
			} else {
				keys_.blend.blendEnable = false;
				dynState_.useBlendColor = false;
			}

			if (gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP)) {
				// Logic Ops
				if (gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY) {
					keys_.blend.blendEnable = false; // Can't have both blend & logic op - although I think the PSP can!
					keys_.blend.logicOpEnable = true;
					keys_.blend.logicOp = logicOps[gstate.getLogicOp()];
				} else {
					keys_.blend.logicOpEnable = false;
				}
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
			if (IsStencilTestOutputDisabled()) {
				amask = false;
			} else {
				// If the stencil type is set to KEEP, we shouldn't write to the stencil/alpha channel.
				if (ReplaceAlphaWithStencilType() == STENCIL_VALUE_KEEP) {
					amask = false;
				}
			}

			keys_.blend.colorWriteMask = (GX2ChannelMask)((rmask ? 1 : 0) | (gmask ? 2 : 0) | (bmask ? 4 : 0) | (amask ? 8 : 0));
		}

		GX2BlendState *bs1 = blendCache_.Get(keys_.blend.value);
		if (bs1 == nullptr) {
			bs1 = new GX2BlendState;
			GX2InitColorControlReg(&bs1->color, keys_.blend.logicOpEnable ? keys_.blend.logicOp : GX2_LOGIC_OP_COPY, keys_.blend.blendEnable ? 0xFF : 0x00, false, keys_.blend.colorWriteMask != 0);
			GX2InitTargetChannelMasksReg(&bs1->mask, keys_.blend.colorWriteMask, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0);
			GX2InitBlendControlReg(&bs1->blend, GX2_RENDER_TARGET_0, keys_.blend.srcColor, keys_.blend.destColor, keys_.blend.blendOpColor, keys_.blend.srcAlpha && keys_.blend.destAlpha, keys_.blend.srcAlpha, keys_.blend.destAlpha, keys_.blend.blendOpAlpha);
			blendCache_.Insert(keys_.blend.value, bs1);
		}
		blendState_ = bs1;
	}

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		keys_.raster.value = 0;
		keys_.raster.frontFace = GX2_FRONT_FACE_CCW;
		// Set cull
		if (!gstate.isModeClear() && !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled()) {
			keys_.raster.cullFront = !!gstate.getCullMode();
			keys_.raster.cullBack = !gstate.getCullMode();
		} else {
			keys_.raster.cullFront = GX2_DISABLE;
			keys_.raster.cullBack = GX2_DISABLE;
		}
		GX2RasterizerState *rs = rasterCache_.Get(keys_.raster.value);
		if (rs == nullptr) {
			rs = new GX2RasterizerState({ keys_.raster.frontFace, keys_.raster.cullFront, keys_.raster.cullBack });
			rasterCache_.Insert(keys_.raster.value, rs);
		}
		rasterState_ = rs;
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		if (gstate.isModeClear()) {
			keys_.depthStencil.value = 0;
			keys_.depthStencil.depthTestEnable = true;
			keys_.depthStencil.depthCompareOp = GX2_COMPARE_FUNC_ALWAYS;
			keys_.depthStencil.depthWriteEnable = gstate.isClearModeDepthMask();
			if (gstate.isClearModeDepthMask()) {
				framebufferManager_->SetDepthUpdated();
			}

			// Stencil Test
			bool alphaMask = gstate.isClearModeAlphaMask();
			if (alphaMask) {
				keys_.depthStencil.stencilTestEnable = true;
				keys_.depthStencil.stencilCompareFunc = GX2_COMPARE_FUNC_ALWAYS;
				keys_.depthStencil.stencilPassOp = GX2_STENCIL_FUNCTION_REPLACE;
				keys_.depthStencil.stencilFailOp = GX2_STENCIL_FUNCTION_REPLACE;
				keys_.depthStencil.stencilDepthFailOp = GX2_STENCIL_FUNCTION_REPLACE;
				dynState_.useStencil = true;
				// In clear mode, the stencil value is set to the alpha value of the vertex.
				// A normal clear will be 2 points, the second point has the color.
				// We override this value in the pipeline from software transform for clear rectangles.
				dynState_.stencilRef = 0xFF;
				// But we still apply the stencil write mask.
				keys_.depthStencil.stencilWriteMask = (~gstate.getStencilWriteMask()) & 0xFF;
			} else {
				keys_.depthStencil.stencilTestEnable = false;
				dynState_.useStencil = false;
			}

		} else {
			keys_.depthStencil.value = 0;
			// Depth Test
			if (gstate.isDepthTestEnabled()) {
				keys_.depthStencil.depthTestEnable = true;
				keys_.depthStencil.depthCompareOp = compareOps[gstate.getDepthTestFunction()];
				keys_.depthStencil.depthWriteEnable = gstate.isDepthWriteEnabled();
				if (gstate.isDepthWriteEnabled()) {
					framebufferManager_->SetDepthUpdated();
				}
			} else {
				keys_.depthStencil.depthTestEnable = false;
				keys_.depthStencil.depthWriteEnable = false;
				keys_.depthStencil.depthCompareOp = GX2_COMPARE_FUNC_ALWAYS;
			}

			GenericStencilFuncState stencilState;
			ConvertStencilFuncState(stencilState);

			// Stencil Test
			if (stencilState.enabled) {
				keys_.depthStencil.stencilTestEnable = true;
				keys_.depthStencil.stencilCompareFunc = compareOps[stencilState.testFunc];
				keys_.depthStencil.stencilPassOp = stencilOps[stencilState.zPass];
				keys_.depthStencil.stencilFailOp = stencilOps[stencilState.sFail];
				keys_.depthStencil.stencilDepthFailOp = stencilOps[stencilState.zFail];
				keys_.depthStencil.stencilCompareMask = stencilState.testMask;
				keys_.depthStencil.stencilWriteMask = stencilState.writeMask;
				dynState_.useStencil = true;
				dynState_.stencilRef = stencilState.testRef;
			} else {
				keys_.depthStencil.stencilTestEnable = false;
				dynState_.useStencil = false;
			}
		}
		GX2DepthStencilControlReg *ds = depthStencilCache_.Get(keys_.depthStencil.value);
		if (ds == nullptr) {
			ds = new GX2DepthStencilControlReg;
			GX2InitDepthStencilControlReg(ds, keys_.depthStencil.depthTestEnable, keys_.depthStencil.depthWriteEnable, keys_.depthStencil.depthCompareOp, keys_.depthStencil.stencilTestEnable, keys_.depthStencil.stencilTestEnable, keys_.depthStencil.stencilCompareFunc, keys_.depthStencil.stencilPassOp, keys_.depthStencil.stencilDepthFailOp, keys_.depthStencil.stencilFailOp, keys_.depthStencil.stencilCompareFunc, keys_.depthStencil.stencilPassOp, keys_.depthStencil.stencilDepthFailOp, keys_.depthStencil.stencilFailOp);
			depthStencilCache_.Insert(keys_.depthStencil.value, ds);
		}
		depthStencilState_ = ds;
	}

	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		ViewportAndScissor vpAndScissor;
		ConvertViewportAndScissor(useBufferedRendering, framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(), framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(), vpAndScissor);

		float depthMin = vpAndScissor.depthRangeMin;
		float depthMax = vpAndScissor.depthRangeMax;

		if (depthMin < 0.0f)
			depthMin = 0.0f;
		if (depthMax > 1.0f)
			depthMax = 1.0f;
		if (vpAndScissor.dirtyDepth) {
			gstate_c.Dirty(DIRTY_DEPTHRANGE);
		}

		Draw::Viewport &vp = dynState_.viewport;
		vp.TopLeftX = vpAndScissor.viewportX;
		vp.TopLeftY = vpAndScissor.viewportY;
		vp.Width = vpAndScissor.viewportW;
		vp.Height = vpAndScissor.viewportH;
		vp.MinDepth = depthMin;
		vp.MaxDepth = depthMax;

		if (vpAndScissor.dirtyProj) {
			gstate_c.Dirty(DIRTY_PROJMATRIX);
		}

		GX2_RECT &scissor = dynState_.scissor;
		if (vpAndScissor.scissorEnable) {
			scissor.left = vpAndScissor.scissorX;
			scissor.top = vpAndScissor.scissorY;
			scissor.right = vpAndScissor.scissorX + std::max(0, vpAndScissor.scissorW);
			scissor.bottom = vpAndScissor.scissorY + std::max(0, vpAndScissor.scissorH);
		} else {
			scissor.left = 0;
			scissor.top = 0;
			scissor.right = framebufferManager_->GetRenderWidth();
			scissor.bottom = framebufferManager_->GetRenderHeight();
		}
	}

	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	} else if (gstate.getTextureAddress(0) == ((gstate.getFrameBufRawAddress() | 0x04000000) & 0x3FFFFFFF)) {
		// This catches the case of clearing a texture.
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	}
}

void DrawEngineGX2::ApplyDrawStateLate(bool applyStencilRef, uint8_t stencilRef) {
	PROFILE_THIS_SCOPE("late drawState");
	if (!gstate.isModeClear()) {
		if (fboTexNeedBind_) {
			framebufferManager_->BindFramebufferAsColorTexture(1, framebufferManager_->GetCurrentRenderVFB(), BINDFBCOLOR_MAY_COPY);
			// No sampler required, we do a plain Load in the pixel shader.
			fboTexBound_ = true;
			fboTexNeedBind_ = false;
		}
		textureCache_->ApplyTexture();
	}

	// we go through Draw here because it automatically handles screen rotation, as needed in UWP on mobiles.
	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		GX2ColorBuffer *current_color_buffer = (GX2ColorBuffer *)draw_->GetNativeObject(Draw::NativeObject::BACKBUFFER_COLOR_VIEW);
		draw_->SetViewports(1, &dynState_.viewport);
		int left = std::min(std::max(0, dynState_.scissor.left), (int)current_color_buffer->surface.width - 1);
		int top = std::min(std::max(0, dynState_.scissor.top), (int)current_color_buffer->surface.height - 1);
		int width = std::min(dynState_.scissor.right - dynState_.scissor.left, (int)current_color_buffer->surface.width - left);
		int height = std::min(dynState_.scissor.bottom - dynState_.scissor.top, (int)current_color_buffer->surface.height - top);
		draw_->SetScissorRect(left, top, width, height);
	}
	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		GX2SetCullOnlyControl(rasterState_->frontFace_, rasterState_->cullFront_, rasterState_->cullBack_);
	}
	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		// Need to do this AFTER ApplyTexture because the process of depallettization can ruin the blend state.
		float blendColor[4];
		Uint8x4ToFloat4(blendColor, dynState_.blendColor);
		GX2SetBlendControlReg(&blendState_->blend);
		GX2SetColorControlReg(&blendState_->color);
		GX2SetTargetChannelMasksReg(&blendState_->mask);
		GX2SetBlendConstantColorReg((GX2BlendConstantColorReg *)blendColor);
	}
	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE) || applyStencilRef) {
		GX2SetDepthStencilControlReg(depthStencilState_);
		if (!applyStencilRef)
			stencilRef = dynState_.stencilRef;
		GX2SetStencilMask(0xFF, 0xFF, stencilRef, 0xFF, 0xFF, stencilRef);
	}
	gstate_c.Clean(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_BLEND_STATE);

	// Must dirty blend state here so we re-copy next time.  Example: Lunar's spell effects.
	if (fboTexBound_)
		gstate_c.Dirty(DIRTY_BLEND_STATE);
}
