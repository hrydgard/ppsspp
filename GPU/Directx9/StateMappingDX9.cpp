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


#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Directx9/StateMappingDX9.h"
#include "GPU/Directx9/GPU_DX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"

namespace DX9 {

static const D3DBLEND aLookup[11] = {
	D3DBLEND_DESTCOLOR,
	D3DBLEND_INVDESTCOLOR,
	D3DBLEND_SRCALPHA,
	D3DBLEND_INVSRCALPHA,
	D3DBLEND_DESTALPHA,
	D3DBLEND_INVDESTALPHA,
	D3DBLEND_SRCALPHA,	// should be 2x
	D3DBLEND_INVSRCALPHA,	 // should be 2x
	D3DBLEND_DESTALPHA,	 // should be 2x
	D3DBLEND_INVDESTALPHA,	 // should be 2x	-	and COLOR?
	D3DBLEND_BLENDFACTOR,	// FIXA
};

static const D3DBLEND bLookup[11] = {
	D3DBLEND_SRCCOLOR,
	D3DBLEND_INVSRCCOLOR,
	D3DBLEND_SRCALPHA,
	D3DBLEND_INVSRCALPHA,
	D3DBLEND_DESTALPHA,
	D3DBLEND_INVDESTALPHA,
	D3DBLEND_SRCALPHA,	// should be 2x
	D3DBLEND_INVSRCALPHA,	 // should be 2x
	D3DBLEND_DESTALPHA,	 // should be 2x
	D3DBLEND_INVDESTALPHA,	 // should be 2x
	D3DBLEND_BLENDFACTOR,	// FIXB
};

static const D3DBLENDOP eqLookup[] = {
	D3DBLENDOP_ADD,
	D3DBLENDOP_SUBTRACT,
	D3DBLENDOP_REVSUBTRACT,
	D3DBLENDOP_MIN,
	D3DBLENDOP_MAX,
	D3DBLENDOP_ADD, // should be abs(diff)
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

static D3DBLEND toDualSource(D3DBLEND blendfunc) {
#if 0
	switch (blendfunc) {
		// TODO
	case D3DBLEND_SRCALPHA:
		return D3DBLEND_SRCCOLOR2;
	case D3DBLEND_INVSRCALPHA:
		return D3DBLEND_INVSRCCOLOR2;
	default:
		return blendfunc;
	}
#else
	return blendfunc;
#endif
}

static D3DBLEND blendColor2Func(u32 fix, bool &approx) {
	if (fix == 0xFFFFFF)
		return D3DBLEND_ONE;
	if (fix == 0)
		return D3DBLEND_ZERO;

	// Otherwise, it's approximate if we pick ONE/ZERO.
	approx = true;

	const Vec3f fix3 = Vec3f::FromRGB(fix);
	if (fix3.x >= 0.99 && fix3.y >= 0.99 && fix3.z >= 0.99)
		return D3DBLEND_ONE;
	else if (fix3.x <= 0.01 && fix3.y <= 0.01 && fix3.z <= 0.01)
		return D3DBLEND_ZERO;
	return D3DBLEND_UNK;
}

static inline bool blendColorSimilar(const Vec3f &a, const Vec3f &b, float margin = 0.1f) {
	const Vec3f diff = a - b;
	if (fabsf(diff.x) <= margin && fabsf(diff.y) <= margin && fabsf(diff.z) <= margin)
		return true;
	return false;
}

bool TransformDrawEngineDX9::ApplyShaderBlending() {
	bool skipBlit = false;

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

	framebufferManager_->BindFramebufferColor(1, nullptr, false);
	// If we are rendering at a higher resolution, linear is probably best for the dest color.
	pD3Ddevice->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	pD3Ddevice->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	fboTexBound_ = true;

	shaderManager_->DirtyUniform(DIRTY_SHADERBLEND);
	return true;
}

inline void TransformDrawEngineDX9::ResetShaderBlending() {
	if (fboTexBound_) {
		pD3Ddevice->SetTexture(1, nullptr);
		fboTexBound_ = false;
	}
}

void TransformDrawEngineDX9::ApplyStencilReplaceAndLogicOp(ReplaceAlphaType replaceAlphaWithStencil) {
	StencilValueType stencilType = STENCIL_VALUE_KEEP;
	if (replaceAlphaWithStencil == REPLACE_ALPHA_YES) {
		stencilType = ReplaceAlphaWithStencilType();
	}

	// Normally, we would add src + 0, but the logic op may have us do differently.
	D3DBLEND srcBlend = D3DBLEND_ONE;
	D3DBLEND dstBlend = D3DBLEND_ZERO;
	D3DBLENDOP blendOp = D3DBLENDOP_ADD;
	if (gstate.isLogicOpEnabled()) {
		switch (gstate.getLogicOp())
		{
		case GE_LOGIC_CLEAR:
			srcBlend = D3DBLEND_ZERO;
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
			srcBlend = D3DBLEND_ONE;
			dstBlend = D3DBLEND_ONE;
			blendOp = D3DBLENDOP_SUBTRACT;
			WARN_LOG_REPORT_ONCE(d3dLogicOpInverted, G3D, "Attempted inverse for logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_NOOP:
			srcBlend = D3DBLEND_ZERO;
			dstBlend = D3DBLEND_ONE;
			break;
		case GE_LOGIC_XOR:
			WARN_LOG_REPORT_ONCE(d3dLogicOpOrXor, G3D, "Unsupported XOR logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_OR:
		case GE_LOGIC_OR_INVERTED:
			// Inverted in shader.
			dstBlend = D3DBLEND_ONE;
			WARN_LOG_REPORT_ONCE(d3dLogicOpOr, G3D, "Attempted or for logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_OR_REVERSE:
			WARN_LOG_REPORT_ONCE(d3dLogicOpOrReverse, G3D, "Unsupported OR REVERSE logic op: %x", gstate.getLogicOp());
			break;
		case GE_LOGIC_SET:
			dstBlend = D3DBLEND_ONE;
			WARN_LOG_REPORT_ONCE(d3dLogicOpSet, G3D, "Attempted set for logic op: %x", gstate.getLogicOp());
			break;
		}
	}

	// We're not blending, but we may still want to blend for stencil.
	// This is only useful for INCR/DECR/INVERT.  Others can write directly.
	switch (stencilType) {
	case STENCIL_VALUE_INCR_4:
	case STENCIL_VALUE_INCR_8:
		// We'll add the incremented value output by the shader.
		dxstate.blendFunc.set(srcBlend, dstBlend, D3DBLEND_ONE, D3DBLEND_ONE);
		dxstate.blendEquation.set(blendOp, D3DBLENDOP_ADD);
		dxstate.blend.enable();
		dxstate.blendSeparate.enable();
		break;

	case STENCIL_VALUE_DECR_4:
	case STENCIL_VALUE_DECR_8:
		// We'll subtract the incremented value output by the shader.
		dxstate.blendFunc.set(srcBlend, dstBlend, D3DBLEND_ONE, D3DBLEND_ONE);
		dxstate.blendEquation.set(blendOp, D3DBLENDOP_SUBTRACT);
		dxstate.blend.enable();
		dxstate.blendSeparate.enable();
		break;

	case STENCIL_VALUE_INVERT:
		// The shader will output one, and reverse subtracting will essentially invert.
		dxstate.blendFunc.set(srcBlend, dstBlend, D3DBLEND_ONE, D3DBLEND_ONE);
		dxstate.blendEquation.set(blendOp, D3DBLENDOP_REVSUBTRACT);
		dxstate.blend.enable();
		dxstate.blendSeparate.enable();
		break;

	default:
		if (srcBlend == D3DBLEND_ONE && dstBlend == D3DBLEND_ZERO && blendOp == D3DBLENDOP_ADD) {
			dxstate.blend.disable();
		} else {
			dxstate.blendFunc.set(srcBlend, dstBlend, D3DBLEND_ONE, D3DBLEND_ZERO);
			dxstate.blendEquation.set(blendOp, D3DBLENDOP_ADD);
			dxstate.blend.enable();
			dxstate.blendSeparate.enable();
		}
		break;
	}
}

void TransformDrawEngineDX9::ApplyBlendState() {
	// Blending is a bit complex to emulate.  This is due to several reasons:
	//
	//  * Doubled blend modes (src, dst, inversed) aren't supported in Direct3D.
	//    If possible, we double the src color or src alpha in the shader to account for these.
	//    These may clip incorrectly, so we avoid unfortunately.
	//  * Direct3D only has one arbitrary fixed color.  We premultiply the other in the shader.
	//  * The written output alpha should actually be the stencil value.  Alpha is not written.
	//  * We try to apply logical operations through blending.
	//
	// If we can't apply blending, we make a copy of the framebuffer and do it manually.

	// Unfortunately, we can't really do this in Direct3D 9...
	gstate_c.allowShaderBlend = false;

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

	if (gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY) {
		WARN_LOG_REPORT_ONCE(logicOpBlend, G3D, "Logic op and blend enabled, unsupported.");
	}

	dxstate.blend.enable();
	dxstate.blendSeparate.enable();
	ResetShaderBlending();

	GEBlendMode blendFuncEq = gstate.getBlendEq();
	int blendFuncA  = gstate.getBlendFuncA();
	int blendFuncB  = gstate.getBlendFuncB();
	if (blendFuncA > GE_SRCBLEND_FIXA)
		blendFuncA = GE_SRCBLEND_FIXA;
	if (blendFuncB > GE_DSTBLEND_FIXB)
		blendFuncB = GE_DSTBLEND_FIXB;

	float constantAlpha = 1.0f;
	D3DBLEND constantAlphaDX = D3DBLEND_ONE;
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
			constantAlphaDX = D3DBLEND_ZERO;
		} else if (constantAlpha < 1.0f) {
			constantAlphaDX = D3DBLEND_BLENDFACTOR;
		}
	}

	// Shortcut by using D3DBLEND_ONE where possible, no need to set blendcolor
	bool approxFuncA = false;
	D3DBLEND glBlendFuncA = blendFuncA == GE_SRCBLEND_FIXA ? blendColor2Func(gstate.getFixA(), approxFuncA) : aLookup[blendFuncA];
	bool approxFuncB = false;
	D3DBLEND glBlendFuncB = blendFuncB == GE_DSTBLEND_FIXB ? blendColor2Func(gstate.getFixB(), approxFuncB) : bLookup[blendFuncB];

	if (usePreSrc) {
		glBlendFuncA = D3DBLEND_ONE;
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
		dxstate.blendColor.set(blendColor);
	};
	auto defaultBlendColor = [&]() {
		if (constantAlphaDX == D3DBLEND_BLENDFACTOR) {
			const float blendColor[4] = {1.0f, 1.0f, 1.0f, constantAlpha};
			dxstate.blendColor.set(blendColor);
		}
	};

	if (blendFuncA == GE_SRCBLEND_FIXA || blendFuncB == GE_DSTBLEND_FIXB) {
		const Vec3f fixA = Vec3f::FromRGB(gstate.getFixA());
		const Vec3f fixB = Vec3f::FromRGB(gstate.getFixB());
		if (glBlendFuncA == D3DBLEND_UNK && glBlendFuncB != D3DBLEND_UNK) {
			// Can use blendcolor trivially.
			setBlendColorv(fixA);
			glBlendFuncA = D3DBLEND_BLENDFACTOR;
		} else if (glBlendFuncA != D3DBLEND_UNK && glBlendFuncB == D3DBLEND_UNK) {
			// Can use blendcolor trivially.
			setBlendColorv(fixB);
			glBlendFuncB = D3DBLEND_BLENDFACTOR;
		} else if (glBlendFuncA == D3DBLEND_UNK && glBlendFuncB == D3DBLEND_UNK) {
			if (blendColorSimilar(fixA, Vec3f::AssignToAll(1.0f) - fixB)) {
				glBlendFuncA = D3DBLEND_BLENDFACTOR;
				glBlendFuncB = D3DBLEND_INVBLENDFACTOR;
				setBlendColorv(fixA);
			} else if (blendColorSimilar(fixA, fixB)) {
				glBlendFuncA = D3DBLEND_BLENDFACTOR;
				glBlendFuncB = D3DBLEND_BLENDFACTOR;
				setBlendColorv(fixA);
			} else {
				DEBUG_LOG(G3D, "ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
				// Let's approximate, at least.  Close is better than totally off.
				const bool nearZeroA = blendColorSimilar(fixA, Vec3f::AssignToAll(0.0f), 0.25f);
				const bool nearZeroB = blendColorSimilar(fixB, Vec3f::AssignToAll(0.0f), 0.25f);
				if (nearZeroA || blendColorSimilar(fixA, Vec3f::AssignToAll(1.0f), 0.25f)) {
					glBlendFuncA = nearZeroA ? D3DBLEND_ZERO : D3DBLEND_ONE;
					glBlendFuncB = D3DBLEND_BLENDFACTOR;
					setBlendColorv(fixB);
				} else {
					// We need to pick something.  Let's go with A as the fixed color.
					glBlendFuncA = D3DBLEND_BLENDFACTOR;
					glBlendFuncB = nearZeroB ? D3DBLEND_ZERO : D3DBLEND_ONE;
					setBlendColorv(fixA);
				}
			}
		} else {
			// We optimized both, but that's probably not necessary, so let's pick one to be constant.
			if (blendFuncA == GE_SRCBLEND_FIXA && !usePreSrc && approxFuncA) {
				glBlendFuncA = D3DBLEND_BLENDFACTOR;
				setBlendColorv(fixA);
			} else if (approxFuncB) {
				glBlendFuncB = D3DBLEND_BLENDFACTOR;
				setBlendColorv(fixB);
			} else {
				defaultBlendColor();
			}
		}
	} else {
		defaultBlendColor();
	}

	// At this point, through all paths above, glBlendFuncA and glBlendFuncB will be set right somehow.

	// The stencil-to-alpha in fragment shader doesn't apply here (blending is enabled), and we shouldn't
	// do any blending in the alpha channel as that doesn't seem to happen on PSP.  So, we attempt to
	// apply the stencil to the alpha, since that's what should be stored.
	D3DBLENDOP alphaEq = D3DBLENDOP_ADD;
	if (replaceAlphaWithStencil != REPLACE_ALPHA_NO) {
		// Let the fragment shader take care of it.
		switch (ReplaceAlphaWithStencilType()) {
		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_INCR_8:
			// We'll add the increment value.
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ONE, D3DBLEND_ONE);
			break;

		case STENCIL_VALUE_DECR_4:
		case STENCIL_VALUE_DECR_8:
			// Like add with a small value, but subtracting.
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ONE, D3DBLEND_ONE);
			alphaEq = D3DBLENDOP_SUBTRACT;
			break;

		case STENCIL_VALUE_INVERT:
			// This will subtract by one, effectively inverting the bits.
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ONE, D3DBLEND_ONE);
			alphaEq = D3DBLENDOP_REVSUBTRACT;
			break;

		default:
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ONE, D3DBLEND_ZERO);
			break;
		}
	} else if (gstate.isStencilTestEnabled()) {
		switch (ReplaceAlphaWithStencilType()) {
		case STENCIL_VALUE_KEEP:
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ZERO, D3DBLEND_ONE);
			break;
		case STENCIL_VALUE_ONE:
			// This won't give one but it's our best shot...
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ONE, D3DBLEND_ONE);
			break;
		case STENCIL_VALUE_ZERO:
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ZERO, D3DBLEND_ZERO);
			break;
		case STENCIL_VALUE_UNIFORM:
			// This won't give a correct value (it multiplies) but it may be better than random values.
			// TODO: Does this work as the alpha component of the fixed blend factor?
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, constantAlphaDX, D3DBLEND_ZERO);
			break;
		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_INCR_8:
			// This won't give a correct value always, but it will try to increase at least.
			// TODO: Does this work as the alpha component of the fixed blend factor?
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, constantAlphaDX, D3DBLEND_ONE);
			break;
		case STENCIL_VALUE_DECR_4:
		case STENCIL_VALUE_DECR_8:
			// This won't give a correct value always, but it will try to decrease at least.
			// TODO: Does this work as the alpha component of the fixed blend factor?
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, constantAlphaDX, D3DBLEND_ONE);
			alphaEq = D3DBLENDOP_SUBTRACT;
			break;
		case STENCIL_VALUE_INVERT:
			dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ONE, D3DBLEND_ONE);
			// If the output alpha is near 1, this will basically invert.  It's our best shot.
			alphaEq = D3DBLENDOP_REVSUBTRACT;
			break;
		}
	} else {
		// Retain the existing value when stencil testing is off.
		dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB, D3DBLEND_ZERO, D3DBLEND_ONE);
	}

	dxstate.blendEquation.set(eqLookup[blendFuncEq], alphaEq);
}

