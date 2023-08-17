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

#include "Common/Profiler/Profiler.h"
#include "Common/GPU/D3D9/D3D9ShaderCompiler.h"
#include "Common/GPU/D3D9/D3D9StateCache.h"

#include "Core/System.h"
#include "Core/Config.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Directx9/GPU_DX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/FramebufferManagerDX9.h"

static const D3DBLEND dxBlendFactorLookup[(size_t)BlendFactor::COUNT] = {
	D3DBLEND_ZERO,
	D3DBLEND_ONE,
	D3DBLEND_SRCCOLOR,
	D3DBLEND_INVSRCCOLOR,
	D3DBLEND_DESTCOLOR,
	D3DBLEND_INVDESTCOLOR,
	D3DBLEND_SRCALPHA,
	D3DBLEND_INVSRCALPHA,
	D3DBLEND_DESTALPHA,
	D3DBLEND_INVDESTALPHA,
	D3DBLEND_BLENDFACTOR,
	D3DBLEND_INVBLENDFACTOR,
	D3DBLEND_BLENDFACTOR,
	D3DBLEND_INVBLENDFACTOR,
#if 0   // TODO: Requires D3D9Ex
	D3DBLEND_SRCCOLOR2,
	D3DBLEND_INVSRCCOLOR2,
	D3DBLEND_SRCCOLOR2,
	D3DBLEND_INVSRCCOLOR2,
#else
	D3DBLEND_FORCE_DWORD,
	D3DBLEND_FORCE_DWORD,
#endif
	D3DBLEND_FORCE_DWORD,
};

static const D3DBLENDOP dxBlendEqLookup[(size_t)BlendEq::COUNT] = {
	D3DBLENDOP_ADD,
	D3DBLENDOP_SUBTRACT,
	D3DBLENDOP_REVSUBTRACT,
	D3DBLENDOP_MIN,
	D3DBLENDOP_MAX,
};

static const D3DCULL cullingMode[] = {
	D3DCULL_CW,
	D3DCULL_CCW,
};

static const D3DCMPFUNC ztests[] = {
	D3DCMP_NEVER, D3DCMP_ALWAYS, D3DCMP_EQUAL, D3DCMP_NOTEQUAL, 
	D3DCMP_LESS, D3DCMP_LESSEQUAL, D3DCMP_GREATER, D3DCMP_GREATEREQUAL,
};

static const D3DSTENCILOP stencilOps[] = {
	D3DSTENCILOP_KEEP,
	D3DSTENCILOP_ZERO,
	D3DSTENCILOP_REPLACE,
	D3DSTENCILOP_INVERT,
	D3DSTENCILOP_INCRSAT,
	D3DSTENCILOP_DECRSAT,
	D3DSTENCILOP_KEEP, // reserved
	D3DSTENCILOP_KEEP, // reserved
};

