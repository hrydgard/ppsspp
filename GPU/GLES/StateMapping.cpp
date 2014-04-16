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


#include "StateMapping.h"
#include "native/gfx_es2/gl_state.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/GLES/GLES_GPU.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/FragmentShaderGenerator.h"

static const GLushort aLookup[11] = {
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_SRC_ALPHA,			// GE_SRCBLEND_DOUBLESRCALPHA
	GL_ONE_MINUS_SRC_ALPHA,		// GE_SRCBLEND_DOUBLEINVSRCALPHA
	GL_DST_ALPHA,			// GE_SRCBLEND_DOUBLEDSTALPHA
	GL_ONE_MINUS_DST_ALPHA,		// GE_SRCBLEND_DOUBLEINVDSTALPHA
	GL_CONSTANT_COLOR,		// FIXA
};

static const GLushort bLookup[11] = {
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_SRC_ALPHA,			// GE_DSTBLEND_DOUBLESRCALPHA
	GL_ONE_MINUS_SRC_ALPHA,		// GE_DSTBLEND_DOUBLEINVSRCALPHA
	GL_DST_ALPHA,			// GE_DSTBLEND_DOUBLEDSTALPHA
	GL_ONE_MINUS_DST_ALPHA,		// GE_DSTBLEND_DOUBLEINVDSTALPHA
	GL_CONSTANT_COLOR,		// FIXB
};

static const GLushort eqLookupNoMinMax[] = {
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
	GL_FUNC_ADD,			// GE_BLENDMODE_MIN
	GL_FUNC_ADD,			// GE_BLENDMODE_MAX
	GL_FUNC_ADD,			// GE_BLENDMODE_ABSDIFF
};

static const GLushort eqLookup[] = {
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
#ifdef USING_GLES2
	GL_MIN_EXT,			// GE_BLENDMODE_MIN
	GL_MAX_EXT,			// GE_BLENDMODE_MAX
	GL_MAX_EXT,			// GE_BLENDMODE_ABSDIFF
#else
	GL_MIN,				// GE_BLENDMODE_MIN
	GL_MAX,				// GE_BLENDMODE_MAX
	GL_MAX,				// GE_BLENDMODE_ABSDIFF
#endif
};

static const GLushort cullingMode[] = {
	GL_BACK,
	GL_FRONT,
};

