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

#include <cmath>
#include <cstdio>
#include <map>

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Text/I18n.h"
#include "Common/GPU/OpenGL/GLDebugLog.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/LogReporting.h"
#include "Common/Math/math_util.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/Shader.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/System/Display.h"
#include "Common/System/OSD.h"
#include "Common/VR/PPSSPPVR.h"

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"

using namespace Lin;

Shader::Shader(GLRenderManager *render, const char *code, const std::string &desc, const ShaderDescGLES &params)
	  : render_(render), useHWTransform_(params.useHWTransform), attrMask_(params.attrMask), uniformMask_(params.uniformMask) {
	PROFILE_THIS_SCOPE("shadercomp");
	isFragment_ = params.glShaderType == GL_FRAGMENT_SHADER;
	source_ = code;
#ifdef SHADERLOG
#ifdef _WIN32
	OutputDebugStringUTF8(code);
#else
	printf("%s\n", code);
#endif
#endif
	shader = render->CreateShader(params.glShaderType, source_, desc);
}

Shader::~Shader() {
	render_->DeleteShader(shader);
}

LinkedShader::LinkedShader(GLRenderManager *render, VShaderID VSID, Shader *vs, FShaderID FSID, Shader *fs, bool useHWTransform, bool preloading)
		: render_(render), useHWTransform_(useHWTransform) {
	PROFILE_THIS_SCOPE("shaderlink");

	_assert_(render);
	_assert_(vs);
	_assert_(fs);

	vs_ = vs;

	std::vector<GLRShader *> shaders;
	shaders.push_back(vs->shader);
	shaders.push_back(fs->shader);

	std::vector<GLRProgram::Semantic> semantics;
	semantics.reserve(7);
	semantics.push_back({ ATTR_POSITION, "position" });
	semantics.push_back({ ATTR_TEXCOORD, "texcoord" });
	if (useHWTransform_)
		semantics.push_back({ ATTR_NORMAL, "normal" });
	else
		semantics.push_back({ ATTR_NORMAL, "fog" });
	semantics.push_back({ ATTR_W1, "w1" });
	semantics.push_back({ ATTR_W2, "w2" });
	semantics.push_back({ ATTR_COLOR0, "color0" });
	semantics.push_back({ ATTR_COLOR1, "color1" });

	std::vector<GLRProgram::UniformLocQuery> queries;
	queries.push_back({ &u_tex, "tex" });
	queries.push_back({ &u_pal, "pal" });
	queries.push_back({ &u_testtex, "testtex" });
	queries.push_back({ &u_fbotex, "fbotex" });

	queries.push_back({ &u_proj, "u_proj" });
	queries.push_back({ &u_proj_lens, "u_proj_lens" });
	queries.push_back({ &u_proj_through, "u_proj_through" });
	queries.push_back({ &u_texenv, "u_texenv" });
	queries.push_back({ &u_fogcolor, "u_fogcolor" });
	queries.push_back({ &u_fogcoef, "u_fogcoef" });
	queries.push_back({ &u_alphacolorref, "u_alphacolorref" });
	queries.push_back({ &u_alphacolormask, "u_alphacolormask" });
	queries.push_back({ &u_colorWriteMask, "u_colorWriteMask" });
	queries.push_back({ &u_stencilReplaceValue, "u_stencilReplaceValue" });
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
	queries.push_back({ &u_cullRangeMin, "u_cullRangeMin" });
	queries.push_back({ &u_cullRangeMax, "u_cullRangeMax" });
	queries.push_back({ &u_rotation, "u_rotation" });

	// These two are only used for VR, but let's always query them for simplicity.
	queries.push_back({ &u_scaleX, "u_scaleX" });
	queries.push_back({ &u_scaleY, "u_scaleY" });

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
	queries.push_back({ &u_texNoAlphaMul, "u_texNoAlphaMul" });
	queries.push_back({ &u_lightControl, "u_lightControl" });

	for (int i = 0; i < 4; i++) {
		static const char * const lightPosNames[4] = { "u_lightpos0", "u_lightpos1", "u_lightpos2", "u_lightpos3", };
		queries.push_back({ &u_lightpos[i], lightPosNames[i] });
		static const char * const lightdir_names[4] = { "u_lightdir0", "u_lightdir1", "u_lightdir2", "u_lightdir3", };
		queries.push_back({ &u_lightdir[i], lightdir_names[i] });
		static const char * const lightatt_names[4] = { "u_lightatt0", "u_lightatt1", "u_lightatt2", "u_lightatt3", };
		queries.push_back({ &u_lightatt[i], lightatt_names[i] });
		static const char * const lightangle_spotCoef_names[4] = { "u_lightangle_spotCoef0", "u_lightangle_spotCoef1", "u_lightangle_spotCoef2", "u_lightangle_spotCoef3", };
		queries.push_back({ &u_lightangle_spotCoef[i], lightangle_spotCoef_names[i] });

		static const char * const lightambient_names[4] = { "u_lightambient0", "u_lightambient1", "u_lightambient2", "u_lightambient3", };
		queries.push_back({ &u_lightambient[i], lightambient_names[i] });
		static const char * const lightdiffuse_names[4] = { "u_lightdiffuse0", "u_lightdiffuse1", "u_lightdiffuse2", "u_lightdiffuse3", };
		queries.push_back({ &u_lightdiffuse[i], lightdiffuse_names[i] });
		static const char * const lightspecular_names[4] = { "u_lightspecular0", "u_lightspecular1", "u_lightspecular2", "u_lightspecular3", };
		queries.push_back({ &u_lightspecular[i], lightspecular_names[i] });
	}

	// We need to fetch these unconditionally, gstate_c.spline or bezier will not be set if we
	// create this shader at load time from the shader cache.
	queries.push_back({ &u_tess_points, "u_tess_points" });
	queries.push_back({ &u_tess_weights_u, "u_tess_weights_u" });
	queries.push_back({ &u_tess_weights_v, "u_tess_weights_v" });
	queries.push_back({ &u_spline_counts, "u_spline_counts" });
	queries.push_back({ &u_depal_mask_shift_off_fmt, "u_depal_mask_shift_off_fmt" });
	queries.push_back({ &u_mipBias, "u_mipBias" });

	attrMask = vs->GetAttrMask();
	availableUniforms = vs->GetUniformMask() | fs->GetUniformMask();

	std::vector<GLRProgram::Initializer> initialize;
	initialize.reserve(7);
	initialize.push_back({ &u_tex,          0, TEX_SLOT_PSP_TEXTURE });
	initialize.push_back({ &u_fbotex,       0, TEX_SLOT_SHADERBLEND_SRC });
	initialize.push_back({ &u_testtex,      0, TEX_SLOT_ALPHATEST });
	initialize.push_back({ &u_pal,          0, TEX_SLOT_CLUT }); // CLUT
	initialize.push_back({ &u_tess_points,  0, TEX_SLOT_SPLINE_POINTS }); // Control Points
	initialize.push_back({ &u_tess_weights_u, 0, TEX_SLOT_SPLINE_WEIGHTS_U });
	initialize.push_back({ &u_tess_weights_v, 0, TEX_SLOT_SPLINE_WEIGHTS_V });

	GLRProgramFlags flags{};
	flags.supportDualSource = gstate_c.Use(GPU_USE_DUALSOURCE_BLEND);
	if (!VSID.Bit(VS_BIT_IS_THROUGH) && gstate_c.Use(GPU_USE_DEPTH_CLAMP)) {
		flags.useClipDistance0 = true;
		if (VSID.Bit(VS_BIT_VERTEX_RANGE_CULLING) && gstate_c.Use(GPU_USE_CLIP_DISTANCE))
			flags.useClipDistance1 = true;
	} else if (VSID.Bit(VS_BIT_VERTEX_RANGE_CULLING) && gstate_c.Use(GPU_USE_CLIP_DISTANCE)) {
		flags.useClipDistance0 = true;
	}

	program = render->CreateProgram(shaders, semantics, queries, initialize, nullptr, flags);

	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_ALL_UNIFORMS;
}

