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
	bool hasNormalTess = id.Bit(VS_BIT_HAS_NORMAL_TESS);
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
		if (!useHWTransform && doTextureTransform && !isModeThrough) {
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
		WRITE(p, "};\n");
		WRITE(p, "layout (std430, set = 0, binding = 6) readonly buffer s_tess_data {\n");
		WRITE(p, "  TessData data[];\n");
		WRITE(p, "} tess_data;\n");

		WRITE(p, "layout (std430) struct TessWeight {\n");
		WRITE(p, "  vec4 basis;\n");
		WRITE(p, "  vec4 deriv;\n");
		WRITE(p, "};\n");
		WRITE(p, "layout (std430, set = 0, binding = 7) readonly buffer s_tess_weights_u {\n");
		WRITE(p, "  TessWeight data[];\n");
		WRITE(p, "} tess_weights_u;\n");
		WRITE(p, "layout (std430, set = 0, binding = 8) readonly buffer s_tess_weights_v {\n");
		WRITE(p, "  TessWeight data[];\n");
		WRITE(p, "} tess_weights_v;\n");

		for (int i = 2; i <= 4; i++) {
			// Define 3 types vec2, vec3, vec4
			WRITE(p, "vec%d tess_sample(in vec%d points[16], mat4 weights) {\n", i, i);
			WRITE(p, "  vec%d pos = vec%d(0.0);\n", i, i);
			for (int v = 0; v < 4; ++v) {
				for (int u = 0; u < 4; ++u) {
					WRITE(p, "  pos += weights[%i][%i] * points[%i];\n", v, u, v * 4 + u);
				}
			}
			WRITE(p, "  return pos;\n");
			WRITE(p, "}\n");
		}

		WRITE(p, "struct Tess {\n");
		WRITE(p, "  vec3 pos;\n");
		if (doTexture)
			WRITE(p, "  vec2 tex;\n");
		WRITE(p, "  vec4 col;\n");
		if (hasNormalTess)
			WRITE(p, "  vec3 nrm;\n");
		WRITE(p, "};\n");

		WRITE(p, "void tessellate(out Tess tess) {\n");
		WRITE(p, "  ivec2 point_pos = ivec2(position.z, normal.z)%s;\n", doBezier ? " * 3" : "");
		WRITE(p, "  ivec2 weight_idx = ivec2(position.xy);\n");
		// Load 4x4 control points
		WRITE(p, "  vec3 _pos[16];\n");
		WRITE(p, "  vec2 _tex[16];\n");
		WRITE(p, "  vec4 _col[16];\n");
		WRITE(p, "  int index;\n");
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				WRITE(p, "  index = (%i + point_pos.y) * int(base.spline_counts) + (%i + point_pos.x);\n", i, j);
				WRITE(p, "  _pos[%i] = tess_data.data[index].pos.xyz;\n", i * 4 + j);
				if (doTexture && hasTexcoordTess)
					WRITE(p, "  _tex[%i] = tess_data.data[index].uv.xy;\n", i * 4 + j);
				if (hasColorTess)
					WRITE(p, "  _col[%i] = tess_data.data[index].color;\n", i * 4 + j);
			}
		}

		// Basis polynomials as weight coefficients
		WRITE(p, "  vec4 basis_u = tess_weights_u.data[weight_idx.x].basis;\n");
		WRITE(p, "  vec4 basis_v = tess_weights_v.data[weight_idx.y].basis;\n");
		WRITE(p, "  mat4 basis = outerProduct(basis_u, basis_v);\n");

		// Tessellate
		WRITE(p, "  tess.pos = tess_sample(_pos, basis);\n");
		if (doTexture) {
			if (hasTexcoordTess)
				WRITE(p, "  tess.tex = tess_sample(_tex, basis);\n");
			else
				WRITE(p, "  tess.tex = normal.xy;\n");
		}
		if (hasColorTess)
			WRITE(p, "  tess.col = tess_sample(_col, basis);\n");
		else
			WRITE(p, "  tess.col = base.matambientalpha;\n");
		if (hasNormalTess) {
			// Derivatives as weight coefficients
			WRITE(p, "  vec4 deriv_u = tess_weights_u.data[weight_idx.x].deriv;\n");
			WRITE(p, "  vec4 deriv_v = tess_weights_v.data[weight_idx.y].deriv;\n");

			WRITE(p, "  vec3 du = tess_sample(_pos, outerProduct(deriv_u, basis_v));\n");
			WRITE(p, "  vec3 dv = tess_sample(_pos, outerProduct(basis_u, deriv_v));\n");
			WRITE(p, "  tess.nrm = normalize(cross(du, dv));\n");
		}
		WRITE(p, "}\n");
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
			WRITE(p, "  vec4 outPos = base.proj_through_mtx * vec4(position.xyz, 1.0);\n");
		} else {
			// The viewport is used in this case, so need to compensate for that.
			if (gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT)) {
				WRITE(p, "  vec4 outPos = depthRoundZVP(base.proj_mtx * vec4(position.xyz, 1.0));\n");
			} else {
				WRITE(p, "  vec4 outPos = base.proj_mtx * vec4(position.xyz, 1.0);\n");
			}
		}
	} else {
		// Step 1: World Transform / Skinning
		if (!enableBones) {
			if (doBezier || doSpline) {
				// Hardware tessellation
				WRITE(p, "  Tess tess;\n");
				WRITE(p, "  tessellate(tess);\n");

				WRITE(p, "  vec3 worldpos = vec4(tess.pos.xyz, 1.0) * base.world_mtx;\n");
				if (hasNormalTess) {
					WRITE(p, "  mediump vec3 worldnormal = normalize(vec4(%stess.nrm, 0.0) * base.world_mtx);\n", flipNormalTess ? "-" : "");
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
			WRITE(p, "  vec4 outPos = depthRoundZVP(base.proj_mtx * viewPos);\n");
		} else {
			WRITE(p, "  vec4 outPos = base.proj_mtx * viewPos;\n");
		}

		// TODO: Declare variables for dots for shade mapping if needed.

		const char *ambientStr = ((matUpdate & 1) && hasColor) ? "color0" : "base.matambientalpha";
		const char *diffuseStr = ((matUpdate & 2) && hasColor) ? "color0.rgb" : "light.matdiffuse";
		const char *specularStr = ((matUpdate & 4) && hasColor) ? "color0.rgb" : "light.matspecular.rgb";
		if (doBezier || doSpline) {
			// TODO: Probably, should use hasColorTess but FF4 has a problem with drawing the background.
			ambientStr = (matUpdate & 1) && hasColor ? "tess.col" : "base.matambientalpha";
			diffuseStr = (matUpdate & 2) && hasColor ? "tess.col.rgb" : "light.matdiffuse";
			specularStr = (matUpdate & 4) && hasColor ? "tess.col.rgb" : "light.matspecular.rgb";
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
				if (comp == GE_LIGHTCOMP_BOTH)
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

			bool doSpecular = comp == GE_LIGHTCOMP_BOTH;
			bool poweredDiffuse = comp == GE_LIGHTCOMP_ONLYPOWDIFFUSE;

			WRITE(p, "  mediump float dot%i = dot(toLight, worldnormal);\n", i);
			if (poweredDiffuse) {
				// pow(0.0, 0.0) may be undefined, but the PSP seems to treat it as 1.0.
				// Seen in Tales of the World: Radiant Mythology (#2424.)
				WRITE(p, "  if (light.matspecular.a == 0.0) {\n");
				WRITE(p, "    dot%i = 1.0;\n", i);
				WRITE(p, "  } else {\n");
				WRITE(p, "    dot%i = pow(max(dot%i, 0.0), light.matspecular.a);\n", i, i);
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
				WRITE(p, "  float angle%i = length(light.dir[%i]) == 0.0 ? 0.0 : dot(normalize(light.dir[%i]), toLight);\n", i, i, i);
				WRITE(p, "  if (angle%i >= light.angle_spotCoef[%i].x) {\n", i, i);
				WRITE(p, "    lightScale = clamp(1.0 / dot(light.att[%i], vec3(1.0, distance, distance*distance)), 0.0, 1.0) * (light.angle_spotCoef[%i].y == 0.0 ? 1.0 : pow(angle%i, light.angle_spotCoef[%i].y));\n", i, i, i, i);
				WRITE(p, "  } else {\n");
				WRITE(p, "    lightScale = 0.0;\n");
				WRITE(p, "  }\n");
				break;
			default:
				// ILLEGAL
				break;
			}

			WRITE(p, "  diffuse = (light.diffuse[%i] * %s) * max(dot%i, 0.0);\n", i, diffuseStr, i);
			if (doSpecular) {
				WRITE(p, "  if (dot%i >= 0.0) {\n", i);
				WRITE(p, "    dot%i = dot(normalize(toLight + vec3(0.0, 0.0, 1.0)), worldnormal);\n", i);
				WRITE(p, "    if (light.matspecular.a == 0.0) {\n");
				WRITE(p, "      dot%i = 1.0;\n", i);
				WRITE(p, "    } else {\n");
				WRITE(p, "      dot%i = pow(max(dot%i, 0.0), light.matspecular.a);\n", i, i);
				WRITE(p, "    }\n");
				WRITE(p, "    if (dot%i > 0.0)\n", i);
				WRITE(p, "      lightSum1 += light.specular[%i] * %s * dot%i %s;\n", i, specularStr, i, timesLightScale);
				WRITE(p, "  }\n");
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
					WRITE(p, "  v_color0 = tess.col;\n");
				else
					WRITE(p, "  v_color0 = color0;\n");
			} else {
				WRITE(p, "  v_color0 = base.matambientalpha;\n");
			}
			if (lmode) {
				WRITE(p, "  v_color1 = vec3(0.0);\n");
			}
		}

		bool scaleUV = !isModeThrough && (uvGenMode == GE_TEXMAP_TEXTURE_COORDS || uvGenMode == GE_TEXMAP_UNKNOWN);

		// Step 3: UV generation
		if (doTexture) {
			switch (uvGenMode) {
			case GE_TEXMAP_TEXTURE_COORDS:  // Scale-offset. Easy.
			case GE_TEXMAP_UNKNOWN: // Not sure what this is, but Riviera uses it.  Treating as coords works.
				if (scaleUV) {
					if (hasTexcoord) {
						if (doBezier || doSpline)
							WRITE(p, "  v_texcoord = vec3(tess.tex.xy * base.uvscaleoffset.xy + base.uvscaleoffset.zw, 0.0);\n");
						else
							WRITE(p, "  v_texcoord = vec3(texcoord.xy * base.uvscaleoffset.xy, 0.0);\n");
					} else {
						WRITE(p, "  v_texcoord = vec3(0.0);\n");
					}
				} else {
					if (hasTexcoord) {
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
			{
				std::string lightFactor0 = StringFromFormat("(length(light.pos[%i]) == 0.0 ? worldnormal.z : dot(normalize(light.pos[%i]), worldnormal))", ls0, ls0);
				std::string lightFactor1 = StringFromFormat("(length(light.pos[%i]) == 0.0 ? worldnormal.z : dot(normalize(light.pos[%i]), worldnormal))", ls1, ls1);
				WRITE(p, "  v_texcoord = vec3(base.uvscaleoffset.xy * vec2(1.0 + %s, 1.0 + %s) * 0.5, 1.0);\n", lightFactor0.c_str(), lightFactor1.c_str());
			}
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

	if (!isModeThrough && gstate_c.Supports(GPU_SUPPORTS_VS_RANGE_CULLING)) {
		WRITE(p, "  vec3 projPos = outPos.xyz / outPos.w;\n");
		// Vertex range culling doesn't happen when depth is clamped, so only do this if in range.
		WRITE(p, "  if (base.cullRangeMin.w <= 0.0 || (projPos.z >= base.cullRangeMin.z && projPos.z <= base.cullRangeMax.z)) {\n");
		const char *outMin = "projPos.x < base.cullRangeMin.x || projPos.y < base.cullRangeMin.y || projPos.z < base.cullRangeMin.z";
		const char *outMax = "projPos.x > base.cullRangeMax.x || projPos.y > base.cullRangeMax.y || projPos.z > base.cullRangeMax.z";
		WRITE(p, "    if (%s || %s) {\n", outMin, outMax);
		WRITE(p, "      outPos.w = base.cullRangeMax.w;\n");
		WRITE(p, "    }\n");
		WRITE(p, "  }\n");
	}
	WRITE(p, "  gl_Position = outPos;\n");

	WRITE(p, "}\n");
	return true;
}
