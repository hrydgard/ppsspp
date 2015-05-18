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

static GLenum blendColor2Func(u32 fix, bool &approx) {
	if (fix == 0xFFFFFF)
		return GL_ONE;
	if (fix == 0)
		return GL_ZERO;

	// Otherwise, it's approximate if we pick ONE/ZERO.
	approx = true;

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

bool TransformDrawEngine::ApplyShaderBlending() {
	if (gl_extensions.ANY_shader_framebuffer_fetch) {
		return true;
	}

	static const int MAX_REASONABLE_BLITS_PER_FRAME = 24;

	static int lastFrameBlit = -1;
	static int blitsThisFrame = 0;
	if (lastFrameBlit != gpuStats.numFlips) {
		if (blitsThisFrame > MAX_REASONABLE_BLITS_PER_FRAME) {
			WARN_LOG_REPORT_ONCE(blendingBlit, G3D, "Lots of blits needed for obscure blending: %d per frame, blend %d/%d/%d", blitsThisFrame, gstate.getBlendFuncA(), gstate.getBlendFuncB(), gstate.getBlendEq());
		}
		blitsThisFrame = 0;
		lastFrameBlit = gpuStats.numFlips;
	}
	++blitsThisFrame;
	if (blitsThisFrame > MAX_REASONABLE_BLITS_PER_FRAME * 2) {
		WARN_LOG_ONCE(blendingBlit2, G3D, "Skipping additional blits needed for obscure blending: %d per frame, blend %d/%d/%d", blitsThisFrame, gstate.getBlendFuncA(), gstate.getBlendFuncB(), gstate.getBlendEq());
		ResetShaderBlending();
		return false;
	}

	framebufferManager_->BindFramebufferColor(GL_TEXTURE1, NULL);
	glActiveTexture(GL_TEXTURE1);
	// If we are rendering at a higher resolution, linear is probably best for the dest color.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glActiveTexture(GL_TEXTURE0);
	fboTexBound_ = true;

	shaderManager_->DirtyUniform(DIRTY_SHADERBLEND);
	return true;
}

inline void TransformDrawEngine::ResetShaderBlending() {
	if (fboTexBound_) {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		fboTexBound_ = false;
	}
}

void TransformDrawEngine::ApplyStencilReplaceAndLogicOp(ReplaceAlphaType replaceAlphaWithStencil) {
	StencilValueType stencilType = STENCIL_VALUE_KEEP;
	if (replaceAlphaWithStencil == REPLACE_ALPHA_YES) {
		stencilType = ReplaceAlphaWithStencilType();
	}

	// Normally, we would add src + 0, but the logic op may have us do differently.
	GLenum srcBlend = GL_ONE;
	GLenum dstBlend = GL_ZERO;
	GLenum blendOp = GL_FUNC_ADD;
#if defined(USING_GLES2)
	if (gstate.isLogicOpEnabled()) {
		switch (gstate.getLogicOp())
		{
		case GE_LOGIC_CLEAR:
			srcBlend = GL_ZERO;
			break;
		case GE_LOGIC_AND:
		case GE_LOGIC_AND_REVERSE:
			WARN_LOG_REPORT_ONCE(d3dLogicOpAnd, G3D, "Unsupported AND logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_COPY:
			// This is the same as off.
			break;
		case GE_LOGIC_COPY_INVERTED:
			// Handled in the shader.
			break;
		case GE_LOGIC_AND_INVERTED:
		case GE_LOGIC_NOR:
		case GE_LOGIC_NAND:
		case GE_LOGIC_EQUIV:
			// Handled in the shader.
			WARN_LOG_REPORT_ONCE(d3dLogicOpAndInverted, G3D, "Attempted invert for logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_INVERTED:
			srcBlend = GL_ONE;
			dstBlend = GL_ONE;
			blendOp = GL_FUNC_SUBTRACT;
			WARN_LOG_REPORT_ONCE(d3dLogicOpInverted, G3D, "Attempted inverse for logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_NOOP:
			srcBlend = GL_ZERO;
			dstBlend = GL_ONE;
			break;
		case GE_LOGIC_XOR:
			WARN_LOG_REPORT_ONCE(d3dLogicOpOrXor, G3D, "Unsupported XOR logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_OR:
		case GE_LOGIC_OR_INVERTED:
			// Inverted in shader.
			dstBlend = GL_ONE;
			WARN_LOG_REPORT_ONCE(d3dLogicOpOr, G3D, "Attempted or for logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_OR_REVERSE:
			WARN_LOG_REPORT_ONCE(d3dLogicOpOrReverse, G3D, "Unsupported OR REVERSE logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_SET:
			dstBlend = GL_ONE;
			WARN_LOG_REPORT_ONCE(d3dLogicOpSet, G3D, "Attempted set for logic op: %x", gstate.getLogicOp());
			break;
		}
	}
#endif

	// We're not blending, but we may still want to blend for stencil.
	// This is only useful for INCR/DECR/INVERT.  Others can write directly.
	switch (stencilType) {
	case STENCIL_VALUE_INCR_4:
	case STENCIL_VALUE_INCR_8:
		// We'll add the incremented value output by the shader.
		glstate.blendFuncSeparate.set(srcBlend, dstBlend, GL_ONE, GL_ONE);
		glstate.blendEquationSeparate.set(blendOp, GL_FUNC_ADD);
		glstate.blend.enable();
		break;

	case STENCIL_VALUE_DECR_4:
	case STENCIL_VALUE_DECR_8:
		// We'll subtract the incremented value output by the shader.
		glstate.blendFuncSeparate.set(srcBlend, dstBlend, GL_ONE, GL_ONE);
		glstate.blendEquationSeparate.set(blendOp, GL_FUNC_SUBTRACT);
		glstate.blend.enable();
		break;

	case STENCIL_VALUE_INVERT:
		// The shader will output one, and reverse subtracting will essentially invert.
		glstate.blendFuncSeparate.set(srcBlend, dstBlend, GL_ONE, GL_ONE);
		glstate.blendEquationSeparate.set(blendOp, GL_FUNC_REVERSE_SUBTRACT);
		glstate.blend.enable();
		break;

	default:
		if (srcBlend == GL_ONE && dstBlend == GL_ZERO && blendOp == GL_FUNC_ADD) {
			glstate.blend.disable();
		} else {
			glstate.blendFuncSeparate.set(srcBlend, dstBlend, GL_ONE, GL_ZERO);
			glstate.blendEquationSeparate.set(blendOp, GL_FUNC_ADD);
			glstate.blend.enable();
		}
		break;
	}
}

void TransformDrawEngine::ApplyBlendState() {
	// Blending is a bit complex to emulate.  This is due to several reasons:
	//
	//  * Doubled blend modes (src, dst, inversed) aren't supported in OpenGL.
	//    If possible, we double the src color or src alpha in the shader to account for these.
	//    These may clip incorrectly, so we avoid unfortunately.
	//  * OpenGL only has one arbitrary fixed color.  We premultiply the other in the shader.
	//  * The written output alpha should actually be the stencil value.  Alpha is not written.
	//
	// If we can't apply blending, we make a copy of the framebuffer and do it manually.
	gstate_c.allowShaderBlend = !g_Config.bDisableSlowFramebufEffects;

	ReplaceBlendType replaceBlend = ReplaceBlendWithShader();
	ReplaceAlphaType replaceAlphaWithStencil = ReplaceAlphaWithStencil(replaceBlend);
	bool usePreSrc = false;

	switch (replaceBlend) {
	case REPLACE_BLEND_NO:
		ResetShaderBlending();
		// We may still want to do something about stencil -> alpha.
		ApplyStencilReplaceAndLogicOp(replaceAlphaWithStencil);
		return;

	case REPLACE_BLEND_COPY_FBO:
		if (ApplyShaderBlending()) {
			// We may still want to do something about stencil -> alpha.
			ApplyStencilReplaceAndLogicOp(replaceAlphaWithStencil);
			return;
		}
		// Until next time, force it off.
		gstate_c.allowShaderBlend = false;
		break;

	case REPLACE_BLEND_PRE_SRC:
	case REPLACE_BLEND_PRE_SRC_2X_ALPHA:
		usePreSrc = true;
		break;

	case REPLACE_BLEND_STANDARD:
	case REPLACE_BLEND_2X_ALPHA:
	case REPLACE_BLEND_2X_SRC:
		break;
	}

	glstate.blend.enable();
	ResetShaderBlending();

	GEBlendMode blendFuncEq = gstate.getBlendEq();
	int blendFuncA  = gstate.getBlendFuncA();
	int blendFuncB  = gstate.getBlendFuncB();
	if (blendFuncA > GE_SRCBLEND_FIXA)
		blendFuncA = GE_SRCBLEND_FIXA;
	if (blendFuncB > GE_DSTBLEND_FIXB)
		blendFuncB = GE_DSTBLEND_FIXB;

	float constantAlpha = 1.0f;
	GLenum constantAlphaGL = GL_ONE;
	if (gstate.isStencilTestEnabled() && replaceAlphaWithStencil == REPLACE_ALPHA_NO) {
		switch (ReplaceAlphaWithStencilType()) {
		case STENCIL_VALUE_UNIFORM:
			constantAlpha = (float) gstate.getStencilTestRef() * (1.0f / 255.0f);
			break;

		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_DECR_4:
			constantAlpha = 1.0f / 15.0f;
			break;

		case STENCIL_VALUE_INCR_8:
		case STENCIL_VALUE_DECR_8:
			constantAlpha = 1.0f / 255.0f;
			break;

		default:
			break;
		}

		// Otherwise it will stay GL_ONE.
		if (constantAlpha <= 0.0f) {
			constantAlphaGL = GL_ZERO;
		} else if (constantAlpha < 1.0f) {
			constantAlphaGL = GL_CONSTANT_ALPHA;
		}
	}

	// Shortcut by using GL_ONE where possible, no need to set blendcolor
	bool approxFuncA = false;
	GLuint glBlendFuncA = blendFuncA == GE_SRCBLEND_FIXA ? blendColor2Func(gstate.getFixA(), approxFuncA) : aLookup[blendFuncA];
	bool approxFuncB = false;
	GLuint glBlendFuncB = blendFuncB == GE_DSTBLEND_FIXB ? blendColor2Func(gstate.getFixB(), approxFuncB) : bLookup[blendFuncB];

	if (usePreSrc) {
		glBlendFuncA = GL_ONE;
		// Need to pull in the fixed color.
		if (blendFuncA == GE_SRCBLEND_FIXA) {
			shaderManager_->DirtyUniform(DIRTY_SHADERBLEND);
		}
	}

	if (replaceAlphaWithStencil == REPLACE_ALPHA_DUALSOURCE) {
		glBlendFuncA = toDualSource(glBlendFuncA);
		glBlendFuncB = toDualSource(glBlendFuncB);
	}

	auto setBlendColorv = [&](const Vec3f &c) {
		const float blendColor[4] = {c.x, c.y, c.z, constantAlpha};
		glstate.blendColor.set(blendColor);
	};
	auto defaultBlendColor = [&]() {
		if (constantAlphaGL == GL_CONSTANT_ALPHA) {
			const float blendColor[4] = {1.0f, 1.0f, 1.0f, constantAlpha};
			glstate.blendColor.set(blendColor);
		}
	};

	if (blendFuncA == GE_SRCBLEND_FIXA || blendFuncB == GE_DSTBLEND_FIXB) {
		const Vec3f fixA = Vec3f::FromRGB(gstate.getFixA());
		const Vec3f fixB = Vec3f::FromRGB(gstate.getFixB());
		if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB != GL_INVALID_ENUM) {
			// Can use blendcolor trivially.
			setBlendColorv(fixA);
			glBlendFuncA = GL_CONSTANT_COLOR;
		} else if (glBlendFuncA != GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {
			// Can use blendcolor trivially.
			setBlendColorv(fixB);
			glBlendFuncB = GL_CONSTANT_COLOR;
		} else if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {
			if (blendColorSimilar(fixA, Vec3f::AssignToAll(1.0f) - fixB)) {
				glBlendFuncA = GL_CONSTANT_COLOR;
				glBlendFuncB = GL_ONE_MINUS_CONSTANT_COLOR;
				setBlendColorv(fixA);
			} else if (blendColorSimilar(fixA, fixB)) {
				glBlendFuncA = GL_CONSTANT_COLOR;
				glBlendFuncB = GL_CONSTANT_COLOR;
				setBlendColorv(fixA);
			} else {
				DEBUG_LOG(G3D, "ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
				// Let's approximate, at least.  Close is better than totally off.
				const bool nearZeroA = blendColorSimilar(fixA, Vec3f::AssignToAll(0.0f), 0.25f);
				const bool nearZeroB = blendColorSimilar(fixB, Vec3f::AssignToAll(0.0f), 0.25f);
				if (nearZeroA || blendColorSimilar(fixA, Vec3f::AssignToAll(1.0f), 0.25f)) {
					glBlendFuncA = nearZeroA ? GL_ZERO : GL_ONE;
					glBlendFuncB = GL_CONSTANT_COLOR;
					setBlendColorv(fixB);
				} else {
					// We need to pick something.  Let's go with A as the fixed color.
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = nearZeroB ? GL_ZERO : GL_ONE;
					setBlendColorv(fixA);
				}
			}
		} else {
			// We optimized both, but that's probably not necessary, so let's pick one to be constant.
			if (blendFuncA == GE_SRCBLEND_FIXA && !usePreSrc && approxFuncA) {
				glBlendFuncA = GL_CONSTANT_COLOR;
				setBlendColorv(fixA);
			} else if (approxFuncB) {
				glBlendFuncB = GL_CONSTANT_COLOR;
				setBlendColorv(fixB);
			} else {
				defaultBlendColor();
			}
		}
	} else {
		defaultBlendColor();
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
	// do any blending in the alpha channel as that doesn't seem to happen on PSP.  So, we attempt to
	// apply the stencil to the alpha, since that's what should be stored.
	GLenum alphaEq = GL_FUNC_ADD;
	if (replaceAlphaWithStencil != REPLACE_ALPHA_NO) {
		// Let the fragment shader take care of it.
		switch (ReplaceAlphaWithStencilType()) {
		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_INCR_8:
			// We'll add the increment value.
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ONE, GL_ONE);
			break;

		case STENCIL_VALUE_DECR_4:
		case STENCIL_VALUE_DECR_8:
			// Like add with a small value, but subtracting.
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ONE, GL_ONE);
			alphaEq = GL_FUNC_SUBTRACT;
			break;

		case STENCIL_VALUE_INVERT:
			// This will subtract by one, effectively inverting the bits.
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ONE, GL_ONE);
			alphaEq = GL_FUNC_REVERSE_SUBTRACT;
			break;

		default:
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ONE, GL_ZERO);
			break;
		}
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
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, constantAlphaGL, GL_ZERO);
			break;
		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_INCR_8:
			// This won't give a correct value always, but it will try to increase at least.
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, constantAlphaGL, GL_ONE);
			break;
		case STENCIL_VALUE_DECR_4:
		case STENCIL_VALUE_DECR_8:
			// This won't give a correct value always, but it will try to decrease at least.
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, constantAlphaGL, GL_ONE);
			alphaEq = GL_FUNC_SUBTRACT;
			break;
		case STENCIL_VALUE_INVERT:
			glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ONE, GL_ONE);
			// If the output alpha is near 1, this will basically invert.  It's our best shot.
			alphaEq = GL_FUNC_REVERSE_SUBTRACT;
			break;
		}
	} else {
		// Retain the existing value when stencil testing is off.
		glstate.blendFuncSeparate.set(glBlendFuncA, glBlendFuncB, GL_ZERO, GL_ONE);
	}

	if (gl_extensions.EXT_blend_minmax || gl_extensions.GLES3) {
		glstate.blendEquationSeparate.set(eqLookup[blendFuncEq], alphaEq);
	} else {
		glstate.blendEquationSeparate.set(eqLookupNoMinMax[blendFuncEq], alphaEq);
	}
}