void TransformDrawEngineDX9::ApplyDrawState(int prim) {
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

	// Set Dither
	if (gstate.isDitherEnabled()) {
		dxstate.dither.enable();
		dxstate.dither.set(true);
	} else
		dxstate.dither.disable();

	// Set ColorMask/Stencil/Depth
	if (gstate.isModeClear()) {

		// Set Cull 
		dxstate.cullMode.set(false, false);
		
		// Depth Test
		dxstate.depthTest.enable();
		dxstate.depthFunc.set(D3DCMP_ALWAYS);
		dxstate.depthWrite.set(gstate.isClearModeDepthMask());
		if (gstate.isClearModeDepthMask()) {
			framebufferManager_->SetDepthUpdated();
		}

		// Color Test
		bool colorMask = gstate.isClearModeColorMask();
		bool alphaMask = gstate.isClearModeAlphaMask();
		dxstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);

		// Stencil Test
		if (alphaMask) {
			dxstate.stencilTest.enable();
			dxstate.stencilOp.set(D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE);
			dxstate.stencilFunc.set(D3DCMP_ALWAYS, 0, 0xFF);
			dxstate.stencilMask.set(0xFF);
		} else {
			dxstate.stencilTest.disable();
		}

	} else {
		// Set cull
		bool wantCull = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		dxstate.cullMode.set(wantCull, gstate.getCullMode());	

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

		dxstate.colorMask.set(rmask, gmask, bmask, amask);
		
		// Stencil Test
		if (gstate.isStencilTestEnabled()) {
			dxstate.stencilTest.enable();
			dxstate.stencilFunc.set(ztests[gstate.getStencilTestFunction()],
				gstate.getStencilTestRef(),
				gstate.getStencilTestMask());
			dxstate.stencilOp.set(stencilOps[gstate.getStencilOpSFail()],  // stencil fail
				stencilOps[gstate.getStencilOpZFail()],  // depth fail
				stencilOps[gstate.getStencilOpZPass()]); // depth pass
			dxstate.stencilMask.set(~abits);
		} else {
			dxstate.stencilTest.disable();
		}
	}

#if defined(DX9_USE_HW_ALPHA_TEST)
	// Older hardware (our target for DX9) often has separate alpha testing hardware that
	// is generally faster than using discard/clip. Let's use it.
	if (gstate.alphaTestEnable) {
		dxstate.alphaTest.enable();
		GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
		dxstate.alphaTestFunc.set(ztests[alphaTestFunc]);
		dxstate.alphaTestRef.set(gstate.getAlphaTestRef());
	} else {
		dxstate.alphaTest.disable();
	}
#endif

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
		dxstate.scissorTest.disable();
	} else {
		dxstate.scissorTest.enable();
		dxstate.scissorRect.set(
			renderX + scissorX1 * renderWidthFactor,
			renderY + scissorY1 * renderHeightFactor,
			renderX + scissorX2 * renderWidthFactor,
			renderY + scissorY2 * renderHeightFactor);
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
		dxstate.viewport.set(
			renderX + (0 + regionX1) * renderWidthFactor, 
			renderY + (0 + regionY1) * renderHeightFactor,
			(regionX2 - regionX1) * renderWidthFactor,
			(regionY2 - regionY1) * renderHeightFactor,
			0.f, 1.f);
	} else {
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
		float vpY0 = vpYCenter - offsetY - fabsf(vpYScale);
		gstate_c.vpWidth = vpXScale * 2.0f;
		gstate_c.vpHeight = vpYScale * 2.0f;

		float vpWidth = fabsf(gstate_c.vpWidth);
		float vpHeight = fabsf(gstate_c.vpHeight);

		vpX0 *= renderWidthFactor;
		vpY0 *= renderHeightFactor;
		vpWidth *= renderWidthFactor;
		vpHeight *= renderHeightFactor;

		float zScale = getFloat24(gstate.viewportz1) / 65535.0f;
		float zOff = getFloat24(gstate.viewportz2) / 65535.0f;

		float depthRangeMin = zOff - fabsf(zScale);
		float depthRangeMax = zOff + fabsf(zScale);

		gstate_c.vpDepth = zScale * 2;

		// D3D doesn't like viewports partially outside the target, so we
		// apply the viewport partially in the shader.
		float left = renderX + vpX0;
		float top = renderY + vpY0;
		float right = left + vpWidth;
		float bottom = top + vpHeight;

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

		if (top < 0.0f || bottom > renderHeight) {
			float overageTop = std::max(-top, 0.0f);
			float overageBottom = std::max(bottom - renderHeight, 0.0f);
			// Our center drifted by the difference in overages.
			float drift = overageBottom - overageTop;

			top += overageTop;
			bottom -= overageBottom;

			hScale = vpHeight / (bottom - top);
			yOffset = -drift / (bottom - top);
		}

		depthRangeMin = std::max(0.0f, depthRangeMin);
		depthRangeMax = std::min(1.0f, depthRangeMax);

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

		dxstate.viewport.set(left, top, right - left, bottom - top, depthRangeMin, depthRangeMax);
	}
}

};
