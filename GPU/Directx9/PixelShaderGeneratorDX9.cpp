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

#include <cstdio>

#include "Core/Reporting.h"
#include "Core/Config.h"
#include "GPU/Directx9/helper/global.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#define WRITE p+=sprintf

// #define DEBUG_SHADER

namespace DX9 {


// Dest factors where it's safe to eliminate the alpha test under certain conditions
static const bool safeDestFactors[16] = {
	true, // GE_DSTBLEND_SRCCOLOR,
	true, // GE_DSTBLEND_INVSRCCOLOR,
	false, // GE_DSTBLEND_SRCALPHA,
	true, // GE_DSTBLEND_INVSRCALPHA,
	true, // GE_DSTBLEND_DSTALPHA,
	true, // GE_DSTBLEND_INVDSTALPHA,
	false, // GE_DSTBLEND_DOUBLESRCALPHA,
	false, // GE_DSTBLEND_DOUBLEINVSRCALPHA,
	true, // GE_DSTBLEND_DOUBLEDSTALPHA,
	true, // GE_DSTBLEND_DOUBLEINVDSTALPHA,
	true, //GE_DSTBLEND_FIXB,
};

bool IsAlphaTestTriviallyTrue() {
	switch (gstate.getAlphaTestFunction()) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_GEQUAL:
		if (gstate_c.vertexFullAlpha && (gstate_c.textureFullAlpha || !gstate.isTextureAlphaUsed()))
			return true;  // If alpha is full, it doesn't matter what the ref value is.
		return gstate.getAlphaTestRef() == 0;

	// Non-zero check. If we have no depth testing (and thus no depth writing), and an alpha func that will result in no change if zero alpha, get rid of the alpha test.
	// Speeds up Lumines by a LOT on PowerVR.
	case GE_COMP_NOTEQUAL:
		if (gstate.getAlphaTestRef() == 255) {
			// Likely to be rare. Let's just skip the vertexFullAlpha optimization here instead of adding
			// complicated code to discard the draw or whatnot.
			return false;
		}
		// Fallthrough on purpose

	case GE_COMP_GREATER:
		{
#if 0
			// Easy way to check the values in the debugger without ruining && early-out
			bool doTextureAlpha = gstate.isTextureAlphaUsed();
			bool stencilTest = gstate.isStencilTestEnabled();
			bool depthTest = gstate.isDepthTestEnabled();
			GEComparison depthTestFunc = gstate.getDepthTestFunction();
			int alphaRef = gstate.getAlphaTestRef();
			int blendA = gstate.getBlendFuncA();
			bool blendEnabled = gstate.isAlphaBlendEnabled();
			int blendB = gstate.getBlendFuncA();
#endif
			return (gstate_c.vertexFullAlpha && (gstate_c.textureFullAlpha || !gstate.isTextureAlphaUsed())) || (
					(!gstate.isStencilTestEnabled() &&
					!gstate.isDepthTestEnabled() &&
					gstate.getAlphaTestRef() == 0 &&
					gstate.isAlphaBlendEnabled() &&
					gstate.getBlendFuncA() == GE_SRCBLEND_SRCALPHA &&
					safeDestFactors[(int)gstate.getBlendFuncB()]));
		}

	case GE_COMP_LEQUAL:
		return gstate.getAlphaTestRef() == 255;

	case GE_COMP_EQUAL:
	case GE_COMP_LESS:
		return false;

	default:
		return false;
	}
}

bool IsAlphaTestAgainstZero() {
	return gstate.getAlphaTestRef() == 0 && gstate.getAlphaTestMask() == 0xFF;
}

bool IsColorTestAgainstZero() {
	return gstate.getColorTestRef() == 0 && gstate.getColorTestMask() == 0xFFFFFF;
}

