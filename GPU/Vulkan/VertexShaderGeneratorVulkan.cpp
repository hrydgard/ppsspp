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
#include <cstdlib>
#include <locale.h>

#include "gfx_es2/gpu_features.h"

#if defined(_WIN32) && defined(_DEBUG)
#include "Common/CommonWindows.h"
#endif

#include "base/stringutil.h"
#include "Common/Vulkan/VulkanLoader.h"
#include "Core/Config.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"

static const char *vulkan_glsl_preamble =
"#version 450\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n\n";

// "Varying" layout - must match fragment shader
// color0 = 0
// color1 = 1
// texcoord = 2
// fog = 3




#undef WRITE

#define WRITE p+=sprintf

static const char * const boneWeightDecl[9] = {
	"#ERROR#",
	"layout(location = 3) in float w1;\n",
	"layout(location = 3) in vec2 w1;\n",
	"layout(location = 3) in vec3 w1;\n",
	"layout(location = 3) in vec4 w1;\n",
	"layout(location = 3) in vec4 w1;\nlayout(location = 4) in float w2;\n",
	"layout(location = 3) in vec4 w1;\nlayout(location = 4) in vec2 w2;\n",
	"layout(location = 3) in vec4 w1;\nlayout(location = 4) in vec3 w2;\n",
	"layout(location = 3) in vec4 w1;\nlayout(location = 4) in vec4 w2;\n",
};

enum DoLightComputation {
	LIGHT_OFF,
	LIGHT_SHADE,
	LIGHT_FULL,
};

// Depth range and viewport
//
// After the multiplication with the projection matrix, we have a 4D vector in clip space.
// In OpenGL, Z is from -1 to 1, while in D3D, Z is from 0 to 1.
// PSP appears to use the OpenGL convention. As Z is from -1 to 1, and the viewport is represented
// by a center and a scale, to find the final Z value, all we need to do is to multiply by ZScale and
// add ZCenter - these are properly scaled to directly give a Z value in [0, 65535].
//
// z = vec.z * ViewportZScale + ViewportZCenter;
//
// That will give us the final value between 0 and 65535, which we can simply floor to simulate
// the limited precision of the PSP's depth buffer. Then we convert it back:
// z = floor(z);
//
// vec.z = (z - ViewportZCenter) / ViewportZScale;
//
// Now, the regular machinery will take over and do the calculation again.
//
// All this above is for full transform mode.
// In through mode, the Z coordinate just goes straight through and there is no perspective division.
// We simulate this of course with pretty much an identity matrix. Rounding Z becomes very easy.
//
// TODO: Skip all this if we can actually get a 16-bit depth buffer along with stencil, which
// is a bit of a rare configuration, although quite common on mobile.

