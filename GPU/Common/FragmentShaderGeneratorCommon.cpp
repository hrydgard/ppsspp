#include "GPU/Common/FragmentShaderGeneratorCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GPUState.h"

#define WRITE p+=sprintf

char *WriteReplaceBlend(char *p, const FShaderID &id, const ShaderCompat &compat) {
	ReplaceBlendType replaceBlend = static_cast<ReplaceBlendType>(id.Bits(FS_BIT_REPLACE_BLEND, 3));
	GEBlendSrcFactor replaceBlendFuncA = (GEBlendSrcFactor)id.Bits(FS_BIT_BLENDFUNC_A, 4);
	GEBlendDstFactor replaceBlendFuncB = (GEBlendDstFactor)id.Bits(FS_BIT_BLENDFUNC_B, 4);
	GEBlendMode replaceBlendEq = (GEBlendMode)id.Bits(FS_BIT_BLENDEQ, 3);

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

		WRITE(p, "  v.rgb = v.rgb * %s;\n", srcFactor);
	}

	if (replaceBlend == REPLACE_BLEND_COPY_FBO && compat.shaderLanguage != ShaderLanguage::HLSL_DX9) {
		switch (compat.shaderLanguage) {
		case ShaderLanguage::GLSL_140:
		case ShaderLanguage::GLSL_VULKAN:
		case ShaderLanguage::GLSL_300:
			if (gstate_c.Supports(GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH)) {
				WRITE(p, "  lowp vec4 destColor = %s;\n", compat.lastFragData);
			} else if (!compat.texelFetch) {
				WRITE(p, "  lowp vec4 destColor = %s(fbotex, gl_FragCoord.xy * u_fbotexSize.xy);\n", compat.texture);
			} else {
				WRITE(p, "  lowp vec4 destColor = %s(fbotex, ivec2(gl_FragCoord.x, gl_FragCoord.y), 0);\n", compat.texelFetch);
			}
			break;
		case ShaderLanguage::HLSL_D3D11:
		case ShaderLanguage::HLSL_D3D11_LEVEL9:
			WRITE(p, "  float4 destColor = fboTex.Load(int3((int)In.pixelPos.x, (int)In.pixelPos.y, 0));\n");
			break;
		}

		const char *srcFactor = "vec3(1.0)";
		const char *dstFactor = "vec3(0.0)";

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
		}
	}

	if (replaceBlend == REPLACE_BLEND_2X_ALPHA || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
		WRITE(p, "  v.a = v.a * 2.0;\n");
	}
	return p;
}

char *WriteShaderDepal(char *p, const FShaderID &id, const ShaderCompat &compat) {
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
	WRITE(p, "  int depalMask = int(u_depal_mask_shift_off_fmt & 0xFF);\n");
	WRITE(p, "  int depalShift = int((u_depal_mask_shift_off_fmt >> 8) & 0xFF);\n");
	WRITE(p, "  int depalOffset = int(((u_depal_mask_shift_off_fmt >> 16) & 0xFF) << 4);\n");
	WRITE(p, "  int depalFmt = int((u_depal_mask_shift_off_fmt >> 24) & 0x3);\n");
	WRITE(p, "  ivec4 col; int index0; int index1; int index2; int index3;\n");
	WRITE(p, "  switch (depalFmt) {\n");  // We might want to include fmt in the shader ID if this is a performance issue.
	WRITE(p, "  case 0:\n");  // 565
	WRITE(p, "    col = ivec4(t.rgb * vec3(31.99, 63.99, 31.99), 0);\n");
	WRITE(p, "    index0 = (col.b << 11) | (col.g << 5) | (col.r);\n");
	WRITE(p, "    if (bilinear) {\n");
	WRITE(p, "      col = ivec4(t1.rgb * vec3(31.99, 63.99, 31.99), 0);\n");
	WRITE(p, "      index1 = (col.b << 11) | (col.g << 5) | (col.r);\n");
	WRITE(p, "      col = ivec4(t2.rgb * vec3(31.99, 63.99, 31.99), 0);\n");
	WRITE(p, "      index2 = (col.b << 11) | (col.g << 5) | (col.r);\n");
	WRITE(p, "      col = ivec4(t3.rgb * vec3(31.99, 63.99, 31.99), 0);\n");
	WRITE(p, "      index3 = (col.b << 11) | (col.g << 5) | (col.r);\n");
	WRITE(p, "    }\n");
	WRITE(p, "    break;\n");
	WRITE(p, "  case 1:\n");  // 5551
	WRITE(p, "    col = ivec4(t.rgba * vec4(31.99, 31.99, 31.99, 1.0));\n");
	WRITE(p, "    index0 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);\n");
	WRITE(p, "    if (bilinear) {\n");
	WRITE(p, "      col = ivec4(t1.rgba * vec4(31.99, 31.99, 31.99, 1.0));\n");
	WRITE(p, "      index1 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);\n");
	WRITE(p, "      col = ivec4(t2.rgba * vec4(31.99, 31.99, 31.99, 1.0));\n");
	WRITE(p, "      index2 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);\n");
	WRITE(p, "      col = ivec4(t3.rgba * vec4(31.99, 31.99, 31.99, 1.0));\n");
	WRITE(p, "      index3 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);\n");
	WRITE(p, "    }\n");
	WRITE(p, "    break;\n");
	WRITE(p, "  case 2:\n");  // 4444
	WRITE(p, "    col = ivec4(t.rgba * vec4(15.99, 15.99, 15.99, 15.99));\n");
	WRITE(p, "    index0 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);\n");
	WRITE(p, "    if (bilinear) {\n");
	WRITE(p, "      col = ivec4(t1.rgba * vec4(15.99, 15.99, 15.99, 15.99));\n");
	WRITE(p, "      index1 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);\n");
	WRITE(p, "      col = ivec4(t2.rgba * vec4(15.99, 15.99, 15.99, 15.99));\n");
	WRITE(p, "      index2 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);\n");
	WRITE(p, "      col = ivec4(t3.rgba * vec4(15.99, 15.99, 15.99, 15.99));\n");
	WRITE(p, "      index3 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);\n");
	WRITE(p, "    }\n");
	WRITE(p, "    break;\n");
	WRITE(p, "  case 3:\n");  // 8888
	WRITE(p, "    col = ivec4(t.rgba * vec4(255.99, 255.99, 255.99, 255.99));\n");
	WRITE(p, "    index0 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);\n");
	WRITE(p, "    if (bilinear) {\n");
	WRITE(p, "      col = ivec4(t1.rgba * vec4(255.99, 255.99, 255.99, 255.99));\n");
	WRITE(p, "      index1 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);\n");
	WRITE(p, "      col = ivec4(t2.rgba * vec4(255.99, 255.99, 255.99, 255.99));\n");
	WRITE(p, "      index2 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);\n");
	WRITE(p, "      col = ivec4(t3.rgba * vec4(255.99, 255.99, 255.99, 255.99));\n");
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
	return p;
}
