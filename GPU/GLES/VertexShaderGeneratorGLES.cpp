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
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "Core/Config.h"
#include "GPU/GLES/VertexShaderGeneratorGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"

#undef WRITE

#define WRITE p+=sprintf

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
// Depth is not clipped to the viewport, but does clip to "minz" and "maxz".  It may also be clamped
// to 0 and 65535 if a depth clamping/clipping flag is set (x/y clipping is performed only if depth
// needs to be clamped.)
//
// All this above is for full transform mode.
// In through mode, the Z coordinate just goes straight through and there is no perspective division.
// We simulate this of course with pretty much an identity matrix. Rounding Z becomes very easy.
//
// TODO: Skip all this if we can actually get a 16-bit depth buffer along with stencil, which
// is a bit of a rare configuration, although quite common on mobile.


void GenerateVertexShader(const VShaderID &id, char *buffer, uint32_t *attrMask, uint64_t *uniformMask) {
	char *p = buffer;
	*attrMask = 0;
	*uniformMask = 0;
	// #define USE_FOR_LOOP

	// In GLSL ES 3.0, you use "out" variables instead.
	bool glslES30 = false;
	const char *varying = "varying";
	const char *attribute = "attribute";
	const char * const * boneWeightDecl = boneWeightAttrDecl;
	const char *texelFetch = NULL;
	bool highpFog = false;
	bool highpTexcoord = false;

	if (gl_extensions.IsGLES) {
		if (gstate_c.Supports(GPU_SUPPORTS_GLSL_ES_300)) {
			WRITE(p, "#version 300 es\n");
			glslES30 = true;
			texelFetch = "texelFetch";
		} else {
			WRITE(p, "#version 100\n");  // GLSL ES 1.0
			if (gl_extensions.EXT_gpu_shader4) {
				WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
				texelFetch = "texelFetch2D";
			}
		}
		WRITE(p, "precision highp float;\n");

		// PowerVR needs highp to do the fog in MHU correctly.
		// Others don't, and some can't handle highp in the fragment shader.
		highpFog = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) ? true : false;
		highpTexcoord = highpFog;
	} else {
		if (!gl_extensions.ForceGL2 || gl_extensions.IsCoreContext) {
			if (gl_extensions.VersionGEThan(3, 3, 0)) {
				glslES30 = true;
				WRITE(p, "#version 330\n");
				texelFetch = "texelFetch";
			} else if (gl_extensions.VersionGEThan(3, 0, 0)) {
				WRITE(p, "#version 130\n");
				if (gl_extensions.EXT_gpu_shader4) {
					WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
					texelFetch = "texelFetch";
				}
			} else {
				WRITE(p, "#version 110\n");
				if (gl_extensions.EXT_gpu_shader4) {
					WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
					texelFetch = "texelFetch2D";
				}
			}
		}

		// We remove these everywhere - GL4, GL3, Mac-forced-GL2, etc.
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	}

	if (glslES30 || gl_extensions.IsCoreContext) {
		attribute = "in";
		varying = "out";
		boneWeightDecl = boneWeightInDecl;
	}

	bool isModeThrough = id.Bit(VS_BIT_IS_THROUGH);
	bool lmode = id.Bit(VS_BIT_LMODE);
	bool doTexture = id.Bit(VS_BIT_DO_TEXTURE);
	bool doTextureProjection = id.Bit(VS_BIT_DO_TEXTURE_TRANSFORM);

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

	const char *shading = "";
	if (glslES30)
		shading = doFlatShading ? "flat " : "";

	DoLightComputation doLight[4] = { LIGHT_OFF, LIGHT_OFF, LIGHT_OFF, LIGHT_OFF };
	if (useHWTransform) {
		int shadeLight0 = doShadeMapping ? ls0 : -1;
		int shadeLight1 = doShadeMapping ? ls1 : -1;
		for (int i = 0; i < 4; i++) {
			if (i == shadeLight0 || i == shadeLight1)
				doLight[i] = LIGHT_SHADE;
			if (enableLighting && id.Bit(VS_BIT_LIGHT0_ENABLE + i))
				doLight[i] = LIGHT_FULL;
		}
	}

	int numBoneWeights = 0;
	int boneWeightScale = id.Bits(VS_BIT_WEIGHT_FMTSCALE, 2);
	if (enableBones) {
		numBoneWeights = 1 + id.Bits(VS_BIT_BONES, 3);
		WRITE(p, "%s", boneWeightDecl[numBoneWeights]);
		*attrMask |= 1 << ATTR_W1;
		if (numBoneWeights >= 5)
			*attrMask |= 1 << ATTR_W2;
	}

	if (useHWTransform)
		WRITE(p, "%s vec3 position;\n", attribute);
	else
		WRITE(p, "%s vec4 position;\n", attribute);  // need to pass the fog coord in w
	*attrMask |= 1 << ATTR_POSITION;

	if (useHWTransform && hasNormal) {
		WRITE(p, "%s mediump vec3 normal;\n", attribute);
		*attrMask |= 1 << ATTR_NORMAL;
	}

	bool texcoordVec3In = false;
	if (doTexture && hasTexcoord) {
		if (!useHWTransform && doTextureProjection && !isModeThrough) {
			WRITE(p, "%s vec3 texcoord;\n", attribute);
			texcoordVec3In = true;
		} else {
			WRITE(p, "%s vec2 texcoord;\n", attribute);
		}
		*attrMask |= 1 << ATTR_TEXCOORD;
	}
	if (hasColor) {
		WRITE(p, "%s lowp vec4 color0;\n", attribute);
		*attrMask |= 1 << ATTR_COLOR0;
		if (lmode && !useHWTransform) { // only software transform supplies color1 as vertex data
			WRITE(p, "%s lowp vec3 color1;\n", attribute);
			*attrMask |= 1 << ATTR_COLOR1;
		}
	}

	if (isModeThrough) {
		WRITE(p, "uniform mat4 u_proj_through;\n");
		*uniformMask |= DIRTY_PROJTHROUGHMATRIX;
	} else {
		WRITE(p, "uniform mat4 u_proj;\n");
		*uniformMask |= DIRTY_PROJMATRIX;
		// Add all the uniforms we'll need to transform properly.
	}

	bool scaleUV = !isModeThrough && (uvGenMode == GE_TEXMAP_TEXTURE_COORDS || uvGenMode == GE_TEXMAP_UNKNOWN);

	if (useHWTransform) {
		// When transforming by hardware, we need a great deal more uniforms...
		WRITE(p, "uniform mat4 u_world;\n");
		WRITE(p, "uniform mat4 u_view;\n");
		*uniformMask |= DIRTY_WORLDMATRIX | DIRTY_VIEWMATRIX;
		if (doTextureProjection) {
			WRITE(p, "uniform mediump mat4 u_texmtx;\n");
			*uniformMask |= DIRTY_TEXMATRIX;
		}
		if (enableBones) {
#ifdef USE_BONE_ARRAY
			WRITE(p, "uniform mediump mat4 u_bone[%i];\n", numBoneWeights);
			*uniformMask |= DIRTY_BONE_UNIFORMS;
#else
			for (int i = 0; i < numBoneWeights; i++) {
				WRITE(p, "uniform mat4 u_bone%i;\n", i);
				*uniformMask |= DIRTY_BONEMATRIX0 << i;
			}
#endif
		}
		if (doTexture) {
			WRITE(p, "uniform vec4 u_uvscaleoffset;\n");
			*uniformMask |= DIRTY_UVSCALEOFFSET;
		}
		for (int i = 0; i < 4; i++) {
			if (doLight[i] != LIGHT_OFF) {
				// This is needed for shade mapping
				WRITE(p, "uniform vec3 u_lightpos%i;\n", i);
				*uniformMask |= DIRTY_LIGHT0 << i;
			}
			if (doLight[i] == LIGHT_FULL) {
				*uniformMask |= DIRTY_LIGHT0 << i;
				GELightType type = static_cast<GELightType>(id.Bits(VS_BIT_LIGHT0_TYPE + 4 * i, 2));
				GELightComputation comp = static_cast<GELightComputation>(id.Bits(VS_BIT_LIGHT0_COMP + 4 * i, 2));

				if (type != GE_LIGHTTYPE_DIRECTIONAL)
					WRITE(p, "uniform mediump vec3 u_lightatt%i;\n", i);

				if (type == GE_LIGHTTYPE_SPOT || type == GE_LIGHTTYPE_UNKNOWN) {
					WRITE(p, "uniform mediump vec3 u_lightdir%i;\n", i);
					WRITE(p, "uniform mediump vec2 u_lightangle_spotCoef%i;\n", i);
				}
				WRITE(p, "uniform lowp vec3 u_lightambient%i;\n", i);
				WRITE(p, "uniform lowp vec3 u_lightdiffuse%i;\n", i);

				if (comp == GE_LIGHTCOMP_BOTH) {
					WRITE(p, "uniform lowp vec3 u_lightspecular%i;\n", i);
				}
			}
		}
		if (enableLighting) {
			WRITE(p, "uniform lowp vec4 u_ambient;\n");
			*uniformMask |= DIRTY_AMBIENT;
			if ((matUpdate & 2) == 0 || !hasColor) {
				WRITE(p, "uniform lowp vec3 u_matdiffuse;\n");
				*uniformMask |= DIRTY_MATDIFFUSE;
			}
			WRITE(p, "uniform lowp vec4 u_matspecular;\n");  // Specular coef is contained in alpha
			WRITE(p, "uniform lowp vec3 u_matemissive;\n");
			*uniformMask |= DIRTY_MATSPECULAR | DIRTY_MATEMISSIVE;
		}
	}

	if (useHWTransform || !hasColor) {
		WRITE(p, "uniform lowp vec4 u_matambientalpha;\n");  // matambient + matalpha
		*uniformMask |= DIRTY_MATAMBIENTALPHA;
	}
	if (enableFog) {
		WRITE(p, "uniform highp vec2 u_fogcoef;\n");
		*uniformMask |= DIRTY_FOGCOEF;
	}

	if (!isModeThrough && gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT)) {
		WRITE(p, "uniform highp vec4 u_depthRange;\n");
		*uniformMask |= DIRTY_DEPTHRANGE;
	}

	if (!isModeThrough) {
		WRITE(p, "uniform highp vec4 u_cullRangeMin;\n");
		WRITE(p, "uniform highp vec4 u_cullRangeMax;\n");
		*uniformMask |= DIRTY_CULLRANGE;
	}

	WRITE(p, "%s%s lowp vec4 v_color0;\n", shading, varying);
	if (lmode) {
		WRITE(p, "%s%s lowp vec3 v_color1;\n", shading, varying);
	}

	if (doTexture) {
		WRITE(p, "%s %s vec3 v_texcoord;\n", varying, highpTexcoord ? "highp" : "mediump");
	}

	if (enableFog) {
		// See the fragment shader generator
		if (highpFog) {
			WRITE(p, "%s highp float v_fogdepth;\n", varying);
		} else {
			WRITE(p, "%s mediump float v_fogdepth;\n", varying);
		}
	}

	// See comment above this function (GenerateVertexShader).
	if (!isModeThrough && gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT)) {
		// Apply the projection and viewport to get the Z buffer value, floor to integer, undo the viewport and projection.
		WRITE(p, "\nvec4 depthRoundZVP(vec4 v) {\n");
		WRITE(p, "  float z = v.z / v.w;\n");
		WRITE(p, "  z = z * u_depthRange.x + u_depthRange.y;\n");
		WRITE(p, "  z = floor(z);\n");
		WRITE(p, "  z = (z - u_depthRange.z) * u_depthRange.w;\n");
		WRITE(p, "  return vec4(v.x, v.y, z * v.w, v.w);\n");
		WRITE(p, "}\n\n");
	}

	// Hardware tessellation
	if (doBezier || doSpline) {
		*uniformMask |= DIRTY_BEZIERSPLINE;

		WRITE(p, "uniform sampler2D u_tess_points;\n"); // Control Points
		WRITE(p, "uniform sampler2D u_tess_weights_u;\n");
		WRITE(p, "uniform sampler2D u_tess_weights_v;\n");

		WRITE(p, "uniform int u_spline_counts;\n");

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

		if (!gl_extensions.VersionGEThan(3, 0, 0)) { // For glsl version 1.10
			WRITE(p, "mat4 outerProduct(vec4 u, vec4 v) {\n");
			WRITE(p, "  return mat4(u * v[0], u * v[1], u * v[2], u * v[3]);\n");
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
		WRITE(p, "  int index_u, index_v;\n");
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				WRITE(p, "  index_u = (%i + point_pos.x);\n", j);
				WRITE(p, "  index_v = (%i + point_pos.y);\n", i);
				WRITE(p, "  _pos[%i] = %s(u_tess_points, ivec2(index_u, index_v), 0).xyz;\n", i * 4 + j, texelFetch);
				if (doTexture && hasTexcoordTess)
					WRITE(p, "  _tex[%i] = %s(u_tess_points, ivec2(index_u + u_spline_counts, index_v), 0).xy;\n", i * 4 + j, texelFetch);
				if (hasColorTess)
					WRITE(p, "  _col[%i] = %s(u_tess_points, ivec2(index_u + u_spline_counts * 2, index_v), 0).rgba;\n", i * 4 + j, texelFetch);
			}
		}

		// Basis polynomials as weight coefficients
		WRITE(p, "  vec4 basis_u = %s(u_tess_weights_u, %s, 0);\n", texelFetch, "ivec2(weight_idx.x * 2, 0)");
		WRITE(p, "  vec4 basis_v = %s(u_tess_weights_v, %s, 0);\n", texelFetch, "ivec2(weight_idx.y * 2, 0)");
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
			WRITE(p, "  tess.col = u_matambientalpha;\n");
		if (hasNormalTess) {
			// Derivatives as weight coefficients
			WRITE(p, "  vec4 deriv_u = %s(u_tess_weights_u, %s, 0);\n", texelFetch, "ivec2(weight_idx.x * 2 + 1, 0)");
			WRITE(p, "  vec4 deriv_v = %s(u_tess_weights_v, %s, 0);\n", texelFetch, "ivec2(weight_idx.y * 2 + 1, 0)");

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
			if (texcoordVec3In) {
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
			WRITE(p, "  v_color0 = u_matambientalpha;\n");
			if (lmode)
				WRITE(p, "  v_color1 = vec3(0.0);\n");
		}
		if (enableFog) {
			WRITE(p, "  v_fogdepth = position.w;\n");
		}
		if (isModeThrough)	{
			WRITE(p, "  vec4 outPos = u_proj_through * vec4(position.xyz, 1.0);\n");
		} else {
			// The viewport is used in this case, so need to compensate for that.
			if (gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT)) {
				WRITE(p, "  vec4 outPos = depthRoundZVP(u_proj * vec4(position.xyz, 1.0));\n");
			} else {
				WRITE(p, "  vec4 outPos = u_proj * vec4(position.xyz, 1.0);\n");
			}
		}
	} else {
		// Step 1: World Transform / Skinning
		if (!enableBones) {
			if (doBezier || doSpline) {
				// Hardware tessellation
				WRITE(p, "  Tess tess;\n");
				WRITE(p, "  tessellate(tess);\n");

				WRITE(p, "  vec3 worldpos = (u_world * vec4(tess.pos.xyz, 1.0)).xyz;\n");
				if (hasNormalTess) {
					WRITE(p, "  mediump vec3 worldnormal = normalize((u_world * vec4(%stess.nrm, 0.0)).xyz);\n", flipNormalTess ? "-" : "");
				} else {
					WRITE(p, "  mediump vec3 worldnormal = vec3(0.0, 0.0, 1.0);\n");
				}
			} else {
				// No skinning, just standard T&L.
				WRITE(p, "  vec3 worldpos = (u_world * vec4(position.xyz, 1.0)).xyz;\n");
				if (hasNormal)
					WRITE(p, "  mediump vec3 worldnormal = normalize((u_world * vec4(%snormal, 0.0)).xyz);\n", flipNormal ? "-" : "");
				else
					WRITE(p, "  mediump vec3 worldnormal = vec3(0.0, 0.0, 1.0);\n");
			}
		} else {
			static const char *rescale[4] = {"", " * 1.9921875", " * 1.999969482421875", ""}; // 2*127.5f/128.f, 2*32767.5f/32768.f, 1.0f};
			const char *factor = rescale[boneWeightScale];

			static const char * const boneWeightAttr[8] = {
				"w1.x", "w1.y", "w1.z", "w1.w",
				"w2.x", "w2.y", "w2.z", "w2.w",
			};

#if defined(USE_FOR_LOOP) && defined(USE_BONE_ARRAY)

			// To loop through the weights, we unfortunately need to put them in a float array.
			// GLSL ES sucks - no way to directly initialize an array!
			switch (numBoneWeights) {
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
			if (numBoneWeights > 1) {
				WRITE(p, "  for (int i = 1; i < %i; i++) {\n", numBoneWeights);
				WRITE(p, "    skinMatrix += w[i] * u_bone[i];\n");
				WRITE(p, "  }\n");
			}

#else

#ifdef USE_BONE_ARRAY
			if (numBoneWeights == 1)
				WRITE(p, "  mat4 skinMatrix = w1 * u_bone[0]");
			else
				WRITE(p, "  mat4 skinMatrix = w1.x * u_bone[0]");
			for (int i = 1; i < numBoneWeights; i++) {
				const char *weightAttr = boneWeightAttr[i];
				// workaround for "cant do .x of scalar" issue
				if (numBoneWeights == 1 && i == 0) weightAttr = "w1";
				if (numBoneWeights == 5 && i == 4) weightAttr = "w2";
				WRITE(p, " + %s * u_bone[%i]", weightAttr, i);
			}
#else
			// Uncomment this to screw up bone shaders to check the vertex shader software fallback
			// WRITE(p, "THIS SHOULD ERROR! #error");
			if (numBoneWeights == 1)
				WRITE(p, "  mat4 skinMatrix = w1 * u_bone0");
			else
				WRITE(p, "  mat4 skinMatrix = w1.x * u_bone0");
			for (int i = 1; i < numBoneWeights; i++) {
				const char *weightAttr = boneWeightAttr[i];
				// workaround for "cant do .x of scalar" issue
				if (numBoneWeights == 1 && i == 0) weightAttr = "w1";
				if (numBoneWeights == 5 && i == 4) weightAttr = "w2";
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
			} else {
				WRITE(p, "  mediump vec3 skinnednormal = (skinMatrix * vec4(0.0, 0.0, %s1.0, 0.0)).xyz %s;\n", flipNormal ? "-" : "", factor);
			}
			WRITE(p, "  mediump vec3 worldnormal = normalize((u_world * vec4(skinnednormal, 0.0)).xyz);\n");
		}

		WRITE(p, "  vec4 viewPos = u_view * vec4(worldpos, 1.0);\n");

		// Final view and projection transforms.
		if (gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT)) {
			WRITE(p, "  vec4 outPos = depthRoundZVP(u_proj * viewPos);\n");
		} else {
			WRITE(p, "  vec4 outPos = u_proj * viewPos;\n");
		}

		// TODO: Declare variables for dots for shade mapping if needed.

		const char *ambientStr = (matUpdate & 1) && hasColor ? "color0" : "u_matambientalpha";
		const char *diffuseStr = (matUpdate & 2) && hasColor ? "color0.rgb" : "u_matdiffuse";
		const char *specularStr = (matUpdate & 4) && hasColor ? "color0.rgb" : "u_matspecular.rgb";
		if (doBezier || doSpline) {
			// TODO: Probably, should use hasColorTess but FF4 has a problem with drawing the background.
			ambientStr = (matUpdate & 1) && hasColor ? "tess.col" : "u_matambientalpha";
			diffuseStr = (matUpdate & 2) && hasColor ? "tess.col.rgb" : "u_matdiffuse";
			specularStr = (matUpdate & 4) && hasColor ? "tess.col.rgb" : "u_matspecular.rgb";
		}

		bool diffuseIsZero = true;
		bool specularIsZero = true;
		bool distanceNeeded = false;
		bool anySpots = false;
		if (enableLighting) {
			WRITE(p, "  lowp vec4 lightSum0 = u_ambient * %s + vec4(u_matemissive, 0.0);\n", ambientStr);

			for (int i = 0; i < 4; i++) {
				GELightType type = static_cast<GELightType>(id.Bits(VS_BIT_LIGHT0_TYPE + 4*i, 2));
				GELightComputation comp = static_cast<GELightComputation>(id.Bits(VS_BIT_LIGHT0_COMP + 4*i, 2));
				if (doLight[i] != LIGHT_FULL)
					continue;
				diffuseIsZero = false;
				if (comp == GE_LIGHTCOMP_BOTH)
					specularIsZero = false;
				if (type != GE_LIGHTTYPE_DIRECTIONAL)
					distanceNeeded = true;
				if (type == GE_LIGHTTYPE_SPOT || type == GE_LIGHTTYPE_UNKNOWN)
					anySpots = true;
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
			WRITE(p, "  mediump float ldot;\n");
			if (anySpots) {
				WRITE(p, "  lowp float angle;\n");
			}
		}

		// Calculate lights if needed. If shade mapping is enabled, lights may need to be
		// at least partially calculated.
		for (int i = 0; i < 4; i++) {
			if (doLight[i] != LIGHT_FULL)
				continue;

			GELightType type = static_cast<GELightType>(id.Bits(VS_BIT_LIGHT0_TYPE + 4*i, 2));
			GELightComputation comp = static_cast<GELightComputation>(id.Bits(VS_BIT_LIGHT0_COMP + 4*i, 2));

			if (type == GE_LIGHTTYPE_DIRECTIONAL) {
				// We prenormalize light positions for directional lights.
				WRITE(p, "  toLight = u_lightpos%i;\n", i);
			} else {
				WRITE(p, "  toLight = u_lightpos%i - worldpos;\n", i);
				WRITE(p, "  distance = length(toLight);\n");
				WRITE(p, "  toLight /= distance;\n");
			}

			bool doSpecular = comp == GE_LIGHTCOMP_BOTH;
			bool poweredDiffuse = comp == GE_LIGHTCOMP_ONLYPOWDIFFUSE;

			WRITE(p, "  ldot = dot(toLight, worldnormal);\n");
			if (poweredDiffuse) {
				// pow(0.0, 0.0) may be undefined, but the PSP seems to treat it as 1.0.
				// Seen in Tales of the World: Radiant Mythology (#2424.)
				WRITE(p, "  if (u_matspecular.a <= 0.0) {\n");
				WRITE(p, "    ldot = 1.0;\n");
				WRITE(p, "  } else {\n");
				WRITE(p, "    ldot = pow(max(ldot, 0.0), u_matspecular.a);\n");
				WRITE(p, "  }\n");
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
				WRITE(p, "  angle = length(u_lightdir%i) == 0.0 ? 0.0 : dot(normalize(u_lightdir%i), toLight);\n", i, i);
				WRITE(p, "  if (angle >= u_lightangle_spotCoef%i.x) {\n", i);
				WRITE(p, "    lightScale = clamp(1.0 / dot(u_lightatt%i, vec3(1.0, distance, distance*distance)), 0.0, 1.0) * (u_lightangle_spotCoef%i.y <= 0.0 ? 1.0 : pow(angle, u_lightangle_spotCoef%i.y));\n", i, i, i);
				WRITE(p, "  } else {\n");
				WRITE(p, "    lightScale = 0.0;\n");
				WRITE(p, "  }\n");
				break;
			default:
				// ILLEGAL
				break;
			}

			WRITE(p, "  diffuse = (u_lightdiffuse%i * %s) * max(ldot, 0.0);\n", i, diffuseStr);
			if (doSpecular) {
				WRITE(p, "  if (ldot >= 0.0) {\n");
				WRITE(p, "    ldot = dot(normalize(toLight + vec3(0.0, 0.0, 1.0)), worldnormal);\n");
				WRITE(p, "    if (u_matspecular.a <= 0.0) {\n");
				WRITE(p, "      ldot = 1.0;\n");
				WRITE(p, "    } else {\n");
				WRITE(p, "      ldot = pow(max(ldot, 0.0), u_matspecular.a);\n");
				WRITE(p, "    }\n");
				WRITE(p, "    if (ldot > 0.0)\n");
				WRITE(p, "      lightSum1 += u_lightspecular%i * %s * ldot %s;\n", i, specularStr, timesLightScale);
				WRITE(p, "  }\n");
			}
			WRITE(p, "  lightSum0.rgb += (u_lightambient%i * %s.rgb + diffuse)%s;\n", i, ambientStr, timesLightScale);
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
				WRITE(p, "  v_color0 = u_matambientalpha;\n");
			}
			if (lmode)
				WRITE(p, "  v_color1 = vec3(0.0);\n");
		}

		// Step 3: UV generation
		if (doTexture) {
			switch (uvGenMode) {
			case GE_TEXMAP_TEXTURE_COORDS:  // Scale-offset. Easy.
			case GE_TEXMAP_UNKNOWN: // Not sure what this is, but Riviera uses it.  Treating as coords works.
				if (scaleUV) {
					if (hasTexcoord) {
						if (doBezier || doSpline)
							WRITE(p, "  v_texcoord = vec3(tess.tex * u_uvscaleoffset.xy + u_uvscaleoffset.zw, 0.0);\n");
						else
							WRITE(p, "  v_texcoord = vec3(texcoord.xy * u_uvscaleoffset.xy, 0.0);\n");
					} else {
						WRITE(p, "  v_texcoord = vec3(0.0);\n");
					}
				} else {
					if (hasTexcoord) {
						WRITE(p, "  v_texcoord = vec3(texcoord.xy * u_uvscaleoffset.xy + u_uvscaleoffset.zw, 0.0);\n");
					} else {
						WRITE(p, "  v_texcoord = vec3(u_uvscaleoffset.zw, 0.0);\n");
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
							// prescale is false here.
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
					WRITE(p, "  v_texcoord = (u_texmtx * %s).xyz * vec3(u_uvscaleoffset.xy, 1.0);\n", temp_tc.c_str());
				}
				break;

			case GE_TEXMAP_ENVIRONMENT_MAP:  // Shade mapping - use dots from light sources.
				{
					std::string lightFactor0 = StringFromFormat("(length(u_lightpos%i) == 0.0 ? worldnormal.z : dot(normalize(u_lightpos%i), worldnormal))", ls0, ls0);
					std::string lightFactor1 = StringFromFormat("(length(u_lightpos%i) == 0.0 ? worldnormal.z : dot(normalize(u_lightpos%i), worldnormal))", ls1, ls1);
					WRITE(p, "  v_texcoord = vec3(u_uvscaleoffset.xy * vec2(1.0 + %s, 1.0 + %s) * 0.5, 1.0);\n", lightFactor0.c_str(), lightFactor1.c_str());
				}
				break;

			default:
				// ILLEGAL
				break;
			}
		}

		// Compute fogdepth
		if (enableFog)
			WRITE(p, "  v_fogdepth = (viewPos.z + u_fogcoef.x) * u_fogcoef.y;\n");
	}

	if (!isModeThrough && gstate_c.Supports(GPU_SUPPORTS_VS_RANGE_CULLING)) {
		WRITE(p, "  vec3 projPos = outPos.xyz / outPos.w;\n");
		// Vertex range culling doesn't happen when depth is clamped, so only do this if in range.
		WRITE(p, "  if (u_cullRangeMin.w <= 0.0 || (projPos.z >= u_cullRangeMin.z && projPos.z <= u_cullRangeMax.z)) {\n");
		const char *outMin = "projPos.x < u_cullRangeMin.x || projPos.y < u_cullRangeMin.y || projPos.z < u_cullRangeMin.z";
		const char *outMax = "projPos.x > u_cullRangeMax.x || projPos.y > u_cullRangeMax.y || projPos.z > u_cullRangeMax.z";
		WRITE(p, "    if (%s || %s) {\n", outMin, outMax);
		WRITE(p, "      outPos.xyzw = vec4(u_cullRangeMax.w);\n");
		WRITE(p, "    }\n");
		WRITE(p, "  }\n");
	}
	WRITE(p, "  gl_Position = outPos;\n");

	WRITE(p, "}\n");
}
