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
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Common/ShaderUniforms.h"

#define WRITE p+=sprintf

// #define DEBUG_SHADER

namespace DX9 {

static const char* sampler_default =
	"float4 tex_sample(float2 coord) {\n"
	"  return tex_sample_direct(coord);\n"
	"};\n";

static const char* sampler_hybrid = sampler_default;
static const char* sampler_bicubic = sampler_default;
static const char* sampler_hybrid_bicubic = sampler_default;
static const char* sampler_gaussian =
	"#define sharpness 1.0\n"
	"#define pi 3.14159265358\n"
	"#define normalGauss(x) ((exp(-(x)*(x)*0.5))/sqrt(2.0*pi))\n"
	"float normalGaussIntegral(float x)\n"
	"{\n"
	"	 float a1 = 0.4361836;\n"
	"	 float a2 = -0.1201676;\n"
	"	 float a3 = 0.9372980;\n"
	"	 float p = 0.3326700;\n"
	"	 float t = 1.0 / (1.0 + p*abs(x));\n"
	"\n"
	"	 return (0.5-normalGauss(x) * (t*(a1 + t*(a2 + a3*t))))*sign(x);\n"
	"}\n"
	"#define KERNEL(x,b) (normalGaussIntegral(sqrt(2*pi)*b*(x - 0.5)) - normalGaussIntegral(sqrt(2*pi)*b*(x + 0.5)))\n"
	"\n"
	"float4 tex_sample(float2 coord) {\n"
	" float2 offset = frac(coord * u_texSize.xy) - 0.5;\n"
	" float4 tempColor = 0.0;\n"
	" float4	c;\n"
	" float range = 2.0f;\n"
	" float i,j;\n"
	" float2 pos;\n"
	" for (i = -range; i < range + 2.0; i++){\n"
	"   pos.x = offset.x - i;\n"
	"   for (j = -range; j<range+2.0 ;j++){\n"
	"     pos.y = offset.y - j;\n"
	"     c=tex_sample_direct(coord - pos * u_texSize.zw).rgba;\n"
	"     tempColor+=c*KERNEL(pos.x,sharpness)*KERNEL(pos.y,sharpness);\n"
	"   }\n"
	" }\n"
	" tempColor=clamp(tempColor,0.0,1.0);\n"
	" return tempColor;\n"
	"};\n";
static const char* sampler_cosine =
	"#define sharpness 1.0\n"
	"#define pi 3.14159265358\n"
	"#define a(x) abs(x)\n"
	"#define d(x,b) (pi*b*min(a(x)+0.5,1.0/b))\n"
	"#define e(x,b) (pi*b*min(max(a(x)-0.5,-1.0/b),1.0/b))\n"
	"#define KERNEL(x,b) ((d(x,b)+sin(d(x,b))-e(x,b)-sin(e(x,b)))/(2.0*pi))\n"
	"\n"
	"float4 tex_sample(float2 coord) {\n"
	" float2 offset = frac(coord * u_texSize.xy) - 0.5;\n"
	" float4 tempColor = 0.0;\n"
	" float4	c;\n"
	" float range = 2.0f;\n"
	" float i,j;\n"
	" float2 pos;\n"
	" for (i = -range; i < range + 2.0; i++){\n"
	"   pos.x = offset.x - i;\n"
	"   for (j = -range; j<range+2.0 ;j++){\n"
	"     pos.y = offset.y - j;\n"
	"     c=tex_sample_direct(coord - pos * u_texSize.zw).rgba;\n"
	"     tempColor+=c*KERNEL(pos.x,sharpness)*KERNEL(pos.y,sharpness);\n"
	"   }\n"
	" }\n"
	" tempColor=clamp(tempColor,0.0,1.0);\n"
	" return tempColor;\n"
	"};\n";
static const char* sampler_xbrz =
	"float c_df(float4 c1, float4 c2) {\n"
	"	float4 df = abs(c1 - c2);\n"
	"	return df.r + df.g + df.b + df.a;\n"
	"}\n"
	"\n"
	"static const float coef = 2.0;\n"
	"\n"
	"static const float4  rgbw          = float4(14.352, 28.176, 5.472, 15.0);\n"
	"static const float4  eq_threshold  = float4(15.0, 15.0, 15.0, 15.0);\n"
	"\n"
	"static const float4 delta   = float4(1.0/4., 1.0/4., 1.0/4., 1.0/4.);\n"
	"static const float4 delta_l = float4(0.5/4., 1.0/4., 0.5/4., 1.0/4.);\n"
	"static const float4 delta_u = delta_l.yxwz;\n"
	"\n"
	"static const float4 Ao = float4( 1.0, -1.0, -1.0, 1.0 );\n"
	"static const float4 Bo = float4( 1.0,  1.0, -1.0,-1.0 );\n"
	"static const float4 Co = float4( 1.5,  0.5, -0.5, 0.5 );\n"
	"static const float4 Ax = float4( 1.0, -1.0, -1.0, 1.0 );\n"
	"static const float4 Bx = float4( 0.5,  2.0, -0.5,-2.0 );\n"
	"static const float4 Cx = float4( 1.0,  1.0, -0.5, 0.0 );\n"
	"static const float4 Ay = float4( 1.0, -1.0, -1.0, 1.0 );\n"
	"static const float4 By = float4( 2.0,  0.5, -2.0,-0.5 );\n"
	"static const float4 Cy = float4( 2.0,  0.0, -1.0, 0.5 );\n"
	"static const float4 Ci = float4(0.25, 0.25, 0.25, 0.25);\n"
	"\n"
	"// Difference between vector components.\n"
	"float4 df(float4 A, float4 B)\n"
	"{\n"
	"	 return float4(abs(A-B));\n"
	"}\n"
	"\n"
	"// Compare two vectors and return their components are different.\n"
	"float4 diff(float4 A, float4 B)\n"
	"{\n"
	"	 return step(0.001, df(A, B));\n"
	"}\n"
	"\n"
	"// Determine if two vector components are equal based on a threshold.\n"
	"float4 eq(float4 A, float4 B)\n"
	"{\n"
	"	 return step(df(A, B), 15.);\n"
	"}\n"
	"\n"
	"// Determine if two vector components are NOT equal based on a threshold.\n"
	"float4 neq(float4 A, float4 B)\n"
	"{\n"
	"	 return (float4(1.0, 1.0, 1.0, 1.0) - eq(A, B));\n"
	"}\n"
	"\n"
	"// Weighted distance.\n"
	"float4 wd(float4 a, float4 b, float4 c, float4 d, float4 e, float4 f, float4 g, float4 h)\n"
	"{\n"
	"	 return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h));\n"
	"}\n"
	"\n"
	"float4 tex_sample(float2 coord)\n"
	"{\n"
	"	float2 tc = coord * u_texSize.xy;\n"
	"	float2 fp = frac(tc);\n"
	"	tc = floor(tc) + 0.5;\n"
	"\n"
	"	float4 xyp_1_2_3    = tc.xxxy + float4(-1.,  0., 1., -2.);\n"
	"	float4 xyp_6_7_8    = tc.xxxy + float4(-1.,  0., 1., -1.);\n"
	"	float4 xyp_11_12_13 = tc.xxxy + float4(-1.,  0., 1.,  0.);\n"
	"	float4 xyp_16_17_18 = tc.xxxy + float4(-1.,  0., 1.,  1.);\n"
	"	float4 xyp_21_22_23 = tc.xxxy + float4(-1.,  0., 1.,  2.);\n"
	"	float4 xyp_5_10_15  = tc.xyyy + float4(-2., -1., 0.,  1.);\n"
	"	float4 xyp_9_14_9   = tc.xyyy + float4( 2., -1., 0.,  1.);\n"
	"\n"
	"	float4 edri;\n"
	"	float4 edr;\n"
	"	float4 edr_l;\n"
	"	float4 edr_u;\n"
	"	float4 px; // px = pixel, edr = edge detection rule\n"
	"	float4 irlv0;\n"
	"	float4 irlv1;\n"
	"	float4 irlv2l;\n"
	"	float4 irlv2u;\n"
	"	float4 block_3d;\n"
	"	float4 fx;\n"
	"	float4 fx_l;\n"
	"	float4 fx_u; // inequations of straight lines.\n"
	"\n"
	"\n"
	"	float4 A1 = tex_sample_direct(u_texSize.zw * xyp_1_2_3.xw    );\n"
	"	float4 B1 = tex_sample_direct(u_texSize.zw * xyp_1_2_3.yw    );\n"
	"	float4 C1 = tex_sample_direct(u_texSize.zw * xyp_1_2_3.zw    );\n"
	"	float4 A  = tex_sample_direct(u_texSize.zw * xyp_6_7_8.xw    );\n"
	"	float4 B  = tex_sample_direct(u_texSize.zw * xyp_6_7_8.yw    );\n"
	"	float4 C  = tex_sample_direct(u_texSize.zw * xyp_6_7_8.zw    );\n"
	"	float4 D  = tex_sample_direct(u_texSize.zw * xyp_11_12_13.xw );\n"
	"	float4 E  = tex_sample_direct(u_texSize.zw * xyp_11_12_13.yw );\n"
	"	float4 F  = tex_sample_direct(u_texSize.zw * xyp_11_12_13.zw );\n"
	"	float4 G  = tex_sample_direct(u_texSize.zw * xyp_16_17_18.xw );\n"
	"	float4 H  = tex_sample_direct(u_texSize.zw * xyp_16_17_18.yw );\n"
	"	float4 I  = tex_sample_direct(u_texSize.zw * xyp_16_17_18.zw );\n"
	"	float4 G5 = tex_sample_direct(u_texSize.zw * xyp_21_22_23.xw );\n"
	"	float4 H5 = tex_sample_direct(u_texSize.zw * xyp_21_22_23.yw );\n"
	"	float4 I5 = tex_sample_direct(u_texSize.zw * xyp_21_22_23.zw );\n"
	"	float4 A0 = tex_sample_direct(u_texSize.zw * xyp_5_10_15.xy  );\n"
	"	float4 D0 = tex_sample_direct(u_texSize.zw * xyp_5_10_15.xz  );\n"
	"	float4 G0 = tex_sample_direct(u_texSize.zw * xyp_5_10_15.xw  );\n"
	"	float4 C4 = tex_sample_direct(u_texSize.zw * xyp_9_14_9.xy   );\n"
	"	float4 F4 = tex_sample_direct(u_texSize.zw * xyp_9_14_9.xz   );\n"
	"	float4 I4 = tex_sample_direct(u_texSize.zw * xyp_9_14_9.xw   );\n"
	"\n"
	"	float4 b  = float4(dot(B ,rgbw), dot(D ,rgbw), dot(H ,rgbw), dot(F ,rgbw));\n"
	"	float4 c  = float4(dot(C ,rgbw), dot(A ,rgbw), dot(G ,rgbw), dot(I ,rgbw));\n"
	"	float4 d  = b.yzwx;\n"
	"	float4 e  = dot(E,rgbw).xxxx;\n"
	"	float4 f  = b.wxyz;\n"
	"	float4 g  = c.zwxy;\n"
	"	float4 h  = b.zwxy;\n"
	"	float4 i  = c.wxyz;\n"
	"	float4 i4 = float4(dot(I4,rgbw), dot(C1,rgbw), dot(A0,rgbw), dot(G5,rgbw));\n"
	"	float4 i5 = float4(dot(I5,rgbw), dot(C4,rgbw), dot(A1,rgbw), dot(G0,rgbw));\n"
	"	float4 h5 = float4(dot(H5,rgbw), dot(F4,rgbw), dot(B1,rgbw), dot(D0,rgbw));\n"
	"	float4 f4 = h5.yzwx;\n"
	"\n"
	"	// These inequations define the line below which interpolation occurs.\n"
	"	fx   = (Ao*fp.y+Bo*fp.x);\n"
	"	fx_l = (Ax*fp.y+Bx*fp.x);\n"
	"	fx_u = (Ay*fp.y+By*fp.x);\n"
	"\n"
	"	irlv1 = irlv0 = diff(e,f) * diff(e,h);\n"
	"\n"
	"	irlv1     = (irlv0  * ( neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c)) );\n"
	"\n"
	"	irlv2l = diff(e,g) * diff(d,g);\n"
	"	irlv2u = diff(e,c) * diff(b,c);\n"
	"\n"
	"	float4 fx45i = clamp((fx   + delta   -Co - Ci)/(2.0*delta  ), 0.0, 1.0);\n"
	"	float4 fx45  = clamp((fx   + delta   -Co     )/(2.0*delta  ), 0.0, 1.0);\n"
	"	float4 fx30  = clamp((fx_l + delta_l -Cx     )/(2.0*delta_l), 0.0, 1.0);\n"
	"	float4 fx60  = clamp((fx_u + delta_u -Cy     )/(2.0*delta_u), 0.0, 1.0);\n"
	"\n"
	"	float4 wd1 = wd( e, c,  g, i, h5, f4, h, f);\n"
	"	float4 wd2 = wd( h, d, i5, f, i4,  b, e, i);\n"
	"\n"
	"	edri  = step(wd1, wd2) * irlv0;\n"
	"	edr   = step(wd1 + float4(0.1, 0.1, 0.1, 0.1), wd2) * step(float4(0.5, 0.5, 0.5, 0.5), irlv1);\n"
	"	edr_l = step( 2.*df(f,g), df(h,c) ) * irlv2l * edr;\n"
	"	edr_u = step( 2.*df(h,c), df(f,g) ) * irlv2u * edr;\n"
	"\n"
	"	fx45  = edr   * fx45;\n"
	"	fx30  = edr_l * fx30;\n"
	"	fx60  = edr_u * fx60;\n"
	"	fx45i = edri  * fx45i;\n"
	"\n"
	"	px = step(df(e,f), df(e,h));\n"
	"\n"
	"	float4 maximos = max(max(fx30, fx60), max(fx45, fx45i));\n"
	"\n"
	"	float4 res1 = E;\n"
	"	res1 = lerp(res1, lerp(H, F, px.x), maximos.x);\n"
	"	res1 = lerp(res1, lerp(B, D, px.z), maximos.z);\n"
	"\n"
	"	float4 res2 = E;\n"
	"	res2 = lerp(res2, lerp(F, B, px.y), maximos.y);\n"
	"	res2 = lerp(res2, lerp(D, H, px.w), maximos.w);\n"
	"\n"
	"	return lerp(res1, res2, step(c_df(E, res1), c_df(E, res2)));\n"
	"}\n";


// Missing: Z depth range
// Also, logic ops etc, of course, as they are not supported in DX9.
bool GenerateFragmentShaderHLSL(const FShaderID &id, char *buffer, ShaderLanguage lang) {
	char *p = buffer;

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
	bool isModeClear = id.Bit(FS_BIT_CLEARMODE);

	bool bgraTexture = id.Bit(FS_BIT_BGRA_TEXTURE);

	GEComparison alphaTestFunc = (GEComparison)id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3);
	GEComparison colorTestFunc = (GEComparison)id.Bits(FS_BIT_COLOR_TEST_FUNC, 2);
	bool needShaderTexClamp = id.Bit(FS_BIT_SHADER_TEX_CLAMP);