static const GLushort ztests[] = {
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

static GLenum toDualSource(GLenum blendfunc) {
	switch (blendfunc) {
#ifndef USING_GLES2
	case GL_SRC_ALPHA:
		return GL_SRC1_ALPHA;
	case GL_ONE_MINUS_SRC_ALPHA:
		return GL_ONE_MINUS_SRC1_ALPHA;
#endif
	default:
		return blendfunc;
	}
}

static GLenum blendColor2Func(u32 fix) {
	if (fix == 0xFFFFFF)
		return GL_ONE;
	if (fix == 0)
		return GL_ZERO;

	const Vec3f fix3 = Vec3f::FromRGB(fix);
	if (fix3.x >= 0.99 && fix3.y >= 0.99 && fix3.z >= 0.99)
		return GL_ONE;
	else if (fix3.x <= 0.01 && fix3.y <= 0.01 && fix3.z <= 0.01)
		return GL_ZERO;
	return GL_INVALID_ENUM;
}

static inline bool blendColorSimilar(const Vec3f &a, const Vec3f &b, float margin = 0.1f) {
	const Vec3f diff = a - b;
	if (fabsf(diff.x) <= margin && fabsf(diff.y) <= margin && fabsf(diff.z) <= margin)
		return true;
	return false;
}

void TransformDrawEngine::ApplyDrawState(int prim) {
	// TODO: All this setup is soon so expensive that we'll need dirty flags, or simply do it in the command writes where we detect dirty by xoring. Silly to do all this work on every drawcall.

	if (gstate_c.textureChanged != TEXCHANGE_UNCHANGED && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.textureChanged = TEXCHANGE_UNCHANGED;
	}

	// TODO: The top bit of the alpha channel should be written to the stencil bit somehow. This appears to require very expensive multipass rendering :( Alternatively, one could do a
	// single fullscreen pass that converts alpha to stencil (or 2 passes, to set both the 0 and 1 values) very easily.

	// Set blend
	bool wantBlend = !gstate.isModeClear() && gstate.isAlphaBlendEnabled();
	glstate.blend.set(wantBlend);
	if (wantBlend) {
		// This can't be done exactly as there are several PSP blend modes that are impossible to do on OpenGL ES 2.0, and some even on regular OpenGL for desktop.
		// HOWEVER - we should be able to approximate the 2x modes in the shader, although they will clip wrongly.

		// Examples of seen unimplementable blend states:
		// Mortal Kombat Unchained: FixA=0000ff FixB=000080 FuncA=10 FuncB=10

		int blendFuncA  = gstate.getBlendFuncA();
		int blendFuncB  = gstate.getBlendFuncB();
		GEBlendMode blendFuncEq = gstate.getBlendEq();
		if (blendFuncA > GE_SRCBLEND_FIXA) blendFuncA = GE_SRCBLEND_FIXA;
		if (blendFuncB > GE_DSTBLEND_FIXB) blendFuncB = GE_DSTBLEND_FIXB;

		float constantAlpha = 1.0f;
		ReplaceAlphaType replaceAlphaWithStencil = ReplaceAlphaWithStencil();
		if (gstate.isStencilTestEnabled() && replaceAlphaWithStencil == REPLACE_ALPHA_NO) {
			if (ReplaceAlphaWithStencilType() == STENCIL_VALUE_UNIFORM) {
				constantAlpha = (float) gstate.getStencilTestRef() * (1.0f / 255.0f);
			}
		}

		// Shortcut by using GL_ONE where possible, no need to set blendcolor
		GLuint glBlendFuncA = blendFuncA == GE_SRCBLEND_FIXA ? blendColor2Func(gstate.getFixA()) : aLookup[blendFuncA];
		GLuint glBlendFuncB = blendFuncB == GE_DSTBLEND_FIXB ? blendColor2Func(gstate.getFixB()) : bLookup[blendFuncB];

		if (replaceAlphaWithStencil == REPLACE_ALPHA_DUALSOURCE) {
			glBlendFuncA = toDualSource(glBlendFuncA);
			glBlendFuncB = toDualSource(glBlendFuncB);
		}

		if (blendFuncA == GE_SRCBLEND_FIXA || blendFuncB == GE_DSTBLEND_FIXB) {
			Vec3f fixA = Vec3f::FromRGB(gstate.getFixA());
			Vec3f fixB = Vec3f::FromRGB(gstate.getFixB());
			if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB != GL_INVALID_ENUM) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {fixA.x, fixA.y, fixA.z, constantAlpha};
				glstate.blendColor.set(blendColor);
				glBlendFuncA = GL_CONSTANT_COLOR;
			} else if (glBlendFuncA != GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {fixB.x, fixB.y, fixB.z, constantAlpha};
				glstate.blendColor.set(blendColor);
				glBlendFuncB = GL_CONSTANT_COLOR;
			} else if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {
				if (blendColorSimilar(fixA, Vec3f::AssignToAll(constantAlpha) - fixB)) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = GL_ONE_MINUS_CONSTANT_COLOR;
					const float blendColor[4] = {fixA.x, fixA.y, fixA.z, constantAlpha};
					glstate.blendColor.set(blendColor);
				} else if (blendColorSimilar(fixA, fixB)) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = GL_CONSTANT_COLOR;
					const float blendColor[4] = {fixA.x, fixA.y, fixA.z, constantAlpha};
					glstate.blendColor.set(blendColor);
				} else {
					static bool didReportBlend = false;
					if (!didReportBlend)
						Reporting::ReportMessage("ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
					didReportBlend = true;

					DEBUG_LOG(G3D, "ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
					// Let's approximate, at least.  Close is better than totally off.
					const bool nearZeroA = blendColorSimilar(fixA, Vec3f::AssignToAll(0.0f), 0.25f);
					const bool nearZeroB = blendColorSimilar(fixB, Vec3f::AssignToAll(0.0f), 0.25f);
					if (nearZeroA || blendColorSimilar(fixA, Vec3f::AssignToAll(1.0f), 0.25f)) {
						glBlendFuncA = nearZeroA ? GL_ZERO : GL_ONE;
						glBlendFuncB = GL_CONSTANT_COLOR;
						const float blendColor[4] = {fixB.x, fixB.y, fixB.z, constantAlpha};
						glstate.blendColor.set(blendColor);
					// We need to pick something.  Let's go with A as the fixed color.
					} else {
						glBlendFuncA = GL_CONSTANT_COLOR;
						glBlendFuncB = nearZeroB ? GL_ZERO : GL_ONE;
						const float blendColor[4] = {fixA.x, fixA.y, fixA.z, constantAlpha};
						glstate.blendColor.set(blendColor);
					}
				}
			} else {
				// We optimized both, but that's probably not necessary, so let's pick one to be constant.
				// For now let's just pick whichever was fixed instead of checking error.
				if (blendFuncA == GE_SRCBLEND_FIXA) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					const float blendColor[4] = {fixA.x, fixA.y, fixA.z, constantAlpha};
					glstate.blendColor.set(blendColor);
				} else {
					glBlendFuncB = GL_CONSTANT_COLOR;
					const float blendColor[4] = {fixB.x, fixB.y, fixB.z, constantAlpha};
					glstate.blendColor.set(blendColor);
				}
			}
		} else if (constantAlpha < 1.0f) {
			const float blendColor[4] = {1.0f, 1.0f, 1.0f, constantAlpha};
			glstate.blendColor.set(blendColor);
		}

		// Some Android devices (especially Mali, it seems) composite badly if there's alpha in the backbuffer.
		// So in non-buffered rendering, we will simply consider the dest alpha to be zero in blending equations.
