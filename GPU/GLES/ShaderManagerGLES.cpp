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

#if defined(_WIN32) && defined(SHADERLOG)
#include "Common/CommonWindows.h"
#endif

#include <map>
#include <cstdio>

#include "math/dataconv.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx/gl_debug_log.h"
#include "thin3d/GLRenderManager.h"
#include "i18n/i18n.h"
#include "math/math_util.h"
#include "math/lin/matrix4x4.h"
#include "profiler/profiler.h"

#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "ext/native/gfx/GLStateCache.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "FramebufferManagerGLES.h"

Shader::Shader(GLRenderManager *render, const char *code, uint32_t glShaderType, bool useHWTransform, uint32_t attrMask, uint64_t uniformMask)
	  : render_(render), failed_(false), useHWTransform_(useHWTransform), attrMask_(attrMask), uniformMask_(uniformMask) {
	PROFILE_THIS_SCOPE("shadercomp");
	isFragment_ = glShaderType == GL_FRAGMENT_SHADER;
	source_ = code;
#ifdef SHADERLOG
#ifdef _WIN32
	OutputDebugStringUTF8(code);
#else
	printf("%s\n", code);
#endif
#endif
	shader = render->CreateShader(glShaderType, source_);
}

Shader::~Shader() {
	render_->DeleteShader(shader);
}

LinkedShader::LinkedShader(GLRenderManager *render, VShaderID VSID, Shader *vs, FShaderID FSID, Shader *fs, bool useHWTransform, bool preloading)
		: render_(render), useHWTransform_(useHWTransform) {
	PROFILE_THIS_SCOPE("shaderlink");

	vs_ = vs;

	std::vector<GLRShader *> shaders;
	shaders.push_back(vs->shader);
	shaders.push_back(fs->shader);


	std::vector<GLRProgram::Semantic> semantics;
	semantics.push_back({ ATTR_POSITION, "position" });
	semantics.push_back({ ATTR_TEXCOORD, "texcoord" });
	semantics.push_back({ ATTR_NORMAL, "normal" });
	semantics.push_back({ ATTR_W1, "w1" });
	semantics.push_back({ ATTR_W2, "w2" });
	semantics.push_back({ ATTR_COLOR0, "color0" });
	semantics.push_back({ ATTR_COLOR1, "color1" });

	std::vector<GLRProgram::UniformLocQuery> queries;
	queries.push_back({ &u_tex, "tex" });
	queries.push_back({ &u_proj, "u_proj" });
	queries.push_back({ &u_proj_through, "u_proj_through" });

	queries.push_back({ &u_proj, "u_proj" });
	queries.push_back({ &u_proj_through, "u_proj_through" });
	queries.push_back({ &u_texenv, "u_texenv" });
	queries.push_back({ &u_fogcolor, "u_fogcolor" });
	queries.push_back({ &u_fogcoef, "u_fogcoef" });
	queries.push_back({ &u_alphacolorref, "u_alphacolorref" });
	queries.push_back({ &u_alphacolormask, "u_alphacolormask" });
	queries.push_back({ &u_stencilReplaceValue, "u_stencilReplaceValue" });
	queries.push_back({ &u_testtex, "testtex" });

	queries.push_back({ &u_fbotex, "fbotex" });
	queries.push_back({ &u_blendFixA, "u_blendFixA" });
	queries.push_back({ &u_blendFixB, "u_blendFixB" });
	queries.push_back({ &u_fbotexSize, "u_fbotexSize" });

	// Transform
	queries.push_back({ &u_view, "u_view" });
	queries.push_back({ &u_world, "u_world" });
	queries.push_back({ &u_texmtx, "u_texmtx" });

	if (VSID.Bit(VS_BIT_ENABLE_BONES))
		numBones = TranslateNumBones(VSID.Bits(VS_BIT_BONES, 3) + 1);
	else
		numBones = 0;
	queries.push_back({ &u_depthRange, "u_depthRange" });

#ifdef USE_BONE_ARRAY
	queries.push_back({ &u_bone, "u_bone" });
#else
	static const char * const boneNames[8] = { "u_bone0", "u_bone1", "u_bone2", "u_bone3", "u_bone4", "u_bone5", "u_bone6", "u_bone7", };
	for (int i = 0; i < 8; i++) {
		queries.push_back({ &u_bone[i], boneNames[i] });
	}
#endif

	// Lighting, texturing
	queries.push_back({ &u_ambient, "u_ambient" });
	queries.push_back({ &u_matambientalpha, "u_matambientalpha" });
	queries.push_back({ &u_matdiffuse, "u_matdiffuse" });
	queries.push_back({ &u_matspecular, "u_matspecular" });
	queries.push_back({ &u_matemissive, "u_matemissive" });
	queries.push_back({ &u_uvscaleoffset, "u_uvscaleoffset" });
	queries.push_back({ &u_texclamp, "u_texclamp" });
	queries.push_back({ &u_texclampoff, "u_texclampoff" });

	for (int i = 0; i < 4; i++) {
		static const char * const lightPosNames[4] = { "u_lightpos0", "u_lightpos1", "u_lightpos2", "u_lightpos3", };
		queries.push_back({ &u_lightpos[i], lightPosNames[i] });
		static const char * const lightdir_names[4] = { "u_lightdir0", "u_lightdir1", "u_lightdir2", "u_lightdir3", };
		queries.push_back({ &u_lightdir[i], lightdir_names[i] });
		static const char * const lightatt_names[4] = { "u_lightatt0", "u_lightatt1", "u_lightatt2", "u_lightatt3", };
		queries.push_back({ &u_lightatt[i], lightatt_names[i] });
		static const char * const lightangle_names[4] = { "u_lightangle0", "u_lightangle1", "u_lightangle2", "u_lightangle3", };
		queries.push_back({ &u_lightangle[i], lightangle_names[i] });

		static const char * const lightspotCoef_names[4] = { "u_lightspotCoef0", "u_lightspotCoef1", "u_lightspotCoef2", "u_lightspotCoef3", };
		queries.push_back({ &u_lightspotCoef[i], lightspotCoef_names[i] });
		static const char * const lightambient_names[4] = { "u_lightambient0", "u_lightambient1", "u_lightambient2", "u_lightambient3", };
		queries.push_back({ &u_lightambient[i], lightambient_names[i] });
		static const char * const lightdiffuse_names[4] = { "u_lightdiffuse0", "u_lightdiffuse1", "u_lightdiffuse2", "u_lightdiffuse3", };
		queries.push_back({ &u_lightdiffuse[i], lightdiffuse_names[i] });
		static const char * const lightspecular_names[4] = { "u_lightspecular0", "u_lightspecular1", "u_lightspecular2", "u_lightspecular3", };
		queries.push_back({ &u_lightspecular[i], lightspecular_names[i] });
	}

	// We need to fetch these unconditionally, gstate_c.spline or bezier will not be set if we
	// create this shader at load time from the shader cache.
	queries.push_back({ &u_tess_pos_tex, "u_tess_pos_tex" });
	queries.push_back({ &u_tess_tex_tex, "u_tess_tex_tex" });
	queries.push_back({ &u_tess_col_tex, "u_tess_col_tex" });
	queries.push_back({ &u_spline_count_u, "u_spline_count_u" });
	queries.push_back({ &u_spline_count_v, "u_spline_count_v" });
	queries.push_back({ &u_spline_type_u, "u_spline_type_u" });
	queries.push_back({ &u_spline_type_v, "u_spline_type_v" });

	attrMask = vs->GetAttrMask();
	availableUniforms = vs->GetUniformMask() | fs->GetUniformMask();

	program = render->CreateProgram(shaders, semantics, queries, gstate_c.featureFlags & GPU_SUPPORTS_DUALSOURCE_BLEND);

	render->BindProgram(program);

	// Default uniform values
	render->SetUniformI1(&u_tex, 0);
	render->SetUniformI1(&u_fbotex, 1);
	render->SetUniformI1(&u_fbotex, 2);
	
	if (u_tess_pos_tex != -1)
		render->SetUniformI1(&u_tess_pos_tex, 4); // Texture unit 4
	if (u_tess_tex_tex != -1)
		render->SetUniformI1(&u_tess_tex_tex, 5); // Texture unit 5
	if (u_tess_col_tex != -1)
		render->SetUniformI1(&u_tess_col_tex, 6); // Texture unit 6

	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_ALL_UNIFORMS;
	CHECK_GL_ERROR_IF_DEBUG();
}

