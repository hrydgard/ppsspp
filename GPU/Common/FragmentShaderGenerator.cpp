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
#include <sstream>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/GPU/thin3d.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#define WRITE(p, ...) p.F(__VA_ARGS__)

bool GenerateFragmentShader(const FShaderID &id, char *buffer, const ShaderLanguageDesc &compat, Draw::Bugs bugs, uint64_t *uniformMask, std::string *errorString) {
	*uniformMask = 0;
	errorString->clear();

	bool highpFog = false;
	bool highpTexcoord = false;
	bool enableFragmentTestCache = g_Config.bFragmentTestCache && ShaderLanguageIsOpenGL(compat.shaderLanguage);

	if (compat.gles) {
		// PowerVR needs highp to do the fog in MHU correctly.
		// Others don't, and some can't handle highp in the fragment shader.
		highpFog = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) ? true : false;
		highpTexcoord = highpFog;
	}

	ReplaceAlphaType stencilToAlpha = static_cast<ReplaceAlphaType>(id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2));

	std::vector<const char*> gl_exts;
	if (ShaderLanguageIsOpenGL(compat.shaderLanguage)) {
		if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE && gl_extensions.EXT_blend_func_extended) {
			gl_exts.push_back("#extension GL_EXT_blend_func_extended : require");
		}
		if (gl_extensions.EXT_gpu_shader4) {
			gl_exts.push_back("#extension GL_EXT_gpu_shader4 : enable");
		}
		if (compat.framebufferFetchExtension) {
			gl_exts.push_back(compat.framebufferFetchExtension);
		}
	}

	ShaderWriter p(buffer, compat, ShaderStage::Fragment, gl_exts.data(), gl_exts.size());

	bool lmode = id.Bit(FS_BIT_LMODE);
	bool doTexture = id.Bit(FS_BIT_DO_TEXTURE);
	bool enableFog = id.Bit(FS_BIT_ENABLE_FOG);
	bool enableAlphaTest = id.Bit(FS_BIT_ALPHA_TEST);

	bool alphaTestAgainstZero = id.Bit(FS_BIT_ALPHA_AGAINST_ZERO);
	bool testForceToZero = id.Bit(FS_BIT_TEST_DISCARD_TO_ZERO);
	bool enableColorTest = id.Bit(FS_BIT_COLOR_TEST);
	bool colorTestAgainstZero = id.Bit(FS_BIT_COLOR_AGAINST_ZERO);
	bool enableColorDoubling = id.Bit(FS_BIT_COLOR_DOUBLE);
	bool doTextureProjection = id.Bit(FS_BIT_DO_TEXTURE_PROJ);
	bool doTextureAlpha = id.Bit(FS_BIT_TEXALPHA);

	bool flatBug = bugs.Has(Draw::Bugs::BROKEN_FLAT_IN_SHADER) && g_Config.bVendorBugChecksEnabled;

	bool doFlatShading = id.Bit(FS_BIT_FLATSHADE) && !flatBug;
	bool shaderDepal = id.Bit(FS_BIT_SHADER_DEPAL);
	bool bgraTexture = id.Bit(FS_BIT_BGRA_TEXTURE);
	bool colorWriteMask = id.Bit(FS_BIT_COLOR_WRITEMASK) && compat.bitwiseOps;

	GEComparison alphaTestFunc = (GEComparison)id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3);
	GEComparison colorTestFunc = (GEComparison)id.Bits(FS_BIT_COLOR_TEST_FUNC, 2);
	bool needShaderTexClamp = id.Bit(FS_BIT_SHADER_TEX_CLAMP);

	GETexFunc texFunc = (GETexFunc)id.Bits(FS_BIT_TEXFUNC, 3);
	bool textureAtOffset = id.Bit(FS_BIT_TEXTURE_AT_OFFSET);

	ReplaceBlendType replaceBlend = static_cast<ReplaceBlendType>(id.Bits(FS_BIT_REPLACE_BLEND, 3));

	bool blueToAlpha = false;
	if (replaceBlend == ReplaceBlendType::REPLACE_BLEND_BLUE_TO_ALPHA) {
		blueToAlpha = true;
	}

	GEBlendSrcFactor replaceBlendFuncA = (GEBlendSrcFactor)id.Bits(FS_BIT_BLENDFUNC_A, 4);
	GEBlendDstFactor replaceBlendFuncB = (GEBlendDstFactor)id.Bits(FS_BIT_BLENDFUNC_B, 4);
	GEBlendMode replaceBlendEq = (GEBlendMode)id.Bits(FS_BIT_BLENDEQ, 3);
	StencilValueType replaceAlphaWithStencilType = (StencilValueType)id.Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4);

	bool isModeClear = id.Bit(FS_BIT_CLEARMODE);

	const char *shading = "";
	if (compat.glslES30 || compat.shaderLanguage == ShaderLanguage::GLSL_VULKAN)
		shading = doFlatShading ? "flat" : "";

	bool useDiscardStencilBugWorkaround = id.Bit(FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL);

	bool readFramebuffer = replaceBlend == REPLACE_BLEND_COPY_FBO || colorWriteMask;
	bool readFramebufferTex = readFramebuffer && !gstate_c.Supports(GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH);

	bool needFragCoord = readFramebuffer || gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT);
	bool writeDepth = gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT);

	if (shaderDepal && !doTexture) {
		*errorString = "depal requires a texture";
		return false;
	}

	if (readFramebuffer && compat.shaderLanguage == HLSL_D3D9) {
		*errorString = "Framebuffer read not yet supported in HLSL D3D9";
		return false;
	}

	if (compat.shaderLanguage == ShaderLanguage::GLSL_VULKAN) {
		if (useDiscardStencilBugWorkaround && !gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT)) {
			WRITE(p, "layout (depth_unchanged) out float gl_FragDepth;\n");
		}

		WRITE(p, "layout (std140, set = 0, binding = 3) uniform baseUBO {\n%s};\n", ub_baseStr);
		if (doTexture) {
			WRITE(p, "layout (binding = 0) uniform sampler2D tex;\n");
		}

		if (readFramebufferTex) {
			WRITE(p, "layout (binding = 1) uniform sampler2D fbotex;\n");
		}

		if (shaderDepal) {
			WRITE(p, "layout (binding = 2) uniform sampler2D pal;\n");
		}

		// Note: the precision qualifiers must match the vertex shader!
		WRITE(p, "layout (location = 1) %s in lowp vec4 v_color0;\n", shading);
		if (lmode)
			WRITE(p, "layout (location = 2) %s in lowp vec3 v_color1;\n", shading);
		if (enableFog) {
			WRITE(p, "layout (location = 3) in highp float v_fogdepth;\n");
		}
		if (doTexture) {
			WRITE(p, "layout (location = 0) in highp vec3 v_texcoord;\n");
		}

		if (enableAlphaTest && !alphaTestAgainstZero) {
			WRITE(p, "int roundAndScaleTo255i(in highp float x) { return int(floor(x * 255.0 + 0.5)); }\n");
		}
		if (enableColorTest && !colorTestAgainstZero) {
			WRITE(p, "ivec3 roundAndScaleTo255iv(in highp vec3 x) { return ivec3(floor(x * 255.0 + 0.5)); }\n");
		}

		WRITE(p, "layout (location = 0, index = 0) out vec4 fragColor0;\n");
		if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
			WRITE(p, "layout (location = 0, index = 1) out vec4 fragColor1;\n");
		}
	} else if (compat.shaderLanguage == HLSL_D3D11 || compat.shaderLanguage == HLSL_D3D9) {
		if (compat.shaderLanguage == HLSL_D3D9) {
			if (doTexture)
				WRITE(p, "sampler tex : register(s0);\n");

			if (readFramebufferTex) {
				WRITE(p, "vec2 u_fbotexSize : register(c%i);\n", CONST_PS_FBOTEXSIZE);
				WRITE(p, "sampler fbotex : register(s1);\n");
			}

			if (replaceBlend > REPLACE_BLEND_STANDARD) {
				if (replaceBlendFuncA >= GE_SRCBLEND_FIXA) {
					WRITE(p, "float3 u_blendFixA : register(c%i);\n", CONST_PS_BLENDFIXA);
				}
				if (replaceBlendFuncB >= GE_DSTBLEND_FIXB) {
					WRITE(p, "float3 u_blendFixB : register(c%i);\n", CONST_PS_BLENDFIXB);
				}
			}
			if (needShaderTexClamp && doTexture) {
				WRITE(p, "vec4 u_texclamp : register(c%i);\n", CONST_PS_TEXCLAMP);
				if (textureAtOffset) {
					WRITE(p, "vec2 u_texclampoff : register(c%i);\n", CONST_PS_TEXCLAMPOFF);
				}
			}

			if (enableAlphaTest || enableColorTest) {
				WRITE(p, "vec4 u_alphacolorref : register(c%i);\n", CONST_PS_ALPHACOLORREF);
				WRITE(p, "vec4 u_alphacolormask : register(c%i);\n", CONST_PS_ALPHACOLORMASK);
			}
			if (stencilToAlpha && replaceAlphaWithStencilType == STENCIL_VALUE_UNIFORM) {
				WRITE(p, "float u_stencilReplaceValue : register(c%i);\n", CONST_PS_STENCILREPLACE);
			}
			if (doTexture && texFunc == GE_TEXFUNC_BLEND) {
				WRITE(p, "float3 u_texenv : register(c%i);\n", CONST_PS_TEXENV);
			}
			if (enableFog) {
				WRITE(p, "float3 u_fogcolor : register(c%i);\n", CONST_PS_FOGCOLOR);
			}
		} else {
			WRITE(p, "SamplerState samp : register(s0);\n");
			WRITE(p, "Texture2D<vec4> tex : register(t0);\n");
			if (readFramebufferTex) {
				// No sampler required, we Load
				WRITE(p, "Texture2D<vec4> fboTex : register(t1);\n");
			}
			WRITE(p, "cbuffer base : register(b0) {\n%s};\n", ub_baseStr);
		}

		if (enableAlphaTest) {
			if (compat.shaderLanguage == HLSL_D3D11) {
				WRITE(p, "int roundAndScaleTo255i(float x) { return int(floor(x * 255.0f + 0.5f)); }\n");
			} else {
				// D3D11 level 9 gets to take this path.
				WRITE(p, "float roundAndScaleTo255f(float x) { return floor(x * 255.0f + 0.5f); }\n");
			}
		}
		if (enableColorTest) {
			if (compat.shaderLanguage == HLSL_D3D11) {
				WRITE(p, "uvec3 roundAndScaleTo255iv(float3 x) { return (floor(x * 255.0f + 0.5f)); }\n");
			} else {
				WRITE(p, "vec3 roundAndScaleTo255v(float3 x) { return floor(x * 255.0f + 0.5f); }\n");
			}
		}

		WRITE(p, "struct PS_IN {\n");
		if (doTexture) {
			WRITE(p, "  vec3 v_texcoord: TEXCOORD0;\n");
		}
		const char *colorInterpolation = doFlatShading && compat.shaderLanguage == HLSL_D3D11 ? "nointerpolation " : "";
		WRITE(p, "  %svec4 v_color0: COLOR0;\n", colorInterpolation);
		if (lmode) {
			WRITE(p, "  vec3 v_color1: COLOR1;\n");
		}
		if (enableFog) {
			WRITE(p, "  float v_fogdepth: TEXCOORD1;\n");
		}
		if (compat.shaderLanguage == HLSL_D3D11 && needFragCoord) {
			WRITE(p, "  vec4 pixelPos : SV_POSITION;\n");
		}
		WRITE(p, "};\n");

		if (compat.shaderLanguage == HLSL_D3D11) {
			WRITE(p, "struct PS_OUT {\n");
			if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
				WRITE(p, "  vec4 target : SV_Target0;\n");
				WRITE(p, "  vec4 target1 : SV_Target1;\n");
			} else {
				WRITE(p, "  vec4 target : SV_Target;\n");
			}
			if (writeDepth) {
				WRITE(p, "  float depth : SV_Depth;\n");
			}
			WRITE(p, "};\n");
		}
	} else if (compat.shaderLanguage == HLSL_D3D9) {
		if (doTexture)
			WRITE(p, "sampler tex : register(s0);\n");
		if (readFramebufferTex) {
			WRITE(p, "vec2 u_fbotexSize : register(c%i);\n", CONST_PS_FBOTEXSIZE);
			WRITE(p, "sampler fbotex : register(s1);\n");
		}
		if (replaceBlend > REPLACE_BLEND_STANDARD) {
			if (replaceBlendFuncA >= GE_SRCBLEND_FIXA) {
				WRITE(p, "float3 u_blendFixA : register(c%i);\n", CONST_PS_BLENDFIXA);
			}
			if (replaceBlendFuncB >= GE_DSTBLEND_FIXB) {
				WRITE(p, "float3 u_blendFixB : register(c%i);\n", CONST_PS_BLENDFIXB);
			}
		}
		if (needShaderTexClamp && doTexture) {
			WRITE(p, "vec4 u_texclamp : register(c%i);\n", CONST_PS_TEXCLAMP);
			if (textureAtOffset) {
				WRITE(p, "vec2 u_texclampoff : register(c%i);\n", CONST_PS_TEXCLAMPOFF);
			}
		}

		if (enableAlphaTest || enableColorTest) {
			WRITE(p, "vec4 u_alphacolorref : register(c%i);\n", CONST_PS_ALPHACOLORREF);
			WRITE(p, "vec4 u_alphacolormask : register(c%i);\n", CONST_PS_ALPHACOLORMASK);
		}
		if (stencilToAlpha && replaceAlphaWithStencilType == STENCIL_VALUE_UNIFORM) {
			WRITE(p, "float u_stencilReplaceValue : register(c%i);\n", CONST_PS_STENCILREPLACE);
		}
		if (doTexture && texFunc == GE_TEXFUNC_BLEND) {
			WRITE(p, "float3 u_texenv : register(c%i);\n", CONST_PS_TEXENV);
		}
		if (enableFog) {
			WRITE(p, "float3 u_fogcolor : register(c%i);\n", CONST_PS_FOGCOLOR);
		}
	} else if (ShaderLanguageIsOpenGL(compat.shaderLanguage)) {
		if ((shaderDepal || colorWriteMask) && gl_extensions.IsGLES) {
			WRITE(p, "precision highp int;\n");
		}

		if (doTexture)
			WRITE(p, "uniform sampler2D tex;\n");

		if (readFramebufferTex) {
			if (!compat.texelFetch) {
				WRITE(p, "uniform vec2 u_fbotexSize;\n");
			}
			WRITE(p, "uniform sampler2D fbotex;\n");
		}

		if (!isModeClear && replaceBlend > REPLACE_BLEND_STANDARD) {
			*uniformMask |= DIRTY_SHADERBLEND;
			if (replaceBlendFuncA >= GE_SRCBLEND_FIXA) {
				WRITE(p, "uniform vec3 u_blendFixA;\n");
			}
			if (replaceBlendFuncB >= GE_DSTBLEND_FIXB) {
				WRITE(p, "uniform vec3 u_blendFixB;\n");
			}
		}

		if (needShaderTexClamp && doTexture) {
			*uniformMask |= DIRTY_TEXCLAMP;
			WRITE(p, "uniform vec4 u_texclamp;\n");
			if (id.Bit(FS_BIT_TEXTURE_AT_OFFSET)) {
				WRITE(p, "uniform vec2 u_texclampoff;\n");
			}
		}

		if (enableAlphaTest || enableColorTest) {
			if (enableFragmentTestCache) {
				WRITE(p, "uniform sampler2D testtex;\n");
			} else {
				*uniformMask |= DIRTY_ALPHACOLORREF;
				WRITE(p, "uniform vec4 u_alphacolorref;\n");
				if (compat.bitwiseOps && ((enableColorTest && !colorTestAgainstZero) || (enableAlphaTest && !alphaTestAgainstZero))) {
					*uniformMask |= DIRTY_ALPHACOLORMASK;
					WRITE(p, "uniform ivec4 u_alphacolormask;\n");
				}
			}
		}

		if (shaderDepal) {
			WRITE(p, "uniform sampler2D pal;\n");
			WRITE(p, "uniform uint u_depal_mask_shift_off_fmt;\n");
			*uniformMask |= DIRTY_DEPAL;
		}

		if (colorWriteMask) {
			WRITE(p, "uniform uint u_colorWriteMask;\n");
			*uniformMask |= DIRTY_COLORWRITEMASK;
		}

		if (stencilToAlpha && replaceAlphaWithStencilType == STENCIL_VALUE_UNIFORM) {
			*uniformMask |= DIRTY_STENCILREPLACEVALUE;
			WRITE(p, "uniform float u_stencilReplaceValue;\n");
		}
		if (doTexture && texFunc == GE_TEXFUNC_BLEND) {
			*uniformMask |= DIRTY_TEXENV;
			WRITE(p, "uniform vec3 u_texenv;\n");
		}

		WRITE(p, "%s %s lowp vec4 v_color0;\n", shading, compat.varying_fs);
		if (lmode)
			WRITE(p, "%s %s lowp vec3 v_color1;\n", shading, compat.varying_fs);
		if (enableFog) {
			*uniformMask |= DIRTY_FOGCOLOR;
			WRITE(p, "uniform vec3 u_fogcolor;\n");
			WRITE(p, "%s %s float v_fogdepth;\n", compat.varying_fs, highpFog ? "highp" : "mediump");
		}
		if (doTexture) {
			WRITE(p, "%s %s vec3 v_texcoord;\n", compat.varying_fs, highpTexcoord ? "highp" : "mediump");
		}

		if (!enableFragmentTestCache) {
			if (enableAlphaTest && !alphaTestAgainstZero) {
				if (compat.bitwiseOps) {
					WRITE(p, "int roundAndScaleTo255i(in float x) { return int(floor(x * 255.0 + 0.5)); }\n");
				} else if (gl_extensions.gpuVendor == GPU_VENDOR_IMGTEC) {
					WRITE(p, "float roundTo255thf(in mediump float x) { mediump float y = x + (0.5/255.0); return y - fract(y * 255.0) * (1.0 / 255.0); }\n");
				} else {
					WRITE(p, "float roundAndScaleTo255f(in float x) { return floor(x * 255.0 + 0.5); }\n");
				}
			}
			if (enableColorTest && !colorTestAgainstZero) {
				if (compat.bitwiseOps) {
					WRITE(p, "ivec3 roundAndScaleTo255iv(in vec3 x) { return ivec3(floor(x * 255.0 + 0.5)); }\n");
				} else if (gl_extensions.gpuVendor == GPU_VENDOR_IMGTEC) {
					WRITE(p, "vec3 roundTo255thv(in vec3 x) { vec3 y = x + (0.5/255.0); return y - fract(y * 255.0) * (1.0 / 255.0); }\n");
				} else {
					WRITE(p, "vec3 roundAndScaleTo255v(in vec3 x) { return floor(x * 255.0 + 0.5); }\n");
				}
			}
		}

		if (!strcmp(compat.fragColor0, "fragColor0")) {
			const char *qualifierColor0 = "out";
			if (readFramebuffer && compat.lastFragData && !strcmp(compat.lastFragData, compat.fragColor0)) {
				qualifierColor0 = "inout";
			}
			// Output the output color definitions.
			if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
				WRITE(p, "%s vec4 fragColor0;\n", qualifierColor0);
				WRITE(p, "out vec4 fragColor1;\n");
			} else {
				WRITE(p, "%s vec4 fragColor0;\n", qualifierColor0);
			}
		}
	}

	bool hasPackUnorm4x8 = false;
	if (compat.shaderLanguage == GLSL_VULKAN) {
		hasPackUnorm4x8 = true;
	} else if (ShaderLanguageIsOpenGL(compat.shaderLanguage)) {
		if (compat.gles) {
			hasPackUnorm4x8 = compat.glslVersionNumber >= 310;
		} else {
			hasPackUnorm4x8 = compat.glslVersionNumber >= 400;
		}
	}

	// Provide implementations of packUnorm4x8 and unpackUnorm4x8 if not available.
	if (colorWriteMask && !hasPackUnorm4x8) {
		WRITE(p, "uint packUnorm4x8(%svec4 v) {\n", compat.shaderLanguage == GLSL_VULKAN ? "highp " : "");
		WRITE(p, "  highp vec4 f = clamp(v, 0.0, 1.0);\n");
		WRITE(p, "  uvec4 u = uvec4(255.0 * f);\n");
		WRITE(p, "  return u.x | (u.y << 8) | (u.z << 16) | (u.w << 24);\n");
		WRITE(p, "}\n");

		WRITE(p, "vec4 unpackUnorm4x8(highp uint x) {\n");
		WRITE(p, "  highp uvec4 u = uvec4(x & 0xFFU, (x >> 8) & 0xFFU, (x >> 16) & 0xFFU, (x >> 24) & 0xFFU);\n");
		WRITE(p, "  highp vec4 f = vec4(u);\n");
		WRITE(p, "  return f * (1.0 / 255.0);\n");
		WRITE(p, "}\n");
	}

	// PowerVR needs a custom modulo function. For some reason, this has far higher precision than the builtin one.
	if ((gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) && needShaderTexClamp) {
		WRITE(p, "float mymod(float a, float b) { return a - b * floor(a / b); }\n");
	}

	if (compat.shaderLanguage == HLSL_D3D11) {
		WRITE(p, "PS_OUT main( PS_IN In ) {\n");
		WRITE(p, "  PS_OUT outfragment;\n");
		if (needFragCoord) {
			WRITE(p, "  vec4 gl_FragCoord = In.pixelPos;\n");
		}
		if (writeDepth) {
			WRITE(p, "  float gl_FragDepth;\n");
		}
	} else if (compat.shaderLanguage == HLSL_D3D9) {
		WRITE(p, "vec4 main( PS_IN In ) : COLOR {\n");
		WRITE(p, "  vec4 target;\n");
	} else {
		WRITE(p, "void main() {\n");
	}

	if (compat.shaderLanguage == HLSL_D3D11 || compat.shaderLanguage == HLSL_D3D9) {
		WRITE(p, "  vec4 v_color0 = In.v_color0;\n");
		if (lmode)
			WRITE(p, "  vec3 v_color1 = In.v_color1;\n");
		if (enableFog) {
			WRITE(p, "  float v_fogdepth = In.v_fogdepth;\n");
		}
		if (doTexture) {
			WRITE(p, "  vec3 v_texcoord = In.v_texcoord;\n");
		}
	}

	// Two things read from the old framebuffer - shader replacement blending and bit-level masking.
	if (readFramebuffer) {
		if (compat.shaderLanguage == HLSL_D3D11) {
			WRITE(p, "  vec4 destColor = fboTex.Load(int3((int)gl_FragCoord.x, (int)gl_FragCoord.y, 0));\n");
		} else if (gstate_c.Supports(GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH)) {
			// If we have EXT_shader_framebuffer_fetch / ARM_shader_framebuffer_fetch, we skip the blit.
			// We can just read the prev value more directly.
			WRITE(p, "  lowp vec4 destColor = %s;\n", compat.lastFragData);
		} else if (!compat.texelFetch) {
			WRITE(p, "  lowp vec4 destColor = %s(fbotex, gl_FragCoord.xy * u_fbotexSize.xy);\n", compat.texture);
		} else {
			WRITE(p, "  lowp vec4 destColor = %s(fbotex, ivec2(gl_FragCoord.x, gl_FragCoord.y), 0);\n", compat.texelFetch);
		}
	}

	if (isModeClear) {
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

		if (doTexture) {
			char texcoord[64] = "v_texcoord";
			// TODO: Not sure the right way to do this for projection.
			// This path destroys resolution on older PowerVR no matter what I do if projection is needed,
			// so we disable it on SGX 540 and lesser, and live with the consequences.
			bool terriblePrecision = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_TERRIBLE) != 0;
			bool clampDisabled = doTextureProjection && terriblePrecision;
			// Also with terrible precision we can't do wrapping without destroying the image. See #9189
			if (terriblePrecision && (!id.Bit(FS_BIT_CLAMP_S) || !id.Bit(FS_BIT_CLAMP_T))) {
				clampDisabled = true;
			}
			if (needShaderTexClamp && !clampDisabled) {
				// We may be clamping inside a larger surface (tex = 64x64, buffer=480x272).
				// We may also be wrapping in such a surface, or either one in a too-small surface.
				// Obviously, clamping to a smaller surface won't work.  But better to clamp to something.
				std::string ucoord = "v_texcoord.x";
				std::string vcoord = "v_texcoord.y";
				if (doTextureProjection) {
					ucoord = "(v_texcoord.x / v_texcoord.z)";
					vcoord = "(v_texcoord.y / v_texcoord.z)";
				}

				std::string modulo = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) ? "mymod" : "mod";

				if (id.Bit(FS_BIT_CLAMP_S)) {
					ucoord = "clamp(" + ucoord + ", u_texclamp.z, u_texclamp.x - u_texclamp.z)";
				} else {
					ucoord = modulo + "(" + ucoord + ", u_texclamp.x)";
				}
				if (id.Bit(FS_BIT_CLAMP_T)) {
					vcoord = "clamp(" + vcoord + ", u_texclamp.w, u_texclamp.y - u_texclamp.w)";
				} else {
					vcoord = modulo + "(" + vcoord + ", u_texclamp.y)";
				}
				if (textureAtOffset) {
					ucoord = "(" + ucoord + " + u_texclampoff.x)";
					vcoord = "(" + vcoord + " + u_texclampoff.y)";
				}

				WRITE(p, "  vec2 fixedcoord = vec2(%s, %s);\n", ucoord.c_str(), vcoord.c_str());
				truncate_cpy(texcoord, "fixedcoord");
				// We already projected it.
				doTextureProjection = false;
			}

			if (!shaderDepal) {
				if (compat.shaderLanguage == HLSL_D3D11) {
					if (doTextureProjection) {
						WRITE(p, "  vec4 t = tex.Sample(samp, v_texcoord.xy / v_texcoord.z)%s;\n", bgraTexture ? ".bgra" : "");
					} else {
						WRITE(p, "  vec4 t = tex.Sample(samp, %s.xy)%s;\n", texcoord, bgraTexture ? ".bgra" : "");
					}
				} else if (compat.shaderLanguage == HLSL_D3D9) {
					if (doTextureProjection) {
						WRITE(p, "  vec4 t = tex2Dproj(tex, vec4(v_texcoord.x, v_texcoord.y, 0, v_texcoord.z))%s;\n", bgraTexture ? ".bgra" : "");
					} else {
						WRITE(p, "  vec4 t = tex2D(tex, %s.xy)%s;\n", texcoord, bgraTexture ? ".bgra" : "");
					}
				} else {
					if (doTextureProjection) {
						WRITE(p, "  vec4 t = %sProj(tex, %s);\n", compat.texture, texcoord);
					} else {
						WRITE(p, "  vec4 t = %s(tex, %s.xy);\n", compat.texture, texcoord);
					}
				} 
			} else {
				if (doTextureProjection) {
					// We don't use textureProj because we need better control and it's probably not much of a savings anyway.
					// However it is good for precision on older hardware like PowerVR.
					WRITE(p, "  vec2 uv = %s.xy/%s.z;\n  vec2 uv_round;\n", texcoord, texcoord);
				} else {
					WRITE(p, "  vec2 uv = %s.xy;\n  vec2 uv_round;\n", texcoord);
				}
				WRITE(p, "  vec2 tsize = vec2(textureSize(tex, 0));\n");
				WRITE(p, "  vec2 fraction;\n");
				WRITE(p, "  bool bilinear = (u_depal_mask_shift_off_fmt >> 31) != 0U;\n");
				WRITE(p, "  if (bilinear) {\n");
				WRITE(p, "    uv_round = uv * tsize - vec2(0.5, 0.5);\n");
				WRITE(p, "    fraction = fract(uv_round);\n");
				WRITE(p, "    uv_round = (uv_round - fraction + vec2(0.5, 0.5)) / tsize;\n");  // We want to take our four point samples at pixel centers.
				WRITE(p, "  } else {\n");
				WRITE(p, "    uv_round = uv;\n");
				WRITE(p, "  }\n");
				WRITE(p, "  highp vec4 t = %s(tex, uv_round);\n", compat.texture);
				WRITE(p, "  highp vec4 t1 = %sOffset(tex, uv_round, ivec2(1, 0));\n", compat.texture);
				WRITE(p, "  highp vec4 t2 = %sOffset(tex, uv_round, ivec2(0, 1));\n", compat.texture);
				WRITE(p, "  highp vec4 t3 = %sOffset(tex, uv_round, ivec2(1, 1));\n", compat.texture);
				WRITE(p, "  uint depalMask = (u_depal_mask_shift_off_fmt & 0xFFU);\n");
				WRITE(p, "  uint depalShift = (u_depal_mask_shift_off_fmt >> 8) & 0xFFU;\n");
				WRITE(p, "  uint depalOffset = ((u_depal_mask_shift_off_fmt >> 16) & 0xFFU) << 4;\n");
				WRITE(p, "  uint depalFmt = (u_depal_mask_shift_off_fmt >> 24) & 0x3U;\n");
				WRITE(p, "  uvec4 col; uint index0; uint index1; uint index2; uint index3;\n");
				WRITE(p, "  switch (int(depalFmt)) {\n");  // We might want to include fmt in the shader ID if this is a performance issue.
				WRITE(p, "  case 0:\n");  // 565
				WRITE(p, "    col = uvec4(t.rgb * vec3(31.99, 63.99, 31.99), 0);\n");
				WRITE(p, "    index0 = (col.b << 11) | (col.g << 5) | (col.r);\n");
				WRITE(p, "    if (bilinear) {\n");
				WRITE(p, "      col = uvec4(t1.rgb * vec3(31.99, 63.99, 31.99), 0);\n");
				WRITE(p, "      index1 = (col.b << 11) | (col.g << 5) | (col.r);\n");
				WRITE(p, "      col = uvec4(t2.rgb * vec3(31.99, 63.99, 31.99), 0);\n");
				WRITE(p, "      index2 = (col.b << 11) | (col.g << 5) | (col.r);\n");
				WRITE(p, "      col = uvec4(t3.rgb * vec3(31.99, 63.99, 31.99), 0);\n");
				WRITE(p, "      index3 = (col.b << 11) | (col.g << 5) | (col.r);\n");
				WRITE(p, "    }\n");
				WRITE(p, "    break;\n");
				WRITE(p, "  case 1:\n");  // 5551
				WRITE(p, "    col = uvec4(t.rgba * vec4(31.99, 31.99, 31.99, 1.0));\n");
				WRITE(p, "    index0 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);\n");
				WRITE(p, "    if (bilinear) {\n");
				WRITE(p, "      col = uvec4(t1.rgba * vec4(31.99, 31.99, 31.99, 1.0));\n");
				WRITE(p, "      index1 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);\n");
				WRITE(p, "      col = uvec4(t2.rgba * vec4(31.99, 31.99, 31.99, 1.0));\n");
				WRITE(p, "      index2 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);\n");
				WRITE(p, "      col = uvec4(t3.rgba * vec4(31.99, 31.99, 31.99, 1.0));\n");
				WRITE(p, "      index3 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);\n");
				WRITE(p, "    }\n");
				WRITE(p, "    break;\n");
				WRITE(p, "  case 2:\n");  // 4444
				WRITE(p, "    col = uvec4(t.rgba * 15.99);\n");
				WRITE(p, "    index0 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);\n");
				WRITE(p, "    if (bilinear) {\n");
				WRITE(p, "      col = uvec4(t1.rgba * 15.99);\n");
				WRITE(p, "      index1 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);\n");
				WRITE(p, "      col = uvec4(t2.rgba * 15.99);\n");
				WRITE(p, "      index2 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);\n");
				WRITE(p, "      col = uvec4(t3.rgba * 15.99);\n");
				WRITE(p, "      index3 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);\n");
				WRITE(p, "    }\n");
				WRITE(p, "    break;\n");
				WRITE(p, "  case 3:\n");  // 8888
				WRITE(p, "    col = uvec4(t.rgba * 255.99);\n");
				WRITE(p, "    index0 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);\n");
				WRITE(p, "    if (bilinear) {\n");
				WRITE(p, "      col = uvec4(t1.rgba * 255.99);\n");
				WRITE(p, "      index1 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);\n");
				WRITE(p, "      col = uvec4(t2.rgba * 255.99);\n");
				WRITE(p, "      index2 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);\n");
				WRITE(p, "      col = uvec4(t3.rgba * 255.99);\n");
				WRITE(p, "      index3 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);\n");
				WRITE(p, "    }\n");
				WRITE(p, "    break;\n");
				WRITE(p, "  };\n");
				WRITE(p, "  index0 = ((index0 >> depalShift) & depalMask) | depalOffset;\n");
				WRITE(p, "  t = texelFetch(pal, ivec2(index0, 0), 0);\n");
				WRITE(p, "  if (bilinear && !(index0 == index1 && index1 == index2 && index2 == index3)) {\n");
				WRITE(p, "    index1 = ((index1 >> depalShift) & depalMask) | depalOffset;\n");
				WRITE(p, "    index2 = ((index2 >> depalShift) & depalMask) | depalOffset;\n");
				WRITE(p, "    index3 = ((index3 >> depalShift) & depalMask) | depalOffset;\n");
				WRITE(p, "    t1 = texelFetch(pal, ivec2(index1, 0), 0);\n");
				WRITE(p, "    t2 = texelFetch(pal, ivec2(index2, 0), 0);\n");
				WRITE(p, "    t3 = texelFetch(pal, ivec2(index3, 0), 0);\n");
				WRITE(p, "    t = mix(t, t1, fraction.x);\n");
				WRITE(p, "    t2 = mix(t2, t3, fraction.x);\n");
				WRITE(p, "    t = mix(t, t2, fraction.y);\n");
				WRITE(p, "  }\n");
			}

			if (texFunc != GE_TEXFUNC_REPLACE || !doTextureAlpha)
				WRITE(p, "  vec4 p = v_color0;\n");

			if (doTextureAlpha) { // texfmt == RGBA
				switch (texFunc) {
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
				switch (texFunc) {
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

			if (enableColorDoubling) {
				// This happens before fog is applied.
				WRITE(p, "  v.rgb = clamp(v.rgb * 2.0, 0.0, 1.0);\n");
			}
		} else {
			// No texture mapping
			WRITE(p, "  vec4 v = v_color0 %s;\n", secondary);
		}

		if (enableFog) {
			WRITE(p, "  float fogCoef = clamp(v_fogdepth, 0.0, 1.0);\n");
			WRITE(p, "  v = mix(vec4(u_fogcolor, v.a), v, fogCoef);\n");
		}

		// Texture access is at half texels [0.5/256, 255.5/256], but colors are normalized [0, 255].
		// So we have to scale to account for the difference.
		char alphaTestXCoord[64] = "0";
		if (enableFragmentTestCache) {
			if (enableColorTest && !colorTestAgainstZero) {
				WRITE(p, "  vec4 vScale256 = v * %f + %f;\n", 255.0 / 256.0, 0.5 / 256.0);
				truncate_cpy(alphaTestXCoord, "vScale256.a");
			} else if (enableAlphaTest && !alphaTestAgainstZero) {
				snprintf(alphaTestXCoord, sizeof(alphaTestXCoord), "v.a * %f + %f", 255.0 / 256.0, 0.5 / 256.0);
			}
		}

		const char *discardStatement = testForceToZero ? "v.a = 0.0;" : "DISCARD;";
		if (enableAlphaTest) {
			if (alphaTestAgainstZero) {
				// When testing against 0 (extremely common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (alphaTestFunc == GE_COMP_NOTEQUAL || alphaTestFunc == GE_COMP_GREATER) {
					WRITE(p, "  if (v.a < 0.002) %s\n", discardStatement);
				} else if (alphaTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.  Happens sometimes, actually...
					WRITE(p, "  if (v.a > 0.002) %s\n", discardStatement);
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  %s\n", discardStatement);
				}
			} else if (enableFragmentTestCache) {
				WRITE(p, "  float aResult = %s(testtex, vec2(%s, 0)).a;\n", compat.texture, alphaTestXCoord);
				WRITE(p, "  if (aResult < 0.5) %s\n", discardStatement);
			} else {
				const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };
				if (alphaTestFuncs[alphaTestFunc][0] != '#') {
					if (compat.bitwiseOps) {
						WRITE(p, "  if ((roundAndScaleTo255i(v.a) & u_alphacolormask.a) %s int(u_alphacolorref.a)) %s\n", alphaTestFuncs[alphaTestFunc], discardStatement);
					} else if (gl_extensions.gpuVendor == GPU_VENDOR_IMGTEC) {
						// Work around bad PVR driver problem where equality check + discard just doesn't work.
						if (alphaTestFunc != GE_COMP_NOTEQUAL) {
							WRITE(p, "  if (roundTo255thf(v.a) %s u_alphacolorref.a) %s\n", alphaTestFuncs[alphaTestFunc], discardStatement);
						}
					} else {
						WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) %s\n", alphaTestFuncs[alphaTestFunc], discardStatement);
					}
				} else {
					// This means NEVER.  See above.
					WRITE(p, "  %s\n", discardStatement);
				}
			}
		}

		if (enableColorTest) {
			if (colorTestAgainstZero) {
				// When testing against 0 (common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (colorTestFunc == GE_COMP_NOTEQUAL) {
					if (compat.shaderLanguage == GLSL_VULKAN) {
						// Old workaround for Adreno driver bug. We could make this the main path actually
						// since the math is roughly equivalent given the non-negative inputs.
						WRITE(p, "  if (v.r + v.g + v.b < 0.002) %s\n", discardStatement);
					} else {
						WRITE(p, "  if (v.r < 0.002 && v.g < 0.002 && v.b < 0.002) %s\n", discardStatement);
					}
				} else if (colorTestFunc != GE_COMP_NEVER) {
					if (compat.shaderLanguage == GLSL_VULKAN) {
						// See the GE_COMP_NOTEQUAL case.
						WRITE(p, "  if (v.r + v.g + v.b > 0.002) %s\n", discardStatement);
					} else {
						// Anything else is a test for == 0.
						WRITE(p, "  if (v.r > 0.002 || v.g > 0.002 || v.b > 0.002) %s\n", discardStatement);
					}
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  %s\n", discardStatement);
				}
			} else if (enableFragmentTestCache) {
				WRITE(p, "  float rResult = %s(testtex, vec2(vScale256.r, 0)).r;\n", compat.texture);
				WRITE(p, "  float gResult = %s(testtex, vec2(vScale256.g, 0)).g;\n", compat.texture);
				WRITE(p, "  float bResult = %s(testtex, vec2(vScale256.b, 0)).b;\n", compat.texture);
				if (colorTestFunc == GE_COMP_EQUAL) {
					// Equal means all parts must be equal (so discard if any is not.)
					WRITE(p, "  if (rResult < 0.5 || gResult < 0.5 || bResult < 0.5) %s\n", discardStatement);
				} else {
					// Not equal means any part must be not equal.
					WRITE(p, "  if (rResult < 0.5 && gResult < 0.5 && bResult < 0.5) %s\n", discardStatement);
				}
			} else {
				const char *colorTestFuncs[] = { "#", "#", " != ", " == " };
				if (colorTestFuncs[colorTestFunc][0] != '#') {
					// TODO: Unify these paths better.
					if (compat.shaderLanguage == HLSL_D3D11) {
						const char *test = colorTestFuncs[colorTestFunc];
						WRITE(p, "  uvec3 v_scaled = roundAndScaleTo255iv(v.rgb);\n");
						WRITE(p, "  uvec3 v_masked = v_scaled & u_alphacolormask.rgb;\n");
						WRITE(p, "  uvec3 colorTestRef = u_alphacolorref.rgb & u_alphacolormask.rgb;\n");
						// We have to test the components separately, or we get incorrect results.  See #10629.
						WRITE(p, "  if (v_masked.r %s colorTestRef.r && v_masked.g %s colorTestRef.g && v_masked.b %s colorTestRef.b) %s\n", test, test, test, discardStatement);
					} else if (compat.shaderLanguage == HLSL_D3D9) {
						const char *test = colorTestFuncs[colorTestFunc];
						// TODO: Use a texture to lookup bitwise ops instead?
						WRITE(p, "  vec3 colortest = roundAndScaleTo255v(v.rgb);\n");
						WRITE(p, "  if ((colortest.r %s u_alphacolorref.r) && (colortest.g %s u_alphacolorref.g) && (colortest.b %s u_alphacolorref.b)) %s\n", test, test, test, discardStatement);
					} else if (compat.bitwiseOps) {
						WRITE(p, "  ivec3 v_scaled = roundAndScaleTo255iv(v.rgb);\n");
						if (compat.shaderLanguage == GLSL_VULKAN) {
							// Apparently GLES3 does not support vector bitwise ops, but Vulkan does?
							WRITE(p, "  if ((v_scaled & u_alphacolormask.rgb) %s (u_alphacolorref.rgb & u_alphacolormask.rgb)) %s\n", colorTestFuncs[colorTestFunc], discardStatement);
						} else {
							const char *maskedFragColor = "ivec3(v_scaled.r & u_alphacolormask.r, v_scaled.g & u_alphacolormask.g, v_scaled.b & u_alphacolormask.b)";
							const char *maskedColorRef = "ivec3(int(u_alphacolorref.r) & u_alphacolormask.r, int(u_alphacolorref.g) & u_alphacolormask.g, int(u_alphacolorref.b) & u_alphacolormask.b)";
							WRITE(p, "  if (%s %s %s) %s\n", maskedFragColor, colorTestFuncs[colorTestFunc], maskedColorRef, discardStatement);
						}
					} else if (gl_extensions.gpuVendor == GPU_VENDOR_IMGTEC) {
						WRITE(p, "  if (roundTo255thv(v.rgb) %s u_alphacolorref.rgb) %s\n", colorTestFuncs[colorTestFunc], discardStatement);
					} else {
						WRITE(p, "  if (roundAndScaleTo255v(v.rgb) %s u_alphacolorref.rgb) %s\n", colorTestFuncs[colorTestFunc], discardStatement);
					}
				} else {
					WRITE(p, "  %s\n", discardStatement);
				}
			}
		}

		if (replaceBlend == REPLACE_BLEND_2X_SRC) {
			WRITE(p, "  v.rgb = v.rgb * 2.0;\n");
		}

		// In some cases we need to replicate the first half of the blend equation here.
		// In case of blue-to-alpha, it's since we overwrite alpha with blue before the actual blend equation runs.
		if (replaceBlend == REPLACE_BLEND_PRE_SRC || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA || replaceBlend == REPLACE_BLEND_BLUE_TO_ALPHA) {
			const char *srcFactor = "ERROR";
			switch (replaceBlendFuncA) {
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "splat3(v.a)"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "splat3(1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "splat3(v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "splat3(1.0 - v.a * 2.0)"; break;
			// PRE_SRC for REPLACE_BLEND_PRE_SRC_2X_ALPHA means "double the src."
			// It's close to the same, but clamping can still be an issue.
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "splat3(2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "ERROR"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			default:                            srcFactor = "u_blendFixA"; break;
			}

			if (!strcmp(srcFactor, "ERROR")) {
				*errorString = "Bad replaceblend src factor";
				return false;
			}

			WRITE(p, "  v.rgb = v.rgb * %s;\n", srcFactor);
		}

		if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
			const char *srcFactor = nullptr;
			const char *dstFactor = nullptr;

			switch (replaceBlendFuncA) {
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "destColor.rgb"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "(splat3(1.0) - destColor.rgb)"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "v.aaa"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "splat3(1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "destColor.aaa"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "(splat3(1.0) - destColor.aaa)"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "v.aaa * 2.0"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "(splat3(1.0) - v.aaa * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "destColor.aaa * 2.0"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "(splat3(1.0) - destColor.aaa * 2.0)"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			default:                            srcFactor = "u_blendFixA"; break;
			}
			switch (replaceBlendFuncB) {
			case GE_DSTBLEND_SRCCOLOR:          dstFactor = "v.rgb"; break;
			case GE_DSTBLEND_INVSRCCOLOR:       dstFactor = "(splat3(1.0) - v.rgb)"; break;
			case GE_DSTBLEND_SRCALPHA:          dstFactor = "v.aaa"; break;
			case GE_DSTBLEND_INVSRCALPHA:       dstFactor = "(splat3(1.0) - v.aaa)"; break;
			case GE_DSTBLEND_DSTALPHA:          dstFactor = "destColor.aaa"; break;
			case GE_DSTBLEND_INVDSTALPHA:       dstFactor = "(splat3(1.0) - destColor.aaa)"; break;
			case GE_DSTBLEND_DOUBLESRCALPHA:    dstFactor = "v.aaa * 2.0"; break;
			case GE_DSTBLEND_DOUBLEINVSRCALPHA: dstFactor = "(splat3(1.0) - v.aaa * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEDSTALPHA:    dstFactor = "destColor.aaa * 2.0"; break;
			case GE_DSTBLEND_DOUBLEINVDSTALPHA: dstFactor = "(splat3(1.0) - destColor.aaa * 2.0)"; break;
			case GE_DSTBLEND_FIXB:              dstFactor = "u_blendFixB"; break;
			default:                            dstFactor = "u_blendFixB"; break;
			}

			switch (replaceBlendEq) {
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
			default:
				*errorString = "Bad replace blend eq";
				return false;
			}
		}

		if (replaceBlend == REPLACE_BLEND_2X_ALPHA || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			WRITE(p, "  v.a *= 2.0;\n");
		}
	}

	char replacedAlpha[64] = "0.0";
	if (stencilToAlpha != REPLACE_ALPHA_NO) {
		switch (replaceAlphaWithStencilType) {
		case STENCIL_VALUE_UNIFORM:
			truncate_cpy(replacedAlpha, "u_stencilReplaceValue");
			break;

		case STENCIL_VALUE_ZERO:
			truncate_cpy(replacedAlpha, "0.0");
			break;

		case STENCIL_VALUE_ONE:
		case STENCIL_VALUE_INVERT:
			// In invert, we subtract by one, but we want to output one here.
			truncate_cpy(replacedAlpha, "1.0");
			break;

		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_DECR_4:
			// We're adding/subtracting, just by the smallest value in 4-bit.
			snprintf(replacedAlpha, sizeof(replacedAlpha), "%f", 1.0 / 15.0);
			break;

		case STENCIL_VALUE_INCR_8:
		case STENCIL_VALUE_DECR_8:
			// We're adding/subtracting, just by the smallest value in 8-bit.
			snprintf(replacedAlpha, sizeof(replacedAlpha), "%f", 1.0 / 255.0);
			break;

		case STENCIL_VALUE_KEEP:
			// Do nothing. We'll mask out the alpha using color mask.
			break;
		}
	}

	switch (stencilToAlpha) {
	case REPLACE_ALPHA_DUALSOURCE:
		WRITE(p, "  %s = vec4(v.rgb, %s);\n", compat.fragColor0, replacedAlpha);
		WRITE(p, "  %s = vec4(0.0, 0.0, 0.0, v.a);\n", compat.fragColor1);
		break;

	case REPLACE_ALPHA_YES:
		WRITE(p, "  %s = vec4(v.rgb, %s);\n", compat.fragColor0, replacedAlpha);
		break;

	case REPLACE_ALPHA_NO:
		WRITE(p, "  %s = v;\n", compat.fragColor0);
		break;

	default:
		*errorString = "Bad stencil-to-alpha type, corrupt ID?";
		return false;
	}

	// TODO: This could support more ops using the shader blending mechanism.
	LogicOpReplaceType replaceLogicOpType = (LogicOpReplaceType)id.Bits(FS_BIT_REPLACE_LOGIC_OP_TYPE, 2);
	switch (replaceLogicOpType) {
	case LOGICOPTYPE_ONE:
		WRITE(p, "  %s.rgb = splat3(1.0);\n", compat.fragColor0);
		break;
	case LOGICOPTYPE_INVERT:
		WRITE(p, "  %s.rgb = splat3(1.0) - %s.rgb;\n", compat.fragColor0, compat.fragColor0);
		break;
	case LOGICOPTYPE_NORMAL:
		break;

	default:
		*errorString = "Bad logic op type, corrupt ID?";
		return false;
	}

	// Final color computed - apply color write mask.
	// TODO: Maybe optimize to only do math on the affected channels?
	// Or .. meh.
	if (colorWriteMask) {
		WRITE(p, "  highp uint v32 = packUnorm4x8(%s);\n", compat.fragColor0);
		WRITE(p, "  highp uint d32 = packUnorm4x8(destColor);\n");
		// Note that the mask has been flipped to the PC way - 1 means write.
		WRITE(p, "  v32 = (v32 & u_colorWriteMask) | (d32 & ~u_colorWriteMask);\n");
		WRITE(p, "  %s = unpackUnorm4x8(v32);\n", compat.fragColor0);
	}

	if (blueToAlpha) {
		WRITE(p, "  %s = vec4(0.0, 0.0, 0.0, %s.z);  // blue to alpha\n", compat.fragColor0, compat.fragColor0);
	}

	if (gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT)) {
		const double scale = DepthSliceFactor() * 65535.0;

		WRITE(p, "  highp float z = gl_FragCoord.z;\n");
		if (gstate_c.Supports(GPU_SUPPORTS_ACCURATE_DEPTH)) {
			// We center the depth with an offset, but only its fraction matters.
			// When (DepthSliceFactor() - 1) is odd, it will be 0.5, otherwise 0.
			if (((int)(DepthSliceFactor() - 1.0f) & 1) == 1) {
				WRITE(p, "  z = (floor((z * %f) - (1.0 / 2.0)) + (1.0 / 2.0)) * (1.0 / %f);\n", scale, scale);
			} else {
				WRITE(p, "  z = floor(z * %f) * (1.0 / %f);\n", scale, scale);
			}
		} else {
			WRITE(p, "  z = (1.0/65535.0) * floor(z * 65535.0);\n");
		}
		WRITE(p, "  gl_FragDepth = z;\n");
	} else if (useDiscardStencilBugWorkaround) {
		// Adreno and some Mali drivers apply early frag tests even with discard in the shader,
		// when only stencil is used. The exact situation seems to vary by driver.
		// Writing depth prevents the bug for both vendors, even with depth_unchanged specified.
		// This doesn't make a ton of sense, but empirically does work.
		WRITE(p, "  gl_FragDepth = gl_FragCoord.z;\n");
	}

	if (compat.shaderLanguage == HLSL_D3D11) {
		if (writeDepth) {
			WRITE(p, "  outfragment.depth = gl_FragDepth;\n");
		}
		WRITE(p, "  return outfragment;\n");
	} else if (compat.shaderLanguage == HLSL_D3D9) {
		WRITE(p, "  return target;\n");
	}

	WRITE(p, "}\n");

	return true;
}

