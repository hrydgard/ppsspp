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
void ComputeVertexShaderID(VertexShaderID *id, int prim) {
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
	if (doTexture)
	{
		id->d[0] |= (gstate_c.flipTexture & 1) << 5;
		id->d[0] |= (doTextureProjection & 1) << 6;
	}

	if (CanUseHardwareTransform(prim)) {
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
	"attribute float a_weight0123;\n",
	"attribute vec2 a_weight0123;\n",
	"attribute vec3 a_weight0123;\n",
	"attribute vec4 a_weight0123;\n",
	"attribute vec4 a_weight0123;\nattribute float a_weight4567;\n",
	"attribute vec4 a_weight0123;\nattribute vec2 a_weight4567;\n",
	"attribute vec4 a_weight0123;\nattribute vec3 a_weight4567;\n",
	"attribute vec4 a_weight0123;\nattribute vec4 a_weight4567;\n",
};

static const char * const boneWeightAttr[8] = {
	"a_weight0123.x",
	"a_weight0123.y",
	"a_weight0123.z",
	"a_weight0123.w",
	"a_weight4567.x",
	"a_weight4567.y",
	"a_weight4567.z",
	"a_weight4567.w",
};

enum DoLightComputation {
	LIGHT_OFF,
	LIGHT_SHADE,
	LIGHT_FULL,
};

void GenerateVertexShader(int prim, char *buffer) {
	// Apparently, sprintf can output "," unless we do this, which is a disaster for this line later on:
	// WRITE(p, "  worldpos = (u_world * vec4(worldpos * %f, 1.0)).xyz;\n", factor);
	setlocale( LC_ALL, "C" );

	char *p = buffer;
#if defined(USING_GLES2)
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

	bool hwXForm = CanUseHardwareTransform(prim);
	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0 || !hwXForm;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0 && hwXForm;
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
	bool flipV = gstate_c.flipTexture;
	bool doTextureProjection = gstate.getUVGenMode() == 1;

	DoLightComputation doLight[4] = {LIGHT_OFF, LIGHT_OFF, LIGHT_OFF, LIGHT_OFF};
	if (hwXForm) {
		int shadeLight0 = gstate.getUVGenMode() == 2 ? gstate.getUVLS0() : -1;
		int shadeLight1 = gstate.getUVGenMode() == 2 ? gstate.getUVLS1() : -1;
		for (int i = 0; i < 4; i++) {
			if (!hasNormal)
				continue;
			if (i == shadeLight0 || i == shadeLight1)
				doLight[i] = LIGHT_SHADE;
			if ((gstate.lightingEnable & 1) && (gstate.lightEnable[i] & 1))
				doLight[i] = LIGHT_FULL;
		}
	}

	if ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
		WRITE(p, "%s", boneWeightAttrDecl[gstate.getNumBoneWeights() - 1]);
	}

	if (hwXForm)
		WRITE(p, "attribute vec3 a_position;\n");
	else
		WRITE(p, "attribute vec4 a_position;\n");  // need to pass the fog coord in w

	if (doTexture) {
		if (!hwXForm && doTextureProjection)
			WRITE(p, "attribute vec3 a_texcoord;\n");
		else
			WRITE(p, "attribute vec2 a_texcoord;\n");
	}
	if (hasColor) {
		WRITE(p, "attribute lowp vec4 a_color0;\n");
		if (lmode && !hwXForm)  // only software transform supplies color1 as vertex data
			WRITE(p, "attribute lowp vec3 a_color1;\n");
	}

	if (hwXForm && hasNormal)
		WRITE(p, "attribute mediump vec3 a_normal;\n");

	if (gstate.isModeThrough())	{
		WRITE(p, "uniform mat4 u_proj_through;\n");
	} else {
		WRITE(p, "uniform mat4 u_proj;\n");
		// Add all the uniforms we'll need to transform properly.
	}

	if (hwXForm || !hasColor)
		WRITE(p, "uniform lowp vec4 u_matambientalpha;\n");  // matambient + matalpha

	if (enableFog) {
		WRITE(p, "uniform vec2 u_fogcoef;\n");
	}

	if (hwXForm) {
		// When transforming by hardware, we need a great deal more uniforms...
		WRITE(p, "uniform mat4 u_world;\n");
		WRITE(p, "uniform mat4 u_view;\n");
		if (gstate.getUVGenMode() == 0)
			WRITE(p, "uniform vec4 u_uvscaleoffset;\n");
		else if (gstate.getUVGenMode() == 1)
			WRITE(p, "uniform mat4 u_texmtx;\n");
		if ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
			int numBones = 1 + ((vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT);
			for (int i = 0; i < numBones; i++) {
				WRITE(p, "uniform mat4 u_bone%i;\n", i);
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
		for (int i = 0; i < 4; i++) {
			if (doLight[i] != LIGHT_OFF) {
				// This is needed for shade mapping
				WRITE(p, "uniform vec3 u_lightpos%i;\n", i);
			}
			if (doLight[i] == LIGHT_FULL) {
				// These are needed for the full thing
				WRITE(p, "uniform vec3 u_lightdir%i;\n", i);
				WRITE(p, "uniform vec3 u_lightatt%i;\n", i);
				WRITE(p, "uniform float u_lightangle%i;\n", i);
				WRITE(p, "uniform float u_lightspotCoef%i;\n", i);

				WRITE(p, "uniform lowp vec3 u_lightambient%i;\n", i);
				WRITE(p, "uniform lowp vec3 u_lightdiffuse%i;\n", i);
				WRITE(p, "uniform lowp vec3 u_lightspecular%i;\n", i);
			}
		}
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

	if (!hwXForm) {
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
				WRITE(p, "  vec3 worldnormal = (u_world * vec4(a_normal, 0.0)).xyz;\n");
		} else {
			WRITE(p, "  vec3 worldpos = vec3(0.0);\n");
			if (hasNormal)
				WRITE(p, "  vec3 worldnormal = vec3(0.0);\n");

			int numWeights = 1 + ((vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT);
			static const float rescale[4] = {0, 2*127.5f/128.f, 2*32767.5f/32768.f, 2.0f};
			float factor = rescale[(vertType & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT];
			for (int i = 0; i < numWeights; i++) {
				const char *weightAttr = boneWeightAttr[i];
				// workaround for "cant do .x of scalar" issue
				if (numWeights == 1 && i == 0) weightAttr = "a_weight0123";
				if (numWeights == 5 && i == 4) weightAttr = "a_weight4567";
				WRITE(p, "  worldpos += %s * (u_bone%i * vec4(a_position.xyz, 1.0)).xyz;\n", weightAttr, i);
				if (hasNormal)
					WRITE(p, "  worldnormal += %s * (u_bone%i * vec4(a_normal, 0.0)).xyz;\n", weightAttr, i);
			}

			// Finally, multiply by world matrix (yes, we have to).
			WRITE(p, "  worldpos = (u_world * vec4(worldpos * %f, 1.0)).xyz;\n", factor);
			if (hasNormal)
				WRITE(p, "  worldnormal = (u_world * vec4(worldnormal, 0.0)).xyz;\n");
		}
		if (hasNormal)
			WRITE(p, "  worldnormal = normalize(worldnormal);\n");

		WRITE(p, "  vec4 viewPos = u_view * vec4(worldpos, 1.0);\n");

		// Step 2: Color/Lighting
		if (hasColor) {
			WRITE(p, "  lowp vec3 unlitColor = a_color0.rgb;\n");
		} else {
			WRITE(p, "  lowp vec3 unlitColor = u_matambientalpha.rgb;\n");
		}
		// TODO: Declare variables for dots for shade mapping if needed.

		const char *ambient = (gstate.materialupdate & 1) ? (hasColor ? "a_color0" : "u_matambientalpha") : "u_matambientalpha";
		const char *diffuse = (gstate.materialupdate & 2) ? "unlitColor" : "u_matdiffuse";
		const char *specular = (gstate.materialupdate & 4) ? "unlitColor" : "u_matspecular.rgb";

		if (gstate.isLightingEnabled()) {
			WRITE(p, "  lowp vec4 lightSum0 = u_ambient * %s + vec4(u_matemissive, 0.0);\n", ambient);
			WRITE(p, "  lowp vec3 lightSum1 = vec3(0.0);\n");
		}

		// Calculate lights if needed. If shade mapping is enabled, lights may need to be
		// at least partially calculated.
		for (int i = 0; i < 4; i++) {
			if (doLight[i] != LIGHT_FULL)
				continue;

			GELightComputation comp = (GELightComputation)(gstate.ltype[i] & 3);
			GELightType type = (GELightType)((gstate.ltype[i] >> 8) & 3);

			if (type == GE_LIGHTTYPE_DIRECTIONAL)
				WRITE(p, "  vec3 toLight%i = u_lightpos%i;\n", i, i);
			else
				WRITE(p, "  vec3 toLight%i = u_lightpos%i - worldpos;\n", i, i);

			bool doSpecular = (comp != GE_LIGHTCOMP_ONLYDIFFUSE);
			bool poweredDiffuse = comp == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE;

			WRITE(p, "  float dot%i = dot(normalize(toLight%i), worldnormal);\n", i, i);
			WRITE(p, "  float distance%i = length(toLight%i);\n", i, i);
			WRITE(p, "  float lightScale%i = 0.0;\n", i);
			WRITE(p, "  float angle%i = 0.0;\n", i);

			if (poweredDiffuse) {
				WRITE(p, "  dot%i = pow(dot%i, u_matspecular.a);\n", i, i);
			}

			// Attenuation
			switch (type) {
			case GE_LIGHTTYPE_DIRECTIONAL:
				WRITE(p, "  lightScale%i = 1.0;\n", i);
				break;
			case GE_LIGHTTYPE_POINT:
				WRITE(p, "  lightScale%i = clamp(1.0 / dot(u_lightatt%i, vec3(1.0, distance%i, distance%i*distance%i)), 0.0, 1.0);\n", i, i, i, i, i);
				break;
			case GE_LIGHTTYPE_SPOT:
				WRITE(p, "  angle%i = dot(normalize(u_lightdir%i), normalize(toLight%i));\n", i, i, i);
				WRITE(p, "  if (angle%i >= u_lightangle%i) {\n", i, i);
				WRITE(p, "    lightScale%i = clamp(1.0 / dot(u_lightatt%i, vec3(1.0, distance%i, distance%i*distance%i)), 0.0, 1.0) * pow(angle%i, u_lightspotCoef%i);\n", i, i, i, i, i, i, i);
				WRITE(p, "  }\n");
				break;
			default:
				// ILLEGAL
				break;
			}

			WRITE(p, "  vec3 diffuse%i = (u_lightdiffuse%i * %s) * max(dot%i, 0.0);\n", i, i, diffuse, i);
			if (doSpecular) {
				WRITE(p, "  vec3 halfVec%i = normalize(normalize(toLight%i) + vec3(0.0, 0.0, 1.0));\n", i, i);
				WRITE(p, "  dot%i = dot(halfVec%i, worldnormal);\n", i, i);
				WRITE(p, "  if (dot%i > 0.0)\n", i);
				WRITE(p, "    lightSum1 += u_lightspecular%i * %s * (pow(dot%i, u_matspecular.a) * lightScale%i);\n", i, specular, i, i);
			}
			WRITE(p, "  lightSum0 += vec4((u_lightambient%i + diffuse%i)*lightScale%i, 0.0);\n", i, i, i);
		}

		if (gstate.isLightingEnabled()) {
			// Sum up ambient, emissive here.
			WRITE(p, "  v_color0 = clamp(lightSum0, 0.0, 1.0);\n");
			if (lmode) {
				WRITE(p, "  v_color1 = clamp(lightSum1, 0.0, 1.0);\n");
			} else {
				WRITE(p, "  v_color0 = clamp(v_color0 + vec4(lightSum1, 0.0), 0.0, 1.0);\n");
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
				switch (gstate.getUVProjMode()) {
				case 0:  // Use model space XYZ as source
					WRITE(p, "  vec3 temp_tc = a_position.xyz;\n");
					break;
				case 1:  // Use unscaled UV as source
					WRITE(p, "  vec3 temp_tc = vec3(a_texcoord.xy * 2.0, 0.0);\n");
					break;
				case 2:  // Use normalized transformed normal as source
					WRITE(p, "  vec3 temp_tc = normalize(a_normal);\n");
					break;
				case 3:  // Use non-normalized transformed normal as source
					WRITE(p, "  vec3 temp_tc = a_normal;\n");
					break;
				}
				// Transform by texture matrix. XYZ as we are doing projection mapping.
				WRITE(p, "  v_texcoord = (u_texmtx * vec4(temp_tc, 1.0)).xyz;\n");
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

		// Step 4: Final view and projection transforms.
		WRITE(p, "  gl_Position = u_proj * viewPos;\n");
	}
	WRITE(p, "}\n");
}