LinkedShader::~LinkedShader() {
	render_->DeleteProgram(program);
}

// Utility
static inline void SetFloatUniform(GLRenderManager *render, GLint *uniform, float value) {
	render->SetUniformF(uniform, 1, &value);
}

static inline void SetColorUniform3(GLRenderManager *render, GLint *uniform, u32 color) {
	float f[4];
	Uint8x4ToFloat4(f, color);
	render->SetUniformF(uniform, 3, f);
}

static void SetColorUniform3Alpha(GLRenderManager *render, GLint *uniform, u32 color, u8 alpha) {
	float f[4];
	Uint8x3ToFloat4_AlphaUint8(f, color, alpha);
	render->SetUniformF(uniform, 4, f);
}

// This passes colors unscaled (e.g. 0 - 255 not 0 - 1.)
static void SetColorUniform3Alpha255(GLRenderManager *render, GLint *uniform, u32 color, u8 alpha) {
	if (gl_extensions.gpuVendor == GPU_VENDOR_IMGTEC) {
		const float col[4] = {
			(float)((color & 0xFF) >> 0) * (1.0f / 255.0f),
			(float)((color & 0xFF00) >> 8) * (1.0f / 255.0f),
			(float)((color & 0xFF0000) >> 16) * (1.0f / 255.0f),
			(float)alpha * (1.0f / 255.0f)
		};
		render->SetUniformF(uniform, 4, col);
	} else {
		const float col[4] = {
			(float)((color & 0xFF) >> 0),
			(float)((color & 0xFF00) >> 8),
			(float)((color & 0xFF0000) >> 16),
			(float)alpha 
		};
		render->SetUniformF(uniform, 4, col);
	}
}

