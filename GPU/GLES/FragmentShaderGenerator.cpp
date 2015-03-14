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

#if !defined(USING_GLES2)
// SDL 1.2 on Apple does not have support for OpenGL 3 and hence needs
// special treatment in the shader generator.
#if defined(__APPLE__)
#define FORCE_OPENGL_2_0
#endif
#endif

#include <cstdio>

#include "base/logging.h"
#include "gfx_es2/gpu_features.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "GPU/GLES/FragmentShaderGenerator.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#define WRITE p+=sprintf

// #define DEBUG_SHADER

// Dest factors where it's safe to eliminate the alpha test under certain conditions
const bool safeDestFactors[16] = {
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
			if (gl_extensions.ARB_blend_func_extended) {
				return REPLACE_ALPHA_DUALSOURCE;
			} else {
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
		if (gl_extensions.EXT_blend_minmax || gl_extensions.GLES3) {
			return REPLACE_BLEND_STANDARD;
		} else {
			return !gstate_c.allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;
		}

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
			if (gl_extensions.ANY_shader_framebuffer_fetch)
				return !gstate_c.allowShaderBlend ? REPLACE_BLEND_PRE_SRC_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
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
				if (gl_extensions.ANY_shader_framebuffer_fetch)
					return !gstate_c.allowShaderBlend ? REPLACE_BLEND_PRE_SRC_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
				return REPLACE_BLEND_PRE_SRC_2X_ALPHA;
			} else {
				// This means dst alpha/color is used in the src factor.
				// Unfortunately, copying here causes overlap problems in Silent Hill games (it seems?)
				// We will just hope that doubling alpha for the dst factor will not clamp too badly.
				if (gl_extensions.ANY_shader_framebuffer_fetch)
					return !gstate_c.allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
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

enum LogicOpReplaceType {
	LOGICOPTYPE_NORMAL,
	LOGICOPTYPE_ONE,
	LOGICOPTYPE_INVERT,
};

static inline LogicOpReplaceType ReplaceLogicOpType() {
#if defined(USING_GLES2)
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
#endif
	return LOGICOPTYPE_NORMAL;
}

// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderID(ShaderID *id) {
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
		bool enableColorDoubling = gstate.isColorDoublingEnabled() && gstate.isTextureMapEnabled();
		bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT;
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
		id1 |= (doFlatShading & 1) << 14;
	}

	id->d[0] = id0;
	id->d[1] = id1;
}

// Missing: Z depth range
void GenerateFragmentShader(char *buffer) {
	char *p = buffer;

	// In GLSL ES 3.0, you use "in" variables instead of varying.

	bool glslES30 = false;
	const char *varying = "varying";
	const char *fragColor0 = "gl_FragColor";
	const char *texture = "texture2D";
	const char *texelFetch = NULL;
	bool highpFog = false;
	bool highpTexcoord = false;
	bool bitwiseOps = false;
	const char *lastFragData = nullptr;

#if defined(USING_GLES2)
	// Let's wait until we have a real use for this.
	// ES doesn't support dual source alpha :(
	if (gl_extensions.GLES3) {
		WRITE(p, "#version 300 es\n");  // GLSL ES 3.0
		fragColor0 = "fragColor0";
		texture = "texture";
		glslES30 = true;
		bitwiseOps = true;
		texelFetch = "texelFetch";
	} else {
		WRITE(p, "#version 100\n");  // GLSL ES 1.0
		if (gl_extensions.EXT_gpu_shader4) {
			WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
			bitwiseOps = true;
			texelFetch = "texelFetch2D";
		}
	}

	// PowerVR needs highp to do the fog in MHU correctly.
	// Others don't, and some can't handle highp in the fragment shader.
	highpFog = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) ? true : false;
	highpTexcoord = highpFog;

	if (gl_extensions.EXT_shader_framebuffer_fetch) {
		WRITE(p, "#extension GL_EXT_shader_framebuffer_fetch : require\n");
		lastFragData = "gl_LastFragData[0]";
	} else if (gl_extensions.NV_shader_framebuffer_fetch) {
		// GL_NV_shader_framebuffer_fetch is available on mobile platform and ES 2.0 only but not on desktop.
		WRITE(p, "#extension GL_NV_shader_framebuffer_fetch : require\n");
		lastFragData = "gl_LastFragData[0]";
	} else if (gl_extensions.ARM_shader_framebuffer_fetch) {
		WRITE(p, "#extension GL_ARM_shader_framebuffer_fetch : require\n");
		lastFragData = "gl_LastFragColorARM";
	}

	WRITE(p, "precision lowp float;\n");
	
