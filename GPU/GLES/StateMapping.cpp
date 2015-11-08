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
#include "profiler/profiler.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/GLES/GLES_GPU.h"
#include "GPU/GLES/GLStateCache.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/FragmentShaderGenerator.h"

static const BlendFactor genericALookup[11] = {
	BlendFactor::DST_COLOR,
	BlendFactor::ONE_MINUS_DST_COLOR,
	BlendFactor::SRC_ALPHA,
	BlendFactor::ONE_MINUS_SRC_ALPHA,
	BlendFactor::DST_ALPHA,
	BlendFactor::ONE_MINUS_DST_ALPHA,
	BlendFactor::SRC_ALPHA,			// GE_SRCBLEND_DOUBLESRCALPHA
	BlendFactor::ONE_MINUS_SRC_ALPHA,		// GE_SRCBLEND_DOUBLEINVSRCALPHA
	BlendFactor::DST_ALPHA,			// GE_SRCBLEND_DOUBLEDSTALPHA
	BlendFactor::ONE_MINUS_DST_ALPHA,		// GE_SRCBLEND_DOUBLEINVDSTALPHA
	BlendFactor::CONSTANT_COLOR,		// FIXA
};

static const BlendFactor genericBLookup[11] = {
	BlendFactor::SRC_COLOR,
	BlendFactor::ONE_MINUS_SRC_COLOR,
	BlendFactor::SRC_ALPHA,
	BlendFactor::ONE_MINUS_SRC_ALPHA,
	BlendFactor::DST_ALPHA,
	BlendFactor::ONE_MINUS_DST_ALPHA,
	BlendFactor::SRC_ALPHA,			// GE_SRCBLEND_DOUBLESRCALPHA
	BlendFactor::ONE_MINUS_SRC_ALPHA,		// GE_SRCBLEND_DOUBLEINVSRCALPHA
	BlendFactor::DST_ALPHA,			// GE_SRCBLEND_DOUBLEDSTALPHA
	BlendFactor::ONE_MINUS_DST_ALPHA,		// GE_SRCBLEND_DOUBLEINVDSTALPHA
	BlendFactor::CONSTANT_COLOR,		// FIXB
};

static const BlendEq eqLookupNoMinMax[] = {
	BlendEq::ADD,
	BlendEq::SUBTRACT,
	BlendEq::REVERSE_SUBTRACT,
	BlendEq::ADD,			// GE_BLENDMODE_MIN
	BlendEq::ADD,			// GE_BLENDMODE_MAX
	BlendEq::ADD,			// GE_BLENDMODE_ABSDIFF
};

static const BlendEq eqLookup[] = {
	BlendEq::ADD,
	BlendEq::SUBTRACT,
	BlendEq::REVERSE_SUBTRACT,
	BlendEq::MIN,			// GE_BLENDMODE_MIN
	BlendEq::MAX,			// GE_BLENDMODE_MAX
	BlendEq::ADD,			// GE_BLENDMODE_ABSDIFF
};

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
	GL_SRC1_ALPHA,
	GL_ONE_MINUS_SRC1_ALPHA,
#else
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

static BlendFactor toDualSource(BlendFactor blendfunc) {
	switch (blendfunc) {
#if !defined(USING_GLES2)   // TODO: Remove when we have better headers
	case BlendFactor::SRC_ALPHA:
		return BlendFactor::SRC1_ALPHA;
	case BlendFactor::ONE_MINUS_SRC_ALPHA:
		return BlendFactor::ONE_MINUS_SRC1_ALPHA;
#endif
	default:
		return blendfunc;
	}
}

static BlendFactor blendColor2Func(u32 fix, bool &approx) {
	if (fix == 0xFFFFFF)
		return BlendFactor::ONE;
	if (fix == 0)
		return BlendFactor::ZERO;

	// Otherwise, it's approximate if we pick ONE/ZERO.
	approx = true;

	const Vec3f fix3 = Vec3f::FromRGB(fix);
	if (fix3.x >= 0.99 && fix3.y >= 0.99 && fix3.z >= 0.99)
		return BlendFactor::ONE;
	else if (fix3.x <= 0.01 && fix3.y <= 0.01 && fix3.z <= 0.01)
		return BlendFactor::ZERO;
	return BlendFactor::INVALID;
}

