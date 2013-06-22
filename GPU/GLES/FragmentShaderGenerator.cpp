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

#include "FragmentShaderGenerator.h"
#include "../ge_constants.h"
#include "../GPUState.h"
#include <cstdio>

#define WRITE p+=sprintf

// #define DEBUG_SHADER

// GL_NV_shader_framebuffer_fetch looks interesting....

static bool IsAlphaTestTriviallyTrue() {
	int alphaTestFunc = gstate.alphatest & 7;
	int alphaTestRef = (gstate.alphatest >> 8) & 0xFF;
	
	switch (alphaTestFunc) {
	case GE_COMP_ALWAYS:
		return true;
	case GE_COMP_GEQUAL:
		if (alphaTestRef == 0)
			return true;

	// This breaks the trees in MotoGP, for example.
	// case GE_COMP_GREATER:
	//if (alphaTestRef == 0 && (gstate.alphaBlendEnable & 1) && gstate.getBlendFuncA() == GE_SRCBLEND_SRCALPHA && gstate.getBlendFuncB() == GE_SRCBLEND_INVSRCALPHA)
	//	return true;

	case GE_COMP_LEQUAL:
		if (alphaTestRef == 255)
			return true;
	default:
		return false;
	}
}

static bool IsColorTestTriviallyTrue() {
	int colorTestFunc = gstate.colortest & 3;
	switch (colorTestFunc) {
	case GE_COMP_ALWAYS:
		return true;
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
	if (gstate.clearmode & 1) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id->d[0] = 1;
	} else {
		int lmode = (gstate.lmode & 1) && gstate.isLightingEnabled();
		bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough();
		bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue();
		bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue();
		bool enableColorDoubling = (gstate.texfunc & 0x10000) != 0;
		// This isn't really correct, but it's a hack to get doubled blend modes to work more correctly.
		bool enableAlphaDoubling = CanDoubleSrcBlendMode();
		bool doTextureProjection = gstate.getUVGenMode() == 1;
		bool doTextureAlpha = (gstate.texfunc & 0x100) != 0;

		// All texfuncs except replace are the same for RGB as for RGBA with full alpha.
		if (gstate_c.textureFullAlpha && (gstate.texfunc & 0x7) != GE_TEXFUNC_REPLACE)
			doTextureAlpha = false;

		// id->d[0] |= (gstate.clearmode & 1);
		if (gstate.isTextureMapEnabled()) {
			id->d[0] |= 1 << 1;
			id->d[0] |= (gstate.texfunc & 0x7) << 2;
			id->d[0] |= (doTextureAlpha & 1) << 5; // rgb or rgba
		}
		id->d[0] |= (lmode & 1) << 7;
		id->d[0] |= gstate.isAlphaTestEnabled() << 8;
		if (enableAlphaTest)
			id->d[0] |= (gstate.alphatest & 0x7) << 9;	 // alpha test func
		id->d[0] |= gstate.isColorTestEnabled() << 12;
		if (enableColorTest)
			id->d[0] |= (gstate.colortest & 0x3) << 13;	 // color test func
		id->d[0] |= (enableFog & 1) << 15;
		id->d[0] |= (doTextureProjection & 1) << 16;
		id->d[0] |= (enableColorDoubling & 1) << 17;
		id->d[0] |= (enableAlphaDoubling & 1) << 18;
	}
}

// Missing: Z depth range
// Also, logic ops etc, of course. Urgh.
void GenerateFragmentShader(char *buffer) {
	char *p = buffer;

#if defined(GLSL_ES_1_0)
	WRITE(p, "#version 100\n");  // GLSL ES 1.0
	WRITE(p, "precision lowp float;\n");
#elif !defined(FORCE_OPENGL_2_0)
	WRITE(p, "#version 110\n");
#endif

	int lmode = (gstate.lmode & 1) && gstate.isLightingEnabled();
	int doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !gstate.isModeClear();
	bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && !gstate.isModeClear();
	bool enableColorDoubling = (gstate.texfunc & 0x10000) != 0;
	// This isn't really correct, but it's a hack to get doubled blend modes to work more correctly.
	bool enableAlphaDoubling = CanDoubleSrcBlendMode();
	bool doTextureProjection = gstate.getUVGenMode() == 1;
	bool doTextureAlpha = (gstate.texfunc & 0x100) != 0;

	if (gstate_c.textureFullAlpha && (gstate.texfunc & 0x7) != GE_TEXFUNC_REPLACE)
		doTextureAlpha = false;

	if (doTexture)
		WRITE(p, "uniform sampler2D tex;\n");

	if (enableAlphaTest || enableColorTest) {
		WRITE(p, "uniform vec4 u_alphacolorref;\n");
		WRITE(p, "uniform vec4 u_colormask;\n");
	}
	if (gstate.isTextureMapEnabled()) 
		WRITE(p, "uniform vec3 u_texenv;\n");
	
	WRITE(p, "varying vec4 v_color0;\n");
	if (lmode)
		WRITE(p, "varying vec3 v_color1;\n");
	if (enableFog) {
		WRITE(p, "uniform vec3 u_fogcolor;\n");
#if defined(GLSL_ES_1_0)
		WRITE(p, "varying mediump float v_fogdepth;\n");
#else
		WRITE(p, "varying float v_fogdepth;\n");
#endif
	}
	if (doTexture)
	{
		if (doTextureProjection)
			WRITE(p, "varying vec3 v_texcoord;\n");
		else
			WRITE(p, "varying vec2 v_texcoord;\n");
	}

	if (enableAlphaTest) {
		WRITE(p, "float roundAndScaleTo255f(in float x) { return floor(x * 255.0 + 0.5); }\n");
	}
	if (enableColorTest) {
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

		if (gstate.textureMapEnable & 1) {
			if (doTextureProjection) {
				WRITE(p, "  vec4 t = texture2DProj(tex, v_texcoord);\n");
			} else {
				WRITE(p, "  vec4 t = texture2D(tex, v_texcoord);\n");
			}
			WRITE(p, "  vec4 p = v_color0;\n");

			if (doTextureAlpha) { // texfmt == RGBA
				switch (gstate.texfunc & 0x7) {
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

			} else {	// texfmt == RGB
				switch (gstate.texfunc & 0x7) {
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
			int alphaTestFunc = gstate.alphatest & 7;
			const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };	// never/always don't make sense
			if (alphaTestFuncs[alphaTestFunc][0] != '#') {
				WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
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
			int colorTestFunc = gstate.colortest & 3;
			const char *colorTestFuncs[] = { "#", "#", " != ", " == " };	// never/always don't make sense
			int colorTestMask = gstate.colormask;
			if (colorTestFuncs[colorTestFunc][0] != '#') {
				WRITE(p, "if (roundAndScaleTo255v(v.rgb) %s u_alphacolorref.rgb) discard;\n", colorTestFuncs[colorTestFunc]);
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

