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
#include "GPU/D3D11/FramebufferManagerD3D11.h"

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

// These are actually the same exact values/order/etc. as the GE ones, but for clarity...
/*
static const D3D11_LOGIC_OP logicOps[] = {
	D3D11_LOGIC_OP_CLEAR,
	D3D11_LOGIC_OP_AND,
	D3D11_LOGIC_OP_AND_REVERSE,
	D3D11_LOGIC_OP_COPY,
	D3D11_LOGIC_OP_AND_INVERTED,
	D3D11_LOGIC_OP_NO_OP,
	D3D11_LOGIC_OP_XOR,
	D3D11_LOGIC_OP_OR,
	D3D11_LOGIC_OP_NOR,
	D3D11_LOGIC_OP_EQUIVALENT,
	D3D11_LOGIC_OP_INVERT,
	D3D11_LOGIC_OP_OR_REVERSE,
	D3D11_LOGIC_OP_COPY_INVERTED,
	D3D11_LOGIC_OP_OR_INVERTED,
	D3D11_LOGIC_OP_NAND,
	D3D11_LOGIC_OP_SET,
};
*/

static bool ApplyShaderBlending() {
	return false;
}

static void ResetShaderBlending() {
	//
}

class FramebufferManagerD3D11;
class ShaderManagerD3D11;

// TODO: Do this more progressively. No need to compute the entire state if the entire state hasn't changed.
// In Vulkan, we simply collect all the state together into a "pipeline key" - we don't actually set any state here
// (the caller is responsible for setting the little dynamic state that is supported, dynState).

struct D3D11BlendKey {
	// Blend
	unsigned int blendEnable : 1;
	unsigned int srcColor : 5;  // D3D11_BLEND
	unsigned int destColor : 5;  // D3D11_BLEND
	unsigned int srcAlpha : 5;  // D3D11_BLEND
	unsigned int destAlpha : 5;  // D3D11_BLEND
	unsigned int blendOpColor : 3;  // D3D11_BLEND_OP
	unsigned int blendOpAlpha : 3;  // D3D11_BLEND_OP
	unsigned int logicOpEnable : 1;
	unsigned int logicOp : 4;  // D3D11_LOGIC_OP
	unsigned int colorWriteMask : 4;
};

struct D3D11DepthStencilKey {
	// Depth/Stencil
	unsigned int depthTestEnable : 1;
	unsigned int depthWriteEnable : 1;
	unsigned int depthCompareOp : 4;  // D3D11_COMPARISON   (-1 and we could fit it in 3 bits)
	unsigned int stencilTestEnable : 1;
	unsigned int stencilCompareOp : 4;  // D3D11_COMPARISON
	unsigned int stencilPassOp : 4; // D3D11_STENCIL_OP
	unsigned int stencilFailOp : 4; // D3D11_STENCIL_OP
	unsigned int stencilDepthFailOp : 4;  // D3D11_STENCIL_OP
};

struct D3D11RasterKey {
	unsigned int cullMode : 2;  // D3D11_CULL_MODE 
};

// In D3D11 we cache blend state objects etc, and we simply emit keys, which are then also used to create these objects.
struct D3D11StateKeys {
	D3D11BlendKey blend;
	D3D11DepthStencilKey depthStencil;
	D3D11RasterKey raster;
};

struct D3D11DynamicState {
	int topology;
	bool useBlendColor;
	uint32_t blendColor;
	bool useStencil;
	uint8_t stencilRef;
	uint8_t stencilWriteMask;
	uint8_t stencilCompareMask;
	D3D11_VIEWPORT viewport;
	D3D11_RECT scissor;
};