static inline bool blendColorSimilar(const Vec3f &a, const Vec3f &b, float margin = 0.1f) {
	const Vec3f diff = a - b;
	if (fabsf(diff.x) <= margin && fabsf(diff.y) <= margin && fabsf(diff.z) <= margin)
		return true;
	return false;
}

bool TransformDrawEngine::ApplyShaderBlending() {
	if (gstate_c.featureFlags & GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH) {
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

	fboTexNeedBind_ = true;

	shaderManager_->DirtyUniform(DIRTY_SHADERBLEND);
	return true;
}

inline void TransformDrawEngine::ResetShaderBlending() {
	// Wait - what does this have to do with FBOs?
	if (fboTexBound_) {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		fboTexBound_ = false;
	}
}

// Try to simulate some common logic ops.
void TransformDrawEngine::ApplyStencilReplaceAndLogicOp(ReplaceAlphaType replaceAlphaWithStencil, GenericBlendState &blendState) {
	StencilValueType stencilType = STENCIL_VALUE_KEEP;
	if (replaceAlphaWithStencil == REPLACE_ALPHA_YES) {
		stencilType = ReplaceAlphaWithStencilType();
	}

	// Normally, we would add src + 0, but the logic op may have us do differently.
	BlendFactor srcBlend = BlendFactor::ONE;
	BlendFactor dstBlend = BlendFactor::ZERO;
	BlendEq blendEq = BlendEq::ADD;

	if (!gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP)) {
		if (gstate.isLogicOpEnabled()) {
			switch (gstate.getLogicOp()) {
			case GE_LOGIC_CLEAR:
				srcBlend = BlendFactor::ZERO;
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
				srcBlend = BlendFactor::ONE;
				dstBlend = BlendFactor::ONE;
				blendEq = BlendEq::SUBTRACT;
				WARN_LOG_REPORT_ONCE(d3dLogicOpInverted, G3D, "Attempted inverse for logic op: %x", gstate.getLogicOp());
				break;
			case GE_LOGIC_NOOP:
				srcBlend = BlendFactor::ZERO;
				dstBlend = BlendFactor::ONE;
				break;
			case GE_LOGIC_XOR:
				WARN_LOG_REPORT_ONCE(d3dLogicOpOrXor, G3D, "Unsupported XOR logic op: %x", gstate.getLogicOp());
				break;
			case GE_LOGIC_OR:
			case GE_LOGIC_OR_INVERTED:
				// Inverted in shader.
				dstBlend = BlendFactor::ONE;
				WARN_LOG_REPORT_ONCE(d3dLogicOpOr, G3D, "Attempted or for logic op: %x", gstate.getLogicOp());
				break;
			case GE_LOGIC_OR_REVERSE:
				WARN_LOG_REPORT_ONCE(d3dLogicOpOrReverse, G3D, "Unsupported OR REVERSE logic op: %x", gstate.getLogicOp());
				break;
			case GE_LOGIC_SET:
				dstBlend = BlendFactor::ONE;
				WARN_LOG_REPORT_ONCE(d3dLogicOpSet, G3D, "Attempted set for logic op: %x", gstate.getLogicOp());
				break;
			}
		}
	}

	// We're not blending, but we may still want to blend for stencil.
	// This is only useful for INCR/DECR/INVERT.  Others can write directly.
	switch (stencilType) {
	case STENCIL_VALUE_INCR_4:
	case STENCIL_VALUE_INCR_8:
		// We'll add the incremented value output by the shader.
		blendState.enabled = true;
		blendState.setFactors(srcBlend, dstBlend, BlendFactor::ONE, BlendFactor::ONE);
		blendState.setEquation(blendEq, BlendEq::ADD);
		break;

	case STENCIL_VALUE_DECR_4:
	case STENCIL_VALUE_DECR_8:
		// We'll subtract the incremented value output by the shader.
		blendState.enabled = true;
		blendState.setFactors(srcBlend, dstBlend, BlendFactor::ONE, BlendFactor::ONE);
		blendState.setEquation(blendEq, BlendEq::SUBTRACT);
		break;

	case STENCIL_VALUE_INVERT:
		// The shader will output one, and reverse subtracting will essentially invert.
		blendState.enabled = true;
		blendState.setFactors(srcBlend, dstBlend, BlendFactor::ONE, BlendFactor::ONE);
		blendState.setEquation(blendEq, BlendEq::REVERSE_SUBTRACT);
		break;

	default:
		if (srcBlend == BlendFactor::ONE && dstBlend == BlendFactor::ZERO && blendEq == BlendEq::ADD) {
			blendState.enabled = false;
		} else {
			blendState.enabled = true;
			blendState.setFactors(srcBlend, dstBlend, BlendFactor::ONE, BlendFactor::ZERO);
			blendState.setEquation(blendEq, BlendEq::ADD);
		}
		break;
	}
}

