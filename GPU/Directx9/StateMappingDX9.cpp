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
#include "Core/Reporting.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Directx9/GPU_DX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/FramebufferManagerDX9.h"

namespace DX9 {

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

inline void DrawEngineDX9::ResetFramebufferRead() {
	if (fboTexBound_) {
		device_->SetTexture(1, nullptr);
		fboTexBound_ = false;
	}
}

void DrawEngineDX9::ApplyDrawState(int prim) {
	// TODO: All this setup is soon so expensive that we'll need dirty flags, or simply do it in the command writes where we detect dirty by xoring. Silly to do all this work on every drawcall.

	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	} else if (gstate.getTextureAddress(0) == ((gstate.getFrameBufRawAddress() | 0x04000000) & 0x3FFFFFFF)) {
		// This catches the case of clearing a texture.
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	}

	// Start profiling here to skip SetTexture which is already accounted for
	PROFILE_THIS_SCOPE("applydrawstate");

	bool useBufferedRendering = framebufferManager_->UseBufferedRendering();

	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		gstate_c.Clean(DIRTY_BLEND_STATE);
		// Unfortunately, this isn't implemented on DX9 yet.
		gstate_c.SetAllowFramebufferRead(false);
		if (gstate.isModeClear()) {
			dxstate.blend.disable();

			// Color Mask
			bool colorMask = gstate.isClearModeColorMask();
			bool alphaMask = gstate.isClearModeAlphaMask();
			dxstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);
		} else {
			GenericMaskState maskState;
			ConvertMaskState(maskState, gstate_c.allowFramebufferRead);

			// Set blend - unless we need to do it in the shader.
			GenericBlendState blendState;
			ConvertBlendState(blendState, gstate_c.allowFramebufferRead, maskState.applyFramebufferRead);

			if (blendState.applyFramebufferRead || maskState.applyFramebufferRead) {
				ApplyFramebufferRead(&fboTexNeedsBind_);
				// The shader takes over the responsibility for blending, so recompute.
				ApplyStencilReplaceAndLogicOpIgnoreBlend(blendState.replaceAlphaWithStencil, blendState);
				gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
			} else if (blendState.resetFramebufferRead) {
				ResetFramebufferRead();
			}

			if (blendState.enabled) {
				dxstate.blend.enable();
				dxstate.blendSeparate.enable();
				dxstate.blendEquation.set(dxBlendEqLookup[(size_t)blendState.eqColor], dxBlendEqLookup[(size_t)blendState.eqAlpha]);
				dxstate.blendFunc.set(
					dxBlendFactorLookup[(size_t)blendState.srcColor], dxBlendFactorLookup[(size_t)blendState.dstColor],
					dxBlendFactorLookup[(size_t)blendState.srcAlpha], dxBlendFactorLookup[(size_t)blendState.dstAlpha]);
				if (blendState.dirtyShaderBlendFixValues) {
					gstate_c.Dirty(DIRTY_SHADERBLEND);
				}
				if (blendState.useBlendColor) {
					dxstate.blendColor.setDWORD(blendState.blendColor);
				}
			} else {
				dxstate.blend.disable();
			}

			dxstate.colorMask.set(maskState.rgba[0], maskState.rgba[1], maskState.rgba[2], maskState.rgba[3]);
		}
	}

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		gstate_c.Clean(DIRTY_RASTER_STATE);
		// Set Dither
		if (gstate.isDitherEnabled()) {
			dxstate.dither.enable();
		} else {
			dxstate.dither.disable();
		}
		bool wantCull = !gstate.isModeClear() && prim != GE_PRIM_RECTANGLES && prim > GE_PRIM_LINE_STRIP && gstate.isCullEnabled();
		dxstate.cullMode.set(wantCull, gstate.getCullMode());
		if (gstate.isModeClear()) {
			// Well, probably doesn't matter...
			dxstate.shadeMode.set(D3DSHADE_GOURAUD);
		} else {
			dxstate.shadeMode.set(gstate.getShadeMode() == GE_SHADE_GOURAUD ? D3DSHADE_GOURAUD : D3DSHADE_FLAT);
		}
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		gstate_c.Clean(DIRTY_DEPTHSTENCIL_STATE);
		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);

		// Set Stencil/Depth
		if (gstate.isModeClear()) {
			// Depth Test
			dxstate.depthTest.enable();
			dxstate.depthFunc.set(D3DCMP_ALWAYS);
			dxstate.depthWrite.set(gstate.isClearModeDepthMask());
			if (gstate.isClearModeDepthMask()) {
				framebufferManager_->SetDepthUpdated();
			}

			// Stencil Test
			bool alphaMask = gstate.isClearModeAlphaMask();
			if (alphaMask) {
				dxstate.stencilTest.enable();
				dxstate.stencilOp.set(D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE);
				dxstate.stencilFunc.set(D3DCMP_ALWAYS, 255, 0xFF);
				dxstate.stencilMask.set(stencilState.writeMask);
			} else {
				dxstate.stencilTest.disable();
			}

		} else {
			// Depth Test
			if (gstate.isDepthTestEnabled()) {
				dxstate.depthTest.enable();
				dxstate.depthFunc.set(ztests[gstate.getDepthTestFunction()]);
				dxstate.depthWrite.set(gstate.isDepthWriteEnabled());
				if (gstate.isDepthWriteEnabled()) {
					framebufferManager_->SetDepthUpdated();
				}
			} else {
				dxstate.depthTest.disable();
			}

			// Stencil Test
			if (stencilState.enabled) {
				dxstate.stencilTest.enable();
				dxstate.stencilFunc.set(ztests[stencilState.testFunc], stencilState.testRef, stencilState.testMask);
				dxstate.stencilOp.set(stencilOps[stencilState.sFail], stencilOps[stencilState.zFail], stencilOps[stencilState.zPass]);
				dxstate.stencilMask.set(stencilState.writeMask);
			} else {
				dxstate.stencilTest.disable();
			}
		}
	}

	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		gstate_c.Clean(DIRTY_VIEWPORTSCISSOR_STATE);
		ViewportAndScissor vpAndScissor;
		ConvertViewportAndScissor(useBufferedRendering,
			framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
			framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
			vpAndScissor);

		dxstate.scissorTest.enable();
		dxstate.scissorRect.set(vpAndScissor.scissorX, vpAndScissor.scissorY, vpAndScissor.scissorX + vpAndScissor.scissorW, vpAndScissor.scissorY + vpAndScissor.scissorH);

		float depthMin = vpAndScissor.depthRangeMin;
		float depthMax = vpAndScissor.depthRangeMax;

		dxstate.viewport.set(vpAndScissor.viewportX, vpAndScissor.viewportY, vpAndScissor.viewportW, vpAndScissor.viewportH, depthMin, depthMax);
		if (vpAndScissor.dirtyProj) {
			gstate_c.Dirty(DIRTY_PROJMATRIX);
		}
		if (vpAndScissor.dirtyDepth) {
			gstate_c.Dirty(DIRTY_DEPTHRANGE);
		}
	}
}

void DrawEngineDX9::ApplyDrawStateLate() {
	// At this point, we know if the vertices are full alpha or not.
	// TODO: Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
	if (!gstate.isModeClear()) {
		textureCache_->ApplyTexture();

		if (fboTexNeedsBind_) {
			// Note that this is positions, not UVs, that we need the copy from.
			framebufferManager_->BindFramebufferAsColorTexture(1, framebufferManager_->GetCurrentRenderVFB(), BINDFBCOLOR_MAY_COPY);
			// If we are rendering at a higher resolution, linear is probably best for the dest color.
			device_->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			device_->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			fboTexBound_ = true;
			fboTexNeedsBind_ = false;
		}

		// TODO: Test texture?
	}
}

}