const bool nonAlphaSrcFactors[16] = {
	true,  // GE_SRCBLEND_DSTCOLOR,
	true,  // GE_SRCBLEND_INVDSTCOLOR,
	false, // GE_SRCBLEND_SRCALPHA,
	false, // GE_SRCBLEND_INVSRCALPHA,
	true,  // GE_SRCBLEND_DSTALPHA,
	true,  // GE_SRCBLEND_INVDSTALPHA,
	false, // GE_SRCBLEND_DOUBLESRCALPHA,
	false, // GE_SRCBLEND_DOUBLEINVSRCALPHA,
	true,  // GE_SRCBLEND_DOUBLEDSTALPHA,
	true,  // GE_SRCBLEND_DOUBLEINVDSTALPHA,
	true,  // GE_SRCBLEND_FIXA,
};

const bool nonAlphaDestFactors[16] = {
	true,  // GE_DSTBLEND_SRCCOLOR,
	true,  // GE_DSTBLEND_INVSRCCOLOR,
	false, // GE_DSTBLEND_SRCALPHA,
	false, // GE_DSTBLEND_INVSRCALPHA,
	true,  // GE_DSTBLEND_DSTALPHA,
	true,  // GE_DSTBLEND_INVDSTALPHA,
	false, // GE_DSTBLEND_DOUBLESRCALPHA,
	false, // GE_DSTBLEND_DOUBLEINVSRCALPHA,
	true,  // GE_DSTBLEND_DOUBLEDSTALPHA,
	true,  // GE_DSTBLEND_DOUBLEINVDSTALPHA,
	true,  // GE_DSTBLEND_FIXB,
};

ReplaceAlphaType ReplaceAlphaWithStencil(ReplaceBlendType replaceBlend) {
	if (!gstate.isStencilTestEnabled() || gstate.isModeClear()) {
		return REPLACE_ALPHA_NO;
	}

	if (replaceBlend != REPLACE_BLEND_NO && replaceBlend != REPLACE_BLEND_COPY_FBO) {
		if (nonAlphaSrcFactors[gstate.getBlendFuncA()] && nonAlphaDestFactors[gstate.getBlendFuncB()]) {
			return REPLACE_ALPHA_YES;
		} else {
			// TODO
#if 0
			if (pD3DdeviceEx) {
				return REPLACE_ALPHA_DUALSOURCE;
			} else {
#else
			{
#endif
				return REPLACE_ALPHA_NO;
			}
		}
	}

	return REPLACE_ALPHA_YES;
}

StencilValueType ReplaceAlphaWithStencilType() {
	switch (gstate.FrameBufFormat()) {
	case GE_FORMAT_565:
		// There's never a stencil value.  Maybe the right alpha is 1?
		return STENCIL_VALUE_ONE;

	case GE_FORMAT_5551:
		switch (gstate.getStencilOpZPass()) {
		// Technically, this should only ever use zero/one.
		case GE_STENCILOP_REPLACE:
			return (gstate.getStencilTestRef() & 0x80) != 0 ? STENCIL_VALUE_ONE : STENCIL_VALUE_ZERO;

		// Decrementing always zeros, since there's only one bit.
		case GE_STENCILOP_DECR:
		case GE_STENCILOP_ZERO:
			return STENCIL_VALUE_ZERO;

		// Incrementing always fills, since there's only one bit.
		case GE_STENCILOP_INCR:
			return STENCIL_VALUE_ONE;

		case GE_STENCILOP_INVERT:
			return STENCIL_VALUE_INVERT;

		case GE_STENCILOP_KEEP:
			return STENCIL_VALUE_KEEP;
		}
		break;

	case GE_FORMAT_4444:
	case GE_FORMAT_8888:
	case GE_FORMAT_INVALID:
		switch (gstate.getStencilOpZPass()) {
		case GE_STENCILOP_REPLACE:
			return STENCIL_VALUE_UNIFORM;

		case GE_STENCILOP_ZERO:
			return STENCIL_VALUE_ZERO;

		case GE_STENCILOP_DECR:
			return gstate.FrameBufFormat() == GE_FORMAT_4444 ? STENCIL_VALUE_DECR_4 : STENCIL_VALUE_DECR_8;

		case GE_STENCILOP_INCR:
			return gstate.FrameBufFormat() == GE_FORMAT_4444 ? STENCIL_VALUE_INCR_4 : STENCIL_VALUE_INCR_8;

		case GE_STENCILOP_INVERT:
			return STENCIL_VALUE_INVERT;

		case GE_STENCILOP_KEEP:
			return STENCIL_VALUE_KEEP;
		}
		break;
	}

	return STENCIL_VALUE_KEEP;
}

bool IsColorTestTriviallyTrue() {
	switch (gstate.getColorTestFunction()) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
	case GE_COMP_NOTEQUAL:
		return false;
	default:
		return false;
	}
}