void LinkedShader::Delete() {
	program->SetDeleteCallback([](void *thiz) {
		LinkedShader *ls = (LinkedShader *)thiz;
		delete ls;
	}, this);
	render_->DeleteProgram(program);
	program = nullptr;
}

LinkedShader::~LinkedShader() {
	_assert_(program == nullptr);
}

// Utility
static inline void SetFloatUniform(GLRenderManager *render, GLint *uniform, float value) {
	render->SetUniformF(uniform, 1, &value);
}

static inline void SetFloatUniform2(GLRenderManager *render, GLint *uniform, float value[2]) {
	render->SetUniformF(uniform, 2, value);
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

static void SetFloat24Uniform3Normalized(GLRenderManager *render, GLint *uniform, const uint32_t data[3]) {
	float f[4];
	ExpandFloat24x3ToFloat4AndNormalize(f, data);
	render->SetUniformF(uniform, 3, f);
}

static void SetFloatUniform4(GLRenderManager *render, GLint *uniform, float data[4]) {
	render->SetUniformF(uniform, 4, data);
}

static void SetMatrix4x3(GLRenderManager *render, GLint *uniform, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4Transposed(m4x4, m4x3);
	render->SetUniformM4x4(uniform, m4x4);
}

static inline void ScaleProjMatrix(Matrix4x4 &in, bool useBufferedRendering) {
	float yOffset = gstate_c.vpYOffset;
	if (!useBufferedRendering) {
		// GL upside down is a pain as usual.
		yOffset = -yOffset;
	}
	const Vec3 trans(gstate_c.vpXOffset, yOffset, gstate_c.vpZOffset);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale);
	in.translateAndScale(trans, scale);
}