bool GenerateVulkanGLSLVertexShader(const VShaderID &id, char *buffer) {
	char *p = buffer;

	WRITE(p, "%s", vulkan_glsl_preamble);

	bool highpFog = false;
	bool highpTexcoord = false;

	bool isModeThrough = id.Bit(VS_BIT_IS_THROUGH);
	bool lmode = id.Bit(VS_BIT_LMODE);
	bool doTexture = id.Bit(VS_BIT_DO_TEXTURE);
	bool doTextureTransform = id.Bit(VS_BIT_DO_TEXTURE_TRANSFORM);

	GETexMapMode uvGenMode = static_cast<GETexMapMode>(id.Bits(VS_BIT_UVGEN_MODE, 2));

	// this is only valid for some settings of uvGenMode
	GETexProjMapMode uvProjMode = static_cast<GETexProjMapMode>(id.Bits(VS_BIT_UVPROJ_MODE, 2));
	bool doShadeMapping = uvGenMode == GE_TEXMAP_ENVIRONMENT_MAP;
	bool doFlatShading = id.Bit(VS_BIT_FLATSHADE);

	bool useHWTransform = id.Bit(VS_BIT_USE_HW_TRANSFORM);
	bool hasColor = id.Bit(VS_BIT_HAS_COLOR) || !useHWTransform;
	bool hasNormal = id.Bit(VS_BIT_HAS_NORMAL) && useHWTransform;
	bool hasTexcoord = id.Bit(VS_BIT_HAS_TEXCOORD) || !useHWTransform;
	bool enableFog = id.Bit(VS_BIT_ENABLE_FOG);
	bool throughmode = id.Bit(VS_BIT_IS_THROUGH);
	bool flipNormal = id.Bit(VS_BIT_NORM_REVERSE);
	int ls0 = id.Bits(VS_BIT_LS0, 2);
	int ls1 = id.Bits(VS_BIT_LS1, 2);
	bool enableBones = id.Bit(VS_BIT_ENABLE_BONES);
	bool enableLighting = id.Bit(VS_BIT_LIGHTING_ENABLE);
	int matUpdate = id.Bits(VS_BIT_MATERIAL_UPDATE, 3);

	bool doBezier = id.Bit(VS_BIT_BEZIER);
	bool doSpline = id.Bit(VS_BIT_SPLINE);
	bool hasColorTess = id.Bit(VS_BIT_HAS_COLOR_TESS);
	bool hasTexcoordTess = id.Bit(VS_BIT_HAS_TEXCOORD_TESS);
	bool flipNormalTess = id.Bit(VS_BIT_NORM_REVERSE_TESS);

	WRITE(p, "\n");
	WRITE(p, "layout (std140, set = 0, binding = 3) uniform baseVars {\n%s} base;\n", ub_baseStr);
	if (enableLighting || doShadeMapping)
		WRITE(p, "layout (std140, set = 0, binding = 4) uniform lightVars {\n%s} light;\n", ub_vs_lightsStr);
	if (enableBones)
		WRITE(p, "layout (std140, set = 0, binding = 5) uniform boneVars {\n%s} bone;\n", ub_vs_bonesStr);

	const char *shading = doFlatShading ? "flat " : "";

	DoLightComputation doLight[4] = { LIGHT_OFF, LIGHT_OFF, LIGHT_OFF, LIGHT_OFF };
	if (useHWTransform) {
		int shadeLight0 = doShadeMapping ? ls0 : -1;
		int shadeLight1 = doShadeMapping ? ls1 : -1;
		for (int i = 0; i < 4; i++) {
			if (i == shadeLight0 || i == shadeLight1)
				doLight[i] = LIGHT_SHADE;
			if (id.Bit(VS_BIT_LIGHTING_ENABLE) && id.Bit(VS_BIT_LIGHT0_ENABLE + i))
				doLight[i] = LIGHT_FULL;
		}
	}

	int numBoneWeights = 0;
	int boneWeightScale = id.Bits(VS_BIT_WEIGHT_FMTSCALE, 2);
	if (enableBones) {
		numBoneWeights = 1 + id.Bits(VS_BIT_BONES, 3);
		WRITE(p, "%s", boneWeightDecl[numBoneWeights]);
	}

	if (useHWTransform)
		WRITE(p, "layout (location = %d) in vec3 position;\n", (int)PspAttributeLocation::POSITION);
	else
		// we pass the fog coord in w
		WRITE(p, "layout (location = %d) in vec4 position;\n", (int)PspAttributeLocation::POSITION);

	if (useHWTransform && hasNormal)
		WRITE(p, "layout (location = %d) in vec3 normal;\n", (int)PspAttributeLocation::NORMAL);

	bool texcoordInVec3 = false;
	if (doTexture && hasTexcoord) {
		if (!useHWTransform && doTextureTransform && !throughmode) {
			WRITE(p, "layout (location = %d) in vec3 texcoord;\n", (int)PspAttributeLocation::TEXCOORD);
			texcoordInVec3 = true;
		}
		else
			WRITE(p, "layout (location = %d) in vec2 texcoord;\n", (int)PspAttributeLocation::TEXCOORD);
	}
	if (hasColor) {
		WRITE(p, "layout (location = %d) in vec4 color0;\n", (int)PspAttributeLocation::COLOR0);
		if (lmode && !useHWTransform)  // only software transform supplies color1 as vertex data
			WRITE(p, "layout (location = %d) in vec3 color1;\n", (int)PspAttributeLocation::COLOR1);
	}

	WRITE(p, "layout (location = 1) %sout vec4 v_color0;\n", shading);
	if (lmode) {
		WRITE(p, "layout (location = 2) %sout vec3 v_color1;\n", shading);
	}

	if (doTexture) {
		WRITE(p, "layout (location = 0) out vec3 v_texcoord;\n");
	}

	if (enableFog) {
		// See the fragment shader generator
		WRITE(p, "layout (location = 3) out float v_fogdepth;\n");
	}

	// See comment above this function (GenerateVertexShader).
	if (!isModeThrough && gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT)) {
		// Apply the projection and viewport to get the Z buffer value, floor to integer, undo the viewport and projection.
		WRITE(p, "\nvec4 depthRoundZVP(vec4 v) {\n");
		WRITE(p, "  float z = v.z / v.w;\n");
		WRITE(p, "  z = z * base.depthRange.x + base.depthRange.y;\n");
		WRITE(p, "  z = floor(z);\n");
		WRITE(p, "  z = (z - base.depthRange.z) * base.depthRange.w;\n");
		WRITE(p, "  return vec4(v.x, v.y, z * v.w, v.w);\n");
		WRITE(p, "}\n\n");
	}
	WRITE(p, "out gl_PerVertex { vec4 gl_Position; };\n");

	if (doBezier || doSpline) {
		WRITE(p, "struct TessData {\n");
		WRITE(p, "  vec4 pos;\n");
		WRITE(p, "  vec4 uv;\n");
		WRITE(p, "  vec4 color;\n");
		WRITE(p, "};");
		WRITE(p, "layout (std430, set = 0, binding = 6) buffer s_tess_data {\n");
		WRITE(p, "  TessData data[];");
		WRITE(p, "} tess_data;\n");

		for (int i = 2; i <= 4; i++) {
			// Define 3 types vec2, vec3, vec4
			WRITE(p, "vec%d tess_sample(in vec%d points[16], in vec2 weights[4]) {\n", i, i);
			WRITE(p, "  vec%d pos = vec%d(0);\n", i, i);
			WRITE(p, "  for (int i = 0; i < 4; ++i) {\n");
			WRITE(p, "    for (int j = 0; j < 4; ++j) {\n");
			WRITE(p, "      float f = weights[j].x * weights[i].y;\n");
			WRITE(p, "      if (f != 0)\n");
			WRITE(p, "        pos = pos + f * points[i * 4 + j];\n");
			WRITE(p, "    }\n");
			WRITE(p, "  }\n");
			WRITE(p, "  return pos;\n");
			WRITE(p, "}\n");
		}
		if (doSpline) {
			WRITE(p, "void spline_knot(ivec2 num_patches, ivec2 type, out vec2 knot[6], ivec2 patch_pos) {\n");
			WRITE(p, "  for (int i = 0; i < 6; ++i) {\n");
			WRITE(p, "    knot[i] = vec2(i + patch_pos.x - 2, i + patch_pos.y - 2);\n");
			WRITE(p, "  }\n");
			WRITE(p, "  if ((type.x & 1) != 0) {\n");
			WRITE(p, "    if (patch_pos.x <= 2)\n");
			WRITE(p, "      knot[0].x = 0;\n");
			WRITE(p, "    if (patch_pos.x <= 1)\n");
			WRITE(p, "      knot[1].x = 0;\n");
			WRITE(p, "  }\n");
			WRITE(p, "  if ((type.x & 2) != 0) {\n");
			WRITE(p, "    if (patch_pos.x >= (num_patches.x - 2))\n");
			WRITE(p, "      knot[5].x = num_patches.x;\n");
			WRITE(p, "    if (patch_pos.x == (num_patches.x - 1))\n");
			WRITE(p, "      knot[4].x = num_patches.x;\n");
			WRITE(p, "  }\n");
			WRITE(p, "  if ((type.y & 1) != 0) {\n");
			WRITE(p, "    if (patch_pos.y <= 2)\n");
			WRITE(p, "      knot[0].y = 0;\n");
			WRITE(p, "    if (patch_pos.y <= 1)\n");
			WRITE(p, "      knot[1].y = 0;\n");
			WRITE(p, "  }\n");
			WRITE(p, "  if ((type.y & 2) != 0) {\n");
			WRITE(p, "    if (patch_pos.y >= (num_patches.y - 2))\n");
			WRITE(p, "      knot[5].y = num_patches.y;\n");
			WRITE(p, "    if (patch_pos.y == (num_patches.y - 1))\n");
			WRITE(p, "      knot[4].y = num_patches.y;\n");
			WRITE(p, "  }\n");
			WRITE(p, "}\n");

			WRITE(p, "void spline_weight(vec2 t, in vec2 knot[6], out vec2 weights[4]) {\n");
			// TODO: Maybe compilers could be coaxed into vectorizing this code without the above explicitly...
			WRITE(p, "  vec2 t0 = (t - knot[0]);\n");
			WRITE(p, "  vec2 t1 = (t - knot[1]);\n");
			WRITE(p, "  vec2 t2 = (t - knot[2]);\n");
			// TODO: All our knots are integers so we should be able to get rid of these divisions (How?)
			WRITE(p, "  vec2 f30 = t0 / (knot[3] - knot[0]);\n");
			WRITE(p, "  vec2 f41 = t1 / (knot[4] - knot[1]);\n");
			WRITE(p, "  vec2 f52 = t2 / (knot[5] - knot[2]);\n");
			WRITE(p, "  vec2 f31 = t1 / (knot[3] - knot[1]);\n");
			WRITE(p, "  vec2 f42 = t2 / (knot[4] - knot[2]);\n");
			WRITE(p, "  vec2 f32 = t2 / (knot[3] - knot[2]);\n");
			WRITE(p, "  vec2 a = (1 - f30)*(1 - f31);\n");
			WRITE(p, "  vec2 b = (f31*f41);\n");
			WRITE(p, "  vec2 c = (1 - f41)*(1 - f42);\n");
			WRITE(p, "  vec2 d = (f42*f52);\n");
			WRITE(p, "  weights[0] = a - (a*f32);\n");
			WRITE(p, "  weights[1] = 1 - a - b + ((a + b + c - 1)*f32);\n");
			WRITE(p, "  weights[2] = b + ((1 - b - c - d)*f32);\n");
			WRITE(p, "  weights[3] = d*f32;\n");
			WRITE(p, "}\n");
		}
	}

	WRITE(p, "void main() {\n");

	if (!useHWTransform) {
		// Simple pass-through of vertex data to fragment shader
		if (doTexture) {
			if (texcoordInVec3) {
				WRITE(p, "  v_texcoord = texcoord;\n");
			} else {
				WRITE(p, "  v_texcoord = vec3(texcoord, 1.0);\n");
			}
		}
		if (hasColor) {
			WRITE(p, "  v_color0 = color0;\n");
			if (lmode)
				WRITE(p, "  v_color1 = color1;\n");
		} else {
			WRITE(p, "  v_color0 = base.matambientalpha;\n");
			if (lmode)
				WRITE(p, "  v_color1 = vec3(0.0);\n");
		}
		if (enableFog) {
			WRITE(p, "  v_fogdepth = position.w;\n");
		}
		if (isModeThrough) {
			WRITE(p, "  gl_Position = base.proj_through_mtx * vec4(position.xyz, 1.0);\n");
		} else {
			// The viewport is used in this case, so need to compensate for that.
			if (gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT)) {
				WRITE(p, "  gl_Position = depthRoundZVP(base.proj_mtx * vec4(position.xyz, 1.0));\n");
			} else {
				WRITE(p, "  gl_Position = base.proj_mtx * vec4(position.xyz, 1.0);\n");
			}
		}
	} else {
		// Step 1: World Transform / Skinning
		if (!enableBones) {
			if (doBezier || doSpline) {
				WRITE(p, "  vec3 _pos[16];\n");
				WRITE(p, "  vec2 _tex[16];\n");
				WRITE(p, "  vec4 _col[16];\n");
				WRITE(p, "  int spline_count_u = int(base.spline_counts & 0xff);\n");
				WRITE(p, "  int spline_count_v = int((base.spline_counts >> 8) & 0xff);\n");
				WRITE(p, "  int num_patches_u = %s;\n", doBezier ? "(spline_count_u - 1) / 3" : "spline_count_u - 3");
				WRITE(p, "  int u = int(mod(gl_InstanceIndex, num_patches_u));\n");
				WRITE(p, "  int v = gl_InstanceIndex / num_patches_u;\n");
				WRITE(p, "  ivec2 patch_pos = ivec2(u, v);\n");
				WRITE(p, "  for (int i = 0; i < 4; i++) {\n");
				WRITE(p, "    for (int j = 0; j < 4; j++) {\n");
				WRITE(p, "      int idx = (i + v%s) * spline_count_u + (j + u%s);\n", doBezier ? " * 3" : "", doBezier ? " * 3" : "");
				WRITE(p, "      _pos[i * 4 + j] = tess_data.data[idx].pos.xyz;\n");
				if (doTexture && hasTexcoord && hasTexcoordTess)
					WRITE(p, "      _tex[i * 4 + j] = tess_data.data[idx].uv.xy;\n");
				if (hasColor && hasColorTess)
					WRITE(p, "      _col[i * 4 + j] = tess_data.data[idx].color;\n");
				WRITE(p, "    }\n");
				WRITE(p, "  }\n");
				WRITE(p, "  vec2 tess_pos = position.xy;\n");
				WRITE(p, "  vec2 weights[4];\n");
				if (doBezier) {
					// Bernstein 3D
					WRITE(p, "  weights[0] = (1 - tess_pos) * (1 - tess_pos) * (1 - tess_pos);\n");
					WRITE(p, "  weights[1] = 3 * tess_pos * (1 - tess_pos) * (1 - tess_pos);\n");
					WRITE(p, "  weights[2] = 3 * tess_pos * tess_pos * (1 - tess_pos);\n");
					WRITE(p, "  weights[3] = tess_pos * tess_pos * tess_pos;\n");
				} else { // Spline
					WRITE(p, "  ivec2 spline_num_patches = ivec2(spline_count_u - 3, spline_count_v - 3);\n");
					WRITE(p, "  int spline_type_u = int((base.spline_counts >> 16) & 0xff);\n");
					WRITE(p, "  int spline_type_v = int((base.spline_counts >> 24) & 0xff);\n");
					WRITE(p, "  ivec2 spline_type = ivec2(spline_type_u, spline_type_v);\n");
					WRITE(p, "  vec2 knots[6];\n");
					WRITE(p, "  spline_knot(spline_num_patches, spline_type, knots, patch_pos);\n");
					WRITE(p, "  spline_weight(tess_pos + patch_pos, knots, weights);\n");
				}
				WRITE(p, "  vec3 pos = tess_sample(_pos, weights);\n");
				if (doTexture && hasTexcoord) {
					if (hasTexcoordTess)
						WRITE(p, "  vec2 tex = tess_sample(_tex, weights);\n");
					else
						WRITE(p, "  vec2 tex = tess_pos + patch_pos;\n");
				}
				if (hasColor) {
					if (hasColorTess)
						WRITE(p, "  vec4 col = tess_sample(_col, weights);\n");
					else
						WRITE(p, "  vec4 col = tess_data.data[0].color;\n");
				}
				if (hasNormal) {
					// Curved surface is probably always need to compute normal(not sampling from control points)
					if (doBezier) {
						// Bernstein derivative
						WRITE(p, "  vec2 bernderiv[4];\n");
						WRITE(p, "  bernderiv[0] = -3 * (tess_pos - 1) * (tess_pos - 1); \n");
						WRITE(p, "  bernderiv[1] = 9 * tess_pos * tess_pos - 12 * tess_pos + 3; \n");
						WRITE(p, "  bernderiv[2] = 3 * (2 - 3 * tess_pos) * tess_pos; \n");
						WRITE(p, "  bernderiv[3] = 3 * tess_pos * tess_pos; \n");

						WRITE(p, "  vec2 bernderiv_u[4];\n");
						WRITE(p, "  vec2 bernderiv_v[4];\n");
						WRITE(p, "  for (int i = 0; i < 4; i++) {\n");
						WRITE(p, "    bernderiv_u[i] = vec2(bernderiv[i].x, weights[i].y);\n");
						WRITE(p, "    bernderiv_v[i] = vec2(weights[i].x, bernderiv[i].y);\n");
						WRITE(p, "  }\n");

						WRITE(p, "  vec3 du = tess_sample(_pos, bernderiv_u);\n");
						WRITE(p, "  vec3 dv = tess_sample(_pos, bernderiv_v);\n");
					} else { // Spline
						WRITE(p, "  vec2 tess_next_u = vec2(normal.x, 0);\n");
						WRITE(p, "  vec2 tess_next_v = vec2(0, normal.y);\n");
						// Right
						WRITE(p, "  vec2 tess_pos_r = tess_pos + tess_next_u;\n");
						WRITE(p, "  spline_weight(tess_pos_r + patch_pos, knots, weights);\n");
						WRITE(p, "  vec3 pos_r = tess_sample(_pos, weights);\n");
						// Left
						WRITE(p, "  vec2 tess_pos_l = tess_pos - tess_next_u;\n");
						WRITE(p, "  spline_weight(tess_pos_l + patch_pos, knots, weights);\n");
						WRITE(p, "  vec3 pos_l = tess_sample(_pos, weights);\n");
						// Down
						WRITE(p, "  vec2 tess_pos_d = tess_pos + tess_next_v;\n");
						WRITE(p, "  spline_weight(tess_pos_d + patch_pos, knots, weights);\n");
						WRITE(p, "  vec3 pos_d = tess_sample(_pos, weights);\n");
						// Up
						WRITE(p, "  vec2 tess_pos_u = tess_pos - tess_next_v;\n");
						WRITE(p, "  spline_weight(tess_pos_u + patch_pos, knots, weights);\n");
						WRITE(p, "  vec3 pos_u = tess_sample(_pos, weights);\n");

						WRITE(p, "  vec3 du = pos_r - pos_l;\n");
						WRITE(p, "  vec3 dv = pos_d - pos_u;\n");
					}
					WRITE(p, "  vec3 nrm = cross(du, dv);\n");
					WRITE(p, "  nrm = normalize(nrm);\n");
				}
				WRITE(p, "  vec3 worldpos = vec4(pos.xyz, 1.0) * base.world_mtx;\n");
				if (hasNormal) {
					WRITE(p, "  mediump vec3 worldnormal = normalize(vec4(%snrm, 0.0) * base.world_mtx);\n", flipNormalTess ? "-" : "");
				} else {
					WRITE(p, "  mediump vec3 worldnormal = vec3(0.0, 0.0, 1.0);\n");
				}
			} else {
				// No skinning, just standard T&L.
				WRITE(p, "  vec3 worldpos = vec4(position.xyz, 1.0) * base.world_mtx;\n");
				if (hasNormal)
					WRITE(p, "  mediump vec3 worldnormal = normalize(vec4(%snormal, 0.0) * base.world_mtx);\n", flipNormal ? "-" : "");
				else
					WRITE(p, "  mediump vec3 worldnormal = vec3(0.0, 0.0, 1.0);\n");
			}
		} else {
			static const char *rescale[4] = { "", " * 1.9921875", " * 1.999969482421875", "" }; // 2*127.5f/128.f, 2*32767.5f/32768.f, 1.0f};
			const char *factor = rescale[boneWeightScale];

			static const char * const boneWeightAttr[8] = {
				"w1.x", "w1.y", "w1.z", "w1.w",
				"w2.x", "w2.y", "w2.z", "w2.w",
			};

			WRITE(p, "  mat3x4 skinMatrix = w1.x * bone.m[0];\n");
			if (numBoneWeights > 1) {
				for (int i = 1; i < numBoneWeights; i++) {
					WRITE(p, "    skinMatrix += %s * bone.m[%i];\n", boneWeightAttr[i], i);
				}
			}

			WRITE(p, ";\n");

			// Trying to simplify this results in bugs in LBP...
			WRITE(p, "  vec3 skinnedpos = (vec4(position, 1.0) * skinMatrix) %s;\n", factor);
			WRITE(p, "  vec3 worldpos = vec4(skinnedpos, 1.0) * base.world_mtx;\n");

			if (hasNormal) {
				WRITE(p, "  mediump vec3 skinnednormal = vec4(%snormal, 0.0) * skinMatrix %s;\n", flipNormal ? "-" : "", factor);
			} else {
				WRITE(p, "  mediump vec3 skinnednormal = vec4(0.0, 0.0, %s1.0, 0.0) * skinMatrix %s;\n", flipNormal ? "-" : "", factor);
			}
			WRITE(p, "  mediump vec3 worldnormal = normalize(vec4(skinnednormal, 0.0) * base.world_mtx);\n");
		}

		WRITE(p, "  vec4 viewPos = vec4(vec4(worldpos, 1.0) * base.view_mtx, 1.0);\n");

		// Final view and projection transforms.
		if (gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT)) {
			WRITE(p, "  gl_Position = depthRoundZVP(base.proj_mtx * viewPos);\n");
		} else {
			WRITE(p, "  gl_Position = base.proj_mtx * viewPos;\n");
		}

		// TODO: Declare variables for dots for shade mapping if needed.

		const char *ambientStr = ((matUpdate & 1) && hasColor) ? "color0" : "base.matambientalpha";
		const char *diffuseStr = ((matUpdate & 2) && hasColor) ? "color0.rgb" : "light.matdiffuse";
		const char *specularStr = ((matUpdate & 4) && hasColor) ? "color0.rgb" : "light.matspecular.rgb";
		if (doBezier || doSpline) {
			ambientStr = (matUpdate & 1) && hasColor ? "col" : "base.matambientalpha";
			diffuseStr = (matUpdate & 2) && hasColor ? "col.rgb" : "light.matdiffuse";
			specularStr = (matUpdate & 4) && hasColor ? "col.rgb" : "light.matspecular.rgb";
		}

		bool diffuseIsZero = true;
		bool specularIsZero = true;
		bool distanceNeeded = false;

		if (enableLighting) {
			WRITE(p, "  vec4 lightSum0 = light.u_ambient * %s + vec4(light.matemissive, 0.0);\n", ambientStr);

			for (int i = 0; i < 4; i++) {
				GELightType type = static_cast<GELightType>(id.Bits(VS_BIT_LIGHT0_TYPE + 4 * i, 2));
				GELightComputation comp = static_cast<GELightComputation>(id.Bits(VS_BIT_LIGHT0_COMP + 4 * i, 2));
				if (doLight[i] != LIGHT_FULL)
					continue;
				diffuseIsZero = false;
				if (comp != GE_LIGHTCOMP_ONLYDIFFUSE)
					specularIsZero = false;
				if (type != GE_LIGHTTYPE_DIRECTIONAL)
					distanceNeeded = true;
			}

			if (!specularIsZero) {
				WRITE(p, "  vec3 lightSum1 = vec3(0.0);\n");
			}
			if (!diffuseIsZero) {
				WRITE(p, "  vec3 toLight;\n");
				WRITE(p, "  vec3 diffuse;\n");
			}
			if (distanceNeeded) {
				WRITE(p, "  float distance;\n");
				WRITE(p, "  float lightScale;\n");
			}
		}

		// Calculate lights if needed. If shade mapping is enabled, lights may need to be
		// at least partially calculated.
		for (int i = 0; i < 4; i++) {
			if (doLight[i] != LIGHT_FULL)
				continue;

			GELightType type = static_cast<GELightType>(id.Bits(VS_BIT_LIGHT0_TYPE + 4 * i, 2));
			GELightComputation comp = static_cast<GELightComputation>(id.Bits(VS_BIT_LIGHT0_COMP + 4 * i, 2));

			if (type == GE_LIGHTTYPE_DIRECTIONAL) {
				// We prenormalize light positions for directional lights.
				WRITE(p, "  toLight = light.pos[%i];\n", i);
			} else {
				WRITE(p, "  toLight = light.pos[%i] - worldpos;\n", i);
				WRITE(p, "  distance = length(toLight);\n");
				WRITE(p, "  toLight /= distance;\n");
			}

			bool doSpecular = comp != GE_LIGHTCOMP_ONLYDIFFUSE;
			bool poweredDiffuse = comp == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE;

			WRITE(p, "  mediump float dot%i = max(dot(toLight, worldnormal), 0.0);\n", i);
			if (poweredDiffuse) {
				// pow(0.0, 0.0) may be undefined, but the PSP seems to treat it as 1.0.
				// Seen in Tales of the World: Radiant Mythology (#2424.)
				WRITE(p, "  if (dot%i == 0.0 && light.matspecular.a == 0.0) {\n", i);
				WRITE(p, "    dot%i = 1.0;\n", i);
				WRITE(p, "  } else {\n");
				WRITE(p, "    dot%i = pow(dot%i, light.matspecular.a);\n", i, i);
				WRITE(p, "  }\n");
			}

			const char *timesLightScale = " * lightScale";

			// Attenuation
			switch (type) {
			case GE_LIGHTTYPE_DIRECTIONAL:
				timesLightScale = "";
				break;
			case GE_LIGHTTYPE_POINT:
				WRITE(p, "  lightScale = clamp(1.0 / dot(light.att[%i], vec3(1.0, distance, distance*distance)), 0.0, 1.0);\n", i);
				break;
			case GE_LIGHTTYPE_SPOT:
			case GE_LIGHTTYPE_UNKNOWN:
				WRITE(p, "  float angle%i = dot(normalize(light.dir[%i]), toLight);\n", i, i);
				WRITE(p, "  if (angle%i >= light.angle_spotCoef[%i].x) {\n", i, i);
				WRITE(p, "    lightScale = clamp(1.0 / dot(light.att[%i], vec3(1.0, distance, distance*distance)), 0.0, 1.0) * pow(angle%i, light.angle_spotCoef[%i].y);\n", i, i, i);
				WRITE(p, "  } else {\n");
				WRITE(p, "    lightScale = 0.0;\n");
				WRITE(p, "  }\n");
				break;
			default:
				// ILLEGAL
				break;
			}

			WRITE(p, "  diffuse = (light.diffuse[%i] * %s) * dot%i;\n", i, diffuseStr, i);
			if (doSpecular) {
				WRITE(p, "  dot%i = dot(normalize(toLight + vec3(0.0, 0.0, 1.0)), worldnormal);\n", i);
				WRITE(p, "  if (dot%i > 0.0)\n", i);
				WRITE(p, "    lightSum1 += light.specular[%i] * %s * (pow(dot%i, light.matspecular.a) %s);\n", i, specularStr, i, timesLightScale);
			}
			WRITE(p, "  lightSum0.rgb += (light.ambient[%i] * %s.rgb + diffuse)%s;\n", i, ambientStr, timesLightScale);
		}

		if (enableLighting) {
			// Sum up ambient, emissive here.
			if (lmode) {
				WRITE(p, "  v_color0 = clamp(lightSum0, 0.0, 1.0);\n");
				// v_color1 only exists when lmode = 1.
				if (specularIsZero) {
					WRITE(p, "  v_color1 = vec3(0.0);\n");
				} else {
					WRITE(p, "  v_color1 = clamp(lightSum1, 0.0, 1.0);\n");
				}
			} else {
				if (specularIsZero) {
					WRITE(p, "  v_color0 = clamp(lightSum0, 0.0, 1.0);\n");
				} else {
					WRITE(p, "  v_color0 = clamp(clamp(lightSum0, 0.0, 1.0) + vec4(lightSum1, 0.0), 0.0, 1.0);\n");
				}
			}
		} else {
			// Lighting doesn't affect color.
			if (hasColor) {
				if (doBezier || doSpline)
					WRITE(p, "  v_color0 = col;\n");
				else
					WRITE(p, "  v_color0 = color0;\n");
			} else {
				WRITE(p, "  v_color0 = base.matambientalpha;\n");
			}
			if (lmode) {
				WRITE(p, "  v_color1 = vec3(0.0);\n");
			}
		}

		bool scaleUV = !throughmode && (uvGenMode == GE_TEXMAP_TEXTURE_COORDS || uvGenMode == GE_TEXMAP_UNKNOWN);

		// Step 3: UV generation
		if (doTexture) {
			switch (uvGenMode) {
			case GE_TEXMAP_TEXTURE_COORDS:  // Scale-offset. Easy.
			case GE_TEXMAP_UNKNOWN: // Not sure what this is, but Riviera uses it.  Treating as coords works.
				if (scaleUV) {
					if (hasTexcoord) {
						if (doBezier || doSpline)
							WRITE(p, "  v_texcoord = vec3(tex.xy * base.uvscaleoffset.xy + base.uvscaleoffset.zw, 0.0);\n");
						else
							WRITE(p, "  v_texcoord = vec3(texcoord.xy * base.uvscaleoffset.xy, 0.0);\n");
					} else {
						WRITE(p, "  v_texcoord = vec3(0.0);\n");
					}
				} else {
					if (hasTexcoord) {
						if (doBezier || doSpline)
							WRITE(p, "  v_texcoord = vec3(tex.xy * base.uvscaleoffset.xy + base.uvscaleoffset.zw, 0.0);\n");
						else
							WRITE(p, "  v_texcoord = vec3(texcoord.xy * base.uvscaleoffset.xy + base.uvscaleoffset.zw, 0.0);\n");
					} else {
						WRITE(p, "  v_texcoord = vec3(base.uvscaleoffset.zw, 0.0);\n");
					}
				}
				break;

			case GE_TEXMAP_TEXTURE_MATRIX:  // Projection mapping.
			{
				std::string temp_tc;
				switch (uvProjMode) {
				case GE_PROJMAP_POSITION:  // Use model space XYZ as source
					temp_tc = "vec4(position.xyz, 1.0)";
					break;
				case GE_PROJMAP_UV:  // Use unscaled UV as source
				{
					// scaleUV is false here.
					if (hasTexcoord) {
						temp_tc = "vec4(texcoord.xy, 0.0, 1.0)";
					} else {
						temp_tc = "vec4(0.0, 0.0, 0.0, 1.0)";
					}
				}
				break;
				case GE_PROJMAP_NORMALIZED_NORMAL:  // Use normalized transformed normal as source
					if (hasNormal)
						temp_tc = flipNormal ? "vec4(normalize(-normal), 1.0)" : "vec4(normalize(normal), 1.0)";
					else
						temp_tc = "vec4(0.0, 0.0, 1.0, 1.0)";
					break;
				case GE_PROJMAP_NORMAL:  // Use non-normalized transformed normal as source
					if (hasNormal)
						temp_tc = flipNormal ? "vec4(-normal, 1.0)" : "vec4(normal, 1.0)";
					else
						temp_tc = "vec4(0.0, 0.0, 1.0, 1.0)";
					break;
				}
				// Transform by texture matrix. XYZ as we are doing projection mapping.
				WRITE(p, "  v_texcoord = (%s * base.tex_mtx).xyz * vec3(base.uvscaleoffset.xy, 1.0);\n", temp_tc.c_str());
			}
			break;

			case GE_TEXMAP_ENVIRONMENT_MAP:  // Shade mapping - use dots from light sources.
				WRITE(p, "  v_texcoord = vec3(base.uvscaleoffset.xy * vec2(1.0 + dot(normalize(light.pos[%i]), worldnormal), 1.0 + dot(normalize(light.pos[%i]), worldnormal)) * 0.5, 1.0);\n", ls0, ls1);
				break;

			default:
				// ILLEGAL
				break;
			}
		}

		// Compute fogdepth
		if (enableFog)
			WRITE(p, "  v_fogdepth = (viewPos.z + base.fogcoef.x) * base.fogcoef.y;\n");
	}
	WRITE(p, "}\n");
	return true;
}