static void SetColorUniform3iAlpha(GLRenderManager *render, GLint *uniform, u32 color, u8 alpha) {
	const int col[4] = {
		(int)((color & 0xFF) >> 0),
		(int)((color & 0xFF00) >> 8),
		(int)((color & 0xFF0000) >> 16),
		(int)alpha,
	};
	render->SetUniformI(uniform, 4, col);
}

static void SetColorUniform3ExtraFloat(GLRenderManager *render, GLint *uniform, u32 color, float extra) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		extra
	};
	render->SetUniformF(uniform, 4, col);
}

static void SetFloat24Uniform3(GLRenderManager *render, GLint *uniform, const uint32_t data[3]) {
	float f[4];
	ExpandFloat24x3ToFloat4(f, data);
	render->SetUniformF(uniform, 3, f);
}

static void SetFloatUniform4(GLRenderManager *render, GLint *uniform, float data[4]) {
	render->SetUniformF(uniform, 4, data);
}

static void SetMatrix4x3(GLRenderManager *render, GLint *uniform, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4(m4x4, m4x3);
	render->SetUniformM4x4(uniform, m4x4);
}

static inline void ScaleProjMatrix(Matrix4x4 &in) {
	float yOffset = gstate_c.vpYOffset;
	if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
		// GL upside down is a pain as usual.
		yOffset = -yOffset;
	}
	const Vec3 trans(gstate_c.vpXOffset, yOffset, gstate_c.vpZOffset);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale);
	in.translateAndScale(trans, scale);
}

void LinkedShader::use(const ShaderID &VSID) {
	render_->BindProgram(program);
	// Note that we no longer track attr masks here - we do it for the input layouts instead.
}