static inline void FlipProjMatrix(Matrix4x4 &in, bool useBufferedRendering) {

	const bool invertedY = useBufferedRendering ? (gstate_c.vpHeight < 0) : (gstate_c.vpHeight > 0);
	if (invertedY) {
		in[1] = -in[1];
		in[5] = -in[5];
		in[9] = -in[9];
		in[13] = -in[13];
	}
	const bool invertedX = gstate_c.vpWidth < 0;
	if (invertedX) {
		in[0] = -in[0];
		in[4] = -in[4];
		in[8] = -in[8];
		in[12] = -in[12];
	}
}

static inline bool GuessVRDrawingHUD(bool is2D, bool flatScreen) {

	bool hud = true;
	//HUD can be disabled in settings
	if (!g_Config.bRescaleHUD) hud = false;
	//HUD cannot be rendered in flatscreen
	else if (flatScreen) hud = false;
	//HUD has to be 2D
	else if (!is2D) hud = false;
	//HUD has to be blended
	else if (!gstate.isAlphaBlendEnabled()) hud = false;
	//HUD cannot be rendered with clear color mask
	else if (gstate.isClearModeColorMask()) hud = false;
	//HUD cannot be rendered with depth color mask
	else if (gstate.isClearModeDepthMask()) hud = false;
	//HUD texture has to contain alpha channel
	else if (!gstate.isTextureAlphaUsed()) hud = false;
	//HUD texture cannot be in 5551 format
	else if (gstate.getTextureFormat() == GETextureFormat::GE_TFMT_5551) hud = false;
	//HUD texture cannot be in CLUT16 format
	else if (gstate.getTextureFormat() == GETextureFormat::GE_TFMT_CLUT16) hud = false;
	//HUD texture cannot be in CLUT32 format
	else if (gstate.getTextureFormat() == GETextureFormat::GE_TFMT_CLUT32) hud = false;
	//HUD cannot have full texture alpha
	else if (gstate_c.textureFullAlpha && gstate.getTextureFormat() != GETextureFormat::GE_TFMT_CLUT4) hud = false;
	//HUD must have full vertex alpha
	else if (!gstate_c.vertexFullAlpha && gstate.getDepthTestFunction() == GE_COMP_NEVER) hud = false;
	//HUD cannot render FB screenshot
	else if (gstate_c.curTextureHeight % 68 <= 1) hud = false;
	//HUD cannot be rendered with add function
	else if (gstate.getTextureFunction() == GETexFunc::GE_TEXFUNC_ADD) hud = false;
	//HUD cannot be rendered with replace function
	else if (gstate.getTextureFunction() == GETexFunc::GE_TEXFUNC_REPLACE) hud = false;
	//HUD cannot be rendered with full clear color mask
	else if ((gstate.getClearModeColorMask() == 0xFFFFFF) && (gstate.getColorMask() == 0xFFFFFF)) hud = false;

	return hud;
}

void LinkedShader::use(const ShaderID &VSID) {
	render_->BindProgram(program);
	// Note that we no longer track attr masks here - we do it for the input layouts instead.
}