	ReplaceBlendType replaceBlend = static_cast<ReplaceBlendType>(id.Bits(FS_BIT_REPLACE_BLEND, 3));
	ReplaceAlphaType stencilToAlpha = static_cast<ReplaceAlphaType>(id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2));

	GETexFunc texFunc = (GETexFunc)id.Bits(FS_BIT_TEXFUNC, 3);
	bool textureAtOffset = id.Bit(FS_BIT_TEXTURE_AT_OFFSET);

	GEBlendSrcFactor replaceBlendFuncA = (GEBlendSrcFactor)id.Bits(FS_BIT_BLENDFUNC_A, 4);
	GEBlendDstFactor replaceBlendFuncB = (GEBlendDstFactor)id.Bits(FS_BIT_BLENDFUNC_B, 4);
	GEBlendMode replaceBlendEq = (GEBlendMode)id.Bits(FS_BIT_BLENDEQ, 3);

	StencilValueType replaceAlphaWithStencilType = (StencilValueType)id.Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4);

	if (lang == HLSL_DX9) {
		if (doTexture) {
			WRITE(p, "sampler tex : register(s0);\n");
			WRITE(p, "float4 u_texSize : register(c%i);\n", CONST_PS_TEXSIZE);
		}
		if (!isModeClear && replaceBlend > REPLACE_BLEND_STANDARD) {
			if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
				WRITE(p, "float2 u_fbotexSize : register(c%i);\n", CONST_PS_FBOTEXSIZE);
				WRITE(p, "sampler fbotex : register(s1);\n");
			}
			if (replaceBlendFuncA >= GE_SRCBLEND_FIXA) {
				WRITE(p, "float3 u_blendFixA : register(c%i);\n", CONST_PS_BLENDFIXA);
			}
			if (replaceBlendFuncB >= GE_DSTBLEND_FIXB) {
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
		WRITE(p, "Texture2D<float4> tex : register(t0);\n");
		if (!isModeClear && replaceBlend > REPLACE_BLEND_STANDARD) {
			if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
				// No sampler required, we Load
				WRITE(p, "Texture2D<float4> fboTex : register(t1);\n");
			}
		}
		WRITE(p, "cbuffer base : register(b0) {\n%s};\n", cb_baseStr);
	}

	if (enableAlphaTest) {
		if (lang == HLSL_D3D11) {
			WRITE(p, "int roundAndScaleTo255i(float x) { return int(floor(x * 255.0f + 0.5f)); }\n");
		} else {
			// D3D11 level 9 gets to take this path.
			WRITE(p, "float roundAndScaleTo255f(float x) { return floor(x * 255.0f + 0.5f); }\n");
		}
	}
	if (enableColorTest) {
		if (lang == HLSL_D3D11) {
			WRITE(p, "uint3 roundAndScaleTo255iv(float3 x) { return uint3(floor(x * 255.0f + 0.5f)); }\n");
		} else {
			WRITE(p, "float3 roundAndScaleTo255v(float3 x) { return floor(x * 255.0f + 0.5f); }\n");
		}
	}

	WRITE(p, "struct PS_IN {\n");
	if (doTexture) {
		WRITE(p, "  float3 v_texcoord: TEXCOORD0;\n");
	}
	WRITE(p, "  float4 v_color0: COLOR0;\n");
	if (lmode) {
		WRITE(p, "  float3 v_color1: COLOR1;\n");
	}
	if (enableFog) {
		WRITE(p, "  float v_fogdepth: TEXCOORD1;\n");
	}
	if ((lang == HLSL_D3D11 || lang == HLSL_D3D11_LEVEL9) && ((replaceBlend == REPLACE_BLEND_COPY_FBO) || gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT))) {
		WRITE(p, "  float4 pixelPos : SV_POSITION;\n");
	}
	WRITE(p, "};\n");

	if (!isModeClear && doTexture) {
		WRITE(p, "float4 tex_sample_direct(float2 coord) {\n");

		if (lang == HLSL_D3D11 || lang == HLSL_D3D11_LEVEL9) {
				WRITE(p, "  return tex.Sample(samp, coord)%s;\n", bgraTexture ? ".bgra" : "");
		} else {
				WRITE(p, "  return tex2D(tex, coord)%s;\n", bgraTexture ? ".bgra" : "");
		}
		WRITE(p, "};\n");

		if(g_Config.bRealtimeTexScaling && g_Config.iTexScalingLevel != 1) {
			switch (g_Config.iTexScalingType) {
			case TextureScalerCommon::XBRZ:
				WRITE(p, sampler_xbrz);
				break;
			case TextureScalerCommon::HYBRID:
				WRITE(p, sampler_hybrid);
				break;
			case TextureScalerCommon::BICUBIC:
				WRITE(p, sampler_bicubic);
				break;
			case TextureScalerCommon::HYBRID_BICUBIC:
				WRITE(p, sampler_hybrid_bicubic);
				break;
			case TextureScalerCommon::GAUSSIAN:
				WRITE(p, sampler_gaussian);
				break;
			case TextureScalerCommon::COSINE:
				WRITE(p, sampler_cosine);
				break;
			default:
				ERROR_LOG(G3D, "Unknown scaling type: %d", g_Config.iTexScalingType);
				break;
			}
		} else {
			WRITE(p, sampler_default);
		}
	}

	if (lang == HLSL_DX9) {
		WRITE(p, "float4 main( PS_IN In ) : COLOR {\n");
	} else {
		WRITE(p, "struct PS_OUT {\n");
		if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
			WRITE(p, "  float4 target : SV_Target0;\n");
			WRITE(p, "  float4 target1 : SV_Target1;\n");
		}
		else {
			WRITE(p, "  float4 target : SV_Target;\n");
		}
		if (gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT)) {
			WRITE(p, "  float depth : SV_DEPTH;\n");
		}
		WRITE(p, "};\n");
		WRITE(p, "PS_OUT main( PS_IN In ) {\n");
		WRITE(p, "  PS_OUT outfragment;\n");
	}

	if (isModeClear) {
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

		if (doTexture) {
			const char *texcoord = "In.v_texcoord.xy";
			if (doTextureProjection) {
				texcoord = "In.v_texcoord.xy / In.v_texcoord.z";
			}
			// TODO: Not sure the right way to do this for projection.
			if (needShaderTexClamp) {
				// We may be clamping inside a larger surface (tex = 64x64, buffer=480x272).
				// We may also be wrapping in such a surface, or either one in a too-small surface.
				// Obviously, clamping to a smaller surface won't work.  But better to clamp to something.
				std::string ucoord = "In.v_texcoord.x";
				std::string vcoord = "In.v_texcoord.y";
				if (doTextureProjection) {
					ucoord = "(In.v_texcoord.x / In.v_texcoord.z)";
					vcoord = "(In.v_texcoord.y / In.v_texcoord.z)";
				}

				if (id.Bit(FS_BIT_CLAMP_S)) {
					ucoord = "clamp(" + ucoord + ", u_texclamp.z, u_texclamp.x - u_texclamp.z)";
				} else {
					ucoord = "fmod(" + ucoord + ", u_texclamp.x)";
				}
				if (id.Bit(FS_BIT_CLAMP_T)) {
					vcoord = "clamp(" + vcoord + ", u_texclamp.w, u_texclamp.y - u_texclamp.w)";
				} else {
					vcoord = "fmod(" + vcoord + ", u_texclamp.y)";
				}
				if (textureAtOffset) {
					ucoord = "(" + ucoord + " + u_texclampoff.x)";
					vcoord = "(" + vcoord + " + u_texclampoff.y)";
				}

				WRITE(p, "  float2 fixedcoord = float2(%s, %s);\n", ucoord.c_str(), vcoord.c_str());
				texcoord = "fixedcoord";
			}

			WRITE(p, "  float4 t = tex_sample(%s);\n", texcoord);
			WRITE(p, "  float4 p = In.v_color0;\n");

			if (doTextureAlpha) { // texfmt == RGBA
				switch (texFunc) {
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
				switch (texFunc) {
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

		if (enableAlphaTest) {
			if (alphaTestAgainstZero) {
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
				const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };	// never/always don't make sense
				if (alphaTestFuncs[alphaTestFunc][0] != '#') {
					// TODO: Rewrite this to use clip() appropriately (like, clip(v.a - u_alphacolorref.a))
					if (lang == HLSL_D3D11) {
						WRITE(p, "  if ((roundAndScaleTo255i(v.a) & u_alphacolormask.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
					} else {
						WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) clip(-1);\n", alphaTestFuncs[alphaTestFunc]);
					}
				} else {
					// This means NEVER.  See above.
					WRITE(p, lang == HLSL_DX9 ? "  clip(-1);\n" : "  discard;\n");
				}
			}
		}
		if (enableColorTest) {
			if (colorTestAgainstZero) {
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
					WRITE(p, lang == HLSL_DX9 ? "  clip(-1);\n" : "  discard;\n");
				}
			} else {
				const char *colorTestFuncs[] = { "#", "#", " != ", " == " };	// never/always don't make sense
				if (colorTestFuncs[colorTestFunc][0] != '#') {
					const char * test = colorTestFuncs[colorTestFunc];
					if (lang == HLSL_D3D11) {
						WRITE(p, "  uint3 v_scaled = roundAndScaleTo255iv(v.rgb);\n");
						WRITE(p, "  if ((v_scaled & u_alphacolormask.rgb) %s (u_alphacolorref.rgb & u_alphacolormask.rgb)) discard;\n", colorTestFuncs[colorTestFunc]);
					} else {
						WRITE(p, "  float3 colortest = roundAndScaleTo255v(v.rgb);\n");
						WRITE(p, "  if ((colortest.r %s u_alphacolorref.r) && (colortest.g %s u_alphacolorref.g) && (colortest.b %s u_alphacolorref.b )) clip(-1);\n", test, test, test);
					}
				}
				else {
					WRITE(p, lang == HLSL_DX9 ? "  clip(-1);\n" : "  discard;\n");
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
			WRITE(p, "  float fogCoef = clamp(In.v_fogdepth, 0.0, 1.0);\n");
			WRITE(p, "  v = lerp(float4(u_fogcolor, v.a), v, fogCoef);\n");
		}

		if (replaceBlend == REPLACE_BLEND_PRE_SRC || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			const char *srcFactor = "ERROR";
			switch (replaceBlendFuncA) {
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "float3(v.a, v.a, v.a)"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "float3(1.0 - v.a, 1.0 - v.a, 1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "float3(v.a * 2.0, v.a * 2.0, v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "float3(1.0 - v.a * 2.0, 1.0 - v.a * 2.0, 1.0 - v.a * 2.0)"; break;
			// PRE_SRC for REPLACE_BLEND_PRE_SRC_2X_ALPHA means "double the src."
			// It's close to the same, but clamping can still be an issue.
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "float3(2.0, 2.0, 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "ERROR"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			default:                            srcFactor = "u_blendFixA"; break;
			}

			WRITE(p, "  v.rgb = v.rgb * %s;\n", srcFactor);
		}

		if ((lang == HLSL_D3D11 || lang == HLSL_D3D11_LEVEL9) && replaceBlend == REPLACE_BLEND_COPY_FBO) {
			WRITE(p, "  float4 destColor = fboTex.Load(int3((int)In.pixelPos.x, (int)In.pixelPos.y, 0));\n");

			const char *srcFactor = "float3(1.0)";
			const char *dstFactor = "float3(0.0)";

			switch (replaceBlendFuncA) {
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "destColor.rgb"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "(float3(1.0, 1.0, 1.0) - destColor.rgb)"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "v.aaa"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "float3(1.0, 1.0, 1.0) - v.aaa"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "float3(destColor.aaa)"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "float3(1.0, 1.0, 1.0) - destColor.aaa"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "v.aaa * 2.0"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "float3(1.0, 1.0, 1.0) - v.aaa * 2.0"; break;
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "destColor.aaa * 2.0"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "float3(1.0, 1.0, 1.0) - destColor.aaa * 2.0"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			default:                            srcFactor = "u_blendFixA"; break;
			}
			switch (replaceBlendFuncB) {
			case GE_DSTBLEND_SRCCOLOR:          dstFactor = "v.rgb"; break;
			case GE_DSTBLEND_INVSRCCOLOR:       dstFactor = "(float3(1.0, 1.0, 1.0) - v.rgb)"; break;
			case GE_DSTBLEND_SRCALPHA:          dstFactor = "v.aaa"; break;
			case GE_DSTBLEND_INVSRCALPHA:       dstFactor = "float3(1.0, 1.0, 1.0) - v.aaa"; break;
			case GE_DSTBLEND_DSTALPHA:          dstFactor = "destColor.aaa"; break;
			case GE_DSTBLEND_INVDSTALPHA:       dstFactor = "float3(1.0, 1.0, 1.0) - destColor.aaa"; break;
			case GE_DSTBLEND_DOUBLESRCALPHA:    dstFactor = "v.aaa * 2.0"; break;
			case GE_DSTBLEND_DOUBLEINVSRCALPHA: dstFactor = "float3(1.0, 1.0, 1.0) - v.aaa * 2.0"; break;
			case GE_DSTBLEND_DOUBLEDSTALPHA:    dstFactor = "destColor.aaa * 2.0"; break;
			case GE_DSTBLEND_DOUBLEINVDSTALPHA: dstFactor = "float3(1.0, 1.0, 1.0) - destColor.aaa * 2.0"; break;
			case GE_DSTBLEND_FIXB:              dstFactor = "u_blendFixB"; break;
			default:                            srcFactor = "u_blendFixB"; break;
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

		// Can do REPLACE_BLEND_COPY_FBO in ps_2_0, but need to apply viewport in the vertex shader
		// so that we can have the output position here to sample the texture at.

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
	case REPLACE_ALPHA_YES:
		WRITE(p, "  v.a = %s;\n", replacedAlpha.c_str());
		break;

	case REPLACE_ALPHA_NO:
		// Do nothing, v is already fine.
		break;
	}

	LogicOpReplaceType replaceLogicOpType = (LogicOpReplaceType)id.Bits(FS_BIT_REPLACE_LOGIC_OP_TYPE, 2);
	switch (replaceLogicOpType) {
	case LOGICOPTYPE_ONE:
		WRITE(p, "  v.rgb = float3(1.0, 1.0, 1.0);\n");
		break;
	case LOGICOPTYPE_INVERT:
		WRITE(p, "  v.rgb = float3(1.0, 1.0, 1.0) - v.rgb;\n");
		break;
	case LOGICOPTYPE_NORMAL:
		break;
	}

	if (gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT)) {
		const double scale = DepthSliceFactor() * 65535.0;

		WRITE(p, "  float z = In.pixelPos.z;\n");
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
		WRITE(p, "  outfragment.depth = z;\n");
	}

	if (lang == HLSL_D3D11 || lang == HLSL_D3D11_LEVEL9) {
		if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
			WRITE(p, "  outfragment.target = float4(v.rgb, %s);\n", replacedAlpha.c_str());
			WRITE(p, "  outfragment.target1 = float4(0.0, 0.0, 0.0, v.a);\n");
			WRITE(p, "  return outfragment;\n");
		}
		else {
			WRITE(p, "  outfragment.target = v;\n");
			WRITE(p, "  return outfragment;\n");
		}
	} else {
		WRITE(p, "  return v;\n");
	}
	WRITE(p, "}\n");
	return true;
}

};
