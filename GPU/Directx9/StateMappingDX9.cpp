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

#include "profiler/profiler.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"

#include "gfx/d3d9_shader.h"
#include "gfx/d3d9_state.h"

#include "GPU/Directx9/GPU_DX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"

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

inline void DrawEngineDX9::ResetShaderBlending() {
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

	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		gstate_c.Clean(DIRTY_BLEND_STATE);
		// Unfortunately, this isn't implemented yet.
		gstate_c.SetAllowShaderBlend(false);
		if (gstate.isModeClear()) {
			dxstate.blend.disable();

			// Color Mask
			bool colorMask = gstate.isClearModeColorMask();
			bool alphaMask = gstate.isClearModeAlphaMask();
			dxstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);
		} else {
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
				dxstate.blend.enable();
				dxstate.blendSeparate.enable();
				dxstate.blendEquation.set(dxBlendEqLookup[(size_t)blendState.eqColor], dxBlendEqLookup[(size_t)blendState.eqAlpha]);
				dxstate.blendFunc.set(
					dxBlendFactorLookup[(size_t)blendState.srcColor], dxBlendFactorLookup[(size_t)blendState.dstColor],
					dxBlendFactorLookup[(size_t)blendState.srcAlpha], dxBlendFactorLookup[(size_t)blendState.dstAlpha]);
				if (blendState.dirtyShaderBlend) {
					gstate_c.Dirty(DIRTY_SHADERBLEND);
				}
				if (blendState.useBlendColor) {
					dxstate.blendColor.setDWORD(blendState.blendColor);
				}
			} else {
				dxstate.blend.disable();
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

			dxstate.colorMask.set(rmask, gmask, bmask, amask);
		}
	}

	bool enableStencilTest = !g_Config.bDisableStencilTest;

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		gstate_c.Clean(DIRTY_RASTER_STATE);
		// Set Dither
		if (gstate.isDitherEnabled()) {
			dxstate.dither.enable();
		} else {
			dxstate.dither.disable();
		}
		if (gstate.isModeClear()) {
			// Set Cull 
			dxstate.cullMode.set(false, false);
			// Well, probably doesn't matter...
			dxstate.shadeMode.set(D3DSHADE_GOURAUD);
		} else {
			// Set cull
			bool wantCull = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
			dxstate.cullMode.set(wantCull, gstate.getCullMode());
			dxstate.shadeMode.set(gstate.getShadeMode() == GE_SHADE_GOURAUD ? D3DSHADE_GOURAUD : D3DSHADE_FLAT);
		}
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		gstate_c.Clean(DIRTY_DEPTHSTENCIL_STATE);
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
			if (alphaMask && enableStencilTest) {
				dxstate.stencilTest.enable();
				dxstate.stencilOp.set(D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE);
				dxstate.stencilFunc.set(D3DCMP_ALWAYS, 255, 0xFF);
				dxstate.stencilMask.set(0xFF);
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

			GenericStencilFuncState stencilState;
			ConvertStencilFuncState(stencilState);

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

		if (vpAndScissor.scissorEnable) {
			dxstate.scissorTest.enable();
			dxstate.scissorRect.set(vpAndScissor.scissorX, vpAndScissor.scissorY, vpAndScissor.scissorX + vpAndScissor.scissorW, vpAndScissor.scissorY + vpAndScissor.scissorH);
		} else {
			dxstate.scissorTest.disable();
		}

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

		if (fboTexNeedBind_) {
			// Note that this is positions, not UVs, that we need the copy from.
			framebufferManager_->BindFramebufferAsColorTexture(1, framebufferManager_->GetCurrentRenderVFB(), BINDFBCOLOR_MAY_COPY);
			// If we are rendering at a higher resolution, linear is probably best for the dest color.
			device_->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			device_->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			fboTexBound_ = true;
			fboTexNeedBind_ = false;
		}

		// TODO: Test texture?
	}
}

}
