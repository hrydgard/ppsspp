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

#include "gfx_es2/gpu_features.h"

#if defined(_WIN32) && defined(_DEBUG)
#include "Common/CommonWindows.h"
#endif

#include "base/stringutil.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "Core/Config.h"
#include "GPU/GLES/VertexShaderGenerator.h"

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

int TranslateNumBones(int bones) {
	if (!bones) return 0;
	if (bones < 4) return 4;
	// if (bones < 8) return 8;   I get drawing problems in FF:CC with this!
	return bones;
}

// prim so we can special case for RECTANGLES :(
void ComputeVertexShaderID(VertexShaderID *id, u32 vertType, int prim, bool useHWTransform) {
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
	bool doShadeMapping = gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP;

	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0;
	bool hasTexcoord = (vertType & GE_VTYPE_TC_MASK) != 0;
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();

	int id0 = 0;
	int id1 = 1;

	id0 = lmode & 1;
	id0 |= (gstate.isModeThrough() & 1) << 1;
	id0 |= (enableFog & 1) << 2;
	id0 |= (hasColor & 1) << 3;
	if (doTexture) {
		id0 |= 1 << 4;
		id0 |= (gstate_c.flipTexture & 1) << 5;
		id0 |= (doTextureProjection & 1) << 6;
	}

	if (useHWTransform) {
		id0 |= 1 << 8;
		id0 |= (hasNormal & 1) << 9;

		// UV generation mode
		id0 |= gstate.getUVGenMode() << 16;

		// The next bits are used differently depending on UVgen mode
		if (doTextureProjection) {
			id0 |= gstate.getUVProjMode() << 18;
		} else if (doShadeMapping) {
			id0 |= gstate.getUVLS0() << 18;
			id0 |= gstate.getUVLS1() << 20;
		}

		// Bones
		if (vertTypeIsSkinningEnabled(vertType))
			id0 |= (TranslateNumBones(vertTypeGetNumBoneWeights(vertType)) - 1) << 22;

		// Okay, d[1] coming up. ==============

		if (gstate.isLightingEnabled() || doShadeMapping) {
			// Light bits
			for (int i = 0; i < 4; i++) {
				id1 |= gstate.getLightComputation(i) << (i * 4);
				id1 |= gstate.getLightType(i) << (i * 4 + 2);
			}
			id1 |= (gstate.materialupdate & 7) << 16;
			for (int i = 0; i < 4; i++) {
				id1 |= (gstate.isLightChanEnabled(i) & 1) << (20 + i);
			}
			// doShadeMapping is stored as UVGenMode, so this is enough for isLightingEnabled.
			id1 |= 1 << 24;
		}
		// 2 bits.
		id1 |= (vertTypeGetWeightMask(vertType) >> GE_VTYPE_WEIGHT_SHIFT) << 25;
		id1 |= (gstate.areNormalsReversed() & 1) << 27;
		if (doTextureProjection && gstate.getUVProjMode() == GE_PROJMAP_UV) {
			id1 |= ((vertType & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT) << 28;  // two bits
		} else {
			id1 |= (hasTexcoord & 1) << 28;
		}
	}

	id->d[0] = id0;
	id->d[1] = id1;
}

static const char * const boneWeightAttrDecl[9] = {
	"#ERROR#",
	"attribute mediump float w1;\n",
	"attribute mediump vec2 w1;\n",
	"attribute mediump vec3 w1;\n",
	"attribute mediump vec4 w1;\n",
	"attribute mediump vec4 w1;\nattribute mediump float w2;\n",
	"attribute mediump vec4 w1;\nattribute mediump vec2 w2;\n",
	"attribute mediump vec4 w1;\nattribute mediump vec3 w2;\n",
	"attribute mediump vec4 w1, w2;\n",
};

static const char * const boneWeightInDecl[9] = {
	"#ERROR#",
	"in mediump float w1;\n",
	"in mediump vec2 w1;\n",
	"in mediump vec3 w1;\n",
	"in mediump vec4 w1;\n",
	"in mediump vec4 w1;\nin mediump float w2;\n",
	"in mediump vec4 w1;\nin mediump vec2 w2;\n",
	"in mediump vec4 w1;\nin mediump vec3 w2;\n",
	"in mediump vec4 w1, w2;\n",
};

enum DoLightComputation {
	LIGHT_OFF,
	LIGHT_SHADE,
	LIGHT_FULL,
};

void GenerateVertexShader(int prim, u32 vertType, char *buffer, bool useHWTransform) {
	char *p = buffer;

// #define USE_FOR_LOOP

	// In GLSL ES 3.0, you use "out" variables instead.
	bool glslES30 = false;
	const char *varying = "varying";
	const char *attribute = "attribute";
	const char * const * boneWeightDecl = boneWeightAttrDecl;
	bool highpFog = false;

#if defined(USING_GLES2)
	// Let's wait until we have a real use for this.
	// ES doesn't support dual source alpha :(
	if (gl_extensions.GLES3) {
		WRITE(p, "#version 300 es\n");
		glslES30 = true;
	} else {
		WRITE(p, "#version 100\n");  // GLSL ES 1.0
	}
	WRITE(p, "precision highp float;\n");

	// PowerVR needs highp to do the fog in MHU correctly.
	// Others don't, and some can't handle highp in the fragment shader.
	highpFog = gl_extensions.gpuVendor == GPU_VENDOR_POWERVR;
#elif !defined(FORCE_OPENGL_2_0)
	if (gl_extensions.VersionGEThan(3, 3, 0)) {
		glslES30 = true;
		WRITE(p, "#version 330\n");
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	} else if (gl_extensions.VersionGEThan(3, 0, 0)) {
		WRITE(p, "#version 130\n");
		// Remove lowp/mediump in non-mobile non-glsl 3 implementations
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	} else {
		WRITE(p, "#version 110\n");
		// Remove lowp/mediump in non-mobile non-glsl 3 implementations
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	}
#else
	// Need to remove lowp/mediump for Mac
	WRITE(p, "#define lowp\n");
	WRITE(p, "#define mediump\n");
	WRITE(p, "#define highp\n");
#endif

	if (glslES30) {
		attribute = "in";
		varying = "out";
		boneWeightDecl = boneWeightInDecl;
	}

	int lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
	bool doShadeMapping = gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP;

	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0 || !useHWTransform;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0 && useHWTransform;
	bool hasTexcoord = (vertType & GE_VTYPE_TC_MASK) != 0 || !useHWTransform;
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
	bool flipV = gstate_c.flipTexture;  // This also means that we are texturing from a render target
	bool flipNormal = gstate.areNormalsReversed();

	DoLightComputation doLight[4] = {LIGHT_OFF, LIGHT_OFF, LIGHT_OFF, LIGHT_OFF};
	if (useHWTransform) {
		int shadeLight0 = doShadeMapping ? gstate.getUVLS0() : -1;
		int shadeLight1 = doShadeMapping ? gstate.getUVLS1() : -1;
		for (int i = 0; i < 4; i++) {
			if (i == shadeLight0 || i == shadeLight1)
				doLight[i] = LIGHT_SHADE;
			if (gstate.isLightingEnabled() && gstate.isLightChanEnabled(i))
				doLight[i] = LIGHT_FULL;
		}
	}

	if (vertTypeIsSkinningEnabled(vertType)) {
		WRITE(p, "%s", boneWeightDecl[TranslateNumBones(vertTypeGetNumBoneWeights(vertType))]);
	}

	if (useHWTransform)
		WRITE(p, "%s vec3 position;\n", attribute);
	else
		WRITE(p, "%s vec4 position;\n", attribute);  // need to pass the fog coord in w

	if (useHWTransform && hasNormal)
		WRITE(p, "%s mediump vec3 normal;\n", attribute);

	if (doTexture && hasTexcoord) {
		if (!useHWTransform && doTextureProjection)
			WRITE(p, "%s vec3 texcoord;\n", attribute);
		else
			WRITE(p, "%s vec2 texcoord;\n", attribute);
	}
	if (hasColor) {
		WRITE(p, "%s lowp vec4 color0;\n", attribute);
		if (lmode && !useHWTransform)  // only software transform supplies color1 as vertex data
			WRITE(p, "%s lowp vec3 color1;\n", attribute);
	}

	if (gstate.isModeThrough())	{
		WRITE(p, "uniform mat4 u_proj_through;\n");
	} else {
		WRITE(p, "uniform mat4 u_proj;\n");
		// Add all the uniforms we'll need to transform properly.
	}

	bool prescale = g_Config.bPrescaleUV && !throughmode && (gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_COORDS || gstate.getUVGenMode() == GE_TEXMAP_UNKNOWN);

	if (useHWTransform) {
		// When transforming by hardware, we need a great deal more uniforms...
		WRITE(p, "uniform mat4 u_world;\n");
		WRITE(p, "uniform mat4 u_view;\n");
		if (doTextureProjection)
			WRITE(p, "uniform mediump mat4 u_texmtx;\n");
		if (vertTypeIsSkinningEnabled(vertType)) {
			int numBones = TranslateNumBones(vertTypeGetNumBoneWeights(vertType));
#ifdef USE_BONE_ARRAY
			WRITE(p, "uniform mediump mat4 u_bone[%i];\n", numBones);
#else
			for (int i = 0; i < numBones; i++) {
				WRITE(p, "uniform mat4 u_bone%i;\n", i);
			}
#endif
		}
		if (doTexture && (flipV || !prescale || gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP || gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX)) {
			WRITE(p, "uniform vec4 u_uvscaleoffset;\n");
		}
		for (int i = 0; i < 4; i++) {
			if (doLight[i] != LIGHT_OFF) {
				// This is needed for shade mapping
				WRITE(p, "uniform vec3 u_lightpos%i;\n", i);
			}
			if (doLight[i] == LIGHT_FULL) {
				GELightType type = gstate.getLightType(i);

				if (type != GE_LIGHTTYPE_DIRECTIONAL)
					WRITE(p, "uniform mediump vec3 u_lightatt%i;\n", i);

				if (type == GE_LIGHTTYPE_SPOT || type == GE_LIGHTTYPE_UNKNOWN) { 
					WRITE(p, "uniform mediump vec3 u_lightdir%i;\n", i);
					WRITE(p, "uniform mediump float u_lightangle%i;\n", i);
					WRITE(p, "uniform mediump float u_lightspotCoef%i;\n", i);
				}
				WRITE(p, "uniform lowp vec3 u_lightambient%i;\n", i);
				WRITE(p, "uniform lowp vec3 u_lightdiffuse%i;\n", i);

				if (gstate.isUsingSpecularLight(i))
					WRITE(p, "uniform lowp vec3 u_lightspecular%i;\n", i);
			}
		}
		if (gstate.isLightingEnabled()) {
			WRITE(p, "uniform lowp vec4 u_ambient;\n");
			if ((gstate.materialupdate & 2) == 0 || !hasColor)
				WRITE(p, "uniform lowp vec3 u_matdiffuse;\n");
			// if ((gstate.materialupdate & 4) == 0)
			WRITE(p, "uniform lowp vec4 u_matspecular;\n");  // Specular coef is contained in alpha
			WRITE(p, "uniform lowp vec3 u_matemissive;\n");
		}
	}

	if (useHWTransform || !hasColor)
		WRITE(p, "uniform lowp vec4 u_matambientalpha;\n");  // matambient + matalpha

	if (enableFog) {
		WRITE(p, "uniform highp vec2 u_fogcoef;\n");
	}

	WRITE(p, "%s lowp vec4 v_color0;\n", varying);
	if (lmode) {
		WRITE(p, "%s lowp vec3 v_color1;\n", varying);
	}
	if (doTexture) {
		if (doTextureProjection)
			WRITE(p, "%s mediump vec3 v_texcoord;\n", varying);
		else
			WRITE(p, "%s mediump vec2 v_texcoord;\n", varying);
	}


	if (enableFog) {
		// See the fragment shader generator
		if (highpFog) {
			WRITE(p, "%s highp float v_fogdepth;\n", varying);
		} else {
			WRITE(p, "%s mediump float v_fogdepth;\n", varying);
		}
	}

	WRITE(p, "void main() {\n");

	if (!useHWTransform) {
		// Simple pass-through of vertex data to fragment shader
		if (doTexture) {
			WRITE(p, "  v_texcoord = texcoord;\n");
		}
		if (hasColor) {
			WRITE(p, "  v_color0 = color0;\n");
			if (lmode)
				WRITE(p, "  v_color1 = color1;\n");
		} else {
			WRITE(p, "  v_color0 = u_matambientalpha;\n");
			if (lmode)
				WRITE(p, "  v_color1 = vec3(0.0);\n");
		}
		if (enableFog) {
			WRITE(p, "  v_fogdepth = position.w;\n");
		}
		if (gstate.isModeThrough())	{
			WRITE(p, "  gl_Position = u_proj_through * vec4(position.xyz, 1.0);\n");
		} else {
			WRITE(p, "  gl_Position = u_proj * vec4(position.xyz, 1.0);\n");
		}
	} else {
		// Step 1: World Transform / Skinning
		if (!vertTypeIsSkinningEnabled(vertType)) {
			// No skinning, just standard T&L.
			WRITE(p, "  vec3 worldpos = (u_world * vec4(position.xyz, 1.0)).xyz;\n");
			if (hasNormal)
				WRITE(p, "  mediump vec3 worldnormal = normalize((u_world * vec4(%snormal, 0.0)).xyz);\n", flipNormal ? "-" : "");
			else
				WRITE(p, "  mediump vec3 worldnormal = vec3(0.0, 0.0, 1.0);\n");
		} else {
			int numWeights = TranslateNumBones(vertTypeGetNumBoneWeights(vertType));

			static const char *rescale[4] = {"", " * 1.9921875", " * 1.999969482421875", ""}; // 2*127.5f/128.f, 2*32767.5f/32768.f, 1.0f};
			const char *factor = rescale[vertTypeGetWeightMask(vertType) >> GE_VTYPE_WEIGHT_SHIFT];

			static const char * const boneWeightAttr[8] = {
				"w1.x", "w1.y", "w1.z", "w1.w",
				"w2.x", "w2.y", "w2.z", "w2.w",
			};

#if defined(USE_FOR_LOOP) && defined(USE_BONE_ARRAY)

			// To loop through the weights, we unfortunately need to put them in a float array.
			// GLSL ES sucks - no way to directly initialize an array!
			switch (numWeights) {
			case 1: WRITE(p, "  float w[1]; w[0] = w1;\n"); break;
			case 2: WRITE(p, "  float w[2]; w[0] = w1.x; w[1] = w1.y;\n"); break;
			case 3: WRITE(p, "  float w[3]; w[0] = w1.x; w[1] = w1.y; w[2] = w1.z;\n"); break;
			case 4: WRITE(p, "  float w[4]; w[0] = w1.x; w[1] = w1.y; w[2] = w1.z; w[3] = w1.w;\n"); break;
			case 5: WRITE(p, "  float w[5]; w[0] = w1.x; w[1] = w1.y; w[2] = w1.z; w[3] = w1.w; w[4] = w2;\n"); break;
			case 6: WRITE(p, "  float w[6]; w[0] = w1.x; w[1] = w1.y; w[2] = w1.z; w[3] = w1.w; w[4] = w2.x; w[5] = w2.y;\n"); break;
			case 7: WRITE(p, "  float w[7]; w[0] = w1.x; w[1] = w1.y; w[2] = w1.z; w[3] = w1.w; w[4] = w2.x; w[5] = w2.y; w[6] = w2.z;\n"); break;
			case 8: WRITE(p, "  float w[8]; w[0] = w1.x; w[1] = w1.y; w[2] = w1.z; w[3] = w1.w; w[4] = w2.x; w[5] = w2.y; w[6] = w2.z; w[7] = w2.w;\n"); break;
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
				WRITE(p, "  mat4 skinMatrix = w1 * u_bone[0]");
			else
				WRITE(p, "  mat4 skinMatrix = w1.x * u_bone[0]");
			for (int i = 1; i < numWeights; i++) {
				const char *weightAttr = boneWeightAttr[i];
				// workaround for "cant do .x of scalar" issue
				if (numWeights == 1 && i == 0) weightAttr = "w1";
				if (numWeights == 5 && i == 4) weightAttr = "w2";
				WRITE(p, " + %s * u_bone[%i]", weightAttr, i);
			}
#else
			// Uncomment this to screw up bone shaders to check the vertex shader software fallback
			// WRITE(p, "THIS SHOULD ERROR! #error");
			if (numWeights == 1)
				WRITE(p, "  mat4 skinMatrix = w1 * u_bone0");
			else
				WRITE(p, "  mat4 skinMatrix = w1.x * u_bone0");
			for (int i = 1; i < numWeights; i++) {
				const char *weightAttr = boneWeightAttr[i];
				// workaround for "cant do .x of scalar" issue
				if (numWeights == 1 && i == 0) weightAttr = "w1";
				if (numWeights == 5 && i == 4) weightAttr = "w2";
				WRITE(p, " + %s * u_bone%i", weightAttr, i);
			}
#endif

#endif

			WRITE(p, ";\n");

			// Trying to simplify this results in bugs in LBP...
			WRITE(p, "  vec3 skinnedpos = (skinMatrix * vec4(position, 1.0)).xyz %s;\n", factor);
			WRITE(p, "  vec3 worldpos = (u_world * vec4(skinnedpos, 1.0)).xyz;\n");

			if (hasNormal) {
				WRITE(p, "  mediump vec3 skinnednormal = (skinMatrix * vec4(%snormal, 0.0)).xyz %s;\n", flipNormal ? "-" : "", factor);
				WRITE(p, "  mediump vec3 worldnormal = normalize((u_world * vec4(skinnednormal, 0.0)).xyz);\n");
			} else {
				WRITE(p, "  mediump vec3 worldnormal = (u_world * (skinMatrix * vec4(0.0, 0.0, 1.0, 0.0))).xyz;\n");
			}
		}

		WRITE(p, "  vec4 viewPos = u_view * vec4(worldpos, 1.0);\n");

		// Final view and projection transforms.
		WRITE(p, "  gl_Position = u_proj * viewPos;\n");

		// TODO: Declare variables for dots for shade mapping if needed.

		const char *ambientStr = (gstate.materialupdate & 1) && hasColor ? "color0" : "u_matambientalpha";
		const char *diffuseStr = (gstate.materialupdate & 2) && hasColor ? "color0.rgb" : "u_matdiffuse";
		const char *specularStr = (gstate.materialupdate & 4) && hasColor ? "color0.rgb" : "u_matspecular.rgb";

		bool diffuseIsZero = true;
		bool specularIsZero = true;
		bool distanceNeeded = false;

		if (gstate.isLightingEnabled()) {
			WRITE(p, "  lowp vec4 lightSum0 = u_ambient * %s + vec4(u_matemissive, 0.0);\n", ambientStr);

			for (int i = 0; i < 4; i++) {
				if (doLight[i] != LIGHT_FULL)
					continue;
				diffuseIsZero = false;
				if (gstate.isUsingSpecularLight(i))
					specularIsZero = false;
				GELightType type = gstate.getLightType(i);
				if (type != GE_LIGHTTYPE_DIRECTIONAL)
					distanceNeeded = true;
			}

			if (!specularIsZero) {
				WRITE(p, "  lowp vec3 lightSum1 = vec3(0.0);\n");
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

			GELightType type = gstate.getLightType(i);

			if (type == GE_LIGHTTYPE_DIRECTIONAL) {
				// We prenormalize light positions for directional lights.
				WRITE(p, "  toLight = u_lightpos%i;\n", i);
			} else {
				WRITE(p, "  toLight = u_lightpos%i - worldpos;\n", i);
				WRITE(p, "  distance = length(toLight);\n");
				WRITE(p, "  toLight /= distance;\n");
			}

			bool doSpecular = gstate.isUsingSpecularLight(i);
			bool poweredDiffuse = gstate.isUsingPoweredDiffuseLight(i);

			if (poweredDiffuse) {
				WRITE(p, "  mediump float dot%i = pow(dot(toLight, worldnormal), u_matspecular.a);\n", i);
				// Ugly NaN check.  pow(0.0, 0.0) may be undefined, but PSP seems to treat it as 1.0.
				// Seen in Tales of the World: Radiant Mythology (#2424.)
				WRITE(p, "  if (!(dot%i < 1.0) && !(dot%i > 0.0))\n", i, i);
				WRITE(p, "    dot%i = 1.0;\n", i);
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
			case GE_LIGHTTYPE_UNKNOWN:
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
				WRITE(p, "  dot%i = dot(normalize(toLight + vec3(0.0, 0.0, 1.0)), worldnormal);\n", i);
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
				WRITE(p, "  v_color0 = color0;\n");
			} else {
				WRITE(p, "  v_color0 = u_matambientalpha;\n");
			}
			if (lmode)
				WRITE(p, "  v_color1 = vec3(0.0);\n");
		}

		// Step 3: UV generation
		if (doTexture) {
			switch (gstate.getUVGenMode()) {
			case GE_TEXMAP_TEXTURE_COORDS:  // Scale-offset. Easy.
			case GE_TEXMAP_UNKNOWN: // Not sure what this is, but Riviera uses it.  Treating as coords works.
				if (prescale && !flipV) {
					if (hasTexcoord) {
						WRITE(p, "  v_texcoord = texcoord;\n");
					} else {
						WRITE(p, "  v_texcoord = vec2(0.0);\n");
					}
				} else {
					if (hasTexcoord) {
						WRITE(p, "  v_texcoord = texcoord * u_uvscaleoffset.xy + u_uvscaleoffset.zw;\n");
					} else {
						WRITE(p, "  v_texcoord = u_uvscaleoffset.zw;\n");
					}
				}
				break;

			case GE_TEXMAP_TEXTURE_MATRIX:  // Projection mapping.
				{
					std::string temp_tc;
					switch (gstate.getUVProjMode()) {
					case GE_PROJMAP_POSITION:  // Use model space XYZ as source
						temp_tc = "vec4(position.xyz, 1.0)";
						break;
					case GE_PROJMAP_UV:  // Use unscaled UV as source
						{
							// prescale is false here.
							if (hasTexcoord) {
								static const char *rescaleuv[4] = {"", " * 1.9921875", " * 1.999969482421875", ""}; // 2*127.5f/128.f, 2*32767.5f/32768.f, 1.0f};
								const char *factor = rescaleuv[(vertType & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT];
								temp_tc = StringFromFormat("vec4(texcoord.xy %s, 0.0, 1.0)", factor);
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
					WRITE(p, "  v_texcoord = (u_texmtx * %s).xyz * vec3(u_uvscaleoffset.xy, 1.0);\n", temp_tc.c_str());
				}
				break;

			case GE_TEXMAP_ENVIRONMENT_MAP:  // Shade mapping - use dots from light sources.
				WRITE(p, "  v_texcoord = u_uvscaleoffset.xy * vec2(1.0 + dot(normalize(u_lightpos%i), worldnormal), 1.0 + dot(normalize(u_lightpos%i), worldnormal)) * 0.5;\n", gstate.getUVLS0(), gstate.getUVLS1());
				break;

			default:
				// ILLEGAL
				break;
			}

			// Will flip in the fragment for GE_TEXMAP_TEXTURE_MATRIX.
			if (flipV && gstate.getUVGenMode() != GE_TEXMAP_TEXTURE_MATRIX)
				WRITE(p, "  v_texcoord.y = 1.0 - v_texcoord.y;\n");
		}

		// Compute fogdepth
		if (enableFog)
			WRITE(p, "  v_fogdepth = (viewPos.z + u_fogcoef.x) * u_fogcoef.y;\n");
	}
	WRITE(p, "}\n");
}

