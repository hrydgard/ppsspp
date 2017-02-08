#pragma once

#include <cstdint>

#include "ShaderCommon.h"

// Used by the "modern" backends that use uniform buffers. They can share this without issue.

// Pretty much full. Will need more bits for more fine grained dirty tracking for lights.
enum : uint64_t {
	DIRTY_BASE_UNIFORMS =
	DIRTY_WORLDMATRIX | DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWMATRIX | DIRTY_TEXMATRIX | DIRTY_ALPHACOLORREF |
	DIRTY_PROJMATRIX | DIRTY_FOGCOLOR | DIRTY_FOGCOEF | DIRTY_TEXENV | DIRTY_STENCILREPLACEVALUE |
	DIRTY_ALPHACOLORMASK | DIRTY_SHADERBLEND | DIRTY_UVSCALEOFFSET | DIRTY_TEXCLAMP | DIRTY_DEPTHRANGE | DIRTY_MATAMBIENTALPHA,
	DIRTY_LIGHT_UNIFORMS =
	DIRTY_LIGHT0 | DIRTY_LIGHT1 | DIRTY_LIGHT2 | DIRTY_LIGHT3 |
	DIRTY_MATDIFFUSE | DIRTY_MATSPECULAR | DIRTY_MATEMISSIVE | DIRTY_AMBIENT,
};

// TODO: Split into two structs, one for software transform and one for hardware transform, to save space.
// 512 bytes. Probably can't get to 256 (nVidia's UBO alignment).
struct UB_VS_FS_Base {
	float proj[16];
	float proj_through[16];
	float view[16];
	float world[16];
	float tex[16];  // not that common, may want to break out
	float uvScaleOffset[4];
	float depthRange[4];
	float fogCoef_stencil[4];
	float matAmbient[4];
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
  mat4 view_mtx;
  mat4 world_mtx;
  mat4 tex_mtx;
  vec4 uvscaleoffset;
  vec4 depthRange;
  vec3 fogcoef_stencilreplace;
  vec4 matambientalpha;
  vec3 fogcolor;
  vec3 texenv;
  ivec4 alphacolorref;
  ivec4 alphacolormask;
  vec3 blendFixA;
  vec3 blendFixB;
  vec4 texclamp;
  vec2 texclampoff;
)";

static const char *cb_baseStr =
R"(  matrix proj_mtx;
	matrix proj_through_mtx;
  matrix view_mtx;
  matrix world_mtx;
  matrix tex_mtx;
  float4 uvscaleoffset;
  float4 depthRange;
  float3 fogcoef_stencilreplace;
  float4 matambientalpha;
  float3 fogcolor;
  float3 texenv;
  ifloat4 alphacolorref;
  ifloat4 alphacolormask;
  float3 blendFixA;
  float3 blendFixB;
  float4 texclamp;
  float2 texclampoff;
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
R"(	vec4 globalAmbient;
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

static const char *cb_vs_lightsStr =
R"(	float4 globalAmbient;
	float3 matdiffuse;
	float4 matspecular;
	float3 matemissive;
	float3 pos[4];
	float3 dir[4];
	float3 att[4];
	float angle[4];
	float spotCoef[4];
	float3 ambient[4];
	float3 diffuse[4];
	float3 specular[4];
)";

// With some cleverness, we could get away with uploading just half this when only the four first
// bones are being used. This is 512b, 256b would be great.
// Could also move to 4x3 matrices - would let us fit 5 bones into 256b.
struct UB_VS_Bones {
	float bones[8][16];
};

static const char *ub_vs_bonesStr =
R"(	mat4 m[8];
)";

static const char *cb_vs_bonesStr =
R"(	matrix m[8];
)";

void BaseUpdateUniforms(UB_VS_FS_Base *ub, uint64_t dirtyUniforms);
void LightUpdateUniforms(UB_VS_Lights *ub, uint64_t dirtyUniforms);
void BoneUpdateUniforms(UB_VS_Bones *ub, uint64_t dirtyUniforms);