void LinkedShader::UpdateUniforms(const ShaderID &vsid, bool useBufferedRendering, const ShaderLanguageDesc &shaderLanguage) {
	u64 dirty = dirtyUniforms & availableUniforms;
	dirtyUniforms = 0;

	// Analyze scene
	bool is2D, flatScreen;
	if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY)) {
		is2D = Is2DVRObject(gstate.projMatrix, gstate.isModeThrough());
		flatScreen = IsFlatVRScene();
	}

	if (!dirty)
		return;

	if (dirty & DIRTY_DEPAL) {
		int indexMask = gstate.getClutIndexMask();
		int indexShift = gstate.getClutIndexShift();
		int indexOffset = gstate.getClutIndexStartPos() >> 4;
		int format = gstate_c.depalFramebufferFormat;
		uint32_t val = BytesToUint32(indexMask, indexShift, indexOffset, format);
		// Poke in a bilinear filter flag in the top bit.
		val |= gstate.isMagnifyFilteringEnabled() << 31;
		render_->SetUniformUI1(&u_depal_mask_shift_off_fmt, val);
	}

	// Set HUD mode
	if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY)) {
		if (GuessVRDrawingHUD(is2D, flatScreen)) {
			render_->SetUniformF1(&u_scaleX, g_Config.fHeadUpDisplayScale * 480.0f / 272.0f);
			render_->SetUniformF1(&u_scaleY, g_Config.fHeadUpDisplayScale);
		} else {
			render_->SetUniformF1(&u_scaleX, 1.0f);
			render_->SetUniformF1(&u_scaleY, 1.0f);
		}
	}

	// Update any dirty uniforms before we draw
	if (dirty & DIRTY_PROJMATRIX) {
		if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY)) {
			Matrix4x4 leftEyeMatrix, rightEyeMatrix;
			if (flatScreen || is2D) {
				memcpy(&leftEyeMatrix, gstate.projMatrix, 16 * sizeof(float));
				memcpy(&rightEyeMatrix, gstate.projMatrix, 16 * sizeof(float));
			} else {
				UpdateVRProjection(gstate.projMatrix, leftEyeMatrix.m, rightEyeMatrix.m);
			}
			UpdateVRParams(gstate.projMatrix);

			FlipProjMatrix(leftEyeMatrix, useBufferedRendering);
			FlipProjMatrix(rightEyeMatrix, useBufferedRendering);
			ScaleProjMatrix(leftEyeMatrix, useBufferedRendering);
			ScaleProjMatrix(rightEyeMatrix, useBufferedRendering);

			render_->SetUniformM4x4Stereo("u_proj_lens", &u_proj_lens, leftEyeMatrix.m, rightEyeMatrix.m);
		}

		Matrix4x4 flippedMatrix;
		memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));

		FlipProjMatrix(flippedMatrix, useBufferedRendering);
		ScaleProjMatrix(flippedMatrix, useBufferedRendering);

		render_->SetUniformM4x4(&u_proj, flippedMatrix.m);
		render_->SetUniformF1(&u_rotation, useBufferedRendering ? 0 : (float)g_display.rotation);
	}
	if (dirty & DIRTY_PROJTHROUGHMATRIX) {
		Matrix4x4 proj_through;
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
	if (dirty & DIRTY_TEX_ALPHA_MUL) {
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE) {
			doTextureAlpha = false;
		}
		float noAlphaMul[2] = { doTextureAlpha ? 0.0f : 1.0f, gstate.isColorDoublingEnabled() ? 2.0f : 1.0f };
		render_->SetUniformF(&u_texNoAlphaMul, 2, noAlphaMul);
	}
	if (dirty & DIRTY_ALPHACOLORREF) {
		if (shaderLanguage.bitwiseOps) {
			render_->SetUniformUI1(&u_alphacolorref, gstate.getColorTestRef() | ((gstate.getAlphaTestRef() & gstate.getAlphaTestMask()) << 24));
		} else {
			SetColorUniform3Alpha255(render_, &u_alphacolorref, gstate.getColorTestRef(), gstate.getAlphaTestRef() & gstate.getAlphaTestMask());
		}
	}
	if (dirty & DIRTY_ALPHACOLORMASK) {
		render_->SetUniformUI1(&u_alphacolormask, gstate.getColorTestMask() | (gstate.getAlphaTestMask() << 24));
	}
	if (dirty & DIRTY_COLORWRITEMASK) {
		render_->SetUniformUI1(&u_colorWriteMask, ~((gstate.pmska << 24) | (gstate.pmskc & 0xFFFFFF)));
	}
	if (dirty & DIRTY_FOGCOLOR) {
		SetColorUniform3(render_, &u_fogcolor, gstate.fogcolor);
		if (IsVREnabled()) {
			SetVRCompat(VR_COMPAT_FOG_COLOR, gstate.fogcolor);
		}
	}
	if (dirty & DIRTY_FOGCOEF) {
		float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		// The PSP just ignores infnan here (ignoring IEEE), so take it down to a valid float.
		// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
		if (my_isnanorinf(fogcoef[0])) {
			// Not really sure what a sensible value might be, but let's try 64k.
			fogcoef[0] = std::signbit(fogcoef[0]) ? -65535.0f : 65535.0f;
		}
		if (my_isnanorinf(fogcoef[1])) {
			fogcoef[1] = std::signbit(fogcoef[1]) ? -65535.0f : 65535.0f;
		}
		render_->SetUniformF(&u_fogcoef, 2, fogcoef);
	}
	if (dirty & DIRTY_UVSCALEOFFSET) {
		float widthFactor = 1.0f;
		float heightFactor = 1.0f;
		if (gstate_c.textureIsFramebuffer) {
			const float invW = 1.0f / (float)gstate_c.curTextureWidth;
			const float invH = 1.0f / (float)gstate_c.curTextureHeight;
			const int w = gstate.getTextureWidth(0);
			const int h = gstate.getTextureHeight(0);
			widthFactor = (float)w * invW;
			heightFactor = (float)h * invH;
		}
		float uvscaleoff[4];
		if (gstate_c.submitType == SubmitType::HW_BEZIER || gstate_c.submitType == SubmitType::HW_SPLINE) {
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

	if ((dirty & DIRTY_MIPBIAS) && u_mipBias != -1) {
		float mipBias = (float)gstate.getTexLevelOffset16() * (1.0 / 16.0f);
		mipBias = (mipBias + 0.5f) / (float)(gstate.getTextureMaxLevel() + 1);

		render_->SetUniformF(&u_mipBias, 1, &mipBias);
	}

	// Transform
	if (dirty & DIRTY_WORLDMATRIX) {
		SetMatrix4x3(render_, &u_world, gstate.worldMatrix);
	}
	if (dirty & DIRTY_VIEWMATRIX) {
		if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY)) {
			float leftEyeView[16];
			float rightEyeView[16];
			ConvertMatrix4x3To4x4Transposed(leftEyeView, gstate.viewMatrix);
			ConvertMatrix4x3To4x4Transposed(rightEyeView, gstate.viewMatrix);
			if (!is2D) {
				UpdateVRView(leftEyeView, rightEyeView);
			}
			render_->SetUniformM4x4Stereo("u_view", &u_view, leftEyeView, rightEyeView);
		} else {
			SetMatrix4x3(render_, &u_view, gstate.viewMatrix);
		}
	}
	if (dirty & DIRTY_TEXMATRIX) {
		SetMatrix4x3(render_, &u_texmtx, gstate.tgenMatrix);
	}
	if (dirty & DIRTY_DEPTHRANGE) {
		// Since depth is [-1, 1] mapping to [minz, maxz], this is easyish.
		float vpZScale = gstate.getViewportZScale();
		float vpZCenter = gstate.getViewportZCenter();

		// These are just the reverse of the formulas in GPUStateUtils.
		float halfActualZRange = InfToZero(gstate_c.vpDepthScale != 0.0f ? vpZScale / gstate_c.vpDepthScale : 0.0f);
		float inverseDepthScale = InfToZero(gstate_c.vpDepthScale != 0.0f ? 1.0f / gstate_c.vpDepthScale : 0.0f);
		float minz = -((gstate_c.vpZOffset * halfActualZRange) - vpZCenter) - halfActualZRange;
		float viewZScale = halfActualZRange;
		float viewZCenter = minz + halfActualZRange;

		if (!gstate_c.Use(GPU_USE_ACCURATE_DEPTH)) {
			viewZScale = vpZScale;
			viewZCenter = vpZCenter;
		}

		float data[4] = { viewZScale, viewZCenter, gstate_c.vpZOffset, inverseDepthScale };
		SetFloatUniform4(render_, &u_depthRange, data);
	}
	if (dirty & DIRTY_CULLRANGE) {
		float minValues[4], maxValues[4];
		CalcCullRange(minValues, maxValues, !useBufferedRendering, true);
		SetFloatUniform4(render_, &u_cullRangeMin, minValues);
		SetFloatUniform4(render_, &u_cullRangeMax, maxValues);
	}

	if (dirty & DIRTY_STENCILREPLACEVALUE) {
		float f = (float)gstate.getStencilTestRef() * (1.0f / 255.0f);
		render_->SetUniformF(&u_stencilReplaceValue, 1, &f);
	}
	float bonetemp[16];
	for (int i = 0; i < numBones; i++) {
		if (dirty & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To4x4Transposed(bonetemp, gstate.boneMatrix + 12 * i);
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
	if (dirty & DIRTY_LIGHT_CONTROL) {
		render_->SetUniformUI1(&u_lightControl, PackLightControlBits());
	}
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
				// Prenormalize for cheaper calculations in shader
				SetFloat24Uniform3Normalized(render_, &u_lightpos[i], &gstate.lpos[i * 3]);
			} else {
				SetFloat24Uniform3(render_, &u_lightpos[i], &gstate.lpos[i * 3]);
			}
			if (u_lightdir[i] != -1) SetFloat24Uniform3Normalized(render_, &u_lightdir[i], &gstate.ldir[i * 3]);
			if (u_lightatt[i] != -1) SetFloat24Uniform3(render_, &u_lightatt[i], &gstate.latt[i * 3]);
			if (u_lightangle_spotCoef[i] != -1) {
				float lightangle_spotCoef[2] = { getFloat24(gstate.lcutoff[i]), getFloat24(gstate.lconv[i]) };
				SetFloatUniform2(render_, &u_lightangle_spotCoef[i], lightangle_spotCoef);
			}
			if (u_lightambient[i] != -1) SetColorUniform3(render_, &u_lightambient[i], gstate.lcolor[i * 3]);
			if (u_lightdiffuse[i] != -1) SetColorUniform3(render_, &u_lightdiffuse[i], gstate.lcolor[i * 3 + 1]);
			if (u_lightspecular[i] != -1) SetColorUniform3(render_, &u_lightspecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}

	if (dirty & DIRTY_BEZIERSPLINE) {
		if (u_spline_counts != -1) {
			render_->SetUniformI1(&u_spline_counts, gstate_c.spline_num_points_u);
		}
	}
}