void LinkedShader::UpdateUniforms(u32 vertType, const ShaderID &vsid) {
	u64 dirty = dirtyUniforms & availableUniforms;
	dirtyUniforms = 0;
	if (!dirty)
		return;

	// Update any dirty uniforms before we draw
	if (dirty & DIRTY_PROJMATRIX) {
		Matrix4x4 flippedMatrix;
		memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));

		bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

		const bool invertedY = useBufferedRendering ? (gstate_c.vpHeight < 0) : (gstate_c.vpHeight > 0);
		if (invertedY) {
			flippedMatrix[1] = -flippedMatrix[1];
			flippedMatrix[5] = -flippedMatrix[5];
			flippedMatrix[9] = -flippedMatrix[9];
			flippedMatrix[13] = -flippedMatrix[13];
		}
		const bool invertedX = gstate_c.vpWidth < 0;
		if (invertedX) {
			flippedMatrix[0] = -flippedMatrix[0];
			flippedMatrix[4] = -flippedMatrix[4];
			flippedMatrix[8] = -flippedMatrix[8];
			flippedMatrix[12] = -flippedMatrix[12];
		}

		// In Phantasy Star Portable 2, depth range sometimes goes negative and is clamped by glDepthRange to 0,
		// causing graphics clipping glitch (issue #1788). This hack modifies the projection matrix to work around it.
		if (gstate_c.Supports(GPU_USE_DEPTH_RANGE_HACK)) {
			float zScale = gstate.getViewportZScale() / 65535.0f;
			float zCenter = gstate.getViewportZCenter() / 65535.0f;

			// if far depth range < 0
			if (zCenter + zScale < 0.0f) {
				// if perspective projection
				if (flippedMatrix[11] < 0.0f) {
					float depthMax = gstate.getDepthRangeMax() / 65535.0f;
					float depthMin = gstate.getDepthRangeMin() / 65535.0f;

					float a = flippedMatrix[10];
					float b = flippedMatrix[14];

					float n = b / (a - 1.0f);
					float f = b / (a + 1.0f);

					f = (n * f) / (n + ((zCenter + zScale) * (n - f) / (depthMax - depthMin)));

					a = (n + f) / (n - f);
					b = (2.0f * n * f) / (n - f);

					if (!my_isnan(a) && !my_isnan(b)) {
						flippedMatrix[10] = a;
						flippedMatrix[14] = b;
					}
				}
			}
		}

		ScaleProjMatrix(flippedMatrix);

		render_->SetUniformM4x4(&u_proj, flippedMatrix.m);
	}
	if (dirty & DIRTY_PROJTHROUGHMATRIX)
	{
		Matrix4x4 proj_through;
		bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
		if (useBufferedRendering) {
			proj_through.setOrtho(0.0f, gstate_c.curRTWidth, 0.0f, gstate_c.curRTHeight, 0.0f, 1.0f);
		} else {
			proj_through.setOrtho(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0.0f, 0.0f, 1.0f);
		}
		render_->SetUniformM4x4(&u_proj_through, proj_through.getReadPtr());
	}
	if (dirty & DIRTY_TEXENV) {
		SetColorUniform3(render_, &u_texenv, gstate.texenvcolor);
	}
	if (dirty & DIRTY_ALPHACOLORREF) {
		SetColorUniform3Alpha255(render_, &u_alphacolorref, gstate.getColorTestRef(), gstate.getAlphaTestRef() & gstate.getAlphaTestMask());
	}
	if (dirty & DIRTY_ALPHACOLORMASK) {
		SetColorUniform3iAlpha(render_, &u_alphacolormask, gstate.colortestmask, gstate.getAlphaTestMask());
	}
	if (dirty & DIRTY_FOGCOLOR) {
		SetColorUniform3(render_, &u_fogcolor, gstate.fogcolor);
	}
	if (dirty & DIRTY_FOGCOEF) {
		float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		if (my_isinf(fogcoef[1])) {
			// not really sure what a sensible value might be.
			fogcoef[1] = fogcoef[1] < 0.0f ? -10000.0f : 10000.0f;
		} else if (my_isnan(fogcoef[1])) {
			// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
			// Just put the fog far away at a large finite distance.
			// Infinities and NaNs are rather unpredictable in shaders on many GPUs
			// so it's best to just make it a sane calculation.
			fogcoef[0] = 100000.0f;
			fogcoef[1] = 1.0f;
		}
#ifndef MOBILE_DEVICE
		else if (my_isnanorinf(fogcoef[1]) || my_isnanorinf(fogcoef[0])) {
			ERROR_LOG_REPORT_ONCE(fognan, G3D, "Unhandled fog NaN/INF combo: %f %f", fogcoef[0], fogcoef[1]);
		}
#endif
		render_->SetUniformF(&u_fogcoef, 2, fogcoef);
	}

	if (dirty & DIRTY_UVSCALEOFFSET) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;
		float uvscaleoff[4];
		if (gstate_c.bezier || gstate_c.spline) {
			// When we are generating UV coordinates through the bezier/spline, we need to apply the scaling.
			// However, this is missing a check that we're not getting our UV:s supplied for us in the vertices.
			uvscaleoff[0] = gstate_c.uv.uScale * widthFactor;
			uvscaleoff[1] = gstate_c.uv.vScale * heightFactor;
			uvscaleoff[2] = gstate_c.uv.uOff * widthFactor;
			uvscaleoff[3] = gstate_c.uv.vOff * heightFactor;
		} else {
			uvscaleoff[0] = widthFactor;
			uvscaleoff[1] = heightFactor;
			uvscaleoff[2] = 0.0f;
			uvscaleoff[3] = 0.0f;
		}
		render_->SetUniformF(&u_uvscaleoffset, 4, uvscaleoff);
	}

	if ((dirty & DIRTY_TEXCLAMP) && u_texclamp != -1) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		// First wrap xy, then half texel xy (for clamp.)
		const float texclamp[4] = {
			widthFactor,
			heightFactor,
			invW * 0.5f,
			invH * 0.5f,
		};
		const float texclampoff[2] = {
			gstate_c.curTextureXOffset * invW,
			gstate_c.curTextureYOffset * invH,
		};
		render_->SetUniformF(&u_texclamp, 4, texclamp);
		if (u_texclampoff != -1) {
			render_->SetUniformF(&u_texclampoff, 2, texclampoff);
		}
	}

	// Transform
	if (dirty & DIRTY_WORLDMATRIX) {
		SetMatrix4x3(render_, &u_world, gstate.worldMatrix);
	}
	if (dirty & DIRTY_VIEWMATRIX) {
		SetMatrix4x3(render_, &u_view, gstate.viewMatrix);
	}
	if (dirty & DIRTY_TEXMATRIX) {
		SetMatrix4x3(render_, &u_texmtx, gstate.tgenMatrix);
	}
	if ((dirty & DIRTY_DEPTHRANGE) && u_depthRange != -1) {
		// Since depth is [-1, 1] mapping to [minz, maxz], this is easyish.
		float vpZScale = gstate.getViewportZScale();
		float vpZCenter = gstate.getViewportZCenter();

		// These are just the reverse of the formulas in GPUStateUtils.
		float halfActualZRange = vpZScale / gstate_c.vpDepthScale;
		float minz = -((gstate_c.vpZOffset * halfActualZRange) - vpZCenter) - halfActualZRange;
		float viewZScale = halfActualZRange;
		float viewZCenter = minz + halfActualZRange;

		if (!gstate_c.Supports(GPU_SUPPORTS_ACCURATE_DEPTH)) {
			viewZScale = vpZScale;
			viewZCenter = vpZCenter;
		}

		float viewZInvScale;
		if (viewZScale != 0.0) {
			viewZInvScale = 1.0f / viewZScale;
		} else {
			viewZInvScale = 0.0;
		}

		float data[4] = { viewZScale, viewZCenter, viewZCenter, viewZInvScale };
		SetFloatUniform4(render_, &u_depthRange, data);
	}

	if (dirty & DIRTY_STENCILREPLACEVALUE) {
		float f = (float)gstate.getStencilTestRef() * (1.0f / 255.0f);
		render_->SetUniformF(&u_stencilReplaceValue, 1, &f);
	}
	float bonetemp[16];
	for (int i = 0; i < numBones; i++) {
		if (dirty & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To4x4(bonetemp, gstate.boneMatrix + 12 * i);
			render_->SetUniformM4x4(&u_bone[i], bonetemp);
		}
	}

	if (dirty & DIRTY_SHADERBLEND) {
		if (u_blendFixA != -1) {
			SetColorUniform3(render_, &u_blendFixA, gstate.getFixA());
		}
		if (u_blendFixB != -1) {
			SetColorUniform3(render_, &u_blendFixB, gstate.getFixB());
		}

		const float fbotexSize[2] = {
			1.0f / (float)gstate_c.curRTRenderWidth,
			1.0f / (float)gstate_c.curRTRenderHeight,
		};
		if (u_fbotexSize != -1) {
			render_->SetUniformF(&u_fbotexSize, 2, fbotexSize);
		}
	}

	// Lighting
	if (dirty & DIRTY_AMBIENT) {
		SetColorUniform3Alpha(render_, &u_ambient, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (dirty & DIRTY_MATAMBIENTALPHA) {
		SetColorUniform3Alpha(render_, &u_matambientalpha, gstate.materialambient, gstate.getMaterialAmbientA());
	}
	if (dirty & DIRTY_MATDIFFUSE) {
		SetColorUniform3(render_, &u_matdiffuse, gstate.materialdiffuse);
	}
	if (dirty & DIRTY_MATEMISSIVE) {
		SetColorUniform3(render_, &u_matemissive, gstate.materialemissive);
	}
	if (dirty & DIRTY_MATSPECULAR) {
		SetColorUniform3ExtraFloat(render_, &u_matspecular, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
	}

	for (int i = 0; i < 4; i++) {
		if (dirty & (DIRTY_LIGHT0 << i)) {
			if (gstate.isDirectionalLight(i)) {
				// Prenormalize
				float x = getFloat24(gstate.lpos[i * 3 + 0]);
				float y = getFloat24(gstate.lpos[i * 3 + 1]);
				float z = getFloat24(gstate.lpos[i * 3 + 2]);
				float len = sqrtf(x*x + y*y + z*z);
				if (len == 0.0f)
					len = 1.0f;
				else
					len = 1.0f / len;
				float vec[3] = { x * len, y * len, z * len };
				render_->SetUniformF(&u_lightpos[i], 3, vec);
			} else {
				SetFloat24Uniform3(render_, &u_lightpos[i], &gstate.lpos[i * 3]);
			}
			if (u_lightdir[i] != -1) SetFloat24Uniform3(render_, &u_lightdir[i], &gstate.ldir[i * 3]);
			if (u_lightatt[i] != -1) SetFloat24Uniform3(render_, &u_lightatt[i], &gstate.latt[i * 3]);
			if (u_lightangle[i] != -1) SetFloatUniform(render_, &u_lightangle[i], getFloat24(gstate.lcutoff[i]));
			if (u_lightspotCoef[i] != -1) SetFloatUniform(render_, &u_lightspotCoef[i], getFloat24(gstate.lconv[i]));
			if (u_lightambient[i] != -1) SetColorUniform3(render_, &u_lightambient[i], gstate.lcolor[i * 3]);
			if (u_lightdiffuse[i] != -1) SetColorUniform3(render_, &u_lightdiffuse[i], gstate.lcolor[i * 3 + 1]);
			if (u_lightspecular[i] != -1) SetColorUniform3(render_, &u_lightspecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}

	if (dirty & DIRTY_BEZIERSPLINE) {
		render_->SetUniformI1(&u_spline_count_u, gstate_c.spline_count_u);
		if (u_spline_count_v != -1)
			render_->SetUniformI1(&u_spline_count_v, gstate_c.spline_count_v);
		if (u_spline_type_u != -1)
			render_->SetUniformI1(&u_spline_type_u, gstate_c.spline_type_u);
		if (u_spline_type_v != -1)
			render_->SetUniformI1(&u_spline_type_v, gstate_c.spline_type_v);
	}
}

ShaderManagerGLES::ShaderManagerGLES(GLRenderManager *render)
		: render_(render), lastShader_(nullptr), shaderSwitchDirtyUniforms_(0), diskCacheDirty_(false), fsCache_(16), vsCache_(16) {
	codeBuffer_ = new char[16384];
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
}

ShaderManagerGLES::~ShaderManagerGLES() {
	delete [] codeBuffer_;
}

void ShaderManagerGLES::Clear() {
	DirtyLastShader();
	for (auto iter = linkedShaderCache_.begin(); iter != linkedShaderCache_.end(); ++iter) {
		delete iter->ls;
	}
	fsCache_.Iterate([&](const FShaderID &key, Shader *shader) {
		delete shader;
	});
	vsCache_.Iterate([&](const VShaderID &key, Shader *shader) {
		delete shader;
	});
	linkedShaderCache_.clear();
	fsCache_.Clear();
	vsCache_.Clear();
	DirtyShader();
}

void ShaderManagerGLES::ClearCache(bool deleteThem) {
	// TODO: Recreate all from the diskcache when we come back.
	Clear();
}

void ShaderManagerGLES::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	DirtyLastShader();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
	shaderSwitchDirtyUniforms_ = 0;
}

void ShaderManagerGLES::DirtyLastShader() { // disables vertex arrays
	lastShader_ = nullptr;
	lastVShaderSame_ = false;
}

Shader *ShaderManagerGLES::CompileFragmentShader(FShaderID FSID) {
	uint64_t uniformMask;
	if (!GenerateFragmentShader(FSID, codeBuffer_, &uniformMask)) {
		return nullptr;
	}
	return new Shader(render_, codeBuffer_, GL_FRAGMENT_SHADER, false, 0, uniformMask);
}

Shader *ShaderManagerGLES::CompileVertexShader(VShaderID VSID) {
	bool useHWTransform = VSID.Bit(VS_BIT_USE_HW_TRANSFORM);
	uint32_t attrMask;
	uint64_t uniformMask;
	GenerateVertexShader(VSID, codeBuffer_, &attrMask, &uniformMask);
	return new Shader(render_, codeBuffer_, GL_VERTEX_SHADER, useHWTransform, attrMask, uniformMask);
}

Shader *ShaderManagerGLES::ApplyVertexShader(int prim, u32 vertType, VShaderID *VSID) {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty) {
		if (lastShader_)
			lastShader_->dirtyUniforms |= dirty;
		shaderSwitchDirtyUniforms_ |= dirty;
		gstate_c.CleanUniforms();
	}

	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		bool useHWTransform = CanUseHardwareTransform(prim);
		ComputeVertexShaderID(VSID, vertType, useHWTransform);
	} else {
		*VSID = lastVSID_;
	}

	if (lastShader_ != 0 && *VSID == lastVSID_) {
		lastVShaderSame_ = true;
		return lastShader_->vs_;  	// Already all set.
	} else {
		lastVShaderSame_ = false;
	}
	lastVSID_ = *VSID;

	Shader *vs = vsCache_.Get(*VSID);
	if (!vs)	{
		// Vertex shader not in cache. Let's compile it.
		vs = CompileVertexShader(*VSID);
		if (vs->Failed()) {
			I18NCategory *gr = GetI18NCategory("Graphics");
			ERROR_LOG(G3D, "Shader compilation failed, falling back to software transform");
			if (!g_Config.bHideSlowWarnings) {
				host->NotifyUserMessage(gr->T("hardware transform error - falling back to software"), 2.5f, 0xFF3030FF);
			}
			delete vs;

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			VShaderID vsidTemp;
			ComputeVertexShaderID(&vsidTemp, vertType, false);
			uint32_t attrMask;
			uint64_t uniformMask;
			GenerateVertexShader(vsidTemp, codeBuffer_, &attrMask, &uniformMask);
			vs = new Shader(render_, codeBuffer_, GL_VERTEX_SHADER, false, attrMask, uniformMask);
		}

		vsCache_.Insert(*VSID, vs);
		diskCacheDirty_ = true;
	}
	return vs;
}

LinkedShader *ShaderManagerGLES::ApplyFragmentShader(VShaderID VSID, Shader *vs, u32 vertType, int prim) {
	FShaderID FSID;
	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID);
	} else {
		FSID = lastFSID_;
	}

	if (lastVShaderSame_ && FSID == lastFSID_) {
		lastShader_->UpdateUniforms(vertType, VSID);
		return lastShader_;
	}

	lastFSID_ = FSID;

	Shader *fs = fsCache_.Get(FSID);
	if (!fs)	{
		// Fragment shader not in cache. Let's compile it.
		fs = CompileFragmentShader(FSID);
		fsCache_.Insert(FSID, fs);
		diskCacheDirty_ = true;
	}

	// Okay, we have both shaders. Let's see if there's a linked one.
	LinkedShader *ls = nullptr;

	u64 switchDirty = shaderSwitchDirtyUniforms_;
	for (auto iter = linkedShaderCache_.begin(); iter != linkedShaderCache_.end(); ++iter) {
		// Deferred dirtying! Let's see if we can make this even more clever later.
		iter->ls->dirtyUniforms |= switchDirty;

		if (iter->vs == vs && iter->fs == fs) {
			ls = iter->ls;
		}
	}
	shaderSwitchDirtyUniforms_ = 0;

	if (ls == nullptr) {
		_dbg_assert_(G3D, FSID.Bit(FS_BIT_LMODE) == VSID.Bit(VS_BIT_LMODE));
		_dbg_assert_(G3D, FSID.Bit(FS_BIT_DO_TEXTURE) == VSID.Bit(VS_BIT_DO_TEXTURE));
		_dbg_assert_(G3D, FSID.Bit(FS_BIT_ENABLE_FOG) == VSID.Bit(VS_BIT_ENABLE_FOG));
		_dbg_assert_(G3D, FSID.Bit(FS_BIT_FLATSHADE) == VSID.Bit(VS_BIT_FLATSHADE));

		// Check if we can link these.
		ls = new LinkedShader(render_, VSID, vs, FSID, fs, vs->UseHWTransform());
		ls->use(VSID);
		const LinkedShaderCacheEntry entry(vs, fs, ls);
		linkedShaderCache_.push_back(entry);
	} else {
		ls->use(VSID);
	}
	ls->UpdateUniforms(vertType, VSID);

	lastShader_ = ls;
	return ls;
}