void TransformDrawEngine::ApplyDrawState(int prim) {
	// TODO: All this setup is soon so expensive that we'll need dirty flags, or simply do it in the command writes where we detect dirty by xoring. Silly to do all this work on every drawcall.

	if (gstate_c.textureChanged != TEXCHANGE_UNCHANGED && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.textureChanged = TEXCHANGE_UNCHANGED;
		if (gstate_c.needShaderTexClamp) {
			// We will rarely need to set this, so let's do it every time on use rather than in runloop.
			// Most of the time non-framebuffer textures will be used which can be clamped themselves.
			shaderManager_->DirtyUniform(DIRTY_TEXCLAMP);
		}
	}

	// Set blend - unless we need to do it in the shader.
	ApplyBlendState();

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
			glstate.stencilMask.set(0xFF);
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
			if (gstate.isDepthWriteEnabled() || alwaysDepthWrite) {
				framebufferManager_->SetDepthUpdated();
			}
		} else {
			glstate.depthTest.disable();
		}

		// PSP color/alpha mask is per bit but we can only support per byte.
		// But let's do that, at least. And let's try a threshold.
		bool rmask = (gstate.pmskc & 0xFF) < 128;
		bool gmask = ((gstate.pmskc >> 8) & 0xFF) < 128;
		bool bmask = ((gstate.pmskc >> 16) & 0xFF) < 128;
		bool amask = (gstate.pmska & 0xFF) < 128;

		u8 abits = (gstate.pmska >> 0) & 0xFF;