// Called even if AlphaBlendEnable == false - it also deals with stencil-related blend state.

void TransformDrawEngine::ConvertBlendState(GenericBlendState &blendState) {
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

	ReplaceBlendType replaceBlend = ReplaceBlendWithShader(gstate_c.allowShaderBlend, gstate.FrameBufFormat());
	ReplaceAlphaType replaceAlphaWithStencil = ReplaceAlphaWithStencil(replaceBlend);
	bool usePreSrc = false;

	switch (replaceBlend) {
	case REPLACE_BLEND_NO:
		ResetShaderBlending();
		// We may still want to do something about stencil -> alpha.
		ApplyStencilReplaceAndLogicOp(replaceAlphaWithStencil, blendState);
		return;

	case REPLACE_BLEND_COPY_FBO:
		if (ApplyShaderBlending()) {
			// We may still want to do something about stencil -> alpha.
			ApplyStencilReplaceAndLogicOp(replaceAlphaWithStencil, blendState);
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

	blendState.enabled = true;
	ResetShaderBlending();

	const GEBlendMode blendFuncEq = gstate.getBlendEq();
	GEBlendSrcFactor blendFuncA = gstate.getBlendFuncA();
	GEBlendDstFactor blendFuncB = gstate.getBlendFuncB();
	const u32 fixA = gstate.getFixA();
	const u32 fixB = gstate.getFixB();

	if (blendFuncA > GE_SRCBLEND_FIXA)
		blendFuncA = GE_SRCBLEND_FIXA;
	if (blendFuncB > GE_DSTBLEND_FIXB)
		blendFuncB = GE_DSTBLEND_FIXB;

	float constantAlpha = 1.0f;
	BlendFactor constantAlphaGL = BlendFactor::ONE;
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
			constantAlphaGL = BlendFactor::ZERO;
		} else if (constantAlpha < 1.0f) {
			constantAlphaGL = BlendFactor::CONSTANT_ALPHA;
		}
	}

	// Shortcut by using GL_ONE where possible, no need to set blendcolor
	bool approxFuncA = false;
	BlendFactor glBlendFuncA = blendFuncA == GE_SRCBLEND_FIXA ? blendColor2Func(fixA, approxFuncA) : genericALookup[blendFuncA];
	bool approxFuncB = false;
	BlendFactor glBlendFuncB = blendFuncB == GE_DSTBLEND_FIXB ? blendColor2Func(fixB, approxFuncB) : genericBLookup[blendFuncB];

	if (gstate.FrameBufFormat() == GE_FORMAT_565) {
		if (blendFuncA == GE_SRCBLEND_DSTALPHA || blendFuncA == GE_SRCBLEND_DOUBLEDSTALPHA) {
			glBlendFuncA = BlendFactor::ZERO;
		}
		if (blendFuncA == GE_SRCBLEND_INVDSTALPHA || blendFuncA == GE_SRCBLEND_DOUBLEINVDSTALPHA) {
			glBlendFuncA = BlendFactor::ONE;
		}
		if (blendFuncB == GE_DSTBLEND_DSTALPHA || blendFuncB == GE_DSTBLEND_DOUBLEDSTALPHA) {
			glBlendFuncB = BlendFactor::ZERO;
		}
		if (blendFuncB == GE_DSTBLEND_INVDSTALPHA || blendFuncB == GE_DSTBLEND_DOUBLEINVDSTALPHA) {
			glBlendFuncB = BlendFactor::ONE;
		}
	}

	if (usePreSrc) {
		glBlendFuncA = BlendFactor::ONE;
		// Need to pull in the fixed color.
		if (blendFuncA == GE_SRCBLEND_FIXA) {
			shaderManager_->DirtyUniform(DIRTY_SHADERBLEND);
		}
	}

	if (replaceAlphaWithStencil == REPLACE_ALPHA_DUALSOURCE && gstate_c.Supports(GPU_SUPPORTS_DUALSOURCE_BLEND)) {
		glBlendFuncA = toDualSource(glBlendFuncA);
		glBlendFuncB = toDualSource(glBlendFuncB);
	}

	auto setBlendColorv = [&](const Vec3f &c) {
		const float blendColor[4] = {c.x, c.y, c.z, constantAlpha};
		glstate.blendColor.set(blendColor);
	};
	auto defaultBlendColor = [&]() {
		if (constantAlphaGL == BlendFactor::CONSTANT_ALPHA) {
			const float blendColor[4] = {1.0f, 1.0f, 1.0f, constantAlpha};
			glstate.blendColor.set(blendColor);
		}
	};

	if (blendFuncA == GE_SRCBLEND_FIXA || blendFuncB == GE_DSTBLEND_FIXB) {
		const Vec3f fixAVec = Vec3f::FromRGB(fixA);
		const Vec3f fixBVec = Vec3f::FromRGB(fixB);
		if (glBlendFuncA == BlendFactor::INVALID && glBlendFuncB != BlendFactor::INVALID) {
			// Can use blendcolor trivially.
			setBlendColorv(fixAVec);
			glBlendFuncA = BlendFactor::CONSTANT_COLOR;
		} else if (glBlendFuncA != BlendFactor::INVALID && glBlendFuncB == BlendFactor::INVALID) {
			// Can use blendcolor trivially.
			setBlendColorv(fixBVec);
			glBlendFuncB = BlendFactor::CONSTANT_COLOR;
		} else if (glBlendFuncA == BlendFactor::INVALID && glBlendFuncB == BlendFactor::INVALID) {
			if (blendColorSimilar(fixAVec, Vec3f::AssignToAll(1.0f) - fixBVec)) {
				glBlendFuncA = BlendFactor::CONSTANT_COLOR;
				glBlendFuncB = BlendFactor::ONE_MINUS_CONSTANT_COLOR;
				setBlendColorv(fixAVec);
			} else if (blendColorSimilar(fixAVec, fixBVec)) {
				glBlendFuncA = BlendFactor::CONSTANT_COLOR;
				glBlendFuncB = BlendFactor::CONSTANT_COLOR;
				setBlendColorv(fixAVec);
			} else {
				DEBUG_LOG(G3D, "ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", fixA, fixB, blendFuncA, blendFuncB);
				// Let's approximate, at least.  Close is better than totally off.
				const bool nearZeroA = blendColorSimilar(fixAVec, Vec3f::AssignToAll(0.0f), 0.25f);
				const bool nearZeroB = blendColorSimilar(fixBVec, Vec3f::AssignToAll(0.0f), 0.25f);
				if (nearZeroA || blendColorSimilar(fixAVec, Vec3f::AssignToAll(1.0f), 0.25f)) {
					glBlendFuncA = nearZeroA ? BlendFactor::ZERO : BlendFactor::ONE;
					glBlendFuncB = BlendFactor::CONSTANT_COLOR;
					setBlendColorv(fixBVec);
				} else {
					// We need to pick something.  Let's go with A as the fixed color.
					glBlendFuncA = BlendFactor::CONSTANT_COLOR;
					glBlendFuncB = nearZeroB ? BlendFactor::ZERO : BlendFactor::ONE;
					setBlendColorv(fixAVec);
				}
			}
		} else {
			// We optimized both, but that's probably not necessary, so let's pick one to be constant.
			if (blendFuncA == GE_SRCBLEND_FIXA && !usePreSrc && approxFuncA) {
				glBlendFuncA = BlendFactor::CONSTANT_COLOR;
				setBlendColorv(fixAVec);
			} else if (approxFuncB) {
				glBlendFuncB = BlendFactor::CONSTANT_COLOR;
				setBlendColorv(fixBVec);
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
		if (glBlendFuncA == BlendFactor::DST_ALPHA) glBlendFuncA = BlendFactor::ZERO;
		if (glBlendFuncB == BlendFactor::DST_ALPHA) glBlendFuncB = BlendFactor::ZERO;
		if (glBlendFuncA == BlendFactor::ONE_MINUS_DST_ALPHA) glBlendFuncA = BlendFactor::ONE;
		if (glBlendFuncB == BlendFactor::ONE_MINUS_DST_ALPHA) glBlendFuncB = BlendFactor::ONE;
	}
#endif

	// At this point, through all paths above, glBlendFuncA and glBlendFuncB will be set right somehow.

	// The stencil-to-alpha in fragment shader doesn't apply here (blending is enabled), and we shouldn't
	// do any blending in the alpha channel as that doesn't seem to happen on PSP.  So, we attempt to
	// apply the stencil to the alpha, since that's what should be stored.
	BlendEq alphaEq = BlendEq::ADD;
	if (replaceAlphaWithStencil != REPLACE_ALPHA_NO) {
		// Let the fragment shader take care of it.
		switch (ReplaceAlphaWithStencilType()) {
		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_INCR_8:
			// We'll add the increment value.
			blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ONE, BlendFactor::ONE);
			break;

		case STENCIL_VALUE_DECR_4:
		case STENCIL_VALUE_DECR_8:
			// Like add with a small value, but subtracting.
			blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ONE, BlendFactor::ONE);
			alphaEq = BlendEq::SUBTRACT;
			break;

		case STENCIL_VALUE_INVERT:
			// This will subtract by one, effectively inverting the bits.
			blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ONE, BlendFactor::ONE);
			alphaEq = BlendEq::REVERSE_SUBTRACT;
			break;

		default:
			blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ONE, BlendFactor::ZERO);
			break;
		}
	} else if (gstate.isStencilTestEnabled()) {
		switch (ReplaceAlphaWithStencilType()) {
		case STENCIL_VALUE_KEEP:
			blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ZERO, BlendFactor::ONE);
			break;
		case STENCIL_VALUE_ONE:
			// This won't give one but it's our best shot...
			blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ONE, BlendFactor::ONE);
			break;
		case STENCIL_VALUE_ZERO:
			blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ZERO, BlendFactor::ZERO);
			break;
		case STENCIL_VALUE_UNIFORM:
			// This won't give a correct value (it multiplies) but it may be better than random values.
			blendState.setFactors(glBlendFuncA, glBlendFuncB, constantAlphaGL, BlendFactor::ZERO);
			break;
		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_INCR_8:
			// This won't give a correct value always, but it will try to increase at least.
			blendState.setFactors(glBlendFuncA, glBlendFuncB, constantAlphaGL, BlendFactor::ONE);
			break;
		case STENCIL_VALUE_DECR_4:
		case STENCIL_VALUE_DECR_8:
			// This won't give a correct value always, but it will try to decrease at least.
			blendState.setFactors(glBlendFuncA, glBlendFuncB, constantAlphaGL, BlendFactor::ONE);
			alphaEq = BlendEq::SUBTRACT;
			break;
		case STENCIL_VALUE_INVERT:
			blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ONE, BlendFactor::ONE);
			// If the output alpha is near 1, this will basically invert.  It's our best shot.
			alphaEq = BlendEq::REVERSE_SUBTRACT;
			break;
		}
	} else {
		// Retain the existing value when stencil testing is off.
		blendState.setFactors(glBlendFuncA, glBlendFuncB, BlendFactor::ZERO, BlendFactor::ONE);
	}

	if (gstate_c.Supports(GPU_SUPPORTS_BLEND_MINMAX)) {
		blendState.setEquation(eqLookup[blendFuncEq], alphaEq);
	} else {
		blendState.setEquation(eqLookupNoMinMax[blendFuncEq], alphaEq);
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

	// Start profiling here to skip SetTexture which is already accounted for
	PROFILE_THIS_SCOPE("applydrawstate");

	// Set blend - unless we need to do it in the shader.
	GenericBlendState blendState;
	ConvertBlendState(blendState);

	if (blendState.enabled) {
		glstate.blend.enable();
		glstate.blendEquationSeparate.set(glBlendEqLookup[(size_t)blendState.eqColor], glBlendEqLookup[(size_t)blendState.eqAlpha]);
		glstate.blendFuncSeparate.set(
			glBlendFactorLookup[(size_t)blendState.srcColor], glBlendFactorLookup[(size_t)blendState.dstColor],
			glBlendFactorLookup[(size_t)blendState.srcAlpha], glBlendFactorLookup[(size_t)blendState.dstAlpha]);
	} else {
		glstate.blend.disable();
	}

	bool alwaysDepthWrite = g_Config.bAlwaysDepthWrite;
	bool enableStencilTest = !g_Config.bDisableStencilTest;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

	// Dither
	if (gstate.isDitherEnabled()) {
		glstate.dither.enable();
		glstate.dither.set(GL_TRUE);
	} else
		glstate.dither.disable();

	if (gstate.isModeClear()) {
#ifndef USING_GLES2
		if (gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP)) {
			// Logic Ops
			glstate.colorLogicOp.disable();
		}
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
#ifndef USING_GLES2
		if (gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP)) {
			// TODO: Make this dynamic
			// Logic Ops
			if (gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY) {
				glstate.colorLogicOp.enable();
				glstate.logicOp.set(logicOps[gstate.getLogicOp()]);
			} else {
				glstate.colorLogicOp.disable();
			}
		}
