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

#include <d3d11.h>
#include <d3d11_1.h>

#include "math/dataconv.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/GPUStateUtils.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"

#include "GPU/Common/FramebufferCommon.h"
#include "GPU/D3D11/DrawEngineD3D11.h"
#include "GPU/D3D11/StateMappingD3D11.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"

// These tables all fit into u8s.
static const D3D11_BLEND d3d11BlendFactorLookup[(size_t)BlendFactor::COUNT] = {
	D3D11_BLEND_ZERO,
	D3D11_BLEND_ONE,
	D3D11_BLEND_SRC_COLOR,
	D3D11_BLEND_INV_SRC_COLOR,
	D3D11_BLEND_DEST_COLOR,
	D3D11_BLEND_INV_DEST_COLOR,
	D3D11_BLEND_SRC_ALPHA,
	D3D11_BLEND_INV_SRC_ALPHA,
	D3D11_BLEND_DEST_ALPHA,
	D3D11_BLEND_INV_DEST_ALPHA,
	D3D11_BLEND_BLEND_FACTOR,
	D3D11_BLEND_INV_BLEND_FACTOR,
	D3D11_BLEND_BLEND_FACTOR,
	D3D11_BLEND_INV_BLEND_FACTOR,
	D3D11_BLEND_SRC1_COLOR,
	D3D11_BLEND_INV_SRC1_COLOR,
	D3D11_BLEND_SRC1_ALPHA,
	D3D11_BLEND_INV_SRC1_ALPHA,
};

static const D3D11_BLEND_OP d3d11BlendEqLookup[(size_t)BlendEq::COUNT] = {
	D3D11_BLEND_OP_ADD,
	D3D11_BLEND_OP_SUBTRACT,
	D3D11_BLEND_OP_REV_SUBTRACT,
	D3D11_BLEND_OP_MIN,
	D3D11_BLEND_OP_MAX,
};

static const D3D11_CULL_MODE cullingMode[] = {
	D3D11_CULL_BACK,
	D3D11_CULL_FRONT,
};

static const D3D11_COMPARISON_FUNC compareOps[] = {
	D3D11_COMPARISON_NEVER,
	D3D11_COMPARISON_ALWAYS,
	D3D11_COMPARISON_EQUAL,
	D3D11_COMPARISON_NOT_EQUAL,
	D3D11_COMPARISON_LESS,
	D3D11_COMPARISON_LESS_EQUAL,
	D3D11_COMPARISON_GREATER,
	D3D11_COMPARISON_GREATER_EQUAL,
};

static const D3D11_STENCIL_OP stencilOps[] = {
	D3D11_STENCIL_OP_KEEP,
	D3D11_STENCIL_OP_ZERO,
	D3D11_STENCIL_OP_REPLACE,
	D3D11_STENCIL_OP_INVERT,
	D3D11_STENCIL_OP_INCR_SAT,
	D3D11_STENCIL_OP_DECR_SAT,
	D3D11_STENCIL_OP_KEEP, // reserved
	D3D11_STENCIL_OP_KEEP, // reserved
};

static const D3D11_PRIMITIVE_TOPOLOGY primToD3D11[8] = {
	D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,  // D3D11 doesn't do triangle fans.
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
};

static const D3D11_LOGIC_OP logicOps[] = {
	D3D11_LOGIC_OP_CLEAR,
	D3D11_LOGIC_OP_AND,
	D3D11_LOGIC_OP_AND_REVERSE,
	D3D11_LOGIC_OP_COPY,
	D3D11_LOGIC_OP_AND_INVERTED,
	D3D11_LOGIC_OP_NOOP,
	D3D11_LOGIC_OP_XOR,
	D3D11_LOGIC_OP_OR,
	D3D11_LOGIC_OP_NOR,
	D3D11_LOGIC_OP_EQUIV,
	D3D11_LOGIC_OP_INVERT,
	D3D11_LOGIC_OP_OR_REVERSE,
	D3D11_LOGIC_OP_COPY_INVERTED,
	D3D11_LOGIC_OP_OR_INVERTED,
	D3D11_LOGIC_OP_NAND,
	D3D11_LOGIC_OP_SET,
};

void DrawEngineD3D11::ResetShaderBlending() {
	if (fboTexBound_) {
		ID3D11ShaderResourceView *srv = nullptr;
		context_->PSSetShaderResources(0, 1, &srv);
		fboTexBound_ = false;
	}
}

class FramebufferManagerD3D11;
class ShaderManagerD3D11;