void ConvertStateToKeys(FramebufferManagerCommon *fbManager, ShaderManagerD3D11 *shaderManager, int prim, D3D11StateKeys &key, D3D11DynamicState &dynState) {
	memset(&key, 0, sizeof(key));
	memset(&dynState, 0, sizeof(dynState));
	// Unfortunately, this isn't implemented yet.
	gstate_c.allowShaderBlend = false;

	// Set blend - unless we need to do it in the shader.
	GenericBlendState blendState;
	ConvertBlendState(blendState, gstate_c.allowShaderBlend);

	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

	ViewportAndScissor vpAndScissor;
	ConvertViewportAndScissor(useBufferedRendering,
		fbManager->GetRenderWidth(), fbManager->GetRenderHeight(),
		fbManager->GetTargetBufferWidth(), fbManager->GetTargetBufferHeight(),
		vpAndScissor);

	if (blendState.applyShaderBlending) {
		if (ApplyShaderBlending()) {
			// We may still want to do something about stencil -> alpha.
			ApplyStencilReplaceAndLogicOp(blendState.replaceAlphaWithStencil, blendState);
		} else {
			// Until next time, force it off.
			ResetShaderBlending();
			gstate_c.allowShaderBlend = false;
		}
	} else if (blendState.resetShaderBlending) {
		ResetShaderBlending();
	}

	if (blendState.enabled) {
		key.blend.blendEnable = true;
		key.blend.blendOpColor = d3d11BlendEqLookup[(size_t)blendState.eqColor];
		key.blend.blendOpAlpha = d3d11BlendEqLookup[(size_t)blendState.eqAlpha];
		key.blend.srcColor = d3d11BlendFactorLookup[(size_t)blendState.srcColor];
		key.blend.srcAlpha = d3d11BlendFactorLookup[(size_t)blendState.srcAlpha];
		key.blend.destColor = d3d11BlendFactorLookup[(size_t)blendState.dstColor];
		key.blend.destAlpha = d3d11BlendFactorLookup[(size_t)blendState.dstAlpha];
		if (blendState.dirtyShaderBlend) {
			gstate_c.Dirty(DIRTY_SHADERBLEND);
		}
		dynState.useBlendColor = blendState.useBlendColor;
		if (blendState.useBlendColor) {
			dynState.blendColor = blendState.blendColor;
		}
	} else {
		key.blend.blendEnable = false;
		dynState.useBlendColor = false;
	}

	dynState.useStencil = false;

	// Set ColorMask/Stencil/Depth
	if (gstate.isModeClear()) {
		key.blend.logicOpEnable = false;
		key.raster.cullMode = D3D11_CULL_NONE;

		key.depthStencil.depthTestEnable = true;
		key.depthStencil.depthCompareOp = D3D11_COMPARISON_ALWAYS;
		key.depthStencil.depthWriteEnable = gstate.isClearModeDepthMask();
		if (gstate.isClearModeDepthMask()) {
			fbManager->SetDepthUpdated();
		}

		// Color Test
		bool colorMask = gstate.isClearModeColorMask();
		bool alphaMask = gstate.isClearModeAlphaMask();
		key.blend.colorWriteMask = (colorMask ? (1 | 2 | 4) : 0) | (alphaMask ? 8 : 0);

		// Stencil Test
		if (alphaMask) {
			key.depthStencil.stencilTestEnable = true;
			key.depthStencil.stencilCompareOp = D3D11_COMPARISON_ALWAYS;
			key.depthStencil.stencilPassOp = D3D11_STENCIL_OP_REPLACE;
			key.depthStencil.stencilFailOp = D3D11_STENCIL_OP_REPLACE;
			key.depthStencil.stencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
			dynState.useStencil = true;
			// In clear mode, the stencil value is set to the alpha value of the vertex.
			// A normal clear will be 2 points, the second point has the color.
			// We override this value in the pipeline from software transform for clear rectangles.
			dynState.stencilRef = 0xFF;
			dynState.stencilWriteMask = 0xFF;
		} else {
			key.depthStencil.stencilTestEnable = false;
			dynState.useStencil = false;
		}
	} else {
		if (gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP)) {
			// Logic Ops
			if (gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY) {
				key.blend.logicOpEnable = true;
				// key.blendKey.logicOp = logicOps[gstate.getLogicOp()];
			} else {
				key.blend.logicOpEnable = false;
			}
		}

		// Set cull
		bool wantCull = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		key.raster.cullMode = wantCull ? (gstate.getCullMode() ? D3D11_CULL_FRONT : D3D11_CULL_BACK) : D3D11_CULL_NONE;

		// Depth Test
		if (gstate.isDepthTestEnabled()) {
			key.depthStencil.depthTestEnable = true;
			key.depthStencil.depthCompareOp = compareOps[gstate.getDepthTestFunction()];
			key.depthStencil.depthWriteEnable = gstate.isDepthWriteEnabled();
			if (gstate.isDepthWriteEnabled()) {
				fbManager->SetDepthUpdated();
			}
		} else {
			key.depthStencil.depthTestEnable = false;
			key.depthStencil.depthWriteEnable = false;
			key.depthStencil.depthCompareOp = D3D11_COMPARISON_ALWAYS;
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

		key.blend.colorWriteMask = (rmask ? 1 : 0) | (gmask ? 2 : 0) | (bmask ? 4 : 0) | (amask ? 8 : 0);

		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);

		// Stencil Test
		if (stencilState.enabled) {
			key.depthStencil.stencilTestEnable = true;
			key.depthStencil.stencilCompareOp = compareOps[stencilState.testFunc];
			key.depthStencil.stencilPassOp = stencilOps[stencilState.zPass];
			key.depthStencil.stencilFailOp = stencilOps[stencilState.sFail];
			key.depthStencil.stencilDepthFailOp = stencilOps[stencilState.zFail];
			dynState.useStencil = true;
			dynState.stencilRef = stencilState.testRef;
			dynState.stencilCompareMask = stencilState.testMask;
			dynState.stencilWriteMask = stencilState.writeMask;
		} else {
			key.depthStencil.stencilTestEnable = false;
			dynState.useStencil = false;
		}
	}

	dynState.topology = primToD3D11[prim];

	D3D11_VIEWPORT &vp = dynState.viewport;
	vp.TopLeftX = vpAndScissor.viewportX;
	vp.TopLeftY = vpAndScissor.viewportY;
	vp.Width = vpAndScissor.viewportW;
	vp.Height = vpAndScissor.viewportH;
	vp.MinDepth = vpAndScissor.depthRangeMin;
	vp.MaxDepth = vpAndScissor.depthRangeMax;
	if (vpAndScissor.dirtyProj) {
		gstate_c.Dirty(DIRTY_PROJMATRIX);
	}

	D3D11_RECT &scissor = dynState.scissor;
	scissor.left = vpAndScissor.scissorX;
	scissor.top = vpAndScissor.scissorY;
	scissor.right = vpAndScissor.scissorX + vpAndScissor.scissorW;
	scissor.bottom = vpAndScissor.scissorY + vpAndScissor.scissorH;

	float depthMin = vpAndScissor.depthRangeMin;
	float depthMax = vpAndScissor.depthRangeMax;

	if (depthMin < 0.0f) depthMin = 0.0f;
	if (depthMax > 1.0f) depthMax = 1.0f;
	if (vpAndScissor.dirtyDepth) {
		gstate_c.Dirty(DIRTY_DEPTHRANGE);
	}
}