static constexpr size_t CODE_BUFFER_SIZE = 32768;

ShaderManagerGLES::ShaderManagerGLES(Draw::DrawContext *draw)
	  : ShaderManagerCommon(draw), fsCache_(16), vsCache_(16) {
	render_ = (GLRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	codeBuffer_ = new char[CODE_BUFFER_SIZE];
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
}

ShaderManagerGLES::~ShaderManagerGLES() {
	delete [] codeBuffer_;
}

void ShaderManagerGLES::Clear() {
	DirtyLastShader();
	for (auto iter = linkedShaderCache_.begin(); iter != linkedShaderCache_.end(); ++iter) {
		iter->ls->Delete();
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
	DirtyLastShader();
}

void ShaderManagerGLES::ClearShaders() {
	// TODO: Recreate all from the diskcache when we come back.
	Clear();
}

void ShaderManagerGLES::DeviceLost() {
	Clear();
	render_ = nullptr;
	draw_ = nullptr;
}

void ShaderManagerGLES::DeviceRestore(Draw::DrawContext *draw) {
	render_ = (GLRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	draw_ = draw;
}

void ShaderManagerGLES::DirtyLastShader() {
	// Forget the last shader ID
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
	shaderSwitchDirtyUniforms_ = 0;
	lastShader_ = nullptr;
	lastVShaderSame_ = false;
}

// Can only fail by failing to generate the code (bad FSID).
// Any actual failures driver-side happens later in the render manager.
Shader *ShaderManagerGLES::CompileFragmentShader(FShaderID FSID) {
	uint64_t uniformMask;
	std::string errorString;
	FragmentShaderFlags flags;
	if (!GenerateFragmentShader(FSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &uniformMask, &flags, &errorString)) {
		ERROR_LOG_REPORT(G3D, "FS shader gen error: %s (%s: %08x:%08x)", errorString.c_str(), "GLES", FSID.d[0], FSID.d[1]);
		return nullptr;
	}
	_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "FS length error: %d", (int)strlen(codeBuffer_));
	std::string desc = FragmentShaderDesc(FSID);
	ShaderDescGLES params{ GL_FRAGMENT_SHADER, 0, uniformMask };
	return new Shader(render_, codeBuffer_, desc, params);
}

// Can only fail by failing to generate the code (bad VSID).
// Any actual failures driver-side happens later in the render manager.
Shader *ShaderManagerGLES::CompileVertexShader(VShaderID VSID) {
	bool useHWTransform = VSID.Bit(VS_BIT_USE_HW_TRANSFORM);
	uint32_t attrMask;
	uint64_t uniformMask;
	std::string errorString;
	VertexShaderFlags flags;
	if (!GenerateVertexShader(VSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &attrMask, &uniformMask, &flags, &errorString)) {
		ERROR_LOG_REPORT(G3D, "VS shader gen error: %s (%s: %08x:%08x)", errorString.c_str(), "GLES", VSID.d[0], VSID.d[1]);
		return nullptr;
	}
	_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "VS length error: %d", (int)strlen(codeBuffer_));
	std::string desc = VertexShaderDesc(VSID);
	ShaderDescGLES params{ GL_VERTEX_SHADER, attrMask, uniformMask };
	params.useHWTransform = useHWTransform;
	return new Shader(render_, codeBuffer_, desc, params);
}

Shader *ShaderManagerGLES::ApplyVertexShader(bool useHWTransform, bool useHWTessellation, VertexDecoder *decoder, bool weightsAsFloat, bool useSkinInDecode, VShaderID *VSID) {
	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(VSID, decoder, useHWTransform, useHWTessellation, weightsAsFloat, useSkinInDecode);
	} else {
		*VSID = lastVSID_;
	}

	if (lastShader_ != nullptr && *VSID == lastVSID_) {
		lastVShaderSame_ = true;
		return lastShader_->vs_;  	// Already all set.
	} else {
		lastVShaderSame_ = false;
	}
	lastVSID_ = *VSID;

	Shader *vs;
	if (vsCache_.Get(*VSID, &vs)) {
		return vs;
	}

	// Vertex shader not in cache. Let's compile it.
	vs = CompileVertexShader(*VSID);
	if (!vs) {
		ERROR_LOG(G3D, "Vertex shader generation failed, falling back to software transform");
		if (!g_Config.bHideSlowWarnings) {
			auto gr = GetI18NCategory(I18NCat::GRAPHICS);
			g_OSD.Show(OSDType::MESSAGE_ERROR, gr->T("hardware transform error - falling back to software"), 2.5f);
		}

		// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
		// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
		// next time and we'll do this over and over...

		// Can still work with software transform.
		VShaderID vsidTemp;
		ComputeVertexShaderID(&vsidTemp, decoder, false, false, weightsAsFloat, true);
		vs = CompileVertexShader(vsidTemp);
	}

	vsCache_.Insert(*VSID, vs);
	return vs;
}