void DrawEngineD3D11::ApplyDrawState(int prim) {
	dynState_.topology = primToD3D11[prim];

	if (!gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE)) {
		// nothing to do
		return;
	}

	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	// Blend
	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		gstate_c.SetAllowShaderBlend(!g_Config.bDisableSlowFramebufEffects);
		if (gstate.isModeClear()) {
			keys_.blend.value = 0;  // full wipe
			keys_.blend.blendEnable = false;
			dynState_.useBlendColor = false;
			// Color Test
			bool alphaMask = gstate.isClearModeAlphaMask();
			bool colorMask = gstate.isClearModeColorMask();
			keys_.blend.colorWriteMask = (colorMask ? (1 | 2 | 4) : 0) | (alphaMask ? 8 : 0);
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
				keys_.blend.blendOpColor = d3d11BlendEqLookup[(size_t)blendState.eqColor];
				keys_.blend.blendOpAlpha = d3d11BlendEqLookup[(size_t)blendState.eqAlpha];
				keys_.blend.srcColor = d3d11BlendFactorLookup[(size_t)blendState.srcColor];
				keys_.blend.srcAlpha = d3d11BlendFactorLookup[(size_t)blendState.srcAlpha];
				keys_.blend.destColor = d3d11BlendFactorLookup[(size_t)blendState.dstColor];
				keys_.blend.destAlpha = d3d11BlendFactorLookup[(size_t)blendState.dstAlpha];
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
					keys_.blend.blendEnable = false;  // Can't have both blend & logic op - although I think the PSP can!
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
			if (!gstate.isStencilTestEnabled()) {
				amask = false;
			} else {
				// If the stencil type is set to KEEP, we shouldn't write to the stencil/alpha channel.
				if (ReplaceAlphaWithStencilType() == STENCIL_VALUE_KEEP) {
					amask = false;
				}
			}

			keys_.blend.colorWriteMask = (rmask ? 1 : 0) | (gmask ? 2 : 0) | (bmask ? 4 : 0) | (amask ? 8 : 0);
		}

		if (!device1_) {
			ID3D11BlendState *bs = blendCache_.Get(keys_.blend.value);
			if (bs == nullptr) {
				D3D11_BLEND_DESC desc{};
				D3D11_RENDER_TARGET_BLEND_DESC &rt = desc.RenderTarget[0];
				rt.BlendEnable = keys_.blend.blendEnable;
				rt.BlendOp = (D3D11_BLEND_OP)keys_.blend.blendOpColor;
				rt.BlendOpAlpha = (D3D11_BLEND_OP)keys_.blend.blendOpAlpha;
				rt.SrcBlend = (D3D11_BLEND)keys_.blend.srcColor;
				rt.DestBlend = (D3D11_BLEND)keys_.blend.destColor;
				rt.SrcBlendAlpha = (D3D11_BLEND)keys_.blend.srcAlpha;
				rt.DestBlendAlpha = (D3D11_BLEND)keys_.blend.destAlpha;
				rt.RenderTargetWriteMask = keys_.blend.colorWriteMask;
				ASSERT_SUCCESS(device_->CreateBlendState(&desc, &bs));
				blendCache_.Insert(keys_.blend.value, bs);
			}
			blendState_ = bs;
		} else {
			ID3D11BlendState1 *bs1 = blendCache1_.Get(keys_.blend.value);
			if (bs1 == nullptr) {
				D3D11_BLEND_DESC1 desc1{};
				D3D11_RENDER_TARGET_BLEND_DESC1 &rt = desc1.RenderTarget[0];
				rt.BlendEnable = keys_.blend.blendEnable;
				rt.BlendOp = (D3D11_BLEND_OP)keys_.blend.blendOpColor;
				rt.BlendOpAlpha = (D3D11_BLEND_OP)keys_.blend.blendOpAlpha;
				rt.SrcBlend = (D3D11_BLEND)keys_.blend.srcColor;
				rt.DestBlend = (D3D11_BLEND)keys_.blend.destColor;
				rt.SrcBlendAlpha = (D3D11_BLEND)keys_.blend.srcAlpha;
				rt.DestBlendAlpha = (D3D11_BLEND)keys_.blend.destAlpha;
				rt.RenderTargetWriteMask = keys_.blend.colorWriteMask;
				rt.LogicOpEnable = keys_.blend.logicOpEnable;
				rt.LogicOp = (D3D11_LOGIC_OP)keys_.blend.logicOp;
				ASSERT_SUCCESS(device1_->CreateBlendState1(&desc1, &bs1));
				blendCache1_.Insert(keys_.blend.value, bs1);
			}
			blendState1_ = bs1;
		}
	}

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		keys_.raster.value = 0;
		if (gstate.isModeClear()) {
			keys_.raster.cullMode = D3D11_CULL_NONE;
		} else {
			// Set cull
			bool wantCull = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
			keys_.raster.cullMode = wantCull ? (gstate.getCullMode() ? D3D11_CULL_FRONT : D3D11_CULL_BACK) : D3D11_CULL_NONE;
		}
		ID3D11RasterizerState *rs = rasterCache_.Get(keys_.raster.value);
		if (rs == nullptr) {
			D3D11_RASTERIZER_DESC desc{};
			desc.CullMode = (D3D11_CULL_MODE)(keys_.raster.cullMode);
			desc.FillMode = D3D11_FILL_SOLID;
			desc.ScissorEnable = TRUE;
			desc.FrontCounterClockwise = TRUE;
			desc.DepthClipEnable = TRUE;
			ASSERT_SUCCESS(device_->CreateRasterizerState(&desc, &rs));
			rasterCache_.Insert(keys_.raster.value, rs);
		}
		rasterState_ = rs;
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		if (gstate.isModeClear()) {
			keys_.depthStencil.value = 0;
			keys_.depthStencil.depthTestEnable = true;
			keys_.depthStencil.depthCompareOp = D3D11_COMPARISON_ALWAYS;
			keys_.depthStencil.depthWriteEnable = gstate.isClearModeDepthMask();
			if (gstate.isClearModeDepthMask()) {
				framebufferManager_->SetDepthUpdated();
			}

			// Stencil Test
			bool alphaMask = gstate.isClearModeAlphaMask();
			if (alphaMask) {
				keys_.depthStencil.stencilTestEnable = true;
				keys_.depthStencil.stencilCompareFunc = D3D11_COMPARISON_ALWAYS;
				keys_.depthStencil.stencilPassOp = D3D11_STENCIL_OP_REPLACE;
				keys_.depthStencil.stencilFailOp = D3D11_STENCIL_OP_REPLACE;
				keys_.depthStencil.stencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
				dynState_.useStencil = true;
				// In clear mode, the stencil value is set to the alpha value of the vertex.
				// A normal clear will be 2 points, the second point has the color.
				// We override this value in the pipeline from software transform for clear rectangles.
				dynState_.stencilRef = 0xFF;
				keys_.depthStencil.stencilWriteMask = 0xFF;
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
				keys_.depthStencil.depthCompareOp = D3D11_COMPARISON_ALWAYS;
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
		ID3D11DepthStencilState *ds = depthStencilCache_.Get(keys_.depthStencil.value);
		if (ds == nullptr) {
			D3D11_DEPTH_STENCIL_DESC desc{};
			desc.DepthEnable = keys_.depthStencil.depthTestEnable;
			desc.DepthWriteMask = keys_.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = (D3D11_COMPARISON_FUNC)keys_.depthStencil.depthCompareOp;
			desc.StencilEnable = keys_.depthStencil.stencilTestEnable;
			desc.StencilReadMask = keys_.depthStencil.stencilCompareMask;
			desc.StencilWriteMask = keys_.depthStencil.stencilWriteMask;
			desc.FrontFace.StencilFailOp = (D3D11_STENCIL_OP)keys_.depthStencil.stencilFailOp;
			desc.FrontFace.StencilPassOp = (D3D11_STENCIL_OP)keys_.depthStencil.stencilPassOp;
			desc.FrontFace.StencilDepthFailOp = (D3D11_STENCIL_OP)keys_.depthStencil.stencilDepthFailOp;
			desc.FrontFace.StencilFunc = (D3D11_COMPARISON_FUNC)keys_.depthStencil.stencilCompareFunc;
			desc.BackFace = desc.FrontFace;
			ASSERT_SUCCESS(device_->CreateDepthStencilState(&desc, &ds));
			depthStencilCache_.Insert(keys_.depthStencil.value, ds);
		}
		depthStencilState_ = ds;
	}

	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		ViewportAndScissor vpAndScissor;
		ConvertViewportAndScissor(useBufferedRendering,
			framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
			framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
			vpAndScissor);

		float depthMin = vpAndScissor.depthRangeMin;
		float depthMax = vpAndScissor.depthRangeMax;

		if (depthMin < 0.0f) depthMin = 0.0f;
		if (depthMax > 1.0f) depthMax = 1.0f;
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

		D3D11_RECT &scissor = dynState_.scissor;
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

void DrawEngineD3D11::ApplyDrawStateLate(bool applyStencilRef, uint8_t stencilRef) {
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
		draw_->SetViewports(1, &dynState_.viewport);
		draw_->SetScissorRect(dynState_.scissor.left, dynState_.scissor.top, dynState_.scissor.right - dynState_.scissor.left, dynState_.scissor.bottom - dynState_.scissor.top);
	}
	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		context_->RSSetState(rasterState_);
	}
	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		// Need to do this AFTER ApplyTexture because the process of depallettization can ruin the blend state.
		float blendColor[4];
		Uint8x4ToFloat4(blendColor, dynState_.blendColor);
		if (device1_) {
			context1_->OMSetBlendState(blendState1_, blendColor, 0xFFFFFFFF);
		} else {
			context_->OMSetBlendState(blendState_, blendColor, 0xFFFFFFFF);
		}
	}
	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE) || applyStencilRef) {
		context_->OMSetDepthStencilState(depthStencilState_, applyStencilRef ? stencilRef : dynState_.stencilRef);
	}
	gstate_c.Clean(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_BLEND_STATE);

	// Must dirty blend state here so we re-copy next time.  Example: Lunar's spell effects.
	if (fboTexBound_)
		gstate_c.Dirty(DIRTY_BLEND_STATE);
}