#ifndef MOBILE_DEVICE
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

			if (gstate.FrameBufFormat() == GE_FORMAT_5551) {
				glstate.stencilMask.set(abits <= 0x7f ? 0xff : 0x00);
			} else {
				glstate.stencilMask.set(~abits);
			}
		} else {
			glstate.stencilTest.disable();
		}
	}

	bool throughmode = gstate.isModeThrough();

	float renderWidthFactor, renderHeightFactor;
	float renderWidth, renderHeight;
	float renderX, renderY;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		renderX = 0.0f;
		renderY = 0.0f;
		renderWidth = framebufferManager_->GetRenderWidth();
		renderHeight = framebufferManager_->GetRenderHeight();
		renderWidthFactor = (float)renderWidth / framebufferManager_->GetTargetBufferWidth();
		renderHeightFactor = (float)renderHeight / framebufferManager_->GetTargetBufferHeight();
	} else {
		float pixelW = PSP_CoreParameter().pixelWidth;
		float pixelH = PSP_CoreParameter().pixelHeight;
		CenterRect(&renderX, &renderY, &renderWidth, &renderHeight, 480, 272, pixelW, pixelH, ROTATION_LOCKED_HORIZONTAL);
		renderWidthFactor = renderWidth / 480.0f;
		renderHeightFactor = renderHeight / 272.0f;
	}

	renderX += gstate_c.cutRTOffsetX * renderWidthFactor;

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
		// If the buffer is too large, offset the viewport to the top.
		renderY += renderHeight - framebufferManager_->GetTargetHeight() * renderHeightFactor;

		// No viewport transform here. Let's experiment with using region.
		glstate.viewport.set(
			renderX + (0 + regionX1) * renderWidthFactor, 
			renderY + (0 - regionY1) * renderHeightFactor,
			(regionX2 - regionX1) * renderWidthFactor,
			(regionY2 - regionY1) * renderHeightFactor);
		glstate.depthRange.set(0.0f, 1.0f);
	} else {
		// These we can turn into a glViewport call, offset by offsetX and offsetY. Math after.
		float vpXScale = getFloat24(gstate.viewportx1);
		float vpXCenter = getFloat24(gstate.viewportx2);
		float vpYScale = getFloat24(gstate.viewporty1);
		float vpYCenter = getFloat24(gstate.viewporty2);

		// The viewport transform appears to go like this:
		// Xscreen = -offsetX + vpXCenter + vpXScale * Xview
		// Yscreen = -offsetY + vpYCenter + vpYScale * Yview
		// Zscreen = vpZCenter + vpZScale * Zview

		// This means that to get the analogue glViewport we must:
		float vpX0 = vpXCenter - offsetX - fabsf(vpXScale);
		float vpY0 = vpYCenter - offsetY + fabsf(vpYScale);   // Need to account for sign of Y
		gstate_c.vpWidth = vpXScale * 2.0f;
		gstate_c.vpHeight = -vpYScale * 2.0f;

		float vpWidth = fabsf(gstate_c.vpWidth);
		float vpHeight = fabsf(gstate_c.vpHeight);

		vpX0 *= renderWidthFactor;
		vpY0 *= renderHeightFactor;
		vpWidth *= renderWidthFactor;
		vpHeight *= renderHeightFactor;

		// Flip vpY0 to match the OpenGL coordinate system.
		vpY0 = renderHeight - vpY0;

		// We used to apply the viewport here via glstate, but there are limits which vary by driver.
		// This may mean some games won't work, or at least won't work at higher render resolutions.
		// So we apply it in the shader instead.
		float left = renderX + vpX0;
		float bottom = renderY + vpY0;
		float right = left + vpWidth;
		float top = bottom + vpHeight;

		float wScale = 1.0f;
		float xOffset = 0.0f;
		float hScale = 1.0f;
		float yOffset = 0.0f;

		// If we're within the bounds, we want clipping the viewport way.  So leave it be.
		if (left < 0.0f || right > renderWidth) {
			float overageLeft = std::max(-left, 0.0f);
			float overageRight = std::max(right - renderWidth, 0.0f);
			// Our center drifted by the difference in overages.
			float drift = overageRight - overageLeft;

			left += overageLeft;
			right -= overageRight;

			wScale = vpWidth / (right - left);
			xOffset = drift / (right - left);
		}

		if (bottom < 0.0f || top > renderHeight) {
			float overageBottom = std::max(-bottom, 0.0f);
			float overageTop = std::max(top - renderHeight, 0.0f);
			// Our center drifted by the difference in overages.
			float drift = overageTop - overageBottom;

			bottom += overageBottom;
			top -= overageTop;

			hScale = vpHeight / (top - bottom);
			yOffset = drift / (top - bottom);
		}

		bool scaleChanged = gstate_c.vpWidthScale != wScale || gstate_c.vpHeightScale != hScale;
		bool offsetChanged = gstate_c.vpXOffset != xOffset || gstate_c.vpYOffset != yOffset;
		if (scaleChanged || offsetChanged)
		{
			gstate_c.vpWidthScale = wScale;
			gstate_c.vpHeightScale = hScale;
			gstate_c.vpXOffset = xOffset;
			gstate_c.vpYOffset = yOffset;
			shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
		}

		glstate.viewport.set(left, bottom, right - left, top - bottom);

		float zScale = getFloat24(gstate.viewportz1) * (1.0f / 65535.0f);
		float zOff = getFloat24(gstate.viewportz2) * (1.0f / 65535.0f);
		float depthRangeMin = zOff - zScale;
		float depthRangeMax = zOff + zScale;
		glstate.depthRange.set(depthRangeMin, depthRangeMax);

#ifndef MOBILE_DEVICE
		float minz = gstate.getDepthRangeMin() * (1.0f / 65535.0f);
		float maxz = gstate.getDepthRangeMax() * (1.0f / 65535.0f);
		if ((minz > depthRangeMin && minz > depthRangeMax) || (maxz < depthRangeMin && maxz < depthRangeMax)) {
			WARN_LOG_REPORT_ONCE(minmaxz, G3D, "Unsupported depth range test - depth range: %f-%f, test: %f-%f", depthRangeMin, depthRangeMax, minz, maxz);
		} else if ((gstate.clipEnable & 1) == 0) {
			// TODO: Need to test whether clipEnable should even affect depth or not.
			if ((minz < depthRangeMin && minz < depthRangeMax) || (maxz > depthRangeMin && maxz > depthRangeMax)) {
				WARN_LOG_REPORT_ONCE(znoclip, G3D, "Unsupported depth range test without clipping - depth range: %f-%f, test: %f-%f", depthRangeMin, depthRangeMax, minz, maxz);
			}
		}
#endif
	}
}

void TransformDrawEngine::ApplyDrawStateLate() {
	// At this point, we know if the vertices are full alpha or not.
	// TODO: Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
	if (!gstate.isModeClear()) {
		if (gstate.isAlphaTestEnabled() || gstate.isColorTestEnabled()) {
			fragmentTestCache_->BindTestTexture(GL_TEXTURE2);
		}
	}
}