ReplaceBlendType ReplaceBlendWithShader() {
	if (!gstate.isAlphaBlendEnabled() || gstate.isModeClear()) {
		return REPLACE_BLEND_NO;
	}

	GEBlendSrcFactor funcA = gstate.getBlendFuncA();
	GEBlendDstFactor funcB = gstate.getBlendFuncB();
	GEBlendMode eq = gstate.getBlendEq();

	// Let's get the non-factor modes out of the way first.
	switch (eq) {
	case GE_BLENDMODE_ABSDIFF:
		return !gstate_c.allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

	case GE_BLENDMODE_MIN:
	case GE_BLENDMODE_MAX:
		return REPLACE_BLEND_STANDARD;

	default:
		break;
	}

	switch (funcA) {
	case GE_SRCBLEND_DOUBLESRCALPHA:
	case GE_SRCBLEND_DOUBLEINVSRCALPHA:
		// 2x alpha in the source function and not in the dest = source color doubling.
		// Even dest alpha is safe, since we're moving the * 2.0 into the src color.
		switch (funcB) {
		case GE_DSTBLEND_SRCCOLOR:
		case GE_DSTBLEND_INVSRCCOLOR:
			// Can't double, we need the source color to be correct.
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			// We can't technically do this correctly (due to clamping) without reading the dst color.
			// Using a copy isn't accurate either, though, when there's overlap.
			return REPLACE_BLEND_PRE_SRC_2X_ALPHA;

		default:
			// TODO: Could use vertexFullAlpha, but it's not calculated yet.
			return REPLACE_BLEND_PRE_SRC;
		}

	case GE_SRCBLEND_DOUBLEDSTALPHA:
	case GE_SRCBLEND_DOUBLEINVDSTALPHA:
		switch (funcB) {
		case GE_DSTBLEND_SRCCOLOR:
		case GE_DSTBLEND_INVSRCCOLOR:
			// Can't double, we need the source color to be correct.
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_2X_SRC : REPLACE_BLEND_COPY_FBO;

		default:
			// We can't technically do this correctly (due to clamping) without reading the dst alpha.
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_2X_SRC : REPLACE_BLEND_COPY_FBO;
		}

	case GE_SRCBLEND_FIXA:
		switch (funcB) {
		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			// Can't safely double alpha, will clamp.
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_FIXB:
			if (gstate.getFixA() == 0xFFFFFF && gstate.getFixB() == 0x000000) {
				// Some games specify this.  Some cards may prefer blending off entirely.
				return REPLACE_BLEND_NO;
			} else if (gstate.getFixA() == 0xFFFFFF || gstate.getFixA() == 0x000000 || gstate.getFixB() == 0xFFFFFF || gstate.getFixB() == 0x000000) {
				return REPLACE_BLEND_STANDARD;
			} else {
				return REPLACE_BLEND_PRE_SRC;
			}

		default:
			return REPLACE_BLEND_STANDARD;
		}

	default:
		switch (funcB) {
		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			if (funcA == GE_SRCBLEND_SRCALPHA || funcA == GE_SRCBLEND_INVSRCALPHA) {
				// Can't safely double alpha, will clamp.  However, a copy may easily be worse due to overlap.
				return REPLACE_BLEND_PRE_SRC_2X_ALPHA;
			} else {
				// This means dst alpha/color is used in the src factor.
				// Unfortunately, copying here causes overlap problems in Silent Hill games (it seems?)
				// We will just hope that doubling alpha for the dst factor will not clamp too badly.
				return REPLACE_BLEND_2X_ALPHA;
			}

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		default:
			return REPLACE_BLEND_STANDARD;
		}
	}
}

static bool CanDoubleSrcBlendMode() {
	if (!gstate.isAlphaBlendEnabled()) {
		return false;
	}

	int funcA = gstate.getBlendFuncA();
	int funcB = gstate.getBlendFuncB();
	if (funcA != GE_SRCBLEND_DOUBLESRCALPHA) {
		funcB = funcA;
		funcA = gstate.getBlendFuncB();
	}
	if (funcA != GE_SRCBLEND_DOUBLESRCALPHA) {
		return false;
	}

	// One side should be doubled.  Let's check the other side.
	// LittleBigPlanet, for example, uses 2.0 * src, 1.0 - src, which can't double.
	switch (funcB) {
	case GE_DSTBLEND_SRCALPHA:
	case GE_DSTBLEND_INVSRCALPHA:
		return false;

	default:
		return true;
	}
}

enum LogicOpReplaceType {
	LOGICOPTYPE_NORMAL,
	LOGICOPTYPE_ONE,
	LOGICOPTYPE_INVERT,
};

static inline LogicOpReplaceType ReplaceLogicOpType() {
	if (gstate.isLogicOpEnabled()) {
		switch (gstate.getLogicOp()) {
		case GE_LOGIC_COPY_INVERTED:
		case GE_LOGIC_AND_INVERTED:
		case GE_LOGIC_OR_INVERTED:
		case GE_LOGIC_NOR:
		case GE_LOGIC_NAND:
		case GE_LOGIC_EQUIV:
			return LOGICOPTYPE_INVERT;
		case GE_LOGIC_INVERTED:
			return LOGICOPTYPE_ONE;
		case GE_LOGIC_SET:
			return LOGICOPTYPE_ONE;
		default:
			return LOGICOPTYPE_NORMAL;
		}
	}
	return LOGICOPTYPE_NORMAL;
}

// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderIDDX9(FragmentShaderIDDX9 *id) {
	int id0 = 0;
	int id1 = 0;
	if (gstate.isModeClear()) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id0 = 1;
	} else {
		bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled() && !gstate.isModeThrough();
		bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough();
		bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !g_Config.bDisableAlphaTest;
		bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue();
		bool enableColorDoubling = gstate.isColorDoublingEnabled();
		bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		ReplaceBlendType replaceBlend = ReplaceBlendWithShader();
		ReplaceAlphaType stencilToAlpha = ReplaceAlphaWithStencil(replaceBlend);

		// All texfuncs except replace are the same for RGB as for RGBA with full alpha.
		if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
			doTextureAlpha = false;

		// id0 |= (gstate.isModeClear() & 1);
		if (gstate.isTextureMapEnabled()) {
			id0 |= 1 << 1;
			id0 |= gstate.getTextureFunction() << 2;
			id0 |= (doTextureAlpha & 1) << 5; // rgb or rgba
			id0 |= (gstate_c.flipTexture & 1) << 6;

			if (gstate_c.needShaderTexClamp) {
				bool textureAtOffset = gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0;
				// 3 bits total.
				id0 |= 1 << 7;
				id0 |= gstate.isTexCoordClampedS() << 8;
				id0 |= gstate.isTexCoordClampedT() << 9;
				id0 |= (textureAtOffset & 1) << 10;
			}
		}

		id0 |= (lmode & 1) << 11;
#if !defined(DX9_USE_HW_ALPHA_TEST)
		if (enableAlphaTest) {
			// 5 bits total.
			id0 |= 1 << 12;
			id0 |= gstate.getAlphaTestFunction() << 13;
			id0 |= (IsAlphaTestAgainstZero() & 1) << 16;
		}
#endif
		if (enableColorTest) {
			// 4 bits total.
			id0 |= 1 << 17;
			id0 |= gstate.getColorTestFunction() << 18;
			id0 |= (IsColorTestAgainstZero() & 1) << 20;
		}
		id0 |= (enableFog & 1) << 21;
		id0 |= (doTextureProjection & 1) << 22;
		id0 |= (enableColorDoubling & 1) << 23;
		// 2 bits
		id0 |= (stencilToAlpha) << 24;

		if (stencilToAlpha != REPLACE_ALPHA_NO) {
			// 4 bits
			id0 |= ReplaceAlphaWithStencilType() << 26;
		}

		if (enableAlphaTest)
			gpuStats.numAlphaTestedDraws++;
		else
			gpuStats.numNonAlphaTestedDraws++;

		// 2 bits.
		id0 |= ReplaceLogicOpType() << 30;

		// 3 bits.
		id1 |= replaceBlend << 0;
		if (replaceBlend > REPLACE_BLEND_STANDARD) {
			// 11 bits total.
			id1 |= gstate.getBlendEq() << 3;
			id1 |= gstate.getBlendFuncA() << 6;
			id1 |= gstate.getBlendFuncB() << 10;
		}

		// TODO: Flat shading?

		id1 |= (gstate_c.bgraTexture & 1) << 15;
	}

	id->d[0] = id0;
	id->d[1] = id1;
}

// Missing: Z depth range
// Also, logic ops etc, of course. Urgh.
void GenerateFragmentShaderDX9(char *buffer) {
	char *p = buffer;

	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled() && !gstate.isModeThrough();
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !gstate.isModeClear() && !g_Config.bDisableAlphaTest;
	bool alphaTestAgainstZero = IsAlphaTestAgainstZero();
	bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && !gstate.isModeClear();
	bool colorTestAgainstZero = IsColorTestAgainstZero();
	bool enableColorDoubling = gstate.isColorDoublingEnabled() && gstate.isTextureMapEnabled();
	bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
	bool doTextureAlpha = gstate.isTextureAlphaUsed();
	bool textureAtOffset = gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0;
	ReplaceBlendType replaceBlend = ReplaceBlendWithShader();
	ReplaceAlphaType stencilToAlpha = ReplaceAlphaWithStencil(replaceBlend);

	if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
		doTextureAlpha = false;

	if (doTexture)
		WRITE(p, "sampler tex : register(s0);\n");
	if (!gstate.isModeClear() && replaceBlend > REPLACE_BLEND_STANDARD) {
		if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
			WRITE(p, "float2 u_fbotexSize : register(c%i);\n", CONST_PS_FBOTEXSIZE);
			WRITE(p, "sampler fbotex : register(s1);\n");
		}
		if (gstate.getBlendFuncA() == GE_SRCBLEND_FIXA) {
			WRITE(p, "float3 u_blendFixA : register(c%i);\n", CONST_PS_BLENDFIXA);
		}
		if (gstate.getBlendFuncB() == GE_DSTBLEND_FIXB) {
			WRITE(p, "float3 u_blendFixB : register(c%i);\n", CONST_PS_BLENDFIXB);
		}
	}
	if (gstate_c.needShaderTexClamp && doTexture) {
		WRITE(p, "float4 u_texclamp : register(c%i);\n", CONST_PS_TEXCLAMP);
		if (textureAtOffset) {
			WRITE(p, "float2 u_texclampoff : register(c%i);\n", CONST_PS_TEXCLAMPOFF);
		}
	}

	if (enableAlphaTest || enableColorTest) {
		WRITE(p, "float4 u_alphacolorref : register(c%i);\n", CONST_PS_ALPHACOLORREF);
		WRITE(p, "float4 u_alphacolormask : register(c%i);\n", CONST_PS_ALPHACOLORMASK);
	}
	if (stencilToAlpha && ReplaceAlphaWithStencilType() == STENCIL_VALUE_UNIFORM) {
		WRITE(p, "float u_stencilReplaceValue : register(c%i);\n", CONST_PS_STENCILREPLACE);
	}
	if (gstate.isTextureMapEnabled() && gstate.getTextureFunction() == GE_TEXFUNC_BLEND) {
		WRITE(p, "float3 u_texenv : register(c%i);\n", CONST_PS_TEXENV);
	}
	if (enableFog) {
		WRITE(p, "float3 u_fogcolor : register(c%i);\n", CONST_PS_FOGCOLOR);
	}

	if (enableAlphaTest) {
		WRITE(p, "float roundAndScaleTo255f(float x) { return floor(x * 255.0f + 0.5f); }\n");
	}
	if (enableColorTest) {
		WRITE(p, "float3 roundAndScaleTo255v(float3 x) { return floor(x * 255.0f + 0.5f); }\n");
	}

	WRITE(p, "struct PS_IN {\n");
	if (doTexture) {
		if (doTextureProjection)
			WRITE(p, "  float3 v_texcoord: TEXCOORD0;\n");
		else
			WRITE(p, "  float2 v_texcoord: TEXCOORD0;\n");
	}
	WRITE(p, "  float4 v_color0: COLOR0;\n");
	if (lmode) {
		WRITE(p, "  float3 v_color1: COLOR1;\n");
	}
	if (enableFog) {
		WRITE(p, "  float2 v_fogdepth: TEXCOORD1;\n");
	}
	WRITE(p, "};\n");
	WRITE(p, "float4 main( PS_IN In ) : COLOR\n");
	WRITE(p, "{\n");

	if (gstate.isModeClear()) {
		// Clear mode does not allow any fancy shading.
		WRITE(p, "  float4 v = In.v_color0;\n");
	} else {
		const char *secondary = "";
		// Secondary color for specular on top of texture
		if (lmode) {
			WRITE(p, "  float4 s = float4(In.v_color1, 0);\n");
			secondary = " + s";
		} else {
			secondary = "";
		}

		if (gstate.isTextureMapEnabled()) {
			const char *texcoord = "In.v_texcoord";
			// TODO: Not sure the right way to do this for projection.
			if (gstate_c.needShaderTexClamp) {
				// We may be clamping inside a larger surface (tex = 64x64, buffer=480x272).
				// We may also be wrapping in such a surface, or either one in a too-small surface.
				// Obviously, clamping to a smaller surface won't work.  But better to clamp to something.
				std::string ucoord = "In.v_texcoord.x";
				std::string vcoord = "In.v_texcoord.y";
				if (doTextureProjection) {
					ucoord += " / In.v_texcoord.z";
					vcoord = "(In.v_texcoord.y / In.v_texcoord.z)";
					// Vertex texcoords are NOT flipped when projecting despite gstate_c.flipTexture.
				} else if (gstate_c.flipTexture) {
					vcoord = "1.0 - " + vcoord;
				}

				if (gstate.isTexCoordClampedS()) {
					ucoord = "clamp(" + ucoord + ", u_texclamp.z, u_texclamp.x - u_texclamp.z)";
				} else {
					ucoord = "fmod(" + ucoord + ", u_texclamp.x)";
				}
				if (gstate.isTexCoordClampedT()) {
					vcoord = "clamp(" + vcoord + ", u_texclamp.w, u_texclamp.y - u_texclamp.w)";
				} else {
					vcoord = "fmod(" + vcoord + ", u_texclamp.y)";
				}
				if (textureAtOffset) {
					ucoord = "(" + ucoord + " + u_texclampoff.x)";
					vcoord = "(" + vcoord + " + u_texclampoff.y)";
				}

				if (gstate_c.flipTexture) {
					vcoord = "1.0 - " + vcoord;
				}

				WRITE(p, "  float2 fixedcoord = float2(%s, %s);\n", ucoord.c_str(), vcoord.c_str());
				texcoord = "fixedcoord";
				// We already projected it.
				doTextureProjection = false;
			} else if (doTextureProjection && gstate_c.flipTexture) {
				// Since we need to flip v, we project manually.
				WRITE(p, "  float2 fixedcoord = float2(v_texcoord.x / v_texcoord.z, 1.0 - (v_texcoord.y / v_texcoord.z));\n");
				texcoord = "fixedcoord";
				doTextureProjection = false;
			}

			if (doTextureProjection) {
				WRITE(p, "  float4 t = tex2Dproj(tex, float4(In.v_texcoord.x, In.v_texcoord.y, 0, In.v_texcoord.z))%s;\n", gstate_c.bgraTexture ? ".bgra" : "");
			} else {
				WRITE(p, "  float4 t = tex2D(tex, %s.xy)%s;\n", texcoord, gstate_c.bgraTexture ? ".bgra" : "");
			}
			WRITE(p, "  float4 p = In.v_color0;\n");

			if (doTextureAlpha) { // texfmt == RGBA
				switch (gstate.getTextureFunction()) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  float4 v = p * t%s;\n", secondary); break;
				case GE_TEXFUNC_DECAL:
					WRITE(p, "  float4 v = float4(lerp(p.rgb, t.rgb, t.a), p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_BLEND:
					WRITE(p, "  float4 v = float4(lerp(p.rgb, u_texenv.rgb, t.rgb), p.a * t.a)%s;\n", secondary); break;
				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  float4 v = t%s;\n", secondary); break;
				case GE_TEXFUNC_ADD:
				case GE_TEXFUNC_UNKNOWN1:
				case GE_TEXFUNC_UNKNOWN2:
				case GE_TEXFUNC_UNKNOWN3:
					WRITE(p, "  float4 v = float4(p.rgb + t.rgb, p.a * t.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  float4 v = p;\n"); break;
				}

			} else {	// texfmt == RGB
				switch (gstate.getTextureFunction()) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  float4 v = float4(t.rgb * p.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_DECAL:
					WRITE(p, "  float4 v = float4(t.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_BLEND:
					WRITE(p, "  float4 v = float4(lerp(p.rgb, u_texenv.rgb, t.rgb), p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  float4 v = float4(t.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_ADD:
				case GE_TEXFUNC_UNKNOWN1:
				case GE_TEXFUNC_UNKNOWN2:
				case GE_TEXFUNC_UNKNOWN3:
					WRITE(p, "  float4 v = float4(p.rgb + t.rgb, p.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  float4 v = p;\n"); break;
				}
			}
		} else {
			// No texture mapping
			WRITE(p, "  float4 v = In.v_color0 %s;\n", secondary);
		}

#if !defined(DX9_USE_HW_ALPHA_TEST)
		if (enableAlphaTest) {
			if (alphaTestAgainstZero) {
				GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
				// When testing against 0 (extremely common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (alphaTestFunc == GE_COMP_NOTEQUAL || alphaTestFunc == GE_COMP_GREATER) {
					WRITE(p, "  clip(v.a - 0.002);\n");
				} else if (alphaTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.  Happens sometimes, actually...
					WRITE(p, "  clip(-v.a + 0.002);\n");
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  clip(-1);\n");
				}
			} else {
				GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
				const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };	// never/always don't make sense
				if (alphaTestFuncs[alphaTestFunc][0] != '#') {
					// TODO: Rewrite this to use clip() appropriately (like, clip(v.a - u_alphacolorref.a))
					WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) clip(-1);\n", alphaTestFuncs[alphaTestFunc]);
				} else {
					// This means NEVER.  See above.
					WRITE(p, "  clip(-1);\n");
				}
			}
		}