#elif !defined(FORCE_OPENGL_2_0)
	if (gl_extensions.VersionGEThan(3, 3, 0)) {
		fragColor0 = "fragColor0";
		texture = "texture";
		glslES30 = true;
		bitwiseOps = true;
		texelFetch = "texelFetch";
		WRITE(p, "#version 330\n");
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	} else if (gl_extensions.VersionGEThan(3, 0, 0)) {
		fragColor0 = "fragColor0";
		bitwiseOps = true;
		texelFetch = "texelFetch";
		WRITE(p, "#version 130\n");
		if (gl_extensions.EXT_gpu_shader4) {
			WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
		}
		// Remove lowp/mediump in non-mobile non-glsl 3 implementations
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	} else {
		WRITE(p, "#version 110\n");
		if (gl_extensions.EXT_gpu_shader4) {
			WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
			bitwiseOps = true;
			texelFetch = "texelFetch2D";
		}
		// Remove lowp/mediump in non-mobile non-glsl 3 implementations
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	}
#else
	// Need to remove lowp/mediump for Mac
	WRITE(p, "#define lowp\n");
	WRITE(p, "#define mediump\n");
	WRITE(p, "#define highp\n");
#endif

	if (glslES30) {
		varying = "in";
	}

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
	bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT && !gstate.isModeClear();

	bool textureAtOffset = gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0;
	ReplaceBlendType replaceBlend = ReplaceBlendWithShader();
	ReplaceAlphaType stencilToAlpha = ReplaceAlphaWithStencil(replaceBlend);

	const char *shading = "";
	if (glslES30)
		shading = doFlatShading ? "flat" : "";

	if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
		doTextureAlpha = false;

	if (doTexture)
		WRITE(p, "uniform sampler2D tex;\n");
	if (!gstate.isModeClear() && replaceBlend > REPLACE_BLEND_STANDARD) {
		if (!gl_extensions.ANY_shader_framebuffer_fetch && replaceBlend == REPLACE_BLEND_COPY_FBO) {
			if (!texelFetch) {
				WRITE(p, "uniform vec2 u_fbotexSize;\n");
			}
			WRITE(p, "uniform sampler2D fbotex;\n");
		}
		if (gstate.getBlendFuncA() == GE_SRCBLEND_FIXA) {
			WRITE(p, "uniform vec3 u_blendFixA;\n");
		}
		if (gstate.getBlendFuncB() == GE_DSTBLEND_FIXB) {
			WRITE(p, "uniform vec3 u_blendFixB;\n");
		}
	}
	if (gstate_c.needShaderTexClamp && doTexture) {
		WRITE(p, "uniform vec4 u_texclamp;\n");
		if (textureAtOffset) {
			WRITE(p, "uniform vec2 u_texclampoff;\n");
		}
	}

	if (enableAlphaTest || enableColorTest) {
		if (g_Config.bFragmentTestCache) {
			WRITE(p, "uniform sampler2D testtex;\n");
		} else {
			WRITE(p, "uniform vec4 u_alphacolorref;\n");
			if (bitwiseOps && ((enableColorTest && !colorTestAgainstZero) || (enableAlphaTest && !alphaTestAgainstZero))) {
				WRITE(p, "uniform ivec4 u_alphacolormask;\n");
			}
		}
	}
	if (stencilToAlpha && ReplaceAlphaWithStencilType() == STENCIL_VALUE_UNIFORM) {
		WRITE(p, "uniform float u_stencilReplaceValue;\n");
	}
	if (gstate.isTextureMapEnabled() && gstate.getTextureFunction() == GE_TEXFUNC_BLEND)
		WRITE(p, "uniform vec3 u_texenv;\n");

	WRITE(p, "%s %s vec4 v_color0;\n", shading, varying);
	if (lmode)
		WRITE(p, "%s %s vec3 v_color1;\n", shading, varying);
	if (enableFog) {
		WRITE(p, "uniform vec3 u_fogcolor;\n");
		WRITE(p, "%s %s float v_fogdepth;\n", varying, highpFog ? "highp" : "mediump");
	}
	if (doTexture) {
		if (doTextureProjection)
			WRITE(p, "%s %s vec3 v_texcoord;\n", varying, highpTexcoord ? "highp" : "mediump");
		else
			WRITE(p, "%s %s vec2 v_texcoord;\n", varying, highpTexcoord ? "highp" : "mediump");
	}

	if (!g_Config.bFragmentTestCache) {
		if (enableAlphaTest && !alphaTestAgainstZero) {
			if (bitwiseOps) {
				WRITE(p, "int roundAndScaleTo255i(in float x) { return int(floor(x * 255.0 + 0.5)); }\n");
			} else if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
				WRITE(p, "float roundTo255thf(in mediump float x) { mediump float y = x + (0.5/255.0); return y - fract(y * 255.0) * (1.0 / 255.0); }\n");
			} else {
				WRITE(p, "float roundAndScaleTo255f(in float x) { return floor(x * 255.0 + 0.5); }\n");
			}
		}
		if (enableColorTest && !colorTestAgainstZero) {
			if (bitwiseOps) {
				WRITE(p, "ivec3 roundAndScaleTo255iv(in vec3 x) { return ivec3(floor(x * 255.0 + 0.5)); }\n");
			} else if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
				WRITE(p, "vec3 roundTo255thv(in vec3 x) { vec3 y = x + (0.5/255.0); return y - fract(y * 255.0) * (1.0 / 255.0); }\n");
			} else {
				WRITE(p, "vec3 roundAndScaleTo255v(in vec3 x) { return floor(x * 255.0 + 0.5); }\n");
			}
		}
	}

	if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
		WRITE(p, "out vec4 fragColor0;\n");
		WRITE(p, "out vec4 fragColor1;\n");
	} else if (!strcmp(fragColor0, "fragColor0")) {
		WRITE(p, "out vec4 fragColor0;\n");
	}

	// PowerVR needs a custom modulo function. For some reason, this has far higher precision than the builtin one.
	if ((gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) && gstate_c.needShaderTexClamp) {
		WRITE(p, "float mymod(float a, float b) { return a - b * floor(a / b); }\n");
	}

	WRITE(p, "void main() {\n");

	if (gstate.isModeClear()) {
		// Clear mode does not allow any fancy shading.
		WRITE(p, "  vec4 v = v_color0;\n");
	} else {
		const char *secondary = "";
		// Secondary color for specular on top of texture
		if (lmode) {
			WRITE(p, "  vec4 s = vec4(v_color1, 0.0);\n");
			secondary = " + s";
		} else {
			secondary = "";
		}

		if (gstate.isTextureMapEnabled()) {
			const char *texcoord = "v_texcoord";
			// TODO: Not sure the right way to do this for projection.
			// This path destroys resolution on older PowerVR no matter what I do, so we disable it on SGX 540 and lesser, and live with the consequences.
			if (gstate_c.needShaderTexClamp && !(gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_TERRIBLE)) {
				// We may be clamping inside a larger surface (tex = 64x64, buffer=480x272).
				// We may also be wrapping in such a surface, or either one in a too-small surface.
				// Obviously, clamping to a smaller surface won't work.  But better to clamp to something.
				std::string ucoord = "v_texcoord.x";
				std::string vcoord = "v_texcoord.y";
				if (doTextureProjection) {
					ucoord += " / v_texcoord.z";
					vcoord = "(v_texcoord.y / v_texcoord.z)";
					// Vertex texcoords are NOT flipped when projecting despite gstate_c.flipTexture.
				} else if (gstate_c.flipTexture) {
					vcoord = "1.0 - " + vcoord;
				}

				std::string modulo = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) ? "mymod" : "mod";

				if (gstate.isTexCoordClampedS()) {
					ucoord = "clamp(" + ucoord + ", u_texclamp.z, u_texclamp.x - u_texclamp.z)";
				} else {
					ucoord = modulo + "(" + ucoord + ", u_texclamp.x)";
				}
				if (gstate.isTexCoordClampedT()) {
					vcoord = "clamp(" + vcoord + ", u_texclamp.w, u_texclamp.y - u_texclamp.w)";
				} else {
					vcoord = modulo + "(" + vcoord + ", u_texclamp.y)";
				}
				if (textureAtOffset) {
					ucoord = "(" + ucoord + " + u_texclampoff.x)";
					vcoord = "(" + vcoord + " + u_texclampoff.y)";
				}

				if (gstate_c.flipTexture) {
					vcoord = "1.0 - " + vcoord;
				}

				WRITE(p, "  vec2 fixedcoord = vec2(%s, %s);\n", ucoord.c_str(), vcoord.c_str());
				texcoord = "fixedcoord";
				// We already projected it.
				doTextureProjection = false;
			} else if (doTextureProjection && gstate_c.flipTexture) {
				// Since we need to flip v, we project manually.
				WRITE(p, "  vec2 fixedcoord = vec2(v_texcoord.x / v_texcoord.z, 1.0 - (v_texcoord.y / v_texcoord.z));\n");
				texcoord = "fixedcoord";
				doTextureProjection = false;
			}

			if (doTextureProjection) {
				WRITE(p, "  vec4 t = %sProj(tex, %s);\n", texture, texcoord);
			} else {
				WRITE(p, "  vec4 t = %s(tex, %s);\n", texture, texcoord);
			}
			WRITE(p, "  vec4 p = v_color0;\n");

			if (doTextureAlpha) { // texfmt == RGBA
				switch (gstate.getTextureFunction()) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  vec4 v = p * t%s;\n", secondary); 
					break;

				case GE_TEXFUNC_DECAL:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, t.rgb, t.a), p.a)%s;\n", secondary); 
					break;

				case GE_TEXFUNC_BLEND:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, u_texenv.rgb, t.rgb), p.a * t.a)%s;\n", secondary); 
					break;

				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  vec4 v = t%s;\n", secondary); 
					break;

				case GE_TEXFUNC_ADD:
				case GE_TEXFUNC_UNKNOWN1:
				case GE_TEXFUNC_UNKNOWN2:
				case GE_TEXFUNC_UNKNOWN3:
					WRITE(p, "  vec4 v = vec4(p.rgb + t.rgb, p.a * t.a)%s;\n", secondary); 
					break;
				default:
					WRITE(p, "  vec4 v = p;\n"); break;
				}
			} else { // texfmt == RGB
				switch (gstate.getTextureFunction()) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  vec4 v = vec4(t.rgb * p.rgb, p.a)%s;\n", secondary); 
					break;

				case GE_TEXFUNC_DECAL:
					WRITE(p, "  vec4 v = vec4(t.rgb, p.a)%s;\n", secondary); 
					break;

				case GE_TEXFUNC_BLEND:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, u_texenv.rgb, t.rgb), p.a)%s;\n", secondary); 
					break;

				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  vec4 v = vec4(t.rgb, p.a)%s;\n", secondary); 
					break;

				case GE_TEXFUNC_ADD:
				case GE_TEXFUNC_UNKNOWN1:
				case GE_TEXFUNC_UNKNOWN2:
				case GE_TEXFUNC_UNKNOWN3:
					WRITE(p, "  vec4 v = vec4(p.rgb + t.rgb, p.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  vec4 v = p;\n"); break;
				}
			}
		} else {
			// No texture mapping
			WRITE(p, "  vec4 v = v_color0 %s;\n", secondary);
		}

		// Texture access is at half texels [0.5/256, 255.5/256], but colors are normalized [0, 255].
		// So we have to scale to account for the difference.
		std::string alphaTestXCoord = "0";
		if (g_Config.bFragmentTestCache) {
			if (enableColorTest && !colorTestAgainstZero) {
				WRITE(p, "  vec4 vScale256 = v * %f + %f;\n", 255.0 / 256.0, 0.5 / 256.0);
				alphaTestXCoord = "vScale256.a";
			} else if (enableAlphaTest && !alphaTestAgainstZero) {
				char temp[64];
				snprintf(temp, sizeof(temp), "v.a * %f + %f", 255.0 / 256.0, 0.5 / 256.0);
				alphaTestXCoord = temp;
			}
		}

		if (enableAlphaTest) {
			if (alphaTestAgainstZero) {
				GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
				// When testing against 0 (extremely common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (alphaTestFunc == GE_COMP_NOTEQUAL || alphaTestFunc == GE_COMP_GREATER) {
					WRITE(p, "  if (v.a < 0.002) discard;\n");
				} else if (alphaTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.  Happens sometimes, actually...
					WRITE(p, "  if (v.a > 0.002) discard;\n");
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  discard;\n");
				}
			} else if (g_Config.bFragmentTestCache) {
				WRITE(p, "  float aResult = %s(testtex, vec2(%s, 0)).a;\n", texture, alphaTestXCoord.c_str());
				WRITE(p, "  if (aResult < 0.5) discard;\n");
			} else {
				GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
				const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };
				if (alphaTestFuncs[alphaTestFunc][0] != '#') {
					if (bitwiseOps) {
						WRITE(p, "  if ((roundAndScaleTo255i(v.a) & u_alphacolormask.a) %s int(u_alphacolorref.a)) discard;\n", alphaTestFuncs[alphaTestFunc]);
					} else if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
						// Work around bad PVR driver problem where equality check + discard just doesn't work.
						if (alphaTestFunc != GE_COMP_NOTEQUAL) {
							WRITE(p, "  if (roundTo255thf(v.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
						}
					} else {
						WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
					}
				} else {
					// This means NEVER.  See above.
					WRITE(p, "  discard;\n");
				}
			}
		}

		if (enableColorTest) {
			if (colorTestAgainstZero) {
				GEComparison colorTestFunc = gstate.getColorTestFunction();
				// When testing against 0 (common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (colorTestFunc == GE_COMP_NOTEQUAL) {
					WRITE(p, "  if (v.r < 0.002 && v.g < 0.002 && v.b < 0.002) discard;\n");
				} else if (colorTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.
					WRITE(p, "  if (v.r > 0.002 || v.g > 0.002 || v.b > 0.002) discard;\n");
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  discard;\n");
				}
			} else if (g_Config.bFragmentTestCache) {
				WRITE(p, "  float rResult = %s(testtex, vec2(vScale256.r, 0)).r;\n", texture);
				WRITE(p, "  float gResult = %s(testtex, vec2(vScale256.g, 0)).g;\n", texture);
				WRITE(p, "  float bResult = %s(testtex, vec2(vScale256.b, 0)).b;\n", texture);
				GEComparison colorTestFunc = gstate.getColorTestFunction();
				if (colorTestFunc == GE_COMP_EQUAL) {
					// Equal means all parts must be equal.
					WRITE(p, "  if (rResult < 0.5 || gResult < 0.5 || bResult < 0.5) discard;\n");
				} else {
					// Not equal means any part must be not equal.
					WRITE(p, "  if (rResult < 0.5 && gResult < 0.5 && bResult < 0.5) discard;\n");
				}
			} else {
				GEComparison colorTestFunc = gstate.getColorTestFunction();
				const char *colorTestFuncs[] = { "#", "#", " != ", " == " };
				if (colorTestFuncs[colorTestFunc][0] != '#') {
					if (bitwiseOps) {
						// Apparently GLES3 does not support vector bitwise ops.
						WRITE(p, "  ivec3 v_scaled = roundAndScaleTo255iv(v.rgb);\n");
						const char *maskedFragColor = "ivec3(v_scaled.r & u_alphacolormask.r, v_scaled.g & u_alphacolormask.g, v_scaled.b & u_alphacolormask.b)";
						const char *maskedColorRef = "ivec3(int(u_alphacolorref.r) & u_alphacolormask.r, int(u_alphacolorref.g) & u_alphacolormask.g, int(u_alphacolorref.b) & u_alphacolormask.b)";
						WRITE(p, "  if (%s %s %s) discard;\n", maskedFragColor, colorTestFuncs[colorTestFunc], maskedColorRef);
					} else if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
						WRITE(p, "  if (roundTo255thv(v.rgb) %s u_alphacolorref.rgb) discard;\n", colorTestFuncs[colorTestFunc]);
					} else {
						WRITE(p, "  if (roundAndScaleTo255v(v.rgb) %s u_alphacolorref.rgb) discard;\n", colorTestFuncs[colorTestFunc]);
					}
				} else {
					WRITE(p, "  discard;\n");
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
			WRITE(p, "  float fogCoef = clamp(v_fogdepth, 0.0, 1.0);\n");
			WRITE(p, "  v = mix(vec4(u_fogcolor, v.a), v, fogCoef);\n");
			// WRITE(p, "  v.x = v_depth;\n");
		}

		if (replaceBlend == REPLACE_BLEND_PRE_SRC || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			GEBlendSrcFactor funcA = gstate.getBlendFuncA();
			const char *srcFactor = "ERROR";
			switch (funcA) {
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "vec3(v.a)"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "vec3(1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "vec3(v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "vec3(1.0 - v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "ERROR"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "ERROR"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			}

			WRITE(p, "  v.rgb = v.rgb * %s;\n", srcFactor);
		}

		if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
			// If we have NV_shader_framebuffer_fetch / EXT_shader_framebuffer_fetch, we skip the blit.
			// We can just read the prev value more directly.
			if (gl_extensions.ANY_shader_framebuffer_fetch) {
				WRITE(p, "  lowp vec4 destColor = %s;\n", lastFragData);
			} else if (!texelFetch) {
				WRITE(p, "  lowp vec4 destColor = %s(fbotex, gl_FragCoord.xy * u_fbotexSize.xy);\n", texture);
			} else {
				WRITE(p, "  lowp vec4 destColor = %s(fbotex, ivec2(gl_FragCoord.x, gl_FragCoord.y), 0);\n", texelFetch);
			}

			GEBlendSrcFactor funcA = gstate.getBlendFuncA();
			GEBlendDstFactor funcB = gstate.getBlendFuncB();
			GEBlendMode eq = gstate.getBlendEq();

			const char *srcFactor = "vec3(1.0)";
			const char *dstFactor = "vec3(0.0)";

			switch (funcA)
			{
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "destColor.rgb"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "(vec3(1.0) - destColor.rgb)"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "vec3(v.a)"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "vec3(1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "vec3(destColor.a)"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "vec3(1.0 - destColor.a)"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "vec3(v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "vec3(1.0 - v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "vec3(destColor.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "vec3(1.0 - destColor.a * 2.0)"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			}
			switch (funcB)
			{
			case GE_DSTBLEND_SRCCOLOR:          dstFactor = "v.rgb"; break;
			case GE_DSTBLEND_INVSRCCOLOR:       dstFactor = "(vec3(1.0) - v.rgb)"; break;
			case GE_DSTBLEND_SRCALPHA:          dstFactor = "vec3(v.a)"; break;
			case GE_DSTBLEND_INVSRCALPHA:       dstFactor = "vec3(1.0 - v.a)"; break;
			case GE_DSTBLEND_DSTALPHA:          dstFactor = "vec3(destColor.a)"; break;
			case GE_DSTBLEND_INVDSTALPHA:       dstFactor = "vec3(1.0 - destColor.a)"; break;
			case GE_DSTBLEND_DOUBLESRCALPHA:    dstFactor = "vec3(v.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEINVSRCALPHA: dstFactor = "vec3(1.0 - v.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEDSTALPHA:    dstFactor = "vec3(destColor.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEINVDSTALPHA: dstFactor = "vec3(1.0 - destColor.a * 2.0)"; break;
			case GE_DSTBLEND_FIXB:              dstFactor = "u_blendFixB"; break;
			}

			switch (eq)
			{
			case GE_BLENDMODE_MUL_AND_ADD:
				WRITE(p, "  v.rgb = v.rgb * %s + destColor.rgb * %s;\n", srcFactor, dstFactor);
				break;
			case GE_BLENDMODE_MUL_AND_SUBTRACT:
				WRITE(p, "  v.rgb = v.rgb * %s - destColor.rgb * %s;\n", srcFactor, dstFactor);
				break;
			case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE:
				WRITE(p, "  v.rgb = destColor.rgb * %s - v.rgb * %s;\n", dstFactor, srcFactor);
				break;
			case GE_BLENDMODE_MIN:
				WRITE(p, "  v.rgb = min(v.rgb, destColor.rgb);\n");
				break;
			case GE_BLENDMODE_MAX:
				WRITE(p, "  v.rgb = max(v.rgb, destColor.rgb);\n");
				break;
			case GE_BLENDMODE_ABSDIFF:
				WRITE(p, "  v.rgb = abs(v.rgb - destColor.rgb);\n");
				break;
			}
		}

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
		WRITE(p, "  fragColor0 = vec4(v.rgb, %s);\n", replacedAlpha.c_str());
		WRITE(p, "  fragColor1 = vec4(0.0, 0.0, 0.0, v.a);\n");
		break;

	case REPLACE_ALPHA_YES:
		WRITE(p, "  %s = vec4(v.rgb, %s);\n", fragColor0, replacedAlpha.c_str());
		break;

	case REPLACE_ALPHA_NO:
		WRITE(p, "  %s = v;\n", fragColor0);
		break;
	}

	switch (ReplaceLogicOpType()) {
	case LOGICOPTYPE_ONE:
		WRITE(p, "  %s.rgb = vec3(1.0, 1.0, 1.0);\n", fragColor0);
		break;
	case LOGICOPTYPE_INVERT:
		WRITE(p, "  %s.rgb = vec3(1.0, 1.0, 1.0) - %s.rgb;\n", fragColor0, fragColor0);
		break;
	case LOGICOPTYPE_NORMAL:
		break;
	}

#ifdef DEBUG_SHADER
	if (doTexture) {
		WRITE(p, "  %s = texture2D(tex, v_texcoord.xy);\n", fragColor0);
		WRITE(p, "  %s += vec4(0.3,0,0.3,0.3);\n", fragColor0);
	} else {
		WRITE(p, "  %s = vec4(1,0,1,1);\n", fragColor0);
	}
#endif
	WRITE(p, "}\n");
}

