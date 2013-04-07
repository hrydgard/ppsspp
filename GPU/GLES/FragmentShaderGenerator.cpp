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


// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderID(FragmentShaderID *id) {
	memset(&id->d[0], 0, sizeof(id->d));
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue();
	bool enableColorTest = gstate.isColorTestEnabled();
	int lmode = (gstate.lmode & 1) && gstate.isLightingEnabled();

	if (gstate.clearmode & 1) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id->d[0] = 1;
	} else {
		// id->d[0] |= (gstate.clearmode & 1);
		if (gstate.isTextureMapEnabled()) {
			id->d[0] |= 1 << 1;
			id->d[0] |= (gstate.texfunc & 0x7) << 2;
			id->d[0] |= ((gstate.texfunc & 0x100) >> 8) << 5; // rgb or rgba
			id->d[0] |= ((gstate.texfunc & 0x10000) >> 16) << 6;	// color double
		}
		id->d[0] |= (lmode & 1) << 7;
		id->d[0] |= gstate.isAlphaTestEnabled() << 8;
		if (enableAlphaTest)
			id->d[0] |= (gstate.alphatest & 0x7) << 9;	 // alpha test func
		id->d[0] |= gstate.isColorTestEnabled() << 12;
		if (enableColorTest)
			id->d[0] |= (gstate.colortest & 0x3) << 13;	 // color test func
		id->d[0] |= (enableFog & 1) << 15;
	}
}

// Missing: Alpha test, color test, Z depth range, fog
// Also, logic ops etc, of course. Urgh.
// We could do all this with booleans, but I don't trust the shader compilers on
// Android devices to be anything but stupid.
void GenerateFragmentShader(char *buffer) {
	char *p = buffer;


#if defined(GLSL_ES_1_0)
	WRITE(p, "precision lowp float;\n");
#elif !defined(FORCE_OPENGL_2_0)
	WRITE(p, "#version 110\n");
#endif

	int lmode = (gstate.lmode & 1) && gstate.isLightingEnabled();
	int doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();

	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool enableAlphaTest = gstate.isAlphaTestEnabled() && !gstate.isModeClear() && !IsAlphaTestTriviallyTrue();
	bool enableColorTest = gstate.isColorTestEnabled() && !gstate.isModeClear();
	bool enableColorDoubling = (gstate.texfunc & 0x10000) != 0;


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
		WRITE(p, "varying vec2 v_texcoord;\n");

	WRITE(p, "void main() {\n");

	if (gstate.clearmode & 1) {
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
			WRITE(p, "  vec4 t = texture2D(tex, v_texcoord);\n");
			WRITE(p, "  vec4 p = v_color0;\n");

			if (gstate.texfunc & 0x100) { // texfmt == RGBA
				switch (gstate.texfunc & 0x7) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  vec4 v = t * p%s;\n", secondary); break;
				case GE_TEXFUNC_DECAL:
					WRITE(p, "  vec4 v = vec4((1.0 - t.a) * p.rgb + t.rgb * t.a, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_BLEND:
					WRITE(p, "  vec4 v = vec4((1.0 - t.rgb) * p.rgb + t.rgb * u_texenv.rgb, p.a * t.a)%s;\n", secondary); break;
				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  vec4 v = t%s;\n", secondary); break;
				case GE_TEXFUNC_ADD:
					WRITE(p, "  vec4 v = vec4(t.rgb + p.rgb, p.a * t.a)%s;\n", secondary); break;
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
					WRITE(p, "  vec4 v = vec4((1.0 - t.rgb) * p.rgb + t.rgb * u_texenv.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  vec4 v = vec4(t.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_ADD:
					WRITE(p, "  vec4 v = vec4(t.rgb + p.rgb, p.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  vec4 v = p;\n"); break;
				}
			}
		} else {
			// No texture mapping
			WRITE(p, "  vec4 v = v_color0 %s;\n", secondary);
		}

		if (enableColorDoubling) {
			WRITE(p, "  v = v * 2.0;\n");
		}

		if (enableAlphaTest) {
			int alphaTestFunc = gstate.alphatest & 7;
			const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };	// never/always don't make sense
			if (alphaTestFuncs[alphaTestFunc][0] != '#')
				WRITE(p, "  if (v.a %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
		}
		
		if (enableColorTest) {
			// TODO: There are some colortestmasks we could handle.
			int colorTestFunc = gstate.colortest & 3;
			const char *colorTestFuncs[] = { "#", "#", " != ", " == " };	// never/always don't make sense
			int colorTestMask = gstate.colormask;
			if (colorTestFuncs[colorTestFunc][0] != '#')
				WRITE(p, "if (v.rgb %s u_alphacolorref.rgb) discard;\n", colorTestFuncs[colorTestFunc]);
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
		WRITE(p, "  gl_FragColor = texture2D(tex, v_texcoord);\n");
	} else {
		WRITE(p, "  gl_FragColor = vec4(1,0,1,1);\n");
	}
#endif
	WRITE(p, "}\n");
}

