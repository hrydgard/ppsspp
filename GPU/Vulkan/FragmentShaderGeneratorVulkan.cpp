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

#include "Common/StringUtils.h"
#include "base/logging.h"
#include "gfx_es2/gpu_features.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

static const char *vulkan_glsl_preamble =
	"#version 450\n"
	"#extension GL_ARB_separate_shader_objects : enable\n"
	"#extension GL_ARB_shading_language_420pack : enable\n\n";

#define WRITE p+=sprintf

// Missing: Z depth range
bool GenerateVulkanGLSLFragmentShader(const FShaderID &id, char *buffer) {
	char *p = buffer;

	const char *lastFragData = nullptr;

	WRITE(p, "%s", vulkan_glsl_preamble);

	bool lmode = id.Bit(FS_BIT_LMODE);
	bool doTexture = id.Bit(FS_BIT_DO_TEXTURE);
	bool enableFog = id.Bit(FS_BIT_ENABLE_FOG);
	bool enableAlphaTest = id.Bit(FS_BIT_ALPHA_TEST);

	bool alphaTestAgainstZero = id.Bit(FS_BIT_ALPHA_AGAINST_ZERO);
	bool enableColorTest = id.Bit(FS_BIT_COLOR_TEST);
	bool colorTestAgainstZero = id.Bit(FS_BIT_COLOR_AGAINST_ZERO);
	bool enableColorDoubling = id.Bit(FS_BIT_COLOR_DOUBLE);
	bool doTextureProjection = id.Bit(FS_BIT_DO_TEXTURE_PROJ);
	bool doTextureAlpha = id.Bit(FS_BIT_TEXALPHA);
	bool doFlatShading = id.Bit(FS_BIT_FLATSHADE);
	bool shaderDepal = id.Bit(FS_BIT_SHADER_DEPAL);

	GEComparison alphaTestFunc = (GEComparison)id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3);
	GEComparison colorTestFunc = (GEComparison)id.Bits(FS_BIT_COLOR_TEST_FUNC, 2);
	bool needShaderTexClamp = id.Bit(FS_BIT_SHADER_TEX_CLAMP);

	GETexFunc texFunc = (GETexFunc)id.Bits(FS_BIT_TEXFUNC, 3);
	bool textureAtOffset = id.Bit(FS_BIT_TEXTURE_AT_OFFSET);

	ReplaceBlendType replaceBlend = static_cast<ReplaceBlendType>(id.Bits(FS_BIT_REPLACE_BLEND, 3));
	ReplaceAlphaType stencilToAlpha = static_cast<ReplaceAlphaType>(id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2));

	GEBlendSrcFactor replaceBlendFuncA = (GEBlendSrcFactor)id.Bits(FS_BIT_BLENDFUNC_A, 4);
	GEBlendDstFactor replaceBlendFuncB = (GEBlendDstFactor)id.Bits(FS_BIT_BLENDFUNC_B, 4);
	GEBlendMode replaceBlendEq = (GEBlendMode)id.Bits(FS_BIT_BLENDEQ, 3);
	StencilValueType replaceAlphaWithStencilType = (StencilValueType)id.Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4);

	bool isModeClear = id.Bit(FS_BIT_CLEARMODE);

	const char *shading = doFlatShading ? "flat" : "";

	WRITE(p, "layout (std140, set = 0, binding = 3) uniform baseUBO {\n%s} base;\n", ub_baseStr);
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

	// PowerVR needs a custom modulo function. For some reason, this has far higher precision than the builtin one.
	if ((gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) && needShaderTexClamp) {
		WRITE(p, "float mymod(float a, float b) { return a - b * floor(a / b); }\n");
	}

	WRITE(p, "void main() {\n");
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
			const char *texcoord = "v_texcoord";
			// TODO: Not sure the right way to do this for projection.
			// This path destroys resolution on older PowerVR no matter what I do, so we disable it on SGX 540 and lesser, and live with the consequences.
			if (needShaderTexClamp && !(gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_TERRIBLE)) {
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
					ucoord = "clamp(" + ucoord + ", base.texclamp.z, base.texclamp.x - base.texclamp.z)";
				} else {
					ucoord = modulo + "(" + ucoord + ", base.texclamp.x)";
				}
				if (id.Bit(FS_BIT_CLAMP_T)) {
					vcoord = "clamp(" + vcoord + ", base.texclamp.w, base.texclamp.y - base.texclamp.w)";
				} else {
					vcoord = modulo + "(" + vcoord + ", base.texclamp.y)";
				}
				if (textureAtOffset) {
					ucoord = "(" + ucoord + " + base.texclampoff.x)";
					vcoord = "(" + vcoord + " + base.texclampoff.y)";
				}

				WRITE(p, "  vec2 fixedcoord = vec2(%s, %s);\n", ucoord.c_str(), vcoord.c_str());
				texcoord = "fixedcoord";
				// We already projected it.
				doTextureProjection = false;
			}

			if (!shaderDepal) {
				if (doTextureProjection) {
					WRITE(p, "  vec4 t = textureProj(tex, %s);\n", texcoord);
				} else {
					WRITE(p, "  vec4 t = texture(tex, %s.xy);\n", texcoord);
				}
			} else {
				if (doTextureProjection) {
					// We don't use textureProj because we need better control and it's probably not much of a savings anyway.
					WRITE(p, "  vec2 uv = %s.xy/%s.z;\n  vec2 uv_round;\n", texcoord, texcoord);
				} else {
					WRITE(p, "  vec2 uv = %s.xy;\n  vec2 uv_round;\n", texcoord);
				}
				WRITE(p, "  vec2 tsize = textureSize(tex, 0);\n");
				WRITE(p, "  vec2 fraction;\n");
				WRITE(p, "  bool bilinear = (base.depal_mask_shift_off_fmt >> 31) != 0;\n");
				WRITE(p, "  if (bilinear) {\n");
				WRITE(p, "    uv_round = uv * tsize - vec2(0.5, 0.5);\n");
				WRITE(p, "    fraction = fract(uv_round);\n");
				WRITE(p, "    uv_round = (uv_round - fraction + vec2(0.5, 0.5)) / tsize;\n");  // We want to take our four point samples at pixel centers.
				WRITE(p, "  } else {\n");
				WRITE(p, "    uv_round = uv;\n");
				WRITE(p, "  }\n");
				WRITE(p, "  vec4 t = texture(tex, uv_round);\n");
				WRITE(p, "  vec4 t1 = textureOffset(tex, uv_round, ivec2(1, 0));\n");
				WRITE(p, "  vec4 t2 = textureOffset(tex, uv_round, ivec2(0, 1));\n");
				WRITE(p, "  vec4 t3 = textureOffset(tex, uv_round, ivec2(1, 1));\n");
				WRITE(p, "  uint depalMask = (base.depal_mask_shift_off_fmt & 0xFF);\n");
				WRITE(p, "  uint depalShift = (base.depal_mask_shift_off_fmt >> 8) & 0xFF;\n");
				WRITE(p, "  uint depalOffset = ((base.depal_mask_shift_off_fmt >> 16) & 0xFF) << 4;\n");
				WRITE(p, "  uint depalFmt = (base.depal_mask_shift_off_fmt >> 24) & 0x3;\n");
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
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, base.texenv.rgb, t.rgb), p.a * t.a)%s;\n", secondary);
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
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, base.texenv.rgb, t.rgb), p.a)%s;\n", secondary);
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

		if (enableAlphaTest) {
			if (alphaTestAgainstZero) {
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
			} else {
				const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };
				if (alphaTestFuncs[alphaTestFunc][0] != '#') {
					WRITE(p, "  if ((roundAndScaleTo255i(v.a) & base.alphacolormask.a) %s base.alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
				} else {
					// This means NEVER.  See above.
					WRITE(p, "  discard;\n");
				}
			}
		}

		if (enableColorTest) {
			if (colorTestAgainstZero) {
				// When testing against 0 (common), we can avoid some math.
				// Have my doubts that this special case is actually worth it, but whatever.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (colorTestFunc == GE_COMP_NOTEQUAL) {
					WRITE(p, "  if (v.r + v.g + v.b < 0.002) discard;\n");
				} else if (colorTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.
					WRITE(p, "  if (v.r + v.g + v.b > 0.002) discard;\n");
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  discard;\n");
				}
			} else {
				const char *colorTestFuncs[] = { "#", "#", " != ", " == " };
				if (colorTestFuncs[colorTestFunc][0] != '#') {
					WRITE(p, "  ivec3 v_scaled = roundAndScaleTo255iv(v.rgb);\n");
					WRITE(p, "  if ((v_scaled & base.alphacolormask.rgb) %s (base.alphacolorref.rgb & base.alphacolormask.rgb)) discard;\n", colorTestFuncs[colorTestFunc]);
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
			WRITE(p, "  v = mix(vec4(base.fogcolor, v.a), v, fogCoef);\n");
			// WRITE(p, "  v.x = v_depth;\n");
		}

		if (replaceBlend == REPLACE_BLEND_PRE_SRC || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			const char *srcFactor = "ERROR";
			switch (replaceBlendFuncA) {
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
			case GE_SRCBLEND_FIXA:              srcFactor = "base.blendFixA"; break;
			}

			WRITE(p, "  v.rgb = v.rgb * %s;\n", srcFactor);
		}

		if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
			WRITE(p, "  lowp vec4 destColor = texelFetch(fbotex, ivec2(gl_FragCoord.x, gl_FragCoord.y), 0);\n");

			const char *srcFactor = "vec3(1.0)";
			const char *dstFactor = "vec3(0.0)";

			switch (replaceBlendFuncA) {
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
			case GE_SRCBLEND_FIXA:              srcFactor = "base.blendFixA"; break;
			}
			switch (replaceBlendFuncB) {
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
			case GE_DSTBLEND_FIXB:              dstFactor = "base.blendFixB"; break;
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
			replacedAlpha = "base.stencilReplace";
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
		WRITE(p, "  fragColor0 = vec4(v.rgb, %s);\n", replacedAlpha.c_str());
		break;

	case REPLACE_ALPHA_NO:
		WRITE(p, "  fragColor0 = v;\n");
		break;

	default:
		ERROR_LOG(G3D, "Bad stencil-to-alpha type, corrupt ID?");
		return false;
	}

	LogicOpReplaceType replaceLogicOpType = (LogicOpReplaceType)id.Bits(FS_BIT_REPLACE_LOGIC_OP_TYPE, 2);
	switch (replaceLogicOpType) {
	case LOGICOPTYPE_ONE:
		WRITE(p, "  fragColor0.rgb = vec3(1.0, 1.0, 1.0);\n");
		break;
	case LOGICOPTYPE_INVERT:
		WRITE(p, "  fragColor0.rgb = vec3(1.0, 1.0, 1.0) - fragColor0.rgb;\n");
		break;
	case LOGICOPTYPE_NORMAL:
		break;

	default:
		ERROR_LOG(G3D, "Bad logic op type, corrupt ID?");
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
	}

	WRITE(p, "}\n");

	return true;
}
