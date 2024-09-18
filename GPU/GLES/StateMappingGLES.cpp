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


// Alpha/stencil is a convoluted mess. Some good comments are here:
// https://github.com/hrydgard/ppsspp/issues/3768

#include "ppsspp_config.h"
#include "StateMappingGLES.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/OpenGL/GLDebugLog.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/Data/Convert/SmallDataConvert.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "GPU/GLES/GPU_GLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/Common/FragmentShaderGenerator.h"

static const GLushort glBlendFactorLookup[(size_t)BlendFactor::COUNT] = {
	GL_ZERO,
	GL_ONE,
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_CONSTANT_COLOR,
	GL_ONE_MINUS_CONSTANT_COLOR,
	GL_CONSTANT_ALPHA,
	GL_ONE_MINUS_CONSTANT_ALPHA,
#if !defined(USING_GLES2)   // TODO: Remove when we have better headers
	GL_SRC1_COLOR,
	GL_ONE_MINUS_SRC1_COLOR,
	GL_SRC1_ALPHA,
	GL_ONE_MINUS_SRC1_ALPHA,
#elif !PPSSPP_PLATFORM(IOS)
	GL_SRC1_COLOR_EXT,
	GL_ONE_MINUS_SRC1_COLOR_EXT,
	GL_SRC1_ALPHA_EXT,
	GL_ONE_MINUS_SRC1_ALPHA_EXT,
#else
	GL_INVALID_ENUM,
	GL_INVALID_ENUM,
	GL_INVALID_ENUM,
	GL_INVALID_ENUM,
#endif
	GL_INVALID_ENUM,
};

static const GLushort glBlendEqLookup[(size_t)BlendEq::COUNT] = {
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
	GL_MIN,
	GL_MAX,
};

static const GLushort cullingMode[] = {
	GL_FRONT,
	GL_BACK,
};

static const GLushort compareOps[] = {
	GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, 
	GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL,
};

static const GLushort stencilOps[] = {
	GL_KEEP,
	GL_ZERO,
	GL_REPLACE,
	GL_INVERT,
	GL_INCR,
	GL_DECR,
	GL_KEEP, // reserved
	GL_KEEP, // reserved
};

#if !defined(USING_GLES2)
static const GLushort logicOps[] = {
	GL_CLEAR,
	GL_AND,
	GL_AND_REVERSE,
	GL_COPY,
	GL_AND_INVERTED,
	GL_NOOP,
	GL_XOR,
	GL_OR,
	GL_NOR,
	GL_EQUIV,
	GL_INVERT,
	GL_OR_REVERSE,
	GL_COPY_INVERTED,
	GL_OR_INVERTED,
	GL_NAND,
	GL_SET,
};
#endif