void DrawEngineDX9::ApplyDrawState(int prim) {
	if (!gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE)) {
		// nothing to do
		return;
	}

	// At this point, we know if the vertices are full alpha or not.
	// TODO: Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
	if (!gstate.isModeClear()) {
		textureCache_->ApplyTexture();

		if (fboTexBindState_ == FBO_TEX_COPY_BIND_TEX) {
			// Note that this is positions, not UVs, that we need the copy from.
			framebufferManager_->BindFramebufferAsColorTexture(1, framebufferManager_->GetCurrentRenderVFB(), BINDFBCOLOR_MAY_COPY, 0);
			// If we are rendering at a higher resolution, linear is probably best for the dest color.
			device_->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			device_->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			fboTexBound_ = true;
			fboTexBindState_ = FBO_TEX_NONE;
		}

		// TODO: Test texture?
	}

	bool useBufferedRendering = framebufferManager_->UseBufferedRendering();

	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		if (gstate.isModeClear()) {
			dxstate.blend.disable();
			// Color Mask
			u32 mask = 0;
			if (gstate.isClearModeColorMask()) {
				mask |= 7;
			}
			if (gstate.isClearModeAlphaMask()) {
				mask |= 8;
			}
			dxstate.colorMask.set(mask);
		} else {
			pipelineState_.Convert(draw_->GetShaderLanguageDesc().bitwiseOps);
			GenericMaskState &maskState = pipelineState_.maskState;
			GenericBlendState &blendState = pipelineState_.blendState;
			// We ignore the logicState on D3D since there's no support, the emulation of it is blend-and-shader only.

			if (pipelineState_.FramebufferRead()) {
				ApplyFramebufferRead(&fboTexBindState_);
				// The shader takes over the responsibility for blending, so recompute.
				ApplyStencilReplaceAndLogicOpIgnoreBlend(blendState.replaceAlphaWithStencil, blendState);

				if (fboTexBindState_ == FBO_TEX_COPY_BIND_TEX) {
					// Note that this is positions, not UVs, that we need the copy from.
					framebufferManager_->BindFramebufferAsColorTexture(1, framebufferManager_->GetCurrentRenderVFB(), BINDFBCOLOR_MAY_COPY | BINDFBCOLOR_UNCACHED, Draw::ALL_LAYERS);
					// If we are rendering at a higher resolution, linear is probably best for the dest color.
					device_->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
					device_->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
					fboTexBound_ = true;
					fboTexBindState_ = FBO_TEX_NONE;
					dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE;
					gstate_c.Dirty(DIRTY_BLEND_STATE);
				} else if (fboTexBindState_ == FBO_TEX_READ_FRAMEBUFFER) {
					// Not supported.
					fboTexBindState_ = FBO_TEX_NONE;
				}

				dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
				gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
			} else {
				if (fboTexBound_) {
					dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
					gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
					fboTexBound_ = false;
				}
			}

			if (blendState.blendEnabled) {
				dxstate.blend.enable();
				dxstate.blendSeparate.enable();
				dxstate.blendEquation.set(dxBlendEqLookup[(size_t)blendState.eqColor], dxBlendEqLookup[(size_t)blendState.eqAlpha]);
				dxstate.blendFunc.set(
					dxBlendFactorLookup[(size_t)blendState.srcColor], dxBlendFactorLookup[(size_t)blendState.dstColor],
					dxBlendFactorLookup[(size_t)blendState.srcAlpha], dxBlendFactorLookup[(size_t)blendState.dstAlpha]);
				if (blendState.dirtyShaderBlendFixValues) {
					dirtyRequiresRecheck_ |= DIRTY_SHADERBLEND;
					gstate_c.Dirty(DIRTY_SHADERBLEND);
				}
				if (blendState.useBlendColor) {
					dxstate.blendColor.setDWORD(blendState.blendColor);
				}
			} else {
				dxstate.blend.disable();
			}

			dxstate.colorMask.set(maskState.channelMask);
		}
	}

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		bool wantCull = !gstate.isModeClear() && prim != GE_PRIM_RECTANGLES && prim > GE_PRIM_LINE_STRIP && gstate.isCullEnabled();
		if (wantCull) {
			if (gstate.getCullMode() == 1) {
				dxstate.cullMode.set(D3DCULL_CCW);
			} else {
				dxstate.cullMode.set(D3DCULL_CW);
			}
		} else {
			dxstate.cullMode.set(D3DCULL_NONE);
		}
		if (gstate.isModeClear()) {
			// Well, probably doesn't matter...
			dxstate.shadeMode.set(D3DSHADE_GOURAUD);
		} else {
			dxstate.shadeMode.set(gstate.getShadeMode() == GE_SHADE_GOURAUD ? D3DSHADE_GOURAUD : D3DSHADE_FLAT);
		}

		// We use fixed-function user clipping on D3D9, where available, for negative Z clipping.
		if (draw_->GetDeviceCaps().clipPlanesSupported >= 1) {
			bool wantClip = !gstate.isModeThrough() && gstate_c.submitType == SubmitType::DRAW;
			dxstate.clipPlaneEnable.set(wantClip ? 1 : 0);
		}
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);

		// Set Stencil/Depth
		if (gstate.isModeClear()) {
			// Depth Test
			dxstate.depthTest.enable();
			dxstate.depthFunc.set(D3DCMP_ALWAYS);
			dxstate.depthWrite.set(gstate.isClearModeDepthMask());

			// Stencil Test
			bool alphaMask = gstate.isClearModeAlphaMask();
			if (alphaMask) {
				dxstate.stencilTest.enable();
				dxstate.stencilOp.set(D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE);
				dxstate.stencilFunc.set(D3DCMP_ALWAYS);
				dxstate.stencilRef.set(0xFF);
				dxstate.stencilCompareMask.set(0xFF);
				dxstate.stencilWriteMask.set(stencilState.writeMask);
			} else {
				dxstate.stencilTest.disable();
			}
		} else {
			// Depth Test
			if (!IsDepthTestEffectivelyDisabled()) {
				dxstate.depthTest.enable();
				dxstate.depthFunc.set(ztests[gstate.getDepthTestFunction()]);
				dxstate.depthWrite.set(gstate.isDepthWriteEnabled());
				UpdateEverUsedEqualDepth(gstate.getDepthTestFunction());
			} else {
				dxstate.depthTest.disable();
			}

			// Stencil Test
			if (stencilState.enabled) {
				dxstate.stencilTest.enable();
				dxstate.stencilFunc.set(ztests[stencilState.testFunc]);
				dxstate.stencilRef.set(stencilState.testRef);
				dxstate.stencilCompareMask.set(stencilState.testMask);
				dxstate.stencilOp.set(stencilOps[stencilState.sFail], stencilOps[stencilState.zFail], stencilOps[stencilState.zPass]);
				dxstate.stencilWriteMask.set(stencilState.writeMask);

				// Nasty special case for Spongebob and similar where it tries to write zeros to alpha/stencil during
				// depth-fail. We can't write to alpha then because the pixel is killed. However, we can invert the depth
				// test and modify the alpha function...
				if (SpongebobDepthInverseConditions(stencilState)) {
					dxstate.blend.set(true);
					dxstate.blendEquation.set(D3DBLENDOP_ADD, D3DBLENDOP_ADD);
					dxstate.blendFunc.set(D3DBLEND_ZERO, D3DBLEND_ZERO, D3DBLEND_ZERO, D3DBLEND_ZERO);
					dxstate.colorMask.set(8);

					dxstate.depthFunc.set(D3DCMP_LESS);
					dxstate.stencilFunc.set(D3DCMP_ALWAYS);
					// Invert
					dxstate.stencilOp.set(D3DSTENCILOP_ZERO, D3DSTENCILOP_KEEP, D3DSTENCILOP_ZERO);

					dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE;
					gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
				}
			} else {
				dxstate.stencilTest.disable();
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

		dxstate.scissorTest.enable();
		dxstate.scissorRect.set(vpAndScissor.scissorX, vpAndScissor.scissorY, vpAndScissor.scissorX + vpAndScissor.scissorW, vpAndScissor.scissorY + vpAndScissor.scissorH);

		float depthMin = vpAndScissor.depthRangeMin;
		float depthMax = vpAndScissor.depthRangeMax;

		dxstate.viewport.set(vpAndScissor.viewportX, vpAndScissor.viewportY, vpAndScissor.viewportW, vpAndScissor.viewportH, depthMin, depthMax);
	}

	gstate_c.Clean(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_BLEND_STATE);
	gstate_c.Dirty(dirtyRequiresRecheck_);
	dirtyRequiresRecheck_ = 0;
}

void DrawEngineDX9::ApplyDrawStateLate() {
	// At this point, we know if the vertices are full alpha or not.
	// TODO: Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
}