std::string Shader::GetShaderString(DebugShaderStringType type, ShaderID id) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return isFragment_ ? FragmentShaderDesc(id) : VertexShaderDesc(id);
	default:
		return "N/A";
	}
}

std::vector<std::string> ShaderManagerGLES::DebugGetShaderIDs(DebugShaderType type) {
	std::string id;
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
		{
			vsCache_.Iterate([&](const VShaderID &id, Shader *shader) {
				std::string idstr;
				id.ToString(&idstr);
				ids.push_back(idstr);
			});
		}
		break;
	case SHADER_TYPE_FRAGMENT:
		{
			fsCache_.Iterate([&](const FShaderID &id, Shader *shader) {
				std::string idstr;
				id.ToString(&idstr);
				ids.push_back(idstr);
			});
		}
		break;
	default:
		break;
	}
	return ids;
}

std::string ShaderManagerGLES::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(id);
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		Shader *vs = vsCache_.Get(VShaderID(shaderId));
		return vs ? vs->GetShaderString(stringType, shaderId) : "";
	}

	case SHADER_TYPE_FRAGMENT:
	{
		Shader *fs = fsCache_.Get(FShaderID(shaderId));
		return fs->GetShaderString(stringType, shaderId);
	}
	default:
		return "N/A";
	}
}

