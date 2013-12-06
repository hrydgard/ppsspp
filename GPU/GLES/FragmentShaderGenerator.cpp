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

#if defined(USING_GLES2)
#define GLSL_ES_1_0
#else
#define GLSL_1_3

// SDL 1.2 on Apple does not have support for OpenGL 3 and hence needs
// special treatment in the shader generator.
#if defined(__APPLE__)
#define FORCE_OPENGL_2_0
#endif
#endif

#include <cstdio>

#include "gfx_es2/gpu_features.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "GPU/GLES/FragmentShaderGenerator.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#define WRITE p+=sprintf

// #define DEBUG_SHADER

// GL_NV_shader_framebuffer_fetch looks interesting....


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

static bool IsAlphaTestTriviallyTrue() {
	GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
	int alphaTestRef = gstate.getAlphaTestRef();
	int alphaTestMask = gstate.getAlphaTestMask();

	switch (alphaTestFunc) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_GEQUAL:
		return alphaTestRef == 0;

	// Non-zero check. If we have no depth testing (and thus no depth writing), and an alpha func that will result in no change if zero alpha, get rid of the alpha test.
	// Speeds up Lumines by a LOT on PowerVR.
	case GE_COMP_NOTEQUAL:
	case GE_COMP_GREATER:
		{
			bool depthTest = gstate.isDepthTestEnabled();
			bool stencilTest = gstate.isStencilTestEnabled();
			GEBlendSrcFactor src = gstate.getBlendFuncA();
			GEBlendDstFactor dst = gstate.getBlendFuncB();
			if (!stencilTest && !depthTest && alphaTestRef == 0 && gstate.isAlphaBlendEnabled() && src == GE_SRCBLEND_SRCALPHA && safeDestFactors[(int)dst])
				return true;
			return false;
		}

	case GE_COMP_LEQUAL:
		return alphaTestRef == 255;

	case GE_COMP_EQUAL:
	case GE_COMP_LESS:
		return false;
	default:
		return false;
	}
}

enum StencilValueType {
	STENCIL_VALUE_UNKNOWN,
	STENCIL_VALUE_UNIFORM,
	STENCIL_VALUE_ZERO,
	STENCIL_VALUE_ONE,
};

static StencilValueType ReplaceAlphaWithStencilType() {
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

		case GE_STENCILOP_KEEP:
		case GE_STENCILOP_INVERT:
			return STENCIL_VALUE_UNKNOWN;
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

		// Decrementing always zeros, since there's only one bit.
		case GE_STENCILOP_DECR:
		case GE_STENCILOP_INCR:
		case GE_STENCILOP_KEEP:
		case GE_STENCILOP_INVERT:
			return STENCIL_VALUE_UNKNOWN;
		}
		break;
	}

	return STENCIL_VALUE_UNKNOWN;
}

static bool IsColorTestTriviallyTrue() {
	GEComparison colorTestFunc = gstate.getColorTestFunction();
	switch (colorTestFunc) {
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


// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderID(FragmentShaderID *id) {
	memset(&id->d[0], 0, sizeof(id->d));
	if (gstate.isModeClear()) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id->d[0] = 1;
	} else {
		bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();
		bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough();
		bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !g_Config.bDisableAlphaTest;
		bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue();
		bool enableColorDoubling = gstate.isColorDoublingEnabled();
		// This isn't really correct, but it's a hack to get doubled blend modes to work more correctly.
		bool enableAlphaDoubling = CanDoubleSrcBlendMode();
		bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		bool stencilToAlpha = gstate.isStencilTestEnabled() && !gstate.isAlphaBlendEnabled();

		// All texfuncs except replace are the same for RGB as for RGBA with full alpha.
		if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
			doTextureAlpha = false;

		// id->d[0] |= (gstate.isModeClear() & 1);
		if (gstate.isTextureMapEnabled()) {
			id->d[0] |= 1 << 1;
			id->d[0] |= gstate.getTextureFunction() << 2;
			id->d[0] |= (doTextureAlpha & 1) << 5; // rgb or rgba
		}

		id->d[0] |= (lmode & 1) << 7;
		id->d[0] |= enableAlphaTest << 8;
		if (enableAlphaTest)
			id->d[0] |= gstate.getAlphaTestFunction() << 9;
		id->d[0] |= enableColorTest << 12;
		if (enableColorTest)
			id->d[0] |= gstate.getColorTestFunction() << 13;	 // color test func
		id->d[0] |= (enableFog & 1) << 15;
		id->d[0] |= (doTextureProjection & 1) << 16;
		id->d[0] |= (enableColorDoubling & 1) << 17;
		id->d[0] |= (enableAlphaDoubling & 1) << 18;
		if (stencilToAlpha) {
			// 2 bits
			id->d[0] |= ReplaceAlphaWithStencilType() << 19;
		}
		if (enableAlphaTest)
			gpuStats.numAlphaTestedDraws++;
		else
			gpuStats.numNonAlphaTestedDraws++;
	}
}

