#pragma once

#include <cstdint>

#include "ShaderCommon.h"

// Used by the "modern" backends that use uniform buffers. They can share this without issue.

enum : uint64_t {
	DIRTY_BASE_UNIFORMS =
	DIRTY_WORLDMATRIX | DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWMATRIX | DIRTY_TEXMATRIX | DIRTY_ALPHACOLORREF |
	DIRTY_PROJMATRIX | DIRTY_FOGCOLOR | DIRTY_FOGCOEF | DIRTY_TEXENV | DIRTY_STENCILREPLACEVALUE | DIRTY_GUARDBAND |
	DIRTY_ALPHACOLORMASK | DIRTY_SHADERBLEND | DIRTY_UVSCALEOFFSET | DIRTY_TEXCLAMP | DIRTY_DEPTHRANGE | DIRTY_MATAMBIENTALPHA |
	DIRTY_BEZIERSPLINE,
	DIRTY_LIGHT_UNIFORMS =
	DIRTY_LIGHT0 | DIRTY_LIGHT1 | DIRTY_LIGHT2 | DIRTY_LIGHT3 |
	DIRTY_MATDIFFUSE | DIRTY_MATSPECULAR | DIRTY_MATEMISSIVE | DIRTY_AMBIENT,
};

// TODO: Split into two structs, one for software transform and one for hardware transform, to save space.
// 512 bytes. Probably can't get to 256 (nVidia's UBO alignment).
// Every line here is a 4-float.
struct UB_VS_FS_Base {
	float proj[16];
	float proj_through[16];
	float view[12];
	float world[12];
	float tex[12];
	float uvScaleOffset[4];
	float depthRange[4];
	float fogCoef[2];	float stencil; float pad0;
	float matAmbient[4];
	int spline_count_u; int spline_count_v; int spline_type_u; int spline_type_v;
	float guardband[4];
	// Fragment data
	float fogColor[4];
	float texEnvColor[4];
	int alphaColorRef[4];
	int colorTestMask[4];
	float blendFixA[4];
	float blendFixB[4];
	float texClamp[4];
	float texClampOffset[4];
};

static const char *ub_baseStr =
R"(  mat4 proj_mtx;
	mat4 proj_through_mtx;
  mat3x4 view_mtx;
  mat3x4 world_mtx;
  mat3x4 tex_mtx;
  vec4 uvscaleoffset;
  vec4 depthRange;
  vec2 fogcoef;
  float stencilReplace;
  vec4 matambientalpha;
  int spline_count_u;
  int spline_count_v;
  int spline_type_u;
  int spline_type_v;
  vec4 guardband;
  // Fragment
  vec3 fogcolor;
  vec3 texenv;
  ivec4 alphacolorref;
  ivec4 alphacolormask;
  vec3 blendFixA;
  vec3 blendFixB;
  vec4 texclamp;
  vec2 texclampoff;
)";

// HLSL code is shared so these names are changed to match those in DX9.
static const char *cb_baseStr =
R"(  float4x4 u_proj;
  float4x4 u_proj_through;
  float4x3 u_view;
  float4x3 u_world;
  float4x3 u_tex;
  float4 u_uvscaleoffset;
  float4 u_depthRange;
  float2 u_fogcoef;
  float u_stencilReplaceValue;
  float4 u_matambientalpha;
  int u_spline_count_u;
  int u_spline_count_v;
  int u_spline_type_u;
  int u_spline_type_v;
  float4 u_guardband;
  // Fragment
  float3 u_fogcolor;
  float3 u_texenv;
  uint4 u_alphacolorref;
  uint4 u_alphacolormask;
  float3 u_blendFixA;
  float3 u_blendFixB;
  float4 u_texclamp;
  float2 u_texclampoff;
)";

// 576 bytes. Can we get down to 512?
struct UB_VS_Lights {
	float ambientColor[4];
	float materialDiffuse[4];
	float materialSpecular[4];
	float materialEmissive[4];
	float lpos[4][4];
	float ldir[4][4];
	float latt[4][4];
	float lightAngle[4][4];   // TODO: Merge with lightSpotCoef, use .xy
	float lightSpotCoef[4][4];
	float lightAmbient[4][4];
	float lightDiffuse[4][4];
	float lightSpecular[4][4];
};

static const char *ub_vs_lightsStr =
R"(	vec4 u_ambient;
	vec3 matdiffuse;
	vec4 matspecular;
	vec3 matemissive;
	vec3 pos[4];
	vec3 dir[4];
	vec3 att[4];
	float angle[4];
	float spotCoef[4];
	vec3 ambient[4];
	vec3 diffuse[4];
	vec3 specular[4];
)";

// HLSL code is shared so these names are changed to match those in DX9.
static const char *cb_vs_lightsStr =
R"(	float4 u_ambient;
	float3 u_matdiffuse;
	float4 u_matspecular;
	float3 u_matemissive;
	float3 u_lightpos0;
	float3 u_lightpos1;
	float3 u_lightpos2;
	float3 u_lightpos3;
	float3 u_lightdir0;
	float3 u_lightdir1;
	float3 u_lightdir2;
	float3 u_lightdir3;
	float3 u_lightatt0;
	float3 u_lightatt1;
	float3 u_lightatt2;
	float3 u_lightatt3;
	float4 u_lightangle0;
	float4 u_lightangle1;
	float4 u_lightangle2;
	float4 u_lightangle3;
	float4 u_lightspotCoef0;
	float4 u_lightspotCoef1;
	float4 u_lightspotCoef2;
	float4 u_lightspotCoef3;
	float3 u_lightambient0;
	float3 u_lightambient1;
	float3 u_lightambient2;
	float3 u_lightambient3;
	float3 u_lightdiffuse0;
	float3 u_lightdiffuse1;
	float3 u_lightdiffuse2;
	float3 u_lightdiffuse3;
	float3 u_lightspecular0;
	float3 u_lightspecular1;
	float3 u_lightspecular2;
	float3 u_lightspecular3;
)";

// With some cleverness, we could get away with uploading just half this when only the four or five first
// bones are being used. This is 512b, 256b would be great.
struct UB_VS_Bones {
	float bones[8][12];
};

static const char *ub_vs_bonesStr =
R"(	mat3x4 m[8];
)";

// HLSL code is shared so these names are changed to match those in DX9.
static const char *cb_vs_bonesStr =
R"(	float4x3 u_bone[8];
)";

void BaseUpdateUniforms(UB_VS_FS_Base *ub, uint64_t dirtyUniforms, bool flipViewport);
void LightUpdateUniforms(UB_VS_Lights *ub, uint64_t dirtyUniforms);
void BoneUpdateUniforms(UB_VS_Bones *ub, uint64_t dirtyUniforms);

// Shared helper functions
void ComputeGuardband(float gb[4], float zmin);