// Shader pseudo-cache.
//
// We simply store the IDs of the shaders used during gameplay. On next startup of
// the same game, we simply compile all the shaders from the start, so we don't have to
// compile them on the fly later. Ideally we would store the actual compiled shaders
// rather than just their IDs, but OpenGL does not support this, except for a few obscure
// vendor-specific extensions.
//
// If things like GPU supported features have changed since the last time, we discard the cache
// as sometimes these features might have an effect on the ID bits.

#define CACHE_HEADER_MAGIC 0x83277592
#define CACHE_VERSION 5
struct CacheHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t featureFlags;
	uint32_t reserved;
	int numVertexShaders;
	int numFragmentShaders;
	int numLinkedPrograms;
};

void ShaderManagerGLES::Load(const std::string &filename) {
	File::IOFile f(filename, "rb");
	u64 sz = f.GetSize();
	if (!f.IsOpen()) {
		return;
	}
	CacheHeader header;
	if (!f.ReadArray(&header, 1)) {
		return;
	}
	if (header.magic != CACHE_HEADER_MAGIC || header.version != CACHE_VERSION || header.featureFlags != gstate_c.featureFlags) {
		return;
	}
	time_update();
	diskCachePending_.start = time_now_d();
	diskCachePending_.Clear();

	// Sanity check the file contents
	if (header.numFragmentShaders > 1000 || header.numVertexShaders > 1000 || header.numLinkedPrograms > 1000) {
		ERROR_LOG(G3D, "Corrupt shader cache file header, aborting.");
		return;
	}

	// Also make sure the size makes sense, in case there's corruption.
	u64 expectedSize = sizeof(header);
	expectedSize += header.numVertexShaders * sizeof(VShaderID);
	expectedSize += header.numFragmentShaders * sizeof(FShaderID);
	expectedSize += header.numLinkedPrograms * (sizeof(VShaderID) + sizeof(FShaderID));
	if (sz != expectedSize) {
		ERROR_LOG(G3D, "Shader cache file is wrong size: %lld instead of %lld", sz, expectedSize);
		return;
	}

	diskCachePending_.vert.resize(header.numVertexShaders);
	if (!f.ReadArray(&diskCachePending_.vert[0], header.numVertexShaders)) {
		diskCachePending_.vert.clear();
		return;
	}

	diskCachePending_.frag.resize(header.numFragmentShaders);
	if (!f.ReadArray(&diskCachePending_.frag[0], header.numFragmentShaders)) {
		diskCachePending_.vert.clear();
		diskCachePending_.frag.clear();
		return;
	}

	for (int i = 0; i < header.numLinkedPrograms; i++) {
		VShaderID vsid;
		FShaderID fsid;
		if (!f.ReadArray(&vsid, 1)) {
			return;
		}
		if (!f.ReadArray(&fsid, 1)) {
			return;
		}
		diskCachePending_.link.push_back(std::make_pair(vsid, fsid));
	}

	// Actual compilation happens in ContinuePrecompile(), called by GPU_GLES's IsReady.
	NOTICE_LOG(G3D, "Precompiling the shader cache from '%s'", filename.c_str());
	diskCacheDirty_ = false;
}