LinkedShader *ShaderManagerGLES::ApplyFragmentShader(VShaderID VSID, Shader *vs, const ComputedPipelineState &pipelineState, bool useBufferedRendering) {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty) {
		if (lastShader_)
			lastShader_->dirtyUniforms |= dirty;
		shaderSwitchDirtyUniforms_ |= dirty;
		gstate_c.CleanUniforms();
	}

	FShaderID FSID;
	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID, pipelineState, draw_->GetBugs());
	} else {
		FSID = lastFSID_;
	}

	if (lastVShaderSame_ && FSID == lastFSID_) {
		lastShader_->UpdateUniforms(VSID, useBufferedRendering, draw_->GetShaderLanguageDesc());
		return lastShader_;
	}

	lastFSID_ = FSID;

	Shader *fs;
	if (!fsCache_.Get(FSID, &fs)) {
		// Fragment shader not in cache. Let's compile it.
		// Can't really tell if we succeeded since the compile is on the GPU thread later.
		// Could fail to generate, in which case we're kinda screwed.
		fs = CompileFragmentShader(FSID);
		if (!fs) {
			ERROR_LOG(G3D, "Failed to generate fragment shader with ID %08x:%08x", FSID.d[0], FSID.d[1]);
			// Still insert it so we don't end up spamming generation.
		}
		fsCache_.Insert(FSID, fs);
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
		_dbg_assert_(FSID.Bit(FS_BIT_LMODE) == VSID.Bit(VS_BIT_LMODE));
		_dbg_assert_(FSID.Bit(FS_BIT_FLATSHADE) == VSID.Bit(VS_BIT_FLATSHADE));

		if (vs == nullptr || fs == nullptr) {
			// Can't draw. This shouldn't really happen (but can happen if fragment shader generation fails)
			return nullptr;
		}

		// Check if we can link these.
		ls = new LinkedShader(render_, VSID, vs, FSID, fs, vs->UseHWTransform());
		ls->use(VSID);
		const LinkedShaderCacheEntry entry(vs, fs, ls);
		linkedShaderCache_.push_back(entry);
	} else {
		ls->use(VSID);
	}
	ls->UpdateUniforms(VSID, useBufferedRendering, draw_->GetShaderLanguageDesc());

	lastShader_ = ls;
	return ls;
}

