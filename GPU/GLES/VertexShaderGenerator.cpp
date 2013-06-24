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

#include <stdio.h>
#include <locale.h>

#if defined(_WIN32) && defined(_DEBUG)
#include <windows.h>
#endif

#include "../ge_constants.h"
#include "../GPUState.h"
#include "../../Core/Config.h"

#include "VertexShaderGenerator.h"

// SDL 1.2 on Apple does not have support for OpenGL 3 and hence needs
// special treatment in the shader generator.
#ifdef __APPLE__
#define FORCE_OPENGL_2_0
#endif

#undef WRITE

#define WRITE p+=sprintf

bool CanUseHardwareTransform(int prim) {
	if (!g_Config.bHardwareTransform)
		return false;
	return !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES;
}

// prim so we can special case for RECTANGLES :(
void ComputeVertexShaderID(VertexShaderID *id, int prim, bool useHWTransform) {
	const u32 vertType = gstate.vertType;
	int doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool doTextureProjection = gstate.getUVGenMode() == 1;

	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0;
	bool hasBones = (vertType & GE_VTYPE_WEIGHT_MASK) != 0;
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool lmode = (gstate.lmode & 1) && gstate.isLightingEnabled();

	memset(id->d, 0, sizeof(id->d));
	id->d[0] = lmode & 1;
	id->d[0] |= ((int)gstate.isModeThrough()) << 1;
	id->d[0] |= ((int)enableFog) << 2;
	id->d[0] |= doTexture << 3;
	id->d[0] |= (hasColor & 1) << 4;
	if (doTexture) {
		id->d[0] |= (gstate_c.flipTexture & 1) << 5;
		id->d[0] |= (doTextureProjection & 1) << 6;
	}

	if (useHWTransform) {
		id->d[0] |= 1 << 8;
		id->d[0] |= (hasNormal & 1) << 9;
		id->d[0] |= (hasBones & 1) << 10;

		// UV generation mode
		id->d[0] |= gstate.getUVGenMode() << 16;

		// The next bits are used differently depending on UVgen mode
		if (gstate.getUVGenMode() == 1) {
			id->d[0] |= gstate.getUVProjMode() << 18;
		} else if (gstate.getUVGenMode() == 2) {
			id->d[0] |= gstate.getUVLS0() << 18;
			id->d[0] |= gstate.getUVLS1() << 20;
		}

		// Bones
		id->d[0] |= (gstate.getNumBoneWeights() - 1) << 22;

		// Okay, d[1] coming up. ==============

		id->d[1] |= gstate.isLightingEnabled() << 24;
		id->d[1] |= ((vertType & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT) << 25;
		if (gstate.isLightingEnabled() || gstate.getUVGenMode() == 2) {
			// Light bits
			for (int i = 0; i < 4; i++) {
				id->d[1] |= (gstate.ltype[i] & 3) << (i * 4);
				id->d[1] |= ((gstate.ltype[i] >> 8) & 3) << (i * 4 + 2);
			}
			id->d[1] |= (gstate.materialupdate & 7) << 16;
			for (int i = 0; i < 4; i++) {
				id->d[1] |= (gstate.lightEnable[i] & 1) << (20 + i);
			}
		}
	}
}

static const char * const boneWeightAttrDecl[8] = {
	"attribute mediump float a_w1;\n",
	"attribute mediump vec2 a_w1;\n",
	"attribute mediump vec3 a_w1;\n",
	"attribute mediump vec4 a_w1;\n",
	"attribute mediump vec4 a_w1;\nattribute mediump float a_w2;\n",
	"attribute mediump vec4 a_w1;\nattribute mediump vec2 a_w2;\n",
	"attribute mediump vec4 a_w1;\nattribute mediump vec3 a_w2;\n",
	"attribute mediump vec4 a_w1;\nattribute mediump vec4 a_w2;\n",
};

enum DoLightComputation {
	LIGHT_OFF,
	LIGHT_SHADE,
	LIGHT_FULL,
};

void GenerateVertexShader(int prim, char *buffer, bool useHWTransform) {
	char *p = buffer;

// #define USE_FOR_LOOP

#if defined(USING_GLES2)
	WRITE(p, "#version 100\n");  // GLSL ES 1.0
	WRITE(p, "precision highp float;\n");

#elif !defined(FORCE_OPENGL_2_0)
	WRITE(p, "#version 110\n");
	// Remove lowp/mediump in non-mobile implementations
	WRITE(p, "#define lowp\n");
	WRITE(p, "#define mediump\n");
#else
	// Need to remove lowp/mediump for Mac
	WRITE(p, "#define lowp\n");
	WRITE(p, "#define mediump\n");
#endif
	const u32 vertType = gstate.vertType;

	int lmode = (gstate.lmode & 1) && gstate.isLightingEnabled();
	int doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();

	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0 || !useHWTransform;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0 && useHWTransform;
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
	bool flipV = gstate_c.flipTexture;
	bool doTextureProjection = gstate.getUVGenMode() == 1;

	DoLightComputation doLight[4] = {LIGHT_OFF, LIGHT_OFF, LIGHT_OFF, LIGHT_OFF};
	if (useHWTransform) {
		int shadeLight0 = gstate.getUVGenMode() == 2 ? gstate.getUVLS0() : -1;
		int shadeLight1 = gstate.getUVGenMode() == 2 ? gstate.getUVLS1() : -1;
		for (int i = 0; i < 4; i++) {
			if (i == shadeLight0 || i == shadeLight1)
				doLight[i] = LIGHT_SHADE;
			if ((gstate.lightingEnable & 1) && (gstate.lightEnable[i] & 1))
				doLight[i] = LIGHT_FULL;
		}
	}

	if ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
		WRITE(p, "%s", boneWeightAttrDecl[gstate.getNumBoneWeights() - 1]);
	}

	if (useHWTransform)
		WRITE(p, "attribute vec3 a_position;\n");
	else
		WRITE(p, "attribute vec4 a_position;\n");  // need to pass the fog coord in w

	if (useHWTransform && hasNormal)
		WRITE(p, "attribute mediump vec3 a_normal;\n");

	if (doTexture) {
		if (!useHWTransform && doTextureProjection)
			WRITE(p, "attribute vec3 a_texcoord;\n");
		else
			WRITE(p, "attribute vec2 a_texcoord;\n");
	}
	if (hasColor) {
		WRITE(p, "attribute lowp vec4 a_color0;\n");
		if (lmode && !useHWTransform)  // only software transform supplies color1 as vertex data
			WRITE(p, "attribute lowp vec3 a_color1;\n");
	}

	if (gstate.isModeThrough())	{
		WRITE(p, "uniform mat4 u_proj_through;\n");
	} else {
		WRITE(p, "uniform mat4 u_proj;\n");
		// Add all the uniforms we'll need to transform properly.
	}

	if (useHWTransform) {
		// When transforming by hardware, we need a great deal more uniforms...
		WRITE(p, "uniform mat4 u_world;\n");
		WRITE(p, "uniform mat4 u_view;\n");
		if (gstate.getUVGenMode() == 1)
			WRITE(p, "uniform mediump mat4 u_texmtx;\n");
		if ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
			int numBones = 1 + ((vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT);
#ifdef USE_BONE_ARRAY
			WRITE(p, "uniform mediump mat4 u_bone[%i];\n", numBones);
#else
			for (int i = 0; i < numBones; i++) {
				WRITE(p, "uniform mat4 u_bone%i;\n", i);
			}
#endif
		}
		if (gstate.getUVGenMode() == 0)
			WRITE(p, "uniform vec4 u_uvscaleoffset;\n");
		for (int i = 0; i < 4; i++) {
			if (doLight[i] != LIGHT_OFF) {
				// This is needed for shade mapping
				WRITE(p, "uniform vec3 u_lightpos%i;\n", i);
			}
			if (doLight[i] == LIGHT_FULL) {
				// These are needed for the full thing
				WRITE(p, "uniform mediump vec3 u_lightdir%i;\n", i);
				GELightType type = (GELightType)((gstate.ltype[i] >> 8) & 3);

				if (type != GE_LIGHTTYPE_DIRECTIONAL)
					WRITE(p, "uniform mediump vec3 u_lightatt%i;\n", i);

				if (type == GE_LIGHTTYPE_SPOT) {
					WRITE(p, "uniform mediump float u_lightangle%i;\n", i);
					WRITE(p, "uniform mediump float u_lightspotCoef%i;\n", i);
				}
				WRITE(p, "uniform lowp vec3 u_lightambient%i;\n", i);
				WRITE(p, "uniform lowp vec3 u_lightdiffuse%i;\n", i);

				GELightComputation comp = (GELightComputation)(gstate.ltype[i] & 3);
				if (comp != GE_LIGHTCOMP_ONLYDIFFUSE)
					WRITE(p, "uniform lowp vec3 u_lightspecular%i;\n", i);
			}
		}
		if (gstate.isLightingEnabled()) {
			WRITE(p, "uniform lowp vec4 u_ambient;\n");
			if ((gstate.materialupdate & 2) == 0)
				WRITE(p, "uniform lowp vec3 u_matdiffuse;\n");
			// if ((gstate.materialupdate & 4) == 0)
			WRITE(p, "uniform lowp vec4 u_matspecular;\n");  // Specular coef is contained in alpha
			WRITE(p, "uniform lowp vec3 u_matemissive;\n");
		}
	}

	if (useHWTransform || !hasColor)
		WRITE(p, "uniform lowp vec4 u_matambientalpha;\n");  // matambient + matalpha

	if (enableFog) {
		WRITE(p, "uniform vec2 u_fogcoef;\n");
	}

	WRITE(p, "varying lowp vec4 v_color0;\n");
	if (lmode) WRITE(p, "varying lowp vec3 v_color1;\n");
	if (doTexture) {
		if (doTextureProjection)
			WRITE(p, "varying vec3 v_texcoord;\n");
		else
			WRITE(p, "varying vec2 v_texcoord;\n");
	}
	if (enableFog) WRITE(p, "varying float v_fogdepth;\n");

	WRITE(p, "void main() {\n");

	if (!useHWTransform) {
		// Simple pass-through of vertex data to fragment shader
		if (doTexture)
			WRITE(p, "  v_texcoord = a_texcoord;\n");
		if (hasColor) {
			WRITE(p, "  v_color0 = a_color0;\n");
			if (lmode)
				WRITE(p, "  v_color1 = a_color1;\n");
		} else {
			WRITE(p, "  v_color0 = u_matambientalpha;\n");
			if (lmode)
				WRITE(p, "  v_color1 = vec3(0.0);\n");
		}
		if (enableFog) {
			WRITE(p, "  v_fogdepth = a_position.w;\n");
		}
		if (gstate.isModeThrough())	{
			WRITE(p, "  gl_Position = u_proj_through * vec4(a_position.xyz, 1.0);\n");
		} else {
			WRITE(p, "  gl_Position = u_proj * vec4(a_position.xyz, 1.0);\n");
		}
	} else {
		// Step 1: World Transform / Skinning
		if ((vertType & GE_VTYPE_WEIGHT_MASK) == GE_VTYPE_WEIGHT_NONE) {
			// No skinning, just standard T&L.
			WRITE(p, "  vec3 worldpos = (u_world * vec4(a_position.xyz, 1.0)).xyz;\n");
			if (hasNormal)
				WRITE(p, "  vec3 worldnormal = normalize((u_world * vec4(a_normal, 0.0)).xyz);\n");
			else
				WRITE(p, "  vec3 worldnormal = vec3(0.0, 0.0, 1.0);\n");
		} else {
			int numWeights = 1 + ((vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT);

			static const float rescale[4] = {0, 2*127.5f/128.f, 2*32767.5f/32768.f, 2.0f};
			float factor = rescale[(vertType & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT];

			static const char * const boneWeightAttr[8] = {
				"a_w1.x", "a_w1.y", "a_w1.z", "a_w1.w",
				"a_w2.x", "a_w2.y", "a_w2.z", "a_w2.w",
			};

#if defined(USE_FOR_LOOP) && defined(USE_BONE_ARRAY)

			// To loop through the weights, we unfortunately need to put them in a float array.
			// GLSL ES sucks - no way to directly initialize an array!
			switch (numWeights) {
			case 1: WRITE(p, "  float w[1]; w[0] = a_w1;\n"); break;
			case 2: WRITE(p, "  float w[2]; w[0] = a_w1.x; w[1] = a_w1.y;\n"); break;
			case 3: WRITE(p, "  float w[3]; w[0] = a_w1.x; w[1] = a_w1.y; w[2] = a_w1.z;\n"); break;
			case 4: WRITE(p, "  float w[4]; w[0] = a_w1.x; w[1] = a_w1.y; w[2] = a_w1.z; w[3] = a_w1.w;\n"); break;
			case 5: WRITE(p, "  float w[5]; w[0] = a_w1.x; w[1] = a_w1.y; w[2] = a_w1.z; w[3] = a_w1.w; w[4] = a_w2;\n"); break;
			case 6: WRITE(p, "  float w[6]; w[0] = a_w1.x; w[1] = a_w1.y; w[2] = a_w1.z; w[3] = a_w1.w; w[4] = a_w2.x; w[5] = a_w2.y;\n"); break;
			case 7: WRITE(p, "  float w[7]; w[0] = a_w1.x; w[1] = a_w1.y; w[2] = a_w1.z; w[3] = a_w1.w; w[4] = a_w2.x; w[5] = a_w2.y; w[6] = a_w2.z;\n"); break;
			case 8: WRITE(p, "  float w[8]; w[0] = a_w1.x; w[1] = a_w1.y; w[2] = a_w1.z; w[3] = a_w1.w; w[4] = a_w2.x; w[5] = a_w2.y; w[6] = a_w2.z; w[7] = a_w2.w;\n"); break;
			}

			WRITE(p, "  mat4 skinMatrix = w[0] * u_bone[0];\n");
			if (numWeights > 1) {
				WRITE(p, "  for (int i = 1; i < %i; i++) {\n", numWeights);
				WRITE(p, "    skinMatrix += w[i] * u_bone[i];\n");
				WRITE(p, "  }\n");
			}

#else

#ifdef USE_BONE_ARRAY
			if (numWeights == 1)
				WRITE(p, "  mat4 skinMatrix = a_w1 * u_bone[0]");
			else
				WRITE(p, "  mat4 skinMatrix = a_w1.x * u_bone[0]");
			for (int i = 1; i < numWeights; i++) {
				const char *weightAttr = boneWeightAttr[i];
				// workaround for "cant do .x of scalar" issue
				if (numWeights == 1 && i == 0) weightAttr = "a_w1";
				if (numWeights == 5 && i == 4) weightAttr = "a_w2";
				WRITE(p, " + %s * u_bone[%i]", weightAttr, i);
			}
#else
			// Uncomment this to screw up bone shaders to check the vertex shader software fallback
			// WRITE(p, "THIS SHOULD ERROR! #error");
			if (numWeights == 1)
				WRITE(p, "  mat4 skinMatrix = a_w1 * u_bone0");
			else
				WRITE(p, "  mat4 skinMatrix = a_w1.x * u_bone0");
			for (int i = 1; i < numWeights; i++) {
				const char *weightAttr = boneWeightAttr[i];
				// workaround for "cant do .x of scalar" issue
				if (numWeights == 1 && i == 0) weightAttr = "a_w1";
				if (numWeights == 5 && i == 4) weightAttr = "a_w2";
				WRITE(p, " + %s * u_bone%i", weightAttr, i);
			}
#endif

#endif

			WRITE(p, ";\n");

			// Trying to simplify this results in bugs in LBP...
			WRITE(p, "  vec3 skinnedpos = (skinMatrix * vec4(a_position, 1.0)).xyz * %f;\n", factor);
			WRITE(p, "  vec3 worldpos = (u_world * vec4(skinnedpos, 1.0)).xyz;\n");

			if (hasNormal) {
				WRITE(p, "  vec3 skinnednormal = (skinMatrix * vec4(a_normal, 0.0)).xyz * %f;\n", factor);
				WRITE(p, "  vec3 worldnormal = normalize((u_world * vec4(skinnednormal, 0.0)).xyz);\n");
			} else {
				WRITE(p, "  vec3 worldnormal = (u_world * (skinMatrix * vec4(0.0, 0.0, 1.0, 0.0))).xyz;\n");
			}
		}

		WRITE(p, "  vec4 viewPos = u_view * vec4(worldpos, 1.0);\n");

		// Final view and projection transforms.
		WRITE(p, "  gl_Position = u_proj * viewPos;\n");

		// TODO: Declare variables for dots for shade mapping if needed.

		const char *ambientStr = (gstate.materialupdate & 1) ? (hasColor ? "a_color0" : "u_matambientalpha") : "u_matambientalpha";
		const char *diffuseStr = (gstate.materialupdate & 2) ? (hasColor ? "a_color0.rgb" : "u_matambientalpha.rgb") : "u_matdiffuse";
		const char *specularStr = (gstate.materialupdate & 4) ? (hasColor ? "a_color0.rgb" : "u_matambientalpha.rgb") : "u_matspecular.rgb";

		bool diffuseIsZero = true;
		bool specularIsZero = true;
		bool distanceNeeded = false;

		if (gstate.isLightingEnabled()) {
			WRITE(p, "  lowp vec4 lightSum0 = u_ambient * %s + vec4(u_matemissive, 0.0);\n", ambientStr);

			for (int i = 0; i < 4; i++) {
				if (doLight[i] != LIGHT_FULL)
					continue;
				diffuseIsZero = false;
				GELightComputation comp = (GELightComputation)(gstate.ltype[i] & 3);
				if (comp != GE_LIGHTCOMP_ONLYDIFFUSE)
					specularIsZero = false;
				GELightType type = (GELightType)((gstate.ltype[i] >> 8) & 3);
				if (type != GE_LIGHTTYPE_DIRECTIONAL)
					distanceNeeded = true;
			}

			if (!specularIsZero) {
				WRITE(p, "  lowp vec3 lightSum1 = vec3(0.0);\n");
				WRITE(p, "  mediump vec3 halfVec;\n");
			}
			if (!diffuseIsZero) {
				WRITE(p, "  vec3 toLight;\n");
				WRITE(p, "  lowp vec3 diffuse;\n");
			}
			if (distanceNeeded) {
				WRITE(p, "  float distance;\n");
				WRITE(p, "  lowp float lightScale;\n");
			}
		}

		// Calculate lights if needed. If shade mapping is enabled, lights may need to be
		// at least partially calculated.
		for (int i = 0; i < 4; i++) {
			if (doLight[i] != LIGHT_FULL)
				continue;

			GELightComputation comp = (GELightComputation)(gstate.ltype[i] & 3);
			GELightType type = (GELightType)((gstate.ltype[i] >> 8) & 3);

			if (type == GE_LIGHTTYPE_DIRECTIONAL) {
				// We prenormalize light positions for directional lights.
				WRITE(p, "  toLight = u_lightpos%i;\n", i);
			} else {
				WRITE(p, "  toLight = u_lightpos%i - worldpos;\n", i);
				WRITE(p, "  distance = length(toLight);\n");
				WRITE(p, "  toLight /= distance;\n");
			}

			bool doSpecular = (comp != GE_LIGHTCOMP_ONLYDIFFUSE);
			bool poweredDiffuse = comp == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE;

			if (poweredDiffuse) {
				WRITE(p, "  mediump float dot%i = pow(dot(toLight, worldnormal), u_matspecular.a);\n", i);
			} else {
				WRITE(p, "  mediump float dot%i = dot(toLight, worldnormal);\n", i);
			}

			const char *timesLightScale = " * lightScale";

			// Attenuation
			switch (type) {
			case GE_LIGHTTYPE_DIRECTIONAL:
				timesLightScale = "";
				break;
			case GE_LIGHTTYPE_POINT:
				WRITE(p, "  lightScale = clamp(1.0 / dot(u_lightatt%i, vec3(1.0, distance, distance*distance)), 0.0, 1.0);\n", i);
				break;
			case GE_LIGHTTYPE_SPOT:
				WRITE(p, "  lowp float angle%i = dot(normalize(u_lightdir%i), toLight);\n", i, i);
				WRITE(p, "  if (angle%i >= u_lightangle%i) {\n", i, i);
				WRITE(p, "    lightScale = clamp(1.0 / dot(u_lightatt%i, vec3(1.0, distance, distance*distance)), 0.0, 1.0) * pow(angle%i, u_lightspotCoef%i);\n", i, i, i);
				WRITE(p, "  } else {\n");
				WRITE(p, "    lightScale = 0.0;\n");
				WRITE(p, "  }\n");
				break;
			default:
				// ILLEGAL
				break;
			}

			WRITE(p, "  diffuse = (u_lightdiffuse%i * %s) * max(dot%i, 0.0);\n", i, diffuseStr, i);
			if (doSpecular) {
				WRITE(p, "  halfVec = normalize(toLight + vec3(0.0, 0.0, 1.0));\n");
				WRITE(p, "  dot%i = dot(halfVec, worldnormal);\n", i);
				WRITE(p, "  if (dot%i > 0.0)\n", i);
				WRITE(p, "    lightSum1 += u_lightspecular%i * %s * (pow(dot%i, u_matspecular.a) %s);\n", i, specularStr, i, timesLightScale);
			}
			WRITE(p, "  lightSum0.rgb += (u_lightambient%i * %s.rgb + diffuse)%s;\n", i, ambientStr, timesLightScale);
		}

		if (gstate.isLightingEnabled()) {
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
				WRITE(p, "  v_color0 = a_color0;\n");
			} else {
				WRITE(p, "  v_color0 = u_matambientalpha;\n");
			}
			if (lmode)
				WRITE(p, "  v_color1 = vec3(0.0);\n");
		}

		// Step 3: UV generation
		if (doTexture) {
			switch (gstate.getUVGenMode()) {
			case 0:  // Scale-offset. Easy.
				WRITE(p, "  v_texcoord = a_texcoord * u_uvscaleoffset.xy + u_uvscaleoffset.zw;\n");
				break;

			case 1:  // Projection mapping.
				{
					const char *temp_tc;
					switch (gstate.getUVProjMode()) {
					case 0:  // Use model space XYZ as source
						temp_tc = "vec4(a_position.xyz, 1.0)";
						break;
					case 1:  // Use unscaled UV as source
						temp_tc = "vec4(a_texcoord.xy * 2.0, 0.0, 1.0)";
						break;
					case 2:  // Use normalized transformed normal as source
						if (hasNormal)
							temp_tc = "vec4(normalize(a_normal), 1.0)";
						else
							temp_tc = "vec4(0.0, 0.0, 1.0, 1.0)";
						break;
					case 3:  // Use non-normalized transformed normal as source
						if (hasNormal)
							temp_tc = "vec4(a_normal, 1.0)";
						else
							temp_tc = "vec4(0.0, 0.0, 1.0, 1.0)";
						break;
					}
					WRITE(p, "  v_texcoord = (u_texmtx * %s).xyz;\n", temp_tc);
				}
				// Transform by texture matrix. XYZ as we are doing projection mapping.
				break;

			case 2:  // Shade mapping - use dots from light sources.
				WRITE(p, "  v_texcoord = vec2(1.0 + dot(normalize(u_lightpos%i), worldnormal), 1.0 - dot(normalize(u_lightpos%i), worldnormal)) * 0.5;\n", gstate.getUVLS0(), gstate.getUVLS1());
				break;

			case 3:
				// ILLEGAL
				break;
			}

			if (flipV) {
				if (throughmode)
					WRITE(p, "  v_texcoord.y = 1.0 - v_texcoord.y;\n");
				else
					WRITE(p, "  v_texcoord.y = 1.0 - v_texcoord.y * 2.0;\n");
			}
		}

		// Compute fogdepth
		if (enableFog)
			WRITE(p, "  v_fogdepth = (viewPos.z + u_fogcoef.x) * u_fogcoef.y;\n");
	}
	WRITE(p, "}\n");
}