void DrawEngineGLES::ApplyDrawState(int prim) {
	GLRenderManager *renderManager = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	if (!gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE)) {
		// Nothing to do, let's early-out
		return;
	}

	// Start profiling here to skip SetTexture which is already accounted for
	PROFILE_THIS_SCOPE("applydrawstate");

	uint64_t dirtyRequiresRecheck_ = 0;
	bool useBufferedRendering = framebufferManager_->UseBufferedRendering();

	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		if (gstate.isModeClear()) {
			// Color Test
			bool colorMask = gstate.isClearModeColorMask();
			bool alphaMask = gstate.isClearModeAlphaMask();
			renderManager->SetNoBlendAndMask((colorMask ? 7 : 0) | (alphaMask ? 8 : 0));
		} else {
			pipelineState_.Convert(draw_->GetShaderLanguageDesc().bitwiseOps);
			GenericMaskState &maskState = pipelineState_.maskState;
			GenericBlendState &blendState = pipelineState_.blendState;
			GenericLogicState &logicState = pipelineState_.logicState;

			if (pipelineState_.FramebufferRead()) {
				FBOTexState fboTexBindState = FBO_TEX_NONE;
				ApplyFramebufferRead(&fboTexBindState);
				// The shader takes over the responsibility for blending, so recompute.
				ApplyStencilReplaceAndLogicOpIgnoreBlend(blendState.replaceAlphaWithStencil, blendState);

				// We copy the framebuffer here, as doing so will wipe any blend state if we do it later.
				// fboTexNeedsBind_ won't be set if we can read directly from the target.
				if (fboTexBindState == FBO_TEX_COPY_BIND_TEX) {
					// Note that this is positions, not UVs, that we need the copy from.
					framebufferManager_->BindFramebufferAsColorTexture(1, framebufferManager_->GetCurrentRenderVFB(), BINDFBCOLOR_MAY_COPY | BINDFBCOLOR_UNCACHED, 0);
					// If we are rendering at a higher resolution, linear is probably best for the dest color.
					renderManager->SetTextureSampler(1, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, 0.0f);
					fboTexBound_ = true;

					framebufferManager_->RebindFramebuffer("RebindFramebuffer - ApplyDrawState");
					// Must dirty blend state here so we re-copy next time.  Example: Lunar's spell effects.
					dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE;
					gstate_c.Dirty(DIRTY_BLEND_STATE);
				} else if (fboTexBindState == FBO_TEX_READ_FRAMEBUFFER) {
					// No action needed here.
					fboTexBindState = FBO_TEX_NONE;
				}
				dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
				gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
			} else {
				if (fboTexBound_) {
					GLRenderManager *renderManager = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
					renderManager->BindTexture(TEX_SLOT_SHADERBLEND_SRC, nullptr);
					fboTexBound_ = false;
					dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
					gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
				}
			}

			if (blendState.blendEnabled) {
				if (blendState.dirtyShaderBlendFixValues) {
					// Not quite sure how necessary this is.
					dirtyRequiresRecheck_ |= DIRTY_SHADERBLEND;
					gstate_c.Dirty(DIRTY_SHADERBLEND);
				}
				if (blendState.useBlendColor) {
					uint32_t color = blendState.blendColor;
					float col[4];
					Uint8x4ToFloat4(col, color);
					renderManager->SetBlendFactor(col);
				}
			}

			int mask = (int)maskState.channelMask;
			if (blendState.blendEnabled) {
				renderManager->SetBlendAndMask(mask, blendState.blendEnabled,
					glBlendFactorLookup[(size_t)blendState.srcColor], glBlendFactorLookup[(size_t)blendState.dstColor],
					glBlendFactorLookup[(size_t)blendState.srcAlpha], glBlendFactorLookup[(size_t)blendState.dstAlpha],
					glBlendEqLookup[(size_t)blendState.eqColor], glBlendEqLookup[(size_t)blendState.eqAlpha]);
			} else {
				renderManager->SetNoBlendAndMask(mask);
			}

			// TODO: Get rid of the ifdef
#ifndef USING_GLES2
			if (gstate_c.Use(GPU_USE_LOGIC_OP)) {
				renderManager->SetLogicOp(logicState.logicOpEnabled, logicOps[(int)logicState.logicOp]);
			}
#endif
		}
	}

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		// Dither
		bool dither = gstate.isDitherEnabled();
		bool cullEnable;
		GLenum cullMode = cullingMode[gstate.getCullMode() ^ !useBufferedRendering];

		cullEnable = !gstate.isModeClear() && prim != GE_PRIM_RECTANGLES && prim > GE_PRIM_LINE_STRIP && gstate.isCullEnabled();

		bool depthClampEnable = false;
		if (gstate.isModeClear() || gstate.isModeThrough()) {
			// TODO: Might happen in clear mode if not through...
			depthClampEnable = false;
		} else {
			if (gstate.getDepthRangeMin() == 0 || gstate.getDepthRangeMax() == 65535) {
				// TODO: Still has a bug where we clamp to depth range if one is not the full range.
				// But the alternate is not clamping in either direction...
				depthClampEnable = gstate.isDepthClampEnabled() && gstate_c.Use(GPU_USE_DEPTH_CLAMP);
			} else {
				// We just want to clip in this case, the clamp would be clipped anyway.
				depthClampEnable = false;
			}
		}

		renderManager->SetRaster(cullEnable, GL_CCW, cullMode, dither, depthClampEnable);
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		ConvertStencilFuncState(stencilState_);

		if (gstate.isModeClear()) {
			renderManager->SetStencil(
				gstate.isClearModeAlphaMask(), GL_ALWAYS, 0xFF, 0xFF,
				stencilState_.writeMask, GL_REPLACE, GL_REPLACE, GL_REPLACE);
			renderManager->SetDepth(true, gstate.isClearModeDepthMask() ? true : false, GL_ALWAYS);
		} else {
			// Depth Test
			bool depthTestUsed = !IsDepthTestEffectivelyDisabled();
			renderManager->SetDepth(depthTestUsed, gstate.isDepthWriteEnabled(), compareOps[gstate.getDepthTestFunction()]);
			if (depthTestUsed)
				UpdateEverUsedEqualDepth(gstate.getDepthTestFunction());

			// Stencil Test
			if (stencilState_.enabled) {
				renderManager->SetStencil(
					stencilState_.enabled, compareOps[stencilState_.testFunc], stencilState_.testRef, stencilState_.testMask,
					stencilState_.writeMask, stencilOps[stencilState_.sFail], stencilOps[stencilState_.zFail], stencilOps[stencilState_.zPass]);

				// Nasty special case for Spongebob and similar where it tries to write zeros to alpha/stencil during
				// depth-fail. We can't write to alpha then because the pixel is killed. However, we can invert the depth
				// test and modify the alpha function...
				if (SpongebobDepthInverseConditions(stencilState_)) {
					renderManager->SetBlendAndMask(0x8, true, GL_ZERO, GL_ZERO, GL_ZERO, GL_ZERO, GL_FUNC_ADD, GL_FUNC_ADD);
					renderManager->SetDepth(true, false, GL_LESS);
					renderManager->SetStencil(true, GL_ALWAYS, 0xFF, 0xFF, 0xFF, GL_ZERO, GL_KEEP, GL_ZERO);

					dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE;
					gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
				}
			} else {
				renderManager->SetStencilDisabled();
			}
		}
	}

	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		ConvertViewportAndScissor(useBufferedRendering,
			framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
			framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
			vpAndScissor_);
		UpdateCachedViewportState(vpAndScissor_);

		renderManager->SetScissor(GLRect2D{ vpAndScissor_.scissorX, vpAndScissor_.scissorY, vpAndScissor_.scissorW, vpAndScissor_.scissorH });
		renderManager->SetViewport({
			vpAndScissor_.viewportX, vpAndScissor_.viewportY,
			vpAndScissor_.viewportW, vpAndScissor_.viewportH,
			vpAndScissor_.depthRangeMin, vpAndScissor_.depthRangeMax });
	}

	gstate_c.Clean(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_BLEND_STATE);
	gstate_c.Dirty(dirtyRequiresRecheck_);
	dirtyRequiresRecheck_ = 0;
}

void DrawEngineGLES::ApplyDrawStateLate(bool setStencilValue, int stencilValue) {
	if (setStencilValue) {
		render_->SetStencil(stencilState_.writeMask, GL_ALWAYS, stencilValue, 255, 0xFF, GL_REPLACE, GL_REPLACE, GL_REPLACE);
		gstate_c.Dirty(DIRTY_DEPTHSTENCIL_STATE);  // For the next time.
	}

	// At this point, we know if the vertices are full alpha or not.
	// TODO: Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
	if (!gstate.isModeClear() && gstate_c.Use(GPU_USE_FRAGMENT_TEST_CACHE)) {
		// Apply last, once we know the alpha params of the texture.
		if (gstate.isAlphaTestEnabled() || gstate.isColorTestEnabled()) {
			fragmentTestCache_->BindTestTexture(TEX_SLOT_ALPHATEST);
		}
	}
}
