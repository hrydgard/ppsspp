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

#include <algorithm>

#include "Common/Data/Convert/SmallDataConvert.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/GPUStateUtils.h"
#include "Core/System.h"
#include "Core/Config.h"

#include "GPU/Common/FramebufferManagerCommon.h"
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
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, // Points are expanded to triangles.
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, // Lines are expanded to triangles too.
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, // Lines are expanded to triangles too.
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,  // D3D11 doesn't do triangle fans.
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, // Rectangles are expanded to triangles.
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

class FramebufferManagerD3D11;
class ShaderManagerD3D11;

void DrawEngineD3D11::ApplyDrawState(int prim) {
	dynState_.topology = primToD3D11[prim];

	if (!gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE)) {
		// nothing to do
		return;
	}

	bool useBufferedRendering = framebufferManager_->UseBufferedRendering();
	// Blend
	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
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

			pipelineState_.Convert(draw_->GetShaderLanguageDesc().bitwiseOps);
			GenericMaskState &maskState = pipelineState_.maskState;
			GenericBlendState &blendState = pipelineState_.blendState;
			// We ignore the logicState on D3D since there's no support, the emulation of it is blend-and-shader only.

			if (pipelineState_.FramebufferRead()) {
				FBOTexState fboTexBindState = FBO_TEX_NONE;
				ApplyFramebufferRead(&fboTexBindState);
				// The shader takes over the responsibility for blending, so recompute.
				ApplyStencilReplaceAndLogicOpIgnoreBlend(blendState.replaceAlphaWithStencil, blendState);

				if (fboTexBindState == FBO_TEX_COPY_BIND_TEX) {
					framebufferManager_->BindFramebufferAsColorTexture(1, framebufferManager_->GetCurrentRenderVFB(), BINDFBCOLOR_MAY_COPY | BINDFBCOLOR_UNCACHED, 0);
					// No sampler required, we do a plain Load in the pixel shader.
					fboTexBound_ = true;
					fboTexBindState = FBO_TEX_NONE;

					framebufferManager_->RebindFramebuffer("RebindFramebuffer - ApplyDrawState");
					// Must dirty blend state here so we re-copy next time.  Example: Lunar's spell effects.
					dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE;
					gstate_c.Dirty(DIRTY_BLEND_STATE);
				}

				dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
				gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
			} else {
				if (fboTexBound_) {
					fboTexBound_ = false;
					dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
					gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
				}
			}

			if (blendState.blendEnabled) {
				keys_.blend.blendEnable = true;
				keys_.blend.logicOpEnable = false;
				keys_.blend.blendOpColor = d3d11BlendEqLookup[(size_t)blendState.eqColor];
				keys_.blend.blendOpAlpha = d3d11BlendEqLookup[(size_t)blendState.eqAlpha];
				keys_.blend.srcColor = d3d11BlendFactorLookup[(size_t)blendState.srcColor];
				keys_.blend.srcAlpha = d3d11BlendFactorLookup[(size_t)blendState.srcAlpha];
				keys_.blend.destColor = d3d11BlendFactorLookup[(size_t)blendState.dstColor];
				keys_.blend.destAlpha = d3d11BlendFactorLookup[(size_t)blendState.dstAlpha];
				if (blendState.dirtyShaderBlendFixValues) {
					dirtyRequiresRecheck_ |= DIRTY_SHADERBLEND;
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

			if (gstate_c.Use(GPU_USE_LOGIC_OP)) {
				// Logic Ops
				if (gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY) {
					keys_.blend.blendEnable = false;  // Can't have both blend & logic op - although I think the PSP can!
					keys_.blend.logicOpEnable = true;
					keys_.blend.logicOp = logicOps[gstate.getLogicOp()];
				} else {
					keys_.blend.logicOpEnable = false;
				}
			}

			keys_.blend.colorWriteMask = maskState.channelMask;
		}
	}

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		keys_.raster.value = 0;
		bool wantCull = !gstate.isModeClear() && prim != GE_PRIM_RECTANGLES && prim > GE_PRIM_LINE_STRIP && gstate.isCullEnabled();
		keys_.raster.cullMode = wantCull ? (gstate.getCullMode() ? D3D11_CULL_FRONT : D3D11_CULL_BACK) : D3D11_CULL_NONE;

		if (gstate.isModeClear() || gstate.isModeThrough()) {
			// TODO: Might happen in clear mode if not through...
			keys_.raster.depthClipEnable = 1;
		} else {
			if (gstate.getDepthRangeMin() == 0 || gstate.getDepthRangeMax() == 65535) {
				// TODO: Still has a bug where we clamp to depth range if one is not the full range.
				// But the alternate is not clamping in either direction...
				keys_.raster.depthClipEnable = !gstate.isDepthClampEnabled() || !gstate_c.Use(GPU_USE_DEPTH_CLAMP);
			} else {
				// We just want to clip in this case, the clamp would be clipped anyway.
				keys_.raster.depthClipEnable = 1;
			}
		}
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);

		if (gstate.isModeClear()) {
			keys_.depthStencil.value = 0;
			keys_.depthStencil.depthTestEnable = true;
			keys_.depthStencil.depthCompareOp = D3D11_COMPARISON_ALWAYS;
			keys_.depthStencil.depthWriteEnable = gstate.isClearModeDepthMask();

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
				// But we still apply the stencil write mask.
				keys_.depthStencil.stencilWriteMask = stencilState.writeMask;
			} else {
				keys_.depthStencil.stencilTestEnable = false;
				dynState_.useStencil = false;
			}

		} else {
			keys_.depthStencil.value = 0;
			// Depth Test
			if (!IsDepthTestEffectivelyDisabled()) {
				keys_.depthStencil.depthTestEnable = true;
				keys_.depthStencil.depthCompareOp = compareOps[gstate.getDepthTestFunction()];
				keys_.depthStencil.depthWriteEnable = gstate.isDepthWriteEnabled();
				UpdateEverUsedEqualDepth(gstate.getDepthTestFunction());
			} else {
				keys_.depthStencil.depthTestEnable = false;
				keys_.depthStencil.depthWriteEnable = false;
				keys_.depthStencil.depthCompareOp = D3D11_COMPARISON_ALWAYS;
			}

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

				// Nasty special case for Spongebob and similar where it tries to write zeros to alpha/stencil during
				// depth-fail. We can't write to alpha then because the pixel is killed. However, we can invert the depth
				// test and modify the alpha function...
				if (SpongebobDepthInverseConditions(stencilState)) {
					keys_.blend.blendEnable = true;
					keys_.blend.blendOpAlpha = D3D11_BLEND_OP_ADD;
					keys_.blend.blendOpColor = D3D11_BLEND_OP_ADD;
					keys_.blend.srcColor = D3D11_BLEND_ZERO;
					keys_.blend.destColor = D3D11_BLEND_ZERO;
					keys_.blend.logicOpEnable = false;
					keys_.blend.srcAlpha = D3D11_BLEND_ZERO;
					keys_.blend.destAlpha = D3D11_BLEND_ZERO;
					keys_.blend.colorWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;

					keys_.depthStencil.depthCompareOp = D3D11_COMPARISON_LESS;  // Inverse of GREATER_EQUAL
					keys_.depthStencil.stencilCompareFunc = D3D11_COMPARISON_ALWAYS;
					// Invert
					keys_.depthStencil.stencilPassOp = D3D11_STENCIL_OP_ZERO;
					keys_.depthStencil.stencilFailOp = D3D11_STENCIL_OP_ZERO;
					keys_.depthStencil.stencilDepthFailOp = D3D11_STENCIL_OP_KEEP;

					dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE;
					gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
				}
			} else {
				keys_.depthStencil.stencilTestEnable = false;
				dynState_.useStencil = false;
			}
		}
	}

	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		ViewportAndScissor vpAndScissor;
		ConvertViewportAndScissor(useBufferedRendering,
			framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
			framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
			vpAndScissor);
		UpdateCachedViewportState(vpAndScissor);

		float depthMin = vpAndScissor.depthRangeMin;
		float depthMax = vpAndScissor.depthRangeMax;

		if (depthMin < 0.0f) depthMin = 0.0f;
		if (depthMax > 1.0f) depthMax = 1.0f;

		Draw::Viewport &vp = dynState_.viewport;
		vp.TopLeftX = vpAndScissor.viewportX;
		vp.TopLeftY = vpAndScissor.viewportY;
		vp.Width = vpAndScissor.viewportW;
		vp.Height = vpAndScissor.viewportH;
		vp.MinDepth = depthMin;
		vp.MaxDepth = depthMax;

		D3D11_RECT &scissor = dynState_.scissor;
		scissor.left = vpAndScissor.scissorX;
		scissor.top = vpAndScissor.scissorY;
		scissor.right = vpAndScissor.scissorX + std::max(0, vpAndScissor.scissorW);
		scissor.bottom = vpAndScissor.scissorY + std::max(0, vpAndScissor.scissorH);
	}

	// Actually create/set the state objects only after we're done mapping all the state.
	// There might have been interactions between depth and blend above.
	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		if (!device1_) {
			ID3D11BlendState *bs = nullptr;
			if (!blendCache_.Get(keys_.blend.value, &bs) || !bs) {
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
			ID3D11BlendState1 *bs1 = nullptr;
			if (!blendCache1_.Get(keys_.blend.value, &bs1) || !bs1) {
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
		ID3D11RasterizerState *rs;
		if (!rasterCache_.Get(keys_.raster.value, &rs) || !rs) {
			D3D11_RASTERIZER_DESC desc{};
			desc.CullMode = (D3D11_CULL_MODE)(keys_.raster.cullMode);
			desc.FillMode = D3D11_FILL_SOLID;
			desc.ScissorEnable = TRUE;
			desc.FrontCounterClockwise = TRUE;
			desc.DepthClipEnable = keys_.raster.depthClipEnable;
			ASSERT_SUCCESS(device_->CreateRasterizerState(&desc, &rs));
			rasterCache_.Insert(keys_.raster.value, rs);
		}
		rasterState_ = rs;
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		ID3D11DepthStencilState *ds;
		if (!depthStencilCache_.Get(keys_.depthStencil.value, &ds) || !ds) {
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

	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	} else if (gstate.getTextureAddress(0) == (gstate.getFrameBufRawAddress() | 0x04000000)) {
		// This catches the case of clearing a texture.
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	}
}

void DrawEngineD3D11::ApplyDrawStateLate(bool applyStencilRef, uint8_t stencilRef) {
	// we go through Draw here because it automatically handles screen rotation, as needed in UWP on mobiles.
	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		draw_->SetViewport(dynState_.viewport);
		draw_->SetScissorRect(dynState_.scissor.left, dynState_.scissor.top, dynState_.scissor.right - dynState_.scissor.left, dynState_.scissor.bottom - dynState_.scissor.top);
	}
	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		context_->RSSetState(rasterState_);
	}
	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		// Need to do this AFTER ApplyTexture because the process of depalettization can ruin the blend state.
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
	gstate_c.Dirty(dirtyRequiresRecheck_);
	dirtyRequiresRecheck_ = 0;
}