#ifdef ANDROID
		if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
			if (glBlendFuncA == GL_DST_ALPHA) glBlendFuncA = GL_ZERO;
			if (glBlendFuncB == GL_DST_ALPHA) glBlendFuncB = GL_ZERO;
			if (glBlendFuncA == GL_ONE_MINUS_DST_ALPHA) glBlendFuncA = GL_ONE;
			if (glBlendFuncB == GL_ONE_MINUS_DST_ALPHA) glBlendFuncB = GL_ONE;
		}
#endif

		// At this point, through all paths above, glBlendFuncA and glBlendFuncB will be set right somehow.

		// The stencil-to-alpha in fragment shader doesn't apply here (blending is enabled), and we shouldn't
		// do any blending in the alpha channel as that doesn't seem to happen on PSP. So lacking a better option,
		// the only value we can set alpha to here without multipass and dual source alpha is zero (by setting
		// the factors to zero). So let's do that.
		if (replaceAlphaWithStencil != REPLACE_ALPHA_NO) {
			// Let the fragment shader take care of it.
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ONE, GL_ZERO);
		} else if (gstate.isStencilTestEnabled()) {
			switch (ReplaceAlphaWithStencilType()) {
			case STENCIL_VALUE_KEEP:
				glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ZERO, GL_ONE);
				break;
			case STENCIL_VALUE_ONE:
				// This won't give one but it's our best shot...
				glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ONE, GL_ONE);
				break;
			case STENCIL_VALUE_ZERO:
				glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ZERO, GL_ZERO);
				break;
			case STENCIL_VALUE_UNIFORM:
				// This won't give a correct value (it multiplies) but it may be better than random values.
				glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_CONSTANT_ALPHA, GL_ZERO);
				break;
			case STENCIL_VALUE_UNKNOWN:
				// For now, let's err at zero.  This is INVERT or INCR/DECR.
				glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ZERO, GL_ZERO);
				break;
			}
		} else {
			// Retain the existing value when stencil testing is off.
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ZERO, GL_ONE);
		}

		if (blendFuncEq == GE_BLENDMODE_ABSDIFF) {
			WARN_LOG_REPORT_ONCE(blendAbsdiff, G3D, "Unsupported absdiff blend mode");
		}

		if (((blendFuncEq >= GE_BLENDMODE_MIN) && gl_extensions.EXT_blend_minmax) || gl_extensions.GLES3) {
			if (blendFuncEq == GE_BLENDMODE_ABSDIFF && gl_extensions.EXT_shader_framebuffer_fetch) {
				// Handle GE_BLENDMODE_ABSDIFF in fragment shader and turn off regular alpha blending here.
				glstate.blend.set(false);
			} else {
				glstate.blendEquation.set(eqLookup[blendFuncEq]);
			}
		} else {
			glstate.blendEquation.set(eqLookupNoMinMax[blendFuncEq]);
		}
	}

	bool alwaysDepthWrite = g_Config.bAlwaysDepthWrite;
	bool enableStencilTest = !g_Config.bDisableStencilTest;

	// Dither
	if (gstate.isDitherEnabled()) {
		glstate.dither.enable();
		glstate.dither.set(GL_TRUE);
	} else
		glstate.dither.disable();

	if (gstate.isModeClear()) {
#if !defined(USING_GLES2)
		// Logic Ops
		glstate.colorLogicOp.disable();
#endif
		// Culling
		glstate.cullFace.disable();

		// Depth Test
		glstate.depthTest.enable();
		glstate.depthFunc.set(GL_ALWAYS);
		glstate.depthWrite.set(gstate.isClearModeDepthMask() || alwaysDepthWrite ? GL_TRUE : GL_FALSE);
		if (gstate.isClearModeDepthMask() || alwaysDepthWrite) {
			framebufferManager_->SetDepthUpdated();
		}

		// Color Test
		bool colorMask = gstate.isClearModeColorMask();
		bool alphaMask = gstate.isClearModeAlphaMask();
		glstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);

		// Stencil Test
		if (alphaMask && enableStencilTest) {
			glstate.stencilTest.enable();
			glstate.stencilOp.set(GL_REPLACE, GL_REPLACE, GL_REPLACE);
			// TODO: In clear mode, the stencil value is set to the alpha value of the vertex.
			// A normal clear will be 2 points, the second point has the color.
			// We should set "ref" to that value instead of 0.
			// In case of clear rectangles, we set it again once we know what the color is.
			glstate.stencilFunc.set(GL_ALWAYS, 255, 0xFF);
		} else {
			glstate.stencilTest.disable();
		}
	} else {
#if !defined(USING_GLES2)
		// Logic Ops
		if (gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY) {
			glstate.colorLogicOp.enable();
			glstate.logicOp.set(logicOps[gstate.getLogicOp()]);
		} else {
			glstate.colorLogicOp.disable();
		}
#endif
		// Set cull
		bool cullEnabled = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		if (cullEnabled) {
			glstate.cullFace.enable();
			glstate.cullFaceMode.set(cullingMode[gstate.getCullMode()]);
		} else {
			glstate.cullFace.disable();
		}

		// Depth Test
		if (gstate.isDepthTestEnabled()) {
			glstate.depthTest.enable();
			glstate.depthFunc.set(ztests[gstate.getDepthTestFunction()]);
			glstate.depthWrite.set(gstate.isDepthWriteEnabled() || alwaysDepthWrite ? GL_TRUE : GL_FALSE);
			framebufferManager_->SetDepthUpdated();
		} else {
			glstate.depthTest.disable();
		}

		// PSP color/alpha mask is per bit but we can only support per byte.
		// But let's do that, at least. And let's try a threshold.
		bool rmask = (gstate.pmskc & 0xFF) < 128;
		bool gmask = ((gstate.pmskc >> 8) & 0xFF) < 128;
		bool bmask = ((gstate.pmskc >> 16) & 0xFF) < 128;
		bool amask = (gstate.pmska & 0xFF) < 128;

		// Let's not write to alpha if stencil isn't enabled.
		if (!gstate.isStencilTestEnabled()) {
			amask = false;
		} else {
			// If the stencil type is set to KEEP, we shouldn't write to the stencil/alpha channel.
			if (ReplaceAlphaWithStencilType() == STENCIL_VALUE_KEEP) {
				amask = false;
			}
		}
		if (g_Config.bAlphaMaskHack) {
			amask = true;  // Yes, this makes no sense, but it "fixes" the 3rd Birthday by popular demand.
		}

		glstate.colorMask.set(rmask, gmask, bmask, amask);

		// Stencil Test
		if (gstate.isStencilTestEnabled() && enableStencilTest) {
			glstate.stencilTest.enable();
			glstate.stencilFunc.set(ztests[gstate.getStencilTestFunction()],
				gstate.getStencilTestRef(),
				gstate.getStencilTestMask());
			glstate.stencilOp.set(stencilOps[gstate.getStencilOpSFail()],  // stencil fail
				stencilOps[gstate.getStencilOpZFail()],  // depth fail
				stencilOps[gstate.getStencilOpZPass()]); // depth pass
		} else {
			glstate.stencilTest.disable();
		}
	}

	float renderWidthFactor, renderHeightFactor;
	float renderWidth, renderHeight;
	float renderX, renderY;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		renderX = 0.0f;
		renderY = 0.0f;
		renderWidth = framebufferManager_->GetRenderWidth();
		renderHeight = framebufferManager_->GetRenderHeight();
		renderWidthFactor = (float)renderWidth / framebufferManager_->GetTargetWidth();
		renderHeightFactor = (float)renderHeight / framebufferManager_->GetTargetHeight();
	} else {
		// TODO: Aspect-ratio aware and centered
		float pixelW = PSP_CoreParameter().pixelWidth;
		float pixelH = PSP_CoreParameter().pixelHeight;
		CenterRect(&renderX, &renderY, &renderWidth, &renderHeight, 480, 272, pixelW, pixelH);
		renderWidthFactor = renderWidth / 480.0f;
		renderHeightFactor = renderHeight / 272.0f;
	}

	bool throughmode = gstate.isModeThrough();

	// Scissor
	int scissorX1 = gstate.getScissorX1();
	int scissorY1 = gstate.getScissorY1();
	int scissorX2 = gstate.getScissorX2() + 1;
	int scissorY2 = gstate.getScissorY2() + 1;

	// This is a bit of a hack as the render buffer isn't always that size
	if (scissorX1 == 0 && scissorY1 == 0 
		&& scissorX2 >= (int) gstate_c.curRTWidth
		&& scissorY2 >= (int) gstate_c.curRTHeight) {
		glstate.scissorTest.disable();
	} else {
		glstate.scissorTest.enable();
		glstate.scissorRect.set(
			renderX + scissorX1 * renderWidthFactor,
			renderY + renderHeight - (scissorY2 * renderHeightFactor),
			(scissorX2 - scissorX1) * renderWidthFactor,
			(scissorY2 - scissorY1) * renderHeightFactor);
	}

	/*
	int regionX1 = gstate.region1 & 0x3FF;
	int regionY1 = (gstate.region1 >> 10) & 0x3FF;
	int regionX2 = (gstate.region2 & 0x3FF) + 1;
	int regionY2 = ((gstate.region2 >> 10) & 0x3FF) + 1;
	*/
	int regionX1 = 0;
	int regionY1 = 0;
	int regionX2 = gstate_c.curRTWidth;
	int regionY2 = gstate_c.curRTHeight;

	float offsetX = gstate.getOffsetX();
	float offsetY = gstate.getOffsetY();

	if (throughmode) {
		// No viewport transform here. Let's experiment with using region.
		glstate.viewport.set(
			renderX + (0 + regionX1) * renderWidthFactor, 
			renderY + (0 - regionY1) * renderHeightFactor,
			(regionX2 - regionX1) * renderWidthFactor,
			(regionY2 - regionY1) * renderHeightFactor);
		glstate.depthRange.set(0.0f, 1.0f);
	} else {
		// These we can turn into a glViewport call, offset by offsetX and offsetY. Math after.
		float vpXa = getFloat24(gstate.viewportx1);
		float vpXb = getFloat24(gstate.viewportx2);
		float vpYa = getFloat24(gstate.viewporty1);
		float vpYb = getFloat24(gstate.viewporty2);

		// The viewport transform appears to go like this: 
		// Xscreen = -offsetX + vpXb + vpXa * Xview
		// Yscreen = -offsetY + vpYb + vpYa * Yview
		// Zscreen = vpZb + vpZa * Zview

		// This means that to get the analogue glViewport we must:
		float vpX0 = vpXb - offsetX - vpXa;
		float vpY0 = vpYb - offsetY + vpYa;   // Need to account for sign of Y
		gstate_c.vpWidth = vpXa * 2.0f;
		gstate_c.vpHeight = -vpYa * 2.0f;

		float vpWidth = fabsf(gstate_c.vpWidth);
		float vpHeight = fabsf(gstate_c.vpHeight);

		vpX0 *= renderWidthFactor;
		vpY0 *= renderHeightFactor;
		vpWidth *= renderWidthFactor;
		vpHeight *= renderHeightFactor;

		vpX0 = (vpXb - offsetX - fabsf(vpXa)) * renderWidthFactor;
		// Flip vpY0 to match the OpenGL coordinate system.
		vpY0 = renderHeight - (vpYb - offsetY + fabsf(vpYa)) * renderHeightFactor;		
		
		glstate.viewport.set(vpX0 + renderX, vpY0 + renderY, vpWidth, vpHeight);
		// Sadly, as glViewport takes integers, we will not be able to support sub pixel offsets this way. But meh.
		// shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);

		float zScale = getFloat24(gstate.viewportz1) / 65535.0f;
		float zOff = getFloat24(gstate.viewportz2) / 65535.0f;
		float depthRangeMin = zOff - zScale;
		float depthRangeMax = zOff + zScale;
		glstate.depthRange.set(depthRangeMin, depthRangeMax);
	}
}