bool ShaderManagerGLES::ContinuePrecompile(float sliceTime) {
	auto &pending = diskCachePending_;
	if (pending.Done()) {
		return true;
	}

	double start = real_time_now();
	// Let's try to keep it under sliceTime if possible.
	double end = start + sliceTime;

	for (size_t &i = pending.vertPos; i < pending.vert.size(); i++) {
		if (real_time_now() >= end) {
			// We'll finish later.
			return false;
		}

		const VShaderID &id = pending.vert[i];
		if (!vsCache_.Get(id)) {
			if (id.Bit(VS_BIT_IS_THROUGH) && id.Bit(VS_BIT_USE_HW_TRANSFORM)) {
				// Clearly corrupt, bailing.
				ERROR_LOG_REPORT(G3D, "Corrupt shader cache: Both IS_THROUGH and USE_HW_TRANSFORM set.");
				pending.Clear();
				return false;
			}

			Shader *vs = CompileVertexShader(id);
			if (vs->Failed()) {
				// Give up on using the cache, just bail. We can't safely create the fallback shaders here
				// without trying to deduce the vertType from the VSID.
				ERROR_LOG(G3D, "Failed to compile a vertex shader loading from cache. Skipping rest of shader cache.");
				delete vs;
				pending.Clear();
				return false;
			}
			vsCache_.Insert(id, vs);
		} else {
			WARN_LOG(G3D, "Duplicate vertex shader found in GL shader cache, ignoring");
		}
	}

	for (size_t &i = pending.fragPos; i < pending.frag.size(); i++) {
		if (real_time_now() >= end) {
			// We'll finish later.
			return false;
		}

		const FShaderID &id = pending.frag[i];
		if (!fsCache_.Get(id)) {
			fsCache_.Insert(id, CompileFragmentShader(id));
		} else {
			WARN_LOG(G3D, "Duplicate fragment shader found in GL shader cache, ignoring");
		}
	}

	for (size_t &i = pending.linkPos; i < pending.link.size(); i++) {
		if (real_time_now() >= end) {
			// We'll finish later.
			return false;
		}

		const VShaderID &vsid = pending.link[i].first;
		const FShaderID &fsid = pending.link[i].second;
		Shader *vs = vsCache_.Get(vsid);
		Shader *fs = fsCache_.Get(fsid);
		if (vs && fs) {
			LinkedShader *ls = new LinkedShader(render_, vsid, vs, fsid, fs, vs->UseHWTransform(), true);
			LinkedShaderCacheEntry entry(vs, fs, ls);
			linkedShaderCache_.push_back(entry);
		}
	}

	// Okay, finally done.  Time to report status.
	time_update();
	double finish = time_now_d();

	NOTICE_LOG(G3D, "Compiled and linked %d programs (%d vertex, %d fragment) in %0.1f milliseconds", (int)pending.link.size(), (int)pending.vert.size(), (int)pending.frag.size(), 1000 * (finish - pending.start));
	pending.Clear();

	return true;
}