#endif
		// Set cull
		bool cullEnabled = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		if (cullEnabled) {
			glstate.cullFace.enable();
			glstate.cullFaceMode.set(cullingMode[gstate.getCullMode() ^ !useBufferedRendering]);
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

			if (gstate.isModeThrough()) {
				GEComparison ztest = gstate.getDepthTestFunction();
				if (ztest == GE_COMP_EQUAL || ztest == GE_COMP_NOTEQUAL || ztest == GE_COMP_LEQUAL || ztest == GE_COMP_GEQUAL) {
					DEBUG_LOG_REPORT_ONCE(ztestequal, G3D, "Depth test requiring depth equality in throughmode: %d", ztest);
				}
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

	ViewportAndScissor vpAndScissor;
	ConvertViewportAndScissor(useBufferedRendering,
		framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
		framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
		vpAndScissor);

	if (vpAndScissor.scissorEnable) {
		glstate.scissorTest.enable();
		if (!useBufferedRendering) {
			vpAndScissor.scissorY = PSP_CoreParameter().pixelHeight - vpAndScissor.scissorH - vpAndScissor.scissorY;
		}
		glstate.scissorRect.set(vpAndScissor.scissorX, vpAndScissor.scissorY, vpAndScissor.scissorW, vpAndScissor.scissorH);
	} else {
		glstate.scissorTest.disable();
	}

	if (!useBufferedRendering) {
		vpAndScissor.viewportY = PSP_CoreParameter().pixelHeight - vpAndScissor.viewportH - vpAndScissor.viewportY;
	}
	glstate.viewport.set(vpAndScissor.viewportX, vpAndScissor.viewportY, vpAndScissor.viewportW, vpAndScissor.viewportH);
	glstate.depthRange.set(vpAndScissor.depthRangeMin, vpAndScissor.depthRangeMax);

	if (vpAndScissor.dirtyProj) {
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}
}

void TransformDrawEngine::ApplyDrawStateLate() {
	// At this point, we know if the vertices are full alpha or not.
	// TODO: Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
	if (!gstate.isModeClear()) {
		if (gstate.isAlphaTestEnabled() || gstate.isColorTestEnabled()) {
			fragmentTestCache_->BindTestTexture(GL_TEXTURE2);
		}

		textureCache_->ApplyTexture();

		if (fboTexNeedBind_) {
			// Note that this is positions, not UVs, that we need the copy from.
			framebufferManager_->BindFramebufferColor(GL_TEXTURE1, gstate.getFrameBufRawAddress(), nullptr, BINDFBCOLOR_MAY_COPY);
			framebufferManager_->RebindFramebuffer();

			glActiveTexture(GL_TEXTURE1);
			// If we are rendering at a higher resolution, linear is probably best for the dest color.
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glActiveTexture(GL_TEXTURE0);
			fboTexBound_ = true;
			fboTexNeedBind_ = false;
		}
	}
}
