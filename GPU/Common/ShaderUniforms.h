#pragma once

#include <cstdint>

#include "ShaderCommon.h"

// Used by the "modern" backends that use uniform buffers. They can share this without issue.

enum : uint64_t {
	DIRTY_BASE_UNIFORMS =
	DIRTY_WORLDMATRIX | DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWMATRIX | DIRTY_TEXMATRIX | DIRTY_ALPHACOLORREF |
	DIRTY_PROJMATRIX | DIRTY_FOGCOLOR | DIRTY_FOGCOEF | DIRTY_TEXENV | DIRTY_TEX_ALPHA_MUL | DIRTY_STENCILREPLACEVALUE |
	DIRTY_ALPHACOLORMASK | DIRTY_SHADERBLEND | DIRTY_COLORWRITEMASK | DIRTY_UVSCALEOFFSET | DIRTY_TEXCLAMP | DIRTY_DEPTHRANGE | DIRTY_MATAMBIENTALPHA |
	DIRTY_BEZIERSPLINE | DIRTY_DEPAL,
	DIRTY_LIGHT_UNIFORMS =
	DIRTY_LIGHT_CONTROL | DIRTY_LIGHT0 | DIRTY_LIGHT1 | DIRTY_LIGHT2 | DIRTY_LIGHT3 |
	DIRTY_MATDIFFUSE | DIRTY_MATSPECULAR | DIRTY_MATEMISSIVE | DIRTY_AMBIENT,
};

// Currently 496 bytes.
// Every line here is a 4-float.
struct alignas(16) UB_VS_FS_Base {
	float proj[16];
	float proj_through[16];
	float view[12];
	float world[12];
	float tex[12];
	float uvScaleOffset[4];
	float depthRange[4];
	float matAmbient[4];
	float cullRangeMin[4];
	float cullRangeMax[4];
	uint32_t spline_counts; uint32_t depal_mask_shift_off_fmt;  // 4 params packed into one.
	uint32_t colorWriteMask; float mipBias;
	// Fragment data
	float texNoAlpha; float texMul; float padding[2];  // this vec4 will hold ubershader stuff. We won't use integer flags in the fragment shader.
	float fogColor[3]; uint32_t alphaColorRef;
	float texEnvColor[3]; uint32_t colorTestMask;
	float texClamp[4];
	float texClampOffset[2]; float fogCoef[2];
	float blendFixA[3]; float stencilReplaceValue;
	float blendFixB[3]; float rotation;
	// VR stuff is to go here, later. For normal drawing, we can then get away
	// with just uploading the first 448 bytes of the struct (up to and including fogCoef).
};

static const char * const ub_baseStr =
R"(  mat4 u_proj;
  mat4 u_proj_through;
  mat3x4 u_view;
  mat3x4 u_world;
  mat3x4 u_texmtx;
  vec4 u_uvscaleoffset;
  vec4 u_depthRange;
  vec4 u_matambientalpha;
  vec4 u_cullRangeMin;
  vec4 u_cullRangeMax;
  uint u_spline_counts;
  uint u_depal_mask_shift_off_fmt;
  uint u_colorWriteMask;
  float u_mipBias;
  vec2 u_texNoAlphaMul; float pad1; float pad2;
  vec3 u_fogcolor;  uint u_alphacolorref;
  vec3 u_texenv;    uint u_alphacolormask;
  vec4 u_texclamp;
  vec2 u_texclampoff; vec2 u_fogcoef;
  vec3 u_blendFixA; float u_stencilReplaceValue;
  vec3 u_blendFixB; float u_rotation;
)";

// 512 bytes. Would like to shrink more. Some colors only have 8-bit precision and we expand
// them to float unnecessarily, could just as well expand in the shader.
struct alignas(16) UB_VS_Lights {
	float ambientColor[4];
	float materialDiffuse[4];
	float materialSpecular[4];
	float materialEmissive[3];
	uint32_t lightControl;
	float lpos[4][4];
	float ldir[4][4];
	float latt[4][4];
	float lightAngle_SpotCoef[4][4];   // TODO: Merge with lightSpotCoef, use .xy
	float lightAmbient[4][4];
	float lightDiffuse[4][4];
	float lightSpecular[4][4];
};

static const char * const ub_vs_lightsStr =
R"(  vec4 u_ambient;
  vec3 u_matdiffuse;
  vec4 u_matspecular;
  vec3 u_matemissive;
  uint u_lightControl;  // light ubershader control bits
  vec3 u_lightpos[4];
  vec3 u_lightdir[4];
  vec3 u_lightatt[4];
  vec4 u_lightangle_spotCoef[4];
  vec3 u_lightambient[4];
  vec3 u_lightdiffuse[4];
  vec3 u_lightspecular[4];
)";

// With some cleverness, we could get away with uploading just half this when only the four or five first
// bones are being used. This is 384b.
struct alignas(16) UB_VS_Bones {
	float bones[8][12];
};

static const char * const ub_vs_bonesStr =
R"(	mat3x4 u_bone0; mat3x4 u_bone1; mat3x4 u_bone2; mat3x4 u_bone3; mat3x4 u_bone4; mat3x4 u_bone5; mat3x4 u_bone6; mat3x4 u_bone7; mat3x4 u_bone8;
)";

void CalcCullRange(float minValues[4], float maxValues[4], bool flipViewport, bool hasNegZ);

void BaseUpdateUniforms(UB_VS_FS_Base *ub, uint64_t dirtyUniforms, bool flipViewport, bool useBufferedRendering);
void LightUpdateUniforms(UB_VS_Lights *ub, uint64_t dirtyUniforms);
void BoneUpdateUniforms(UB_VS_Bones *ub, uint64_t dirtyUniforms);

uint32_t PackLightControlBits();