void DrawEngineD3D11::ApplyDrawState(int prim) {
	D3D11StateKeys keys;
	D3D11DynamicState dynState;
	ConvertStateToKeys(framebufferManager_, shaderManager_, prim, keys, dynState);

	uint32_t blendKey, depthKey, rasterKey;
	memcpy(&blendKey, &keys.blend, sizeof(uint32_t));
	memcpy(&depthKey, &keys.depthStencil, sizeof(uint32_t));
	memcpy(&rasterKey, &keys.raster, sizeof(uint32_t));

	ID3D11BlendState *bs = nullptr;
	ID3D11DepthStencilState *ds = nullptr;
	ID3D11RasterizerState *rs = nullptr;

	auto blendIter = blendCache_.find(blendKey);
	if (blendIter == blendCache_.end()) {
		D3D11_BLEND_DESC desc{};
		D3D11_RENDER_TARGET_BLEND_DESC &rt = desc.RenderTarget[0];
		rt.BlendEnable = keys.blend.blendEnable;
		rt.BlendOp = (D3D11_BLEND_OP)keys.blend.blendOpColor;
		rt.BlendOpAlpha = (D3D11_BLEND_OP)keys.blend.blendOpAlpha;
		rt.SrcBlend = (D3D11_BLEND)keys.blend.srcColor;
		rt.DestBlend = (D3D11_BLEND)keys.blend.destColor;
		rt.SrcBlendAlpha = (D3D11_BLEND)keys.blend.srcAlpha;
		rt.DestBlendAlpha = (D3D11_BLEND)keys.blend.destAlpha;
		rt.RenderTargetWriteMask = keys.blend.colorWriteMask;
		device_->CreateBlendState(&desc, &bs);
		blendCache_.insert(std::pair<uint32_t, ID3D11BlendState *>(blendKey, bs));
	} else {
		bs = blendIter->second;
	}

	auto depthIter = depthStencilCache_.find(depthKey);
	if (depthIter == depthStencilCache_.end()) {
		D3D11_DEPTH_STENCIL_DESC desc{};
		desc.DepthEnable = keys.depthStencil.depthTestEnable;
		desc.DepthWriteMask = keys.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = (D3D11_COMPARISON_FUNC)keys.depthStencil.depthCompareOp;
		desc.StencilEnable = FALSE; // keys.depthStencil.stencilTestEnable;

		// ...
		device_->CreateDepthStencilState(&desc, &ds);
		depthStencilCache_.insert(std::pair<uint32_t, ID3D11DepthStencilState *>(depthKey, ds));
	} else {
		ds = depthIter->second;
	}

	float blendColor[4];
	Uint8x4ToFloat4(blendColor, dynState.blendColor);
	context_->OMSetBlendState(bs, blendColor, 0xFFFFFFFF);
}

void DrawEngineD3D11::ApplyDrawStateLate(bool applyStencilRef, uint8_t stencilRef) {
	if (applyStencilRef) {
		// context_->OMSetDepthStencilState(state, stencilRef);
	}
}