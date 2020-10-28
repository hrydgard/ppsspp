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
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/GLES/FragmentShaderGeneratorGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#define WRITE p+=sprintf

static const char *vulkan_glsl_preamble =
"#version 450\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"#extension GL_ARB_conservative_depth : enable\n"
"#extension GL_ARB_shader_image_load_store : enable\n"
"#define splat3(x) vec3(x)\n"
"#define lowp\n"
"#define mediump\n"
"#define highp\n"
"#define DISCARD discard\n"
"\n";
extern const char *hlsl_preamble;
extern const char *hlsl_d3d9_preamble;
extern const char *hlsl_d3d11_preamble;
extern const char *hlsl_late_preamble;

bool GenerateFragmentShaderGLSL(const FShaderID &id, char *buffer, const GLSLShaderCompat &compat, uint64_t *uniformMask, std::string *errorString) {
	*uniformMask = 0;

	bool highpFog = false;
	bool highpTexcoord = false;
	bool enableFragmentTestCache = g_Config.bFragmentTestCache && !compat.vulkan && !compat.d3d11;

	if (compat.gles) {
		// PowerVR needs highp to do the fog in MHU correctly.
		// Others don't, and some can't handle highp in the fragment shader.
		highpFog = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) ? true : false;
		highpTexcoord = highpFog;
	}

	ReplaceAlphaType stencilToAlpha = static_cast<ReplaceAlphaType>(id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2));

	char *p = buffer;

	if (compat.vulkan) {
		WRITE(p, "%s", vulkan_glsl_preamble);
	} else if (compat.d3d11) {
		WRITE(p, "%s", hlsl_preamble);
		WRITE(p, "%s", hlsl_d3d11_preamble);
	} else if (compat.d3d9) {
		WRITE(p, "%s", hlsl_preamble);
		WRITE(p, "%s", hlsl_d3d9_preamble);
	} else {
		WRITE(p, "#version %d%s\n", compat.glslVersionNumber, compat.gles ? " es" : "");
		WRITE(p, "#define DISCARD discard\n");

		if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE && gl_extensions.EXT_blend_func_extended) {
			WRITE(p, "#extension GL_EXT_blend_func_extended : require\n");
		}
		if (gl_extensions.EXT_gpu_shader4) {
			WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
		}
		if (compat.framebufferFetchExtension) {
			WRITE(p, "%s\n", compat.framebufferFetchExtension);
		}
		if (!compat.gles) {
			WRITE(p, "#define lowp\n");
			WRITE(p, "#define mediump\n");
			WRITE(p, "#define highp\n");
		} else {
			WRITE(p, "precision lowp float;\n");
		}

		WRITE(p, "#define splat3(x) vec3(x)\n");
	}

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
	bool doFlatShading = id.Bit(FS_BIT_FLATSHADE);
	bool shaderDepal = id.Bit(FS_BIT_SHADER_DEPAL);
	bool bgraTexture = id.Bit(FS_BIT_BGRA_TEXTURE);

	GEComparison alphaTestFunc = (GEComparison)id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3);
	GEComparison colorTestFunc = (GEComparison)id.Bits(FS_BIT_COLOR_TEST_FUNC, 2);
	bool needShaderTexClamp = id.Bit(FS_BIT_SHADER_TEX_CLAMP);

	GETexFunc texFunc = (GETexFunc)id.Bits(FS_BIT_TEXFUNC, 3);
	bool textureAtOffset = id.Bit(FS_BIT_TEXTURE_AT_OFFSET);

	ReplaceBlendType replaceBlend = static_cast<ReplaceBlendType>(id.Bits(FS_BIT_REPLACE_BLEND, 3));

	GEBlendSrcFactor replaceBlendFuncA = (GEBlendSrcFactor)id.Bits(FS_BIT_BLENDFUNC_A, 4);
	GEBlendDstFactor replaceBlendFuncB = (GEBlendDstFactor)id.Bits(FS_BIT_BLENDFUNC_B, 4);
	GEBlendMode replaceBlendEq = (GEBlendMode)id.Bits(FS_BIT_BLENDEQ, 3);
	StencilValueType replaceAlphaWithStencilType = (StencilValueType)id.Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4);

	bool isModeClear = id.Bit(FS_BIT_CLEARMODE);

	const char *shading = "";
	if (compat.glslES30 || compat.vulkan)
		shading = doFlatShading ? "flat" : "";

	bool earlyFragmentTests = ((!enableAlphaTest && !enableColorTest) || testForceToZero) && !gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT);
	bool useAdrenoBugWorkaround = id.Bit(FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL);

	if (compat.vulkan) {
		if (earlyFragmentTests) {
			WRITE(p, "layout (early_fragment_tests) in;\n");
		} else if (useAdrenoBugWorkaround && !gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT)) {
			WRITE(p, "layout (depth_unchanged) out float gl_FragDepth;\n");
		}

		WRITE(p, "layout (std140, set = 0, binding = 3) uniform baseUBO {\n%s};\n", ub_baseStr);
		if (doTexture) {
			WRITE(p, "layout (binding = 0) uniform sampler2D tex;\n");
		}

		if (!isModeClear && replaceBlend > REPLACE_BLEND_STANDARD) {
			if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
				WRITE(p, "layout (binding = 1) uniform sampler2D fbotex;\n");
			}
		}

		if (shaderDepal) {
			WRITE(p, "layout (binding = 2) uniform sampler2D pal;\n");
		}

		WRITE(p, "layout (location = 1) %s in vec4 v_color0;\n", shading);
		if (lmode)
			WRITE(p, "layout (location = 2) %s in vec3 v_color1;\n", shading);
		if (enableFog) {
			WRITE(p, "layout (location = 3) in float v_fogdepth;\n");
		}
		if (doTexture) {
			WRITE(p, "layout (location = 0) in vec3 v_texcoord;\n");
		}

		if (enableAlphaTest && !alphaTestAgainstZero) {
			WRITE(p, "int roundAndScaleTo255i(in float x) { return int(floor(x * 255.0 + 0.5)); }\n");
		}
		if (enableColorTest && !colorTestAgainstZero) {
			WRITE(p, "ivec3 roundAndScaleTo255iv(in vec3 x) { return ivec3(floor(x * 255.0 + 0.5)); }\n");
		}

		WRITE(p, "layout (location = 0, index = 0) out vec4 fragColor0;\n");
		if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
			WRITE(p, "layout (location = 0, index = 1) out vec4 fragColor1;\n");
		}
	} else if (compat.d3d11) {
		WRITE(p, "SamplerState samp : register(s0);\n");
		WRITE(p, "Texture2D<vec4> tex : register(t0);\n");
		if (!isModeClear && replaceBlend > REPLACE_BLEND_STANDARD) {
			if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
				// No sampler required, we Load
				WRITE(p, "Texture2D<vec4> fboTex : register(t1);\n");
			}
		}
		WRITE(p, "cbuffer base : register(b0) {\n%s};\n", cb_baseStr);

		if (enableAlphaTest) {
			WRITE(p, "int roundAndScaleTo255i(float x) { return int(floor(x * 255.0f + 0.5f)); }\n");
		}
		if (enableColorTest) {
			WRITE(p, "uvec3 roundAndScaleTo255iv(float3 x) { return uvec3(floor(x * 255.0f + 0.5f)); }\n");
		}

		WRITE(p, "struct PS_IN {\n");
		if (doTexture) {
			WRITE(p, "  vec3 v_texcoord: TEXCOORD0;\n");
		}
		const char *colorInterpolation = doFlatShading ? "nointerpolation " : "";
		WRITE(p, "  %svec4 v_color0: COLOR0;\n", colorInterpolation);
		if (lmode) {
			WRITE(p, "  vec3 v_color1: COLOR1;\n");
		}
		if (enableFog) {
			WRITE(p, "  float v_fogdepth: TEXCOORD1;\n");
		}
		if ((replaceBlend == REPLACE_BLEND_COPY_FBO) || gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT)) {
			WRITE(p, "  vec4 pixelPos : SV_POSITION;\n");
		}
		WRITE(p, "};\n");

		WRITE(p, "struct PS_OUT {\n");
		if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
			WRITE(p, "  vec4 target : SV_Target0;\n");
			WRITE(p, "  vec4 target1 : SV_Target1;\n");
		} else {
			WRITE(p, "  vec4 target : SV_Target;\n");
		}
		if (gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT)) {
			WRITE(p, "  float depth : SV_DEPTH;\n");
		}
		WRITE(p, "};\n");
	} else {
		if (shaderDepal && gl_extensions.IsGLES) {
			WRITE(p, "precision highp int;\n");
		}

		if (doTexture)
			WRITE(p, "uniform sampler2D tex;\n");

		if (!isModeClear && replaceBlend > REPLACE_BLEND_STANDARD) {
			*uniformMask |= DIRTY_SHADERBLEND;
			if (!gstate_c.Supports(GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH) && replaceBlend == REPLACE_BLEND_COPY_FBO) {
				if (!compat.texelFetch) {
					WRITE(p, "uniform vec2 u_fbotexSize;\n");
				}
				WRITE(p, "uniform sampler2D fbotex;\n");
			}
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
			WRITE(p, "uniform int u_depal_mask_shift_off_fmt;\n");
			*uniformMask |= DIRTY_DEPAL;
		}

		if (stencilToAlpha && replaceAlphaWithStencilType == STENCIL_VALUE_UNIFORM) {
			*uniformMask |= DIRTY_STENCILREPLACEVALUE;
			WRITE(p, "uniform float u_stencilReplaceValue;\n");
		}
		if (doTexture && texFunc == GE_TEXFUNC_BLEND) {
			*uniformMask |= DIRTY_TEXENV;
			WRITE(p, "uniform vec3 u_texenv;\n");
		}

		WRITE(p, "%s %s vec4 v_color0;\n", shading, compat.varying_fs);
		if (lmode)
			WRITE(p, "%s %s vec3 v_color1;\n", shading, compat.varying_fs);
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
			if (compat.lastFragData && !strcmp(compat.lastFragData, compat.fragColor0)) {
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

	// PowerVR needs a custom modulo function. For some reason, this has far higher precision than the builtin one.
	if ((gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) && needShaderTexClamp) {
		WRITE(p, "float mymod(float a, float b) { return a - b * floor(a / b); }\n");
	}

	if (compat.d3d11) {
		WRITE(p, "%s", hlsl_late_preamble);
		WRITE(p, "PS_OUT main( PS_IN In ) {\n");
		WRITE(p, "  PS_OUT outfragment;\n");
	} else {
		WRITE(p, "void main() {\n");
	}
	if (isModeClear) {
		// Clear mode does not allow any fancy shading.
		WRITE(p, "  vec4 v = %sv_color0;\n", compat.inPrefix);
	} else {
		const char *secondary = "";
		// Secondary color for specular on top of texture
		if (lmode) {
			WRITE(p, "  vec4 s = vec4(%sv_color1, 0.0);\n", compat.inPrefix);
			secondary = " + s";
		} else {
			secondary = "";
		}

		if (doTexture) {
			std::string texcoord = std::string(compat.inPrefix) + "v_texcoord";
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
				std::string ucoord = std::string(compat.inPrefix) + "v_texcoord.x";
				std::string vcoord = std::string(compat.inPrefix) + "v_texcoord.y";
				if (doTextureProjection) {
					ucoord = StringFromFormat("(%sv_texcoord.x / %sv_texcoord.z)", compat.inPrefix, compat.inPrefix);
					vcoord = StringFromFormat("(%sv_texcoord.y / %sv_texcoord.z)", compat.inPrefix, compat.inPrefix);
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
				texcoord = "fixedcoord";
				// We already projected it.
				doTextureProjection = false;
			}

			if (!shaderDepal) {
				if (compat.d3d11) {
					if (doTextureProjection) {
						WRITE(p, "  vec4 t = tex.Sample(samp, %sv_texcoord.xy / %sv_texcoord.z)%s;\n", compat.inPrefix, compat.inPrefix, bgraTexture ? ".bgra" : "");
					} else {
						WRITE(p, "  vec4 t = tex.Sample(samp, %s.xy)%s;\n", texcoord.c_str(), bgraTexture ? ".bgra" : "");
					}
				} else {
					if (doTextureProjection) {
						WRITE(p, "  vec4 t = %sProj(tex, %s);\n", compat.texture, texcoord.c_str());
					} else {
						WRITE(p, "  vec4 t = %s(tex, %s.xy);\n", compat.texture, texcoord.c_str());
					}
				}
			} else {
				if (doTextureProjection) {
					// We don't use textureProj because we need better control and it's probably not much of a savings anyway.
					// However it is good for precision on older hardware like PowerVR.
					WRITE(p, "  vec2 uv = %s.xy/%s.z;\n  vec2 uv_round;\n", texcoord.c_str(), texcoord.c_str());
				} else {
					WRITE(p, "  vec2 uv = %s.xy;\n  vec2 uv_round;\n", texcoord.c_str());
				}
				WRITE(p, "  vec2 tsize = vec2(textureSize(tex, 0));\n");
				WRITE(p, "  vec2 fraction;\n");
				WRITE(p, "  bool bilinear = (u_depal_mask_shift_off_fmt >> 31) != 0;\n");
				WRITE(p, "  if (bilinear) {\n");
				WRITE(p, "    uv_round = uv * tsize - vec2(0.5, 0.5);\n");
				WRITE(p, "    fraction = fract(uv_round);\n");
				WRITE(p, "    uv_round = (uv_round - fraction + vec2(0.5, 0.5)) / tsize;\n");  // We want to take our four point samples at pixel centers.
				WRITE(p, "  } else {\n");
				WRITE(p, "    uv_round = uv;\n");
				WRITE(p, "  }\n");
				WRITE(p, "  vec4 t = %s(tex, uv_round);\n", compat.texture);
				WRITE(p, "  vec4 t1 = %sOffset(tex, uv_round, ivec2(1, 0));\n", compat.texture);
				WRITE(p, "  vec4 t2 = %sOffset(tex, uv_round, ivec2(0, 1));\n", compat.texture);
				WRITE(p, "  vec4 t3 = %sOffset(tex, uv_round, ivec2(1, 1));\n", compat.texture);
				WRITE(p, "  uint depalMask = (u_depal_mask_shift_off_fmt & 0xFF);\n");
				WRITE(p, "  uint depalShift = (u_depal_mask_shift_off_fmt >> 8) & 0xFF;\n");
				WRITE(p, "  uint depalOffset = ((u_depal_mask_shift_off_fmt >> 16) & 0xFF) << 4;\n");
				WRITE(p, "  uint depalFmt = (u_depal_mask_shift_off_fmt >> 24) & 0x3;\n");
				WRITE(p, "  uvec4 col; uint index0; uint index1; uint index2; uint index3;\n");
				WRITE(p, "  switch (depalFmt) {\n");  // We might want to include fmt in the shader ID if this is a performance issue.
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
				WRITE(p, "  vec4 p = %sv_color0;\n", compat.inPrefix);

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
			WRITE(p, "  vec4 v = %sv_color0 %s;\n", compat.inPrefix, secondary);
		}

		if (enableFog) {
			WRITE(p, "  float fogCoef = clamp(%sv_fogdepth, 0.0, 1.0);\n", compat.inPrefix);
			WRITE(p, "  v = mix(vec4(u_fogcolor, v.a), v, fogCoef);\n");
			// WRITE(p, "  v.x = v_depth;\n");
		}

		// Texture access is at half texels [0.5/256, 255.5/256], but colors are normalized [0, 255].
		// So we have to scale to account for the difference.
		std::string alphaTestXCoord = "0";
		if (enableFragmentTestCache) {
			if (enableColorTest && !colorTestAgainstZero) {
				WRITE(p, "  vec4 vScale256 = v * %f + %f;\n", 255.0 / 256.0, 0.5 / 256.0);
				alphaTestXCoord = "vScale256.a";
			} else if (enableAlphaTest && !alphaTestAgainstZero) {
				char temp[64];
				snprintf(temp, sizeof(temp), "v.a * %f + %f", 255.0 / 256.0, 0.5 / 256.0);
				alphaTestXCoord = temp;
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
				WRITE(p, "  float aResult = %s(testtex, vec2(%s, 0)).a;\n", compat.texture, alphaTestXCoord.c_str());
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
					WRITE(p, "  if (v.r < 0.002 && v.g < 0.002 && v.b < 0.002) %s\n", discardStatement);
				} else if (colorTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.
					WRITE(p, "  if (v.r > 0.002 || v.g > 0.002 || v.b > 0.002) %s\n", discardStatement);
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
					if (compat.bitwiseOps) {
						// Apparently GLES3 does not support vector bitwise ops.
						WRITE(p, "  ivec3 v_scaled = roundAndScaleTo255iv(v.rgb);\n");
						if (compat.vulkan) {
							// TODO: Use this for GL as well?
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

		if (replaceBlend == REPLACE_BLEND_PRE_SRC || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
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
			// If we have NV_shader_framebuffer_fetch / EXT_shader_framebuffer_fetch, we skip the blit.
			// We can just read the prev value more directly.
			if (gstate_c.Supports(GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH)) {
				WRITE(p, "  lowp vec4 destColor = %s;\n", compat.lastFragData);
			} else if (!compat.texelFetch) {
				WRITE(p, "  lowp vec4 destColor = %s(fbotex, gl_FragCoord.xy * u_fbotexSize.xy);\n", compat.texture);
			} else {
				WRITE(p, "  lowp vec4 destColor = %s(fbotex, ivec2(gl_FragCoord.x, gl_FragCoord.y), 0);\n", compat.texelFetch);
			}

			const char *srcFactor = "splat3(1.0)";
			const char *dstFactor = "splat3(0.0)";

			switch (replaceBlendFuncA) {
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "destColor.rgb"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "(splat3(1.0) - destColor.rgb)"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "splat3(v.a)"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "splat3(1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "splat3(destColor.a)"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "splat3(1.0 - destColor.a)"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "splat3(v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "splat3(1.0 - v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "splat3(destColor.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "splat3(1.0 - destColor.a * 2.0)"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			default:                            srcFactor = "u_blendFixA"; break;
			}
			switch (replaceBlendFuncB) {
			case GE_DSTBLEND_SRCCOLOR:          dstFactor = "v.rgb"; break;
			case GE_DSTBLEND_INVSRCCOLOR:       dstFactor = "(splat3(1.0) - v.rgb)"; break;
			case GE_DSTBLEND_SRCALPHA:          dstFactor = "splat3(v.a)"; break;
			case GE_DSTBLEND_INVSRCALPHA:       dstFactor = "splat3(1.0 - v.a)"; break;
			case GE_DSTBLEND_DSTALPHA:          dstFactor = "splat3(destColor.a)"; break;
			case GE_DSTBLEND_INVDSTALPHA:       dstFactor = "splat3(1.0 - destColor.a)"; break;
			case GE_DSTBLEND_DOUBLESRCALPHA:    dstFactor = "splat3(v.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEINVSRCALPHA: dstFactor = "splat3(1.0 - v.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEDSTALPHA:    dstFactor = "splat3(destColor.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEINVDSTALPHA: dstFactor = "splat3(1.0 - destColor.a * 2.0)"; break;
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
			WRITE(p, "  v.a = v.a * 2.0;\n");
		}
	}

	std::string replacedAlpha = "0.0";
	char replacedAlphaTemp[64] = "";
	if (stencilToAlpha != REPLACE_ALPHA_NO) {
		switch (replaceAlphaWithStencilType) {
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
		WRITE(p, "  %s = vec4(v.rgb, %s);\n", compat.fragColor0, replacedAlpha.c_str());
		WRITE(p, "  %s = vec4(0.0, 0.0, 0.0, v.a);\n", compat.fragColor1);
		break;

	case REPLACE_ALPHA_YES:
		WRITE(p, "  %s = vec4(v.rgb, %s);\n", compat.fragColor0, replacedAlpha.c_str());
		break;

	case REPLACE_ALPHA_NO:
		WRITE(p, "  %s = v;\n", compat.fragColor0);
		break;

	default:
		*errorString = "Bad stencil-to-alpha type, corrupt ID?";
		return false;
	}

	LogicOpReplaceType replaceLogicOpType = (LogicOpReplaceType)id.Bits(FS_BIT_REPLACE_LOGIC_OP_TYPE, 2);
	switch (replaceLogicOpType) {
	case LOGICOPTYPE_ONE:
		WRITE(p, "  %s.rgb = vec3(1.0, 1.0, 1.0);\n", compat.fragColor0);
		break;
	case LOGICOPTYPE_INVERT:
		WRITE(p, "  %s.rgb = vec3(1.0, 1.0, 1.0) - %s.rgb;\n", compat.fragColor0, compat.fragColor0);
		break;
	case LOGICOPTYPE_NORMAL:
		break;

	default:
		*errorString = "Bad logic op type, corrupt ID?";
		return false;
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
	} else if (!earlyFragmentTests && useAdrenoBugWorkaround) {
		// Adreno (and possibly MESA/others) apply early frag tests even with discard in the shader.
		// Writing depth prevents the bug, even with depth_unchanged specified.
		WRITE(p, "  gl_FragDepth = gl_FragCoord.z;\n");
	}

	if (compat.d3d11) {
		WRITE(p, "  return outfragment;\n");
	}

	WRITE(p, "}\n");

	return true;
}