std::string Shader::GetShaderString(DebugShaderStringType type, ShaderID id) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return isFragment_ ? FragmentShaderDesc(FShaderID(id)) : VertexShaderDesc(VShaderID(id));
	default:
		return "N/A";
	}
}

std::vector<std::string> ShaderManagerGLES::DebugGetShaderIDs(DebugShaderType type) {
	std::string id;
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
		vsCache_.Iterate([&](const VShaderID &id, Shader *shader) {
			std::string idstr;
			id.ToString(&idstr);
			ids.push_back(idstr);
		});
		break;
	case SHADER_TYPE_FRAGMENT:
		fsCache_.Iterate([&](const FShaderID &id, Shader *shader) {
			std::string idstr;
			id.ToString(&idstr);
			ids.push_back(idstr);
		});
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
		Shader *vs;
		if (vsCache_.Get(VShaderID(shaderId), &vs) && vs) {
			return vs->GetShaderString(stringType, shaderId);
		} else {
			return "";
		}
	}

	case SHADER_TYPE_FRAGMENT:
	{
		Shader *fs;
		if (fsCache_.Get(FShaderID(shaderId), &fs) && fs) {
			return fs->GetShaderString(stringType, shaderId);
		} else {
			return "";
		}
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

enum class CacheDetectFlags {
	EQUAL_DEPTH = 1,
};

#define CACHE_HEADER_MAGIC 0x83277592
#define CACHE_VERSION 36

struct CacheHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t useFlags;
	uint32_t detectFlags;
	int numVertexShaders;
	int numFragmentShaders;
	int numLinkedPrograms;
};

bool ShaderManagerGLES::LoadCacheFlags(File::IOFile &f, DrawEngineGLES *drawEngine) {
	CacheHeader header;
	if (!f.ReadArray(&header, 1)) {
		return false;
	}
	if (header.magic != CACHE_HEADER_MAGIC || header.version != CACHE_VERSION) {
		return false;
	}

	if ((header.detectFlags & (uint32_t)CacheDetectFlags::EQUAL_DEPTH) != 0) {
		drawEngine->SetEverUsedExactEqualDepth(true);
	}

	return true;
}

