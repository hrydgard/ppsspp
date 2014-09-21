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
void ComputeFragmentShaderIDDX9(FragmentShaderIDDX9 *id) {
	int id0 = 0;
	int id1 = 0;
	if (gstate.isModeClear()) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id0 = 1;
	} else {
		bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();
		bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough();
		bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !g_Config.bDisableAlphaTest;
		bool alphaTestAgainstZero = IsAlphaTestAgainstZero();
		bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue();
		bool enableColorDoubling = gstate.isColorDoublingEnabled();
		// This isn't really correct, but it's a hack to get doubled blend modes to work more correctly.
		bool enableAlphaDoubling = CanDoubleSrcBlendMode();
		bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
		bool doTextureAlpha = gstate.isTextureAlphaUsed();

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
			// 4 bits total.
			id0 |= 1 << 12;
			id0 |= gstate.getAlphaTestFunction() << 13;
		}
#endif
		if (enableColorTest) {
			// 3 bits total.
			id0 |= 1 << 16;
			id0 |= gstate.getColorTestFunction() << 17;
		}
		id0 |= (enableFog & 1) << 19;
		id0 |= (doTextureProjection & 1) << 20;
		id0 |= (enableColorDoubling & 1) << 21;
		id0 |= (enableAlphaDoubling & 1) << 22;
		id0 |= (gstate_c.bgraTexture & 1) << 23;

		id0 |= (alphaTestAgainstZero & 1) << 28;
		if (enableAlphaTest)
			gpuStats.numAlphaTestedDraws++;
		else
			gpuStats.numNonAlphaTestedDraws++;
	}

	id->d[0] = id0;
	id->d[1] = id1;
}

// Missing: Z depth range
// Also, logic ops etc, of course. Urgh.
void GenerateFragmentShaderDX9(char *buffer) {
	char *p = buffer;

	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !gstate.isModeClear() && !g_Config.bDisableAlphaTest;
	bool alphaTestAgainstZero = IsAlphaTestAgainstZero();
	bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && !gstate.isModeClear();
	bool enableColorDoubling = gstate.isColorDoublingEnabled();
	// This isn't really correct, but it's a hack to get doubled blend modes to work more correctly.
	bool enableAlphaDoubling = CanDoubleSrcBlendMode();
	bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
	bool doTextureAlpha = gstate.isTextureAlphaUsed();

	if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
		doTextureAlpha = false;

	if (doTexture)
		WRITE(p, "sampler tex: register(s0);\n");

	if (enableAlphaTest || enableColorTest) {
		WRITE(p, "float4 u_alphacolorref : register(c%i);\n", CONST_PS_ALPHACOLORREF);
		WRITE(p, "float4 u_alphacolormask : register(c%i);\n", CONST_PS_ALPHACOLORMASK);
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
		WRITE(p, "  return In.v_color0;\n");
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
			if (doTextureProjection && gstate_c.flipTexture) {
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
			u32 colorTestMask = gstate.getColorTestMask();
			if (colorTestFuncs[colorTestFunc][0] != '#') {
				const char * test = colorTestFuncs[colorTestFunc];
				WRITE(p, "float3 colortest = roundAndScaleTo255v(v.rgb);\n");
				WRITE(p, "if ((colortest.r %s u_alphacolorref.r) && (colortest.g %s u_alphacolorref.g) && (colortest.b %s u_alphacolorref.b ))  clip(-1);\n", test, test, test);
			}
		}

		if (enableFog) {
			WRITE(p, "  float fogCoef = clamp(In.v_fogdepth.x, 0.0, 1.0);\n");
			WRITE(p, "  return lerp(float4(u_fogcolor, v.a), v, fogCoef);\n");
		} else {
			WRITE(p, "  return v;\n");
		}
	}
	WRITE(p, "}\n");
}

};