void ShaderManagerGLES::Save(const std::string &filename) {
	if (!diskCacheDirty_) {
		return;
	}
	if (linkedShaderCache_.empty()) {
		return;
	}
	INFO_LOG(G3D, "Saving the shader cache to '%s'", filename.c_str());
	FILE *f = File::OpenCFile(filename, "wb");
	if (!f) {
		// Can't save, give up for now.
		diskCacheDirty_ = false;
		return;
	}
	CacheHeader header;
	header.magic = CACHE_HEADER_MAGIC;
	header.version = CACHE_VERSION;
	header.reserved = 0;
	header.featureFlags = gstate_c.featureFlags;
	header.numVertexShaders = GetNumVertexShaders();
	header.numFragmentShaders = GetNumFragmentShaders();
	header.numLinkedPrograms = GetNumPrograms();
	fwrite(&header, 1, sizeof(header), f);
	vsCache_.Iterate([&](const ShaderID &id, Shader *shader) {
		fwrite(&id, 1, sizeof(id), f);
	});
	fsCache_.Iterate([&](const ShaderID &id, Shader *shader) {
		fwrite(&id, 1, sizeof(id), f);
	});
	for (auto iter : linkedShaderCache_) {
		ShaderID vsid, fsid;
		vsCache_.Iterate([&](const ShaderID &id, Shader *shader) {
			if (iter.vs == shader)
				vsid = id;
		});
		fsCache_.Iterate([&](const ShaderID &id, Shader *shader) {
			if (iter.fs == shader)
				fsid = id;
		});
		fwrite(&vsid, 1, sizeof(vsid), f);
		fwrite(&fsid, 1, sizeof(fsid), f);
	}
	fclose(f);
	diskCacheDirty_ = false;
}