// Missing: Z depth range
void GenerateFragmentShader(char *buffer) {
	char *p = buffer;

	bool highpFog = false;
#if defined(GLSL_ES_1_0)
	WRITE(p, "#version 100\n");  // GLSL ES 1.0
	WRITE(p, "precision lowp float;\n");

	// PowerVR needs highp to do the fog in MHU correctly.
	// Others don't, and some can't handle highp in the fragment shader.
	highpFog = gl_extensions.gpuVendor == GPU_VENDOR_POWERVR;
#elif !defined(FORCE_OPENGL_2_0)
	WRITE(p, "#version 110\n");
	// Remove lowp/mediump in non-mobile implementations
	WRITE(p, "#define lowp\n");
	WRITE(p, "#define mediump\n");
	WRITE(p, "#define highp\n");
#else
	// Remove lowp/mediump in non-mobile implementations
	WRITE(p, "#define lowp\n");
	WRITE(p, "#define mediump\n");
	WRITE(p, "#define highp\n");
#endif

	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !gstate.isModeClear() && !g_Config.bDisableAlphaTest;
	bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && !gstate.isModeClear();
	bool enableColorDoubling = gstate.isColorDoublingEnabled();
	// This isn't really correct, but it's a hack to get doubled blend modes to work more correctly.
	bool enableAlphaDoubling = CanDoubleSrcBlendMode();
	bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
	bool doTextureAlpha = gstate.isTextureAlphaUsed();
	bool stencilToAlpha = !gstate.isModeClear() && gstate.isStencilTestEnabled() && !gstate.isAlphaBlendEnabled();

	if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
		doTextureAlpha = false;

	if (doTexture)
		WRITE(p, "uniform sampler2D tex;\n");

	if (enableAlphaTest || enableColorTest) {
		WRITE(p, "uniform vec4 u_alphacolorref;\n");
	}
	if (stencilToAlpha && ReplaceAlphaWithStencilType() == STENCIL_VALUE_UNIFORM) {
		WRITE(p, "uniform float u_stencilReplaceValue;\n");
	}
	if (gstate.isTextureMapEnabled() && gstate.getTextureFunction() == GE_TEXFUNC_BLEND) 
		WRITE(p, "uniform lowp vec3 u_texenv;\n");

	WRITE(p, "varying lowp vec4 v_color0;\n");
	if (lmode)
		WRITE(p, "varying lowp vec3 v_color1;\n");
	if (enableFog) {
		WRITE(p, "uniform lowp vec3 u_fogcolor;\n");
		WRITE(p, "varying %s float v_fogdepth;\n", highpFog ? "highp" : "mediump");
	}
	if (doTexture)
	{
		if (doTextureProjection)
			WRITE(p, "varying mediump vec3 v_texcoord;\n");
		else
			WRITE(p, "varying mediump vec2 v_texcoord;\n");
	}

	if (enableAlphaTest) {
		if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) 
			WRITE(p, "float roundTo255thf(in mediump float x) { mediump float y = x + (0.5/255.0); return y - fract(y * 255.0) * (1.0 / 255.0); }\n");
		else
			WRITE(p, "float roundAndScaleTo255f(in float x) { return floor(x * 255.0 + 0.5); }\n"); 
	}
	if (enableColorTest) {
		if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) 
			WRITE(p, "vec3 roundTo255thv(in vec3 x) { vec3 y = x + (0.5/255.0); return y - fract(y * 255.0) * (1.0 / 255.0); }\n");
		else
			WRITE(p, "vec3 roundAndScaleTo255v(in vec3 x) { return floor(x * 255.0 + 0.5); }\n"); 
	}

	WRITE(p, "void main() {\n");

	if (gstate.isModeClear()) {
		// Clear mode does not allow any fancy shading.
		WRITE(p, "  gl_FragColor = v_color0;\n");
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
			if (doTextureProjection) {
				WRITE(p, "  vec4 t = texture2DProj(tex, v_texcoord);\n");
			} else {
				WRITE(p, "  vec4 t = texture2D(tex, v_texcoord);\n");
			}
			WRITE(p, "  vec4 p = v_color0;\n");

			if (doTextureAlpha) { // texfmt == RGBA
				switch (gstate.getTextureFunction()) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  vec4 v = p * t%s;\n", secondary); break;
				case GE_TEXFUNC_DECAL:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, t.rgb, t.a), p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_BLEND:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, u_texenv.rgb, t.rgb), p.a * t.a)%s;\n", secondary); break;
				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  vec4 v = t%s;\n", secondary); break;
				case GE_TEXFUNC_ADD:
					WRITE(p, "  vec4 v = vec4(p.rgb + t.rgb, p.a * t.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  vec4 v = p;\n"); break;
				}

			} else { // texfmt == RGB
				switch (gstate.getTextureFunction()) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  vec4 v = vec4(t.rgb * p.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_DECAL:
					WRITE(p, "  vec4 v = vec4(t.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_BLEND:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, u_texenv.rgb, t.rgb), p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  vec4 v = vec4(t.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_ADD:
					WRITE(p, "  vec4 v = vec4(p.rgb + t.rgb, p.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  vec4 v = p;\n"); break;
				}
			}
		} else {
			// No texture mapping
			WRITE(p, "  vec4 v = v_color0 %s;\n", secondary);
		}

		if (enableAlphaTest) {
			GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
			const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };	// never/always don't make sense
			if (alphaTestFuncs[alphaTestFunc][0] != '#') {
				if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
					// Work around bad PVR driver problem where equality check + discard just doesn't work.
					if (alphaTestFunc != 3)
						WRITE(p, "  if (roundTo255thf(v.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
				} else {
					WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
				}
			}
		}

		// TODO: Before or after the color test?
		if (enableColorDoubling && enableAlphaDoubling) {
			WRITE(p, "  v = v * 2.0;\n");
		} else if (enableColorDoubling) {
			WRITE(p, "  v.rgb = v.rgb * 2.0;\n");
		} else if (enableAlphaDoubling) {
			WRITE(p, "  v.a = v.a * 2.0;\n");
		}
		
		if (enableColorTest) {
			GEComparison colorTestFunc = gstate.getColorTestFunction();
			const char *colorTestFuncs[] = { "#", "#", " != ", " == " };	// never/always don't make sense
			WARN_LOG_REPORT_ONCE(colortest, G3D, "Color test function : %s", colorTestFuncs[colorTestFunc]); 
			u32 colorTestMask = gstate.getColorTestMask();
			if (colorTestFuncs[colorTestFunc][0] != '#') {
				if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) 
					WRITE(p, "  if (roundTo255thv(v.rgb) %s u_alphacolorref.rgb) discard;\n", colorTestFuncs[colorTestFunc]);
				else
					WRITE(p, "  if (roundAndScaleTo255v(v.rgb) %s u_alphacolorref.rgb) discard;\n", colorTestFuncs[colorTestFunc]);
			}
		}

		if (enableFog) {
			WRITE(p, "  float fogCoef = clamp(v_fogdepth, 0.0, 1.0);\n");
			WRITE(p, "  gl_FragColor = mix(vec4(u_fogcolor, v.a), v, fogCoef);\n");
			// WRITE(p, "  v.x = v_depth;\n");
		} else {
			WRITE(p, "  gl_FragColor = v;\n");
		}
	}

	if (stencilToAlpha) {
		switch (ReplaceAlphaWithStencilType()) {
		case STENCIL_VALUE_UNIFORM:
			WRITE(p, "  gl_FragColor.a = u_stencilReplaceValue;\n");
			break;

		case STENCIL_VALUE_ZERO:
			WRITE(p, "  gl_FragColor.a = 0.0;\n");
			break;

		case STENCIL_VALUE_ONE:
			WRITE(p, "  gl_FragColor.a = 1.0;\n");
			break;

		case STENCIL_VALUE_UNKNOWN:
			// Maybe we should even mask away alpha using glColorMask and not change it at all? We do get here
			// if the stencil mode is KEEP for example.
			WRITE(p, "  gl_FragColor.a = 0.0;\n");
			break;
		}
	}
#ifdef DEBUG_SHADER
	if (doTexture) {
		WRITE(p, "  gl_FragColor = texture2D(tex, v_texcoord.xy);\n");
		WRITE(p, "  gl_FragColor += vec4(0.3,0,0.3,0.3);\n");
	} else {
		WRITE(p, "  gl_FragColor = vec4(1,0,1,1);\n");
	}
#endif
	WRITE(p, "}\n");
}