#endif
		if (enableColorTest) {
			if (colorTestAgainstZero) {
				GEComparison colorTestFunc = gstate.getColorTestFunction();
				// When testing against 0 (common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (colorTestFunc == GE_COMP_NOTEQUAL) {
					WRITE(p, "  if (v.r < 0.002 && v.g < 0.002 && v.b < 0.002) clip(-1);\n");
				} else if (colorTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.
					WRITE(p, "  if (v.r > 0.002 || v.g > 0.002 || v.b > 0.002) clip(-1);\n");
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  clip(-1);\n");
				}
			} else {
				GEComparison colorTestFunc = gstate.getColorTestFunction();
				const char *colorTestFuncs[] = { "#", "#", " != ", " == " };	// never/always don't make sense
				u32 colorTestMask = gstate.getColorTestMask();
				if (colorTestFuncs[colorTestFunc][0] != '#') {
					const char * test = colorTestFuncs[colorTestFunc];
					WRITE(p, "  float3 colortest = roundAndScaleTo255v(v.rgb);\n");
					WRITE(p, "  if ((colortest.r %s u_alphacolorref.r) && (colortest.g %s u_alphacolorref.g) && (colortest.b %s u_alphacolorref.b )) clip(-1);\n", test, test, test);
				} else {
					WRITE(p, "  clip(-1);\n");
				}
			}
		}

		// Color doubling happens after the color test.
		if (enableColorDoubling && replaceBlend == REPLACE_BLEND_2X_SRC) {
			WRITE(p, "  v.rgb = v.rgb * 4.0;\n");
		} else if (enableColorDoubling || replaceBlend == REPLACE_BLEND_2X_SRC) {
			WRITE(p, "  v.rgb = v.rgb * 2.0;\n");
		}

		if (enableFog) {
			WRITE(p, "  float fogCoef = clamp(In.v_fogdepth.x, 0.0, 1.0);\n");
			WRITE(p, "  v = lerp(float4(u_fogcolor, v.a), v, fogCoef);\n");
		}

		if (replaceBlend == REPLACE_BLEND_PRE_SRC || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			GEBlendSrcFactor funcA = gstate.getBlendFuncA();
			const char *srcFactor = "ERROR";
			switch (funcA) {
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "float3(v.a, v.a, v.a)"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "float3(1.0 - v.a, 1.0 - v.a, 1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "float3(v.a * 2.0, v.a * 2.0, v.a * 2.0)"; break;
			// TODO: Double inverse, or inverse double?  Following softgpu for now...
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "float3(1.0 - v.a * 2.0, 1.0 - v.a * 2.0, 1.0 - v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "ERROR"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "ERROR"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			}

			WRITE(p, "  v.rgb = v.rgb * %s;\n", srcFactor);
		}

		// Can do REPLACE_BLEND_COPY_FBO in ps_2_0, but need to apply viewport in the vertex shader
		// so that we can have the output position here to sample the texture at.

		if (replaceBlend == REPLACE_BLEND_2X_ALPHA || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			WRITE(p, "  v.a = v.a * 2.0;\n");
		}
	}

	std::string replacedAlpha = "0.0";
	char replacedAlphaTemp[64] = "";
	if (stencilToAlpha != REPLACE_ALPHA_NO) {
		switch (ReplaceAlphaWithStencilType()) {
		case STENCIL_VALUE_UNIFORM:
			replacedAlpha = "u_stencilReplaceValue";
			break;

		case STENCIL_VALUE_ZERO:
			replacedAlpha = "0.0";
			break;

		case STENCIL_VALUE_ONE:
		case STENCIL_VALUE_INVERT:
			// In invert, we subtract by one, but we want to output one here.
			replacedAlpha = "1.0";
			break;

		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_DECR_4:
			// We're adding/subtracting, just by the smallest value in 4-bit.
			snprintf(replacedAlphaTemp, sizeof(replacedAlphaTemp), "%f", 1.0 / 15.0);
			replacedAlpha = replacedAlphaTemp;
			break;

		case STENCIL_VALUE_INCR_8:
		case STENCIL_VALUE_DECR_8:
			// We're adding/subtracting, just by the smallest value in 8-bit.
			snprintf(replacedAlphaTemp, sizeof(replacedAlphaTemp), "%f", 1.0 / 255.0);
			replacedAlpha = replacedAlphaTemp;
			break;

		case STENCIL_VALUE_KEEP:
			// Do nothing. We'll mask out the alpha using color mask.
			break;
		}
	}

	switch (stencilToAlpha) {
	case REPLACE_ALPHA_DUALSOURCE:
		WRITE(p, "  v.a = %s;\n", replacedAlpha.c_str());
		// TODO: Output the second color as well using original v.a.
		break;

	case REPLACE_ALPHA_YES:
		WRITE(p, "  v.a = %s;\n", replacedAlpha.c_str());
		break;

	case REPLACE_ALPHA_NO:
		// Do nothing, v is already fine.
		break;
	}

	switch (ReplaceLogicOpType()) {
	case LOGICOPTYPE_ONE:
		WRITE(p, "  v.rgb = float3(1.0, 1.0, 1.0);\n");
		break;
	case LOGICOPTYPE_INVERT:
		WRITE(p, "  v.rgb = float3(1.0, 1.0, 1.0) - v.rgb;\n");
		break;
	case LOGICOPTYPE_NORMAL:
		break;
	}

	WRITE(p, "  return v;\n");
	WRITE(p, "}\n");
}

};