bool ShaderManagerGLES::LoadCache(File::IOFile &f) {
	// TODO: Get rid of this struct.
	struct {
		std::vector<VShaderID> vert;
		std::vector<FShaderID> frag;
		std::vector<std::pair<VShaderID, FShaderID>> link;

		size_t vertPos = 0;
		size_t fragPos = 0;
		size_t linkPos = 0;
		double start;

		void Clear() {
			vert.clear();
			frag.clear();
			link.clear();
			vertPos = 0;
			fragPos = 0;
			linkPos = 0;
		}

		bool Done() {
			return vertPos >= vert.size() && fragPos >= frag.size() && linkPos >= link.size();
		}
	} diskCachePending_;

	u64 sz = f.GetSize();
	f.Seek(0, SEEK_SET);
	CacheHeader header;
	if (!f.ReadArray(&header, 1)) {
		return false;
	}
	// We don't recheck the version, done in LoadCacheFlags().
	if (header.useFlags != gstate_c.GetUseFlags()) {
		return false;
	}
	diskCachePending_.start = time_now_d();
	diskCachePending_.Clear();

	// Sanity check the file contents
	if (header.numFragmentShaders > 1000 || header.numVertexShaders > 1000 || header.numLinkedPrograms > 1000) {
		ERROR_LOG(G3D, "Corrupt shader cache file header, aborting.");
		return false;
	}

	// Also make sure the size makes sense, in case there's corruption.
	u64 expectedSize = sizeof(header);
	expectedSize += header.numVertexShaders * sizeof(VShaderID);
	expectedSize += header.numFragmentShaders * sizeof(FShaderID);
	expectedSize += header.numLinkedPrograms * (sizeof(VShaderID) + sizeof(FShaderID));
	if (sz != expectedSize) {
		ERROR_LOG(G3D, "Shader cache file is wrong size: %lld instead of %lld", sz, expectedSize);
		return false;
	}

	diskCachePending_.vert.resize(header.numVertexShaders);
	if (!f.ReadArray(&diskCachePending_.vert[0], header.numVertexShaders)) {
		diskCachePending_.vert.clear();
		return false;
	}

	diskCachePending_.frag.resize(header.numFragmentShaders);
	if (!f.ReadArray(&diskCachePending_.frag[0], header.numFragmentShaders)) {
		diskCachePending_.vert.clear();
		diskCachePending_.frag.clear();
		return false;
	}

	for (int i = 0; i < header.numLinkedPrograms; i++) {
		VShaderID vsid;
		FShaderID fsid;
		if (!f.ReadArray(&vsid, 1)) {
			return false;
		}
		if (!f.ReadArray(&fsid, 1)) {
			return false;
		}
		diskCachePending_.link.emplace_back(vsid, fsid);
	}

	auto &pending = diskCachePending_;
	if (pending.Done()) {
		return true;
	}

	PSP_SetLoading("Compiling shaders...");

	double start = time_now_d();

	for (size_t &i = pending.vertPos; i < pending.vert.size(); i++) {
		const VShaderID &id = pending.vert[i];
		if (!vsCache_.ContainsKey(id)) {
			if (id.Bit(VS_BIT_IS_THROUGH) && id.Bit(VS_BIT_USE_HW_TRANSFORM)) {
				// Clearly corrupt, bailing.
				ERROR_LOG_REPORT(G3D, "Corrupt shader cache: Both IS_THROUGH and USE_HW_TRANSFORM set.");
				pending.Clear();
				return false;
			}

			Shader *vs = CompileVertexShader(id);
			if (!vs) {
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
		const FShaderID &id = pending.frag[i];
		if (!fsCache_.ContainsKey(id)) {
			Shader *fs = CompileFragmentShader(id);
			if (!fs) {
				// Give up on using the cache - something went wrong.
				// We'll still keep the shaders we generated so far around.
				ERROR_LOG(G3D, "Failed to compile a fragment shader loading from cache. Skipping rest of shader cache.");
				delete fs;
				pending.Clear();
				return false;
			}
			fsCache_.Insert(id, fs);
		} else {
			WARN_LOG(G3D, "Duplicate fragment shader found in GL shader cache, ignoring");
		}
	}

	linkedShaderCache_.reserve(pending.link.size() - pending.linkPos);
	for (size_t &i = pending.linkPos; i < pending.link.size(); i++) {
		const VShaderID &vsid = pending.link[i].first;
		const FShaderID &fsid = pending.link[i].second;
		Shader *vs = nullptr;
		Shader *fs = nullptr;
		vsCache_.Get(vsid, &vs);
		fsCache_.Get(fsid, &fs);
		if (vs && fs) {
			LinkedShader *ls = new LinkedShader(render_, vsid, vs, fsid, fs, vs->UseHWTransform(), true);
			LinkedShaderCacheEntry entry(vs, fs, ls);
			linkedShaderCache_.push_back(entry);
		}
	}

	// Okay, finally done.  Time to report status.
	double finish = time_now_d();

	NOTICE_LOG(G3D, "Precompile: Compiled and linked %d programs (%d vertex, %d fragment) in %0.1f milliseconds", (int)pending.link.size(), (int)pending.vert.size(), (int)pending.frag.size(), 1000 * (finish - pending.start));
	pending.Clear();

	return true;
}

void ShaderManagerGLES::SaveCache(const Path &filename, DrawEngineGLES *drawEngine) {
	if (linkedShaderCache_.empty()) {
		return;
	}
	INFO_LOG(G3D, "Saving the shader cache to '%s'", filename.c_str());
	FILE *f = File::OpenCFile(filename, "wb");
	if (!f) {
		// Can't save, give up for now.
		return;
	}
	CacheHeader header;
	header.magic = CACHE_HEADER_MAGIC;
	header.version = CACHE_VERSION;
	header.detectFlags = 0;
	if (drawEngine->EverUsedExactEqualDepth())
		header.detectFlags |= (uint32_t)CacheDetectFlags::EQUAL_DEPTH;
	header.useFlags = gstate_c.GetUseFlags();
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
	for (const auto &iter : linkedShaderCache_) {
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
}
