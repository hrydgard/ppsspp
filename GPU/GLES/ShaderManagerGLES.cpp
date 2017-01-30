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

#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx/gl_debug_log.h"
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

Shader::Shader(const char *code, uint32_t glShaderType, bool useHWTransform)
	  : failed_(false), useHWTransform_(useHWTransform) {
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
	shader = glCreateShader(glShaderType);
	glShaderSource(shader, 1, &code, 0);
	glCompileShader(shader);
	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
#define MAX_INFO_LOG_SIZE 2048
		GLchar infoLog[MAX_INFO_LOG_SIZE];
		GLsizei len;
		glGetShaderInfoLog(shader, MAX_INFO_LOG_SIZE, &len, infoLog);
		infoLog[len] = '\0';
#ifdef __ANDROID__
		ELOG("Error in shader compilation! %s\n", infoLog);
		ELOG("Shader source:\n%s\n", (const char *)code);
#endif
		ERROR_LOG(G3D, "Error in shader compilation!\n");
		ERROR_LOG(G3D, "Info log: %s\n", infoLog);
		ERROR_LOG(G3D, "Shader source:\n%s\n", (const char *)code);
		Reporting::ReportMessage("Error in shader compilation: info: %s / code: %s", infoLog, (const char *)code);
#ifdef SHADERLOG
		OutputDebugStringUTF8(infoLog);
#endif
		failed_ = true;
		shader = 0;
	} else {
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

Shader::~Shader() {
	if (shader)
		glDeleteShader(shader);
}

LinkedShader::LinkedShader(ShaderID VSID, Shader *vs, ShaderID FSID, Shader *fs, bool useHWTransform)
		: useHWTransform_(useHWTransform), program(0), dirtyUniforms(0) {
	PROFILE_THIS_SCOPE("shaderlink");

	program = glCreateProgram();
	vs_ = vs;
	glAttachShader(program, vs->shader);
	glAttachShader(program, fs->shader);

	// Bind attribute locations to fixed locations so that they're
	// the same in all shaders. We use this later to minimize the calls to
	// glEnableVertexAttribArray and glDisableVertexAttribArray.
	glBindAttribLocation(program, ATTR_POSITION, "position");
	glBindAttribLocation(program, ATTR_TEXCOORD, "texcoord");
	glBindAttribLocation(program, ATTR_NORMAL, "normal");
	glBindAttribLocation(program, ATTR_W1, "w1");
	glBindAttribLocation(program, ATTR_W2, "w2");
	glBindAttribLocation(program, ATTR_COLOR0, "color0");
	glBindAttribLocation(program, ATTR_COLOR1, "color1");

#if !defined(USING_GLES2)
	if (gstate_c.featureFlags & GPU_SUPPORTS_DUALSOURCE_BLEND) {
		// Dual source alpha
		glBindFragDataLocationIndexed(program, 0, 0, "fragColor0");
		glBindFragDataLocationIndexed(program, 0, 1, "fragColor1");
	} else if (gl_extensions.VersionGEThan(3, 3, 0)) {
		glBindFragDataLocation(program, 0, "fragColor0");
	}
#elif !defined(IOS)
	if (gl_extensions.GLES3) {
		if (gstate_c.featureFlags & GPU_SUPPORTS_DUALSOURCE_BLEND) {
			glBindFragDataLocationIndexedEXT(program, 0, 0, "fragColor0");
			glBindFragDataLocationIndexedEXT(program, 0, 1, "fragColor1");
		}
	}
#endif

	glLinkProgram(program);

	GLint linkStatus = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus != GL_TRUE) {
		GLint bufLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
		if (bufLength) {
			char* buf = new char[bufLength];
			glGetProgramInfoLog(program, bufLength, NULL, buf);
#ifdef __ANDROID__
			ELOG("Could not link program:\n %s", buf);
#endif
			ERROR_LOG(G3D, "Could not link program:\n %s", buf);
			ERROR_LOG(G3D, "VS desc:\n%s\n", vs->GetShaderString(SHADER_STRING_SHORT_DESC, VSID).c_str());
			ERROR_LOG(G3D, "FS desc:\n%s\n", fs->GetShaderString(SHADER_STRING_SHORT_DESC, FSID).c_str());
			std::string vs_source = vs->GetShaderString(SHADER_STRING_SOURCE_CODE, VSID);
			std::string fs_source = fs->GetShaderString(SHADER_STRING_SOURCE_CODE, FSID);
			ERROR_LOG(G3D, "VS:\n%s\n", vs_source.c_str());
			ERROR_LOG(G3D, "FS:\n%s\n", fs_source.c_str());
			Reporting::ReportMessage("Error in shader program link: info: %s / fs: %s / vs: %s", buf, fs_source.c_str(), vs_source.c_str());
#ifdef SHADERLOG
			OutputDebugStringUTF8(buf);
			OutputDebugStringUTF8(vs_source.c_str());
			OutputDebugStringUTF8(fs_source.c_str());
#endif
			delete [] buf;	// we're dead!
		}
		// Prevent a buffer overflow.
		numBones = 0;
		// Avoid weird attribute enables.
		attrMask = 0;
		availableUniforms = 0;
		return;
	}

	INFO_LOG(G3D, "Linked shader: vs %i fs %i", (int)vs->shader, (int)fs->shader);

	u_tex = glGetUniformLocation(program, "tex");
	u_proj = glGetUniformLocation(program, "u_proj");
	u_proj_through = glGetUniformLocation(program, "u_proj_through");
	u_texenv = glGetUniformLocation(program, "u_texenv");
	u_fogcolor = glGetUniformLocation(program, "u_fogcolor");
	u_fogcoef = glGetUniformLocation(program, "u_fogcoef");
	u_alphacolorref = glGetUniformLocation(program, "u_alphacolorref");
	u_alphacolormask = glGetUniformLocation(program, "u_alphacolormask");
	u_stencilReplaceValue = glGetUniformLocation(program, "u_stencilReplaceValue");
	u_testtex = glGetUniformLocation(program, "testtex");

	u_fbotex = glGetUniformLocation(program, "fbotex");
	u_blendFixA = glGetUniformLocation(program, "u_blendFixA");
	u_blendFixB = glGetUniformLocation(program, "u_blendFixB");
	u_fbotexSize = glGetUniformLocation(program, "u_fbotexSize");

	// Transform
	u_view = glGetUniformLocation(program, "u_view");
	u_world = glGetUniformLocation(program, "u_world");
	u_texmtx = glGetUniformLocation(program, "u_texmtx");
	if (VSID.Bit(VS_BIT_ENABLE_BONES))
		numBones = TranslateNumBones(VSID.Bits(VS_BIT_BONES, 3) + 1);
	else
		numBones = 0;
	u_depthRange = glGetUniformLocation(program, "u_depthRange");

#ifdef USE_BONE_ARRAY
	u_bone = glGetUniformLocation(program, "u_bone");
#else
	for (int i = 0; i < 8; i++) {
		char name[10];
		sprintf(name, "u_bone%i", i);
		u_bone[i] = glGetUniformLocation(program, name);
	}
#endif

	// Lighting, texturing
	u_ambient = glGetUniformLocation(program, "u_ambient");
	u_matambientalpha = glGetUniformLocation(program, "u_matambientalpha");
	u_matdiffuse = glGetUniformLocation(program, "u_matdiffuse");
	u_matspecular = glGetUniformLocation(program, "u_matspecular");
	u_matemissive = glGetUniformLocation(program, "u_matemissive");
	u_uvscaleoffset = glGetUniformLocation(program, "u_uvscaleoffset");
	u_texclamp = glGetUniformLocation(program, "u_texclamp");
	u_texclampoff = glGetUniformLocation(program, "u_texclampoff");

	for (int i = 0; i < 4; i++) {
		char temp[64];
		sprintf(temp, "u_lightpos%i", i);
		u_lightpos[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightdir%i", i);
		u_lightdir[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightatt%i", i);
		u_lightatt[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightangle%i", i);
		u_lightangle[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightspotCoef%i", i);
		u_lightspotCoef[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightambient%i", i);
		u_lightambient[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightdiffuse%i", i);
		u_lightdiffuse[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightspecular%i", i);
		u_lightspecular[i] = glGetUniformLocation(program, temp);
	}

	// We need to fetch these unconditionally, gstate_c.spline or bezier will not be set if we
	// create this shader at load time from the shader cache.
	u_tess_pos_tex = glGetUniformLocation(program, "u_tess_pos_tex");
	u_tess_tex_tex = glGetUniformLocation(program, "u_tess_tex_tex");
	u_tess_col_tex = glGetUniformLocation(program, "u_tess_col_tex");
	u_spline_count_u = glGetUniformLocation(program, "u_spline_count_u");
	u_spline_count_v = glGetUniformLocation(program, "u_spline_count_v");
	u_spline_type_u = glGetUniformLocation(program, "u_spline_type_u");
	u_spline_type_v = glGetUniformLocation(program, "u_spline_type_v");

	attrMask = 0;
	if (-1 != glGetAttribLocation(program, "position")) attrMask |= 1 << ATTR_POSITION;
	if (-1 != glGetAttribLocation(program, "texcoord")) attrMask |= 1 << ATTR_TEXCOORD;
	if (-1 != glGetAttribLocation(program, "normal")) attrMask |= 1 << ATTR_NORMAL;
	if (-1 != glGetAttribLocation(program, "w1")) attrMask |= 1 << ATTR_W1;
	if (-1 != glGetAttribLocation(program, "w2")) attrMask |= 1 << ATTR_W2;
	if (-1 != glGetAttribLocation(program, "color0")) attrMask |= 1 << ATTR_COLOR0;
	if (-1 != glGetAttribLocation(program, "color1")) attrMask |= 1 << ATTR_COLOR1;

	availableUniforms = 0;
	if (u_proj != -1) availableUniforms |= DIRTY_PROJMATRIX;
	if (u_proj_through != -1) availableUniforms |= DIRTY_PROJTHROUGHMATRIX;
	if (u_texenv != -1) availableUniforms |= DIRTY_TEXENV;
	if (u_alphacolorref != -1) availableUniforms |= DIRTY_ALPHACOLORREF;
	if (u_alphacolormask != -1) availableUniforms |= DIRTY_ALPHACOLORMASK;
	if (u_fogcolor != -1) availableUniforms |= DIRTY_FOGCOLOR;
	if (u_fogcoef != -1) availableUniforms |= DIRTY_FOGCOEF;
	if (u_texenv != -1) availableUniforms |= DIRTY_TEXENV;
	if (u_uvscaleoffset != -1) availableUniforms |= DIRTY_UVSCALEOFFSET;
	if (u_texclamp != -1) availableUniforms |= DIRTY_TEXCLAMP;
	if (u_world != -1) availableUniforms |= DIRTY_WORLDMATRIX;
	if (u_view != -1) availableUniforms |= DIRTY_VIEWMATRIX;
	if (u_texmtx != -1) availableUniforms |= DIRTY_TEXMATRIX;
	if (u_stencilReplaceValue != -1) availableUniforms |= DIRTY_STENCILREPLACEVALUE;
	if (u_blendFixA != -1 || u_blendFixB != -1 || u_fbotexSize != -1) availableUniforms |= DIRTY_SHADERBLEND;
	if (u_depthRange != -1)
		availableUniforms |= DIRTY_DEPTHRANGE;

	// Looping up to numBones lets us avoid checking u_bone[i]
#ifdef USE_BONE_ARRAY
	if (u_bone != -1) {
		for (int i = 0; i < numBones; i++) {
			availableUniforms |= DIRTY_BONEMATRIX0 << i;
		}
	}
#else
	for (int i = 0; i < numBones; i++) {
		if (u_bone[i] != -1)
			availableUniforms |= DIRTY_BONEMATRIX0 << i;
	}
#endif
	if (u_ambient != -1) availableUniforms |= DIRTY_AMBIENT;
	if (u_matambientalpha != -1) availableUniforms |= DIRTY_MATAMBIENTALPHA;
	if (u_matdiffuse != -1) availableUniforms |= DIRTY_MATDIFFUSE;
	if (u_matemissive != -1) availableUniforms |= DIRTY_MATEMISSIVE;
	if (u_matspecular != -1) availableUniforms |= DIRTY_MATSPECULAR;
	for (int i = 0; i < 4; i++) {
		if (u_lightdir[i] != -1 ||
				u_lightspecular[i] != -1 ||
				u_lightpos[i] != -1)
			availableUniforms |= DIRTY_LIGHT0 << i;
	}
	if (u_spline_count_u != -1) availableUniforms |= DIRTY_BEZIERCOUNTU;
	if (u_spline_count_u != -1) availableUniforms |= DIRTY_SPLINECOUNTU;
	if (u_spline_count_v != -1) availableUniforms |= DIRTY_SPLINECOUNTV;
	if (u_spline_type_u != -1) availableUniforms |= DIRTY_SPLINETYPEU;
	if (u_spline_type_v != -1) availableUniforms |= DIRTY_SPLINETYPEV;

	glUseProgram(program);

	// Default uniform values
	glUniform1i(u_tex, 0);
	glUniform1i(u_fbotex, 1);
	glUniform1i(u_testtex, 2);

	
	if (u_tess_pos_tex != -1)
		glUniform1i(u_tess_pos_tex, 4); // Texture unit 4
	if (u_tess_tex_tex != -1)
		glUniform1i(u_tess_tex_tex, 5); // Texture unit 5
	if (u_tess_col_tex != -1)
		glUniform1i(u_tess_col_tex, 6); // Texture unit 6

	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_ALL_UNIFORMS;
	CHECK_GL_ERROR_IF_DEBUG();
}

LinkedShader::~LinkedShader() {
	// Shaders are automatically detached by glDeleteProgram.
	glDeleteProgram(program);
}

// Utility
static void SetColorUniform3(int uniform, u32 color) {
	const float col[3] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f
	};
	glUniform3fv(uniform, 1, col);
}

static void SetColorUniform3Alpha(int uniform, u32 color, u8 alpha) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		alpha/255.0f
	};
	glUniform4fv(uniform, 1, col);
}

// This passes colors unscaled (e.g. 0 - 255 not 0 - 1.)
static void SetColorUniform3Alpha255(int uniform, u32 color, u8 alpha) {
	if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
		const float col[4] = {
			(float)((color & 0xFF) >> 0) * (1.0f / 255.0f),
			(float)((color & 0xFF00) >> 8) * (1.0f / 255.0f),
			(float)((color & 0xFF0000) >> 16) * (1.0f / 255.0f),
			(float)alpha * (1.0f / 255.0f)
		};
		glUniform4fv(uniform, 1, col);
	} else {
		const float col[4] = {
			(float)((color & 0xFF) >> 0),
			(float)((color & 0xFF00) >> 8),
			(float)((color & 0xFF0000) >> 16),
			(float)alpha 
		};
		glUniform4fv(uniform, 1, col);
	}
}

static void SetColorUniform3iAlpha(int uniform, u32 color, u8 alpha) {
	const int col[4] = {
		(int)((color & 0xFF) >> 0),
		(int)((color & 0xFF00) >> 8),
		(int)((color & 0xFF0000) >> 16),
		(int)alpha,
	};
	glUniform4iv(uniform, 1, col);
}

static void SetColorUniform3ExtraFloat(int uniform, u32 color, float extra) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		extra
	};
	glUniform4fv(uniform, 1, col);
}

static void SetFloat24Uniform3(int uniform, const u32 data[3]) {
	const u32 col[3] = {
		data[0] << 8, data[1] << 8, data[2] << 8
	};
	glUniform3fv(uniform, 1, (const GLfloat *)&col[0]);
}

static void SetFloatUniform4(int uniform, float data[4]) {
	glUniform4fv(uniform, 1, data);
}

static void SetMatrix4x3(int uniform, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4(m4x4, m4x3);
	glUniformMatrix4fv(uniform, 1, GL_FALSE, m4x4);
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

void LinkedShader::use(const ShaderID &VSID, LinkedShader *previous) {
	glUseProgram(program);
	int enable, disable;
	if (previous) {
		enable = attrMask & ~previous->attrMask;
		disable = (~attrMask) & previous->attrMask;
	} else {
		enable = attrMask;
		disable = ~attrMask;
	}
	for (int i = 0; i < ATTR_COUNT; i++) {
		if (enable & (1 << i))
			glEnableVertexAttribArray(i);
		else if (disable & (1 << i))
			glDisableVertexAttribArray(i);
	}
}

void LinkedShader::stop() {
	for (int i = 0; i < ATTR_COUNT; i++) {
		if (attrMask & (1 << i))
			glDisableVertexAttribArray(i);
	}
}

void LinkedShader::UpdateUniforms(u32 vertType, const ShaderID &vsid) {
	CHECK_GL_ERROR_IF_DEBUG();

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

		glUniformMatrix4fv(u_proj, 1, GL_FALSE, flippedMatrix.m);
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
		glUniformMatrix4fv(u_proj_through, 1, GL_FALSE, proj_through.getReadPtr());
	}
	if (dirty & DIRTY_TEXENV) {
		SetColorUniform3(u_texenv, gstate.texenvcolor);
	}
	if (dirty & DIRTY_ALPHACOLORREF) {
		SetColorUniform3Alpha255(u_alphacolorref, gstate.getColorTestRef(), gstate.getAlphaTestRef() & gstate.getAlphaTestMask());
	}
	if (dirty & DIRTY_ALPHACOLORMASK) {
		SetColorUniform3iAlpha(u_alphacolormask, gstate.colortestmask, gstate.getAlphaTestMask());
	}
	if (dirty & DIRTY_FOGCOLOR) {
		SetColorUniform3(u_fogcolor, gstate.fogcolor);
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
		glUniform2fv(u_fogcoef, 1, fogcoef);
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
		glUniform4fv(u_uvscaleoffset, 1, uvscaleoff);
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
		glUniform4fv(u_texclamp, 1, texclamp);
		if (u_texclampoff != -1) {
			glUniform2fv(u_texclampoff, 1, texclampoff);
		}
	}

	// Transform
	if (dirty & DIRTY_WORLDMATRIX) {
		SetMatrix4x3(u_world, gstate.worldMatrix);
	}
	if (dirty & DIRTY_VIEWMATRIX) {
		SetMatrix4x3(u_view, gstate.viewMatrix);
	}
	if (dirty & DIRTY_TEXMATRIX) {
		SetMatrix4x3(u_texmtx, gstate.tgenMatrix);
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
		SetFloatUniform4(u_depthRange, data);
	}

	if (dirty & DIRTY_STENCILREPLACEVALUE) {
		glUniform1f(u_stencilReplaceValue, (float)gstate.getStencilTestRef() * (1.0f / 255.0f));
	}
	// TODO: Could even set all bones in one go if they're all dirty.
#ifdef USE_BONE_ARRAY
	if (u_bone != -1) {
		float allBones[8 * 16];

		bool allDirty = true;
		for (int i = 0; i < numBones; i++) {
			if (dirty & (DIRTY_BONEMATRIX0 << i)) {
				ConvertMatrix4x3To4x4(allBones + 16 * i, gstate.boneMatrix + 12 * i);
			} else {
				allDirty = false;
			}
		}
		if (allDirty) {
			// Set them all with one call
			glUniformMatrix4fv(u_bone, numBones, GL_FALSE, allBones);
		} else {
			// Set them one by one. Could try to coalesce two in a row etc but too lazy.
			for (int i = 0; i < numBones; i++) {
				if (dirty & (DIRTY_BONEMATRIX0 << i)) {
					glUniformMatrix4fv(u_bone + i, 1, GL_FALSE, allBones + 16 * i);
				}
			}
		}
	}
#else
	float bonetemp[16];
	for (int i = 0; i < numBones; i++) {
		if (dirty & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To4x4(bonetemp, gstate.boneMatrix + 12 * i);
			glUniformMatrix4fv(u_bone[i], 1, GL_FALSE, bonetemp);
		}
	}
#endif

	if (dirty & DIRTY_SHADERBLEND) {
		if (u_blendFixA != -1) {
			SetColorUniform3(u_blendFixA, gstate.getFixA());
		}
		if (u_blendFixB != -1) {
			SetColorUniform3(u_blendFixB, gstate.getFixB());
		}

		const float fbotexSize[2] = {
			1.0f / (float)gstate_c.curRTRenderWidth,
			1.0f / (float)gstate_c.curRTRenderHeight,
		};
		if (u_fbotexSize != -1) {
			glUniform2fv(u_fbotexSize, 1, fbotexSize);
		}
	}

	// Lighting
	if (dirty & DIRTY_AMBIENT) {
		SetColorUniform3Alpha(u_ambient, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (dirty & DIRTY_MATAMBIENTALPHA) {
		SetColorUniform3Alpha(u_matambientalpha, gstate.materialambient, gstate.getMaterialAmbientA());
	}
	if (dirty & DIRTY_MATDIFFUSE) {
		SetColorUniform3(u_matdiffuse, gstate.materialdiffuse);
	}
	if (dirty & DIRTY_MATEMISSIVE) {
		SetColorUniform3(u_matemissive, gstate.materialemissive);
	}
	if (dirty & DIRTY_MATSPECULAR) {
		SetColorUniform3ExtraFloat(u_matspecular, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
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
				glUniform3fv(u_lightpos[i], 1, vec);
			} else {
				SetFloat24Uniform3(u_lightpos[i], &gstate.lpos[i * 3]);
			}
			if (u_lightdir[i] != -1) SetFloat24Uniform3(u_lightdir[i], &gstate.ldir[i * 3]);
			if (u_lightatt[i] != -1) SetFloat24Uniform3(u_lightatt[i], &gstate.latt[i * 3]);
			if (u_lightangle[i] != -1) glUniform1f(u_lightangle[i], getFloat24(gstate.lcutoff[i]));
			if (u_lightspotCoef[i] != -1) glUniform1f(u_lightspotCoef[i], getFloat24(gstate.lconv[i]));
			if (u_lightambient[i] != -1) SetColorUniform3(u_lightambient[i], gstate.lcolor[i * 3]);
			if (u_lightdiffuse[i] != -1) SetColorUniform3(u_lightdiffuse[i], gstate.lcolor[i * 3 + 1]);
			if (u_lightspecular[i] != -1) SetColorUniform3(u_lightspecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}

	if (gstate_c.bezier) {
		if (dirty & DIRTY_BEZIERCOUNTU)
			glUniform1i(u_spline_count_u, gstate_c.bezier_count_u);
	} else if (gstate_c.spline) {
		if (dirty & DIRTY_SPLINECOUNTU)
			glUniform1i(u_spline_count_u, gstate_c.spline_count_u);
		if (dirty & DIRTY_SPLINECOUNTV)
			glUniform1i(u_spline_count_v, gstate_c.spline_count_v);
		if (dirty & DIRTY_SPLINETYPEU)
			glUniform1i(u_spline_type_u, gstate_c.spline_type_u);
		if (dirty & DIRTY_SPLINETYPEV)
			glUniform1i(u_spline_type_v, gstate_c.spline_type_v);
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

ShaderManagerGLES::ShaderManagerGLES()
		: lastShader_(nullptr), shaderSwitchDirtyUniforms_(0), diskCacheDirty_(false) {
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
	for (auto iter = fsCache_.begin(); iter != fsCache_.end(); ++iter)	{
		delete iter->second;
	}
	for (auto iter = vsCache_.begin(); iter != vsCache_.end(); ++iter)	{
		delete iter->second;
	}
	linkedShaderCache_.clear();
	fsCache_.clear();
	vsCache_.clear();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
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
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);
	shaderSwitchDirtyUniforms_ = 0;
}

void ShaderManagerGLES::DirtyLastShader() { // disables vertex arrays
	if (lastShader_)
		lastShader_->stop();
	lastShader_ = nullptr;
	lastVShaderSame_ = false;
}

Shader *ShaderManagerGLES::CompileFragmentShader(ShaderID FSID) {
	if (!GenerateFragmentShader(FSID, codeBuffer_)) {
		return nullptr;
	}
	return new Shader(codeBuffer_, GL_FRAGMENT_SHADER, false);
}

Shader *ShaderManagerGLES::CompileVertexShader(ShaderID VSID) {
	bool useHWTransform = VSID.Bit(VS_BIT_USE_HW_TRANSFORM);
	GenerateVertexShader(VSID, codeBuffer_);
	return new Shader(codeBuffer_, GL_VERTEX_SHADER, useHWTransform);
}

Shader *ShaderManagerGLES::ApplyVertexShader(int prim, u32 vertType, ShaderID *VSID) {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty) {
		if (lastShader_)
			lastShader_->dirtyUniforms |= dirty;
		shaderSwitchDirtyUniforms_ |= dirty;
		gstate_c.CleanUniforms();
	}

	bool useHWTransform = CanUseHardwareTransform(prim);

	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(VSID, vertType, useHWTransform);

		// Just update uniforms if this is the same shader as last time.
		if (lastShader_ != 0 && *VSID == lastVSID_) {
			lastVShaderSame_ = true;
			return lastShader_->vs_;  	// Already all set.
		} else {
			lastVShaderSame_ = false;
		}
		lastVSID_ = *VSID;
	} else {
		// Sanity check for debug.
		/*
		ShaderID temp;
		ComputeVertexShaderID(&temp, vertType, useHWTransform);
		if (temp != lastVSID_)
			Crash();
		*/
		if (lastShader_ != 0) {
			lastVShaderSame_ = true;
			*VSID = lastVSID_;
			return lastShader_->vs_;  	// Already all set.
		} else {
			lastVShaderSame_ = false;
			// Still need to compute this..
			ComputeVertexShaderID(VSID, vertType, useHWTransform);
			lastVSID_ = *VSID;
		}
	}


	VSCache::iterator vsIter = vsCache_.find(*VSID);
	Shader *vs;
	if (vsIter == vsCache_.end())	{
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
			ShaderID vsidTemp;
			ComputeVertexShaderID(&vsidTemp, vertType, false);
			GenerateVertexShader(vsidTemp, codeBuffer_);
			vs = new Shader(codeBuffer_, GL_VERTEX_SHADER, false);
		}

		vsCache_[*VSID] = vs;
		diskCacheDirty_ = true;
	} else {
		vs = vsIter->second;
	}
	return vs;
}

LinkedShader *ShaderManagerGLES::ApplyFragmentShader(ShaderID VSID, Shader *vs, u32 vertType, int prim) {
	ShaderID FSID;
	ComputeFragmentShaderID(&FSID);
	if (lastVShaderSame_ && FSID == lastFSID_) {
		lastShader_->UpdateUniforms(vertType, VSID);
		return lastShader_;
	}

	lastFSID_ = FSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	Shader *fs;
	if (fsIter == fsCache_.end())	{
		// Fragment shader not in cache. Let's compile it.
		fs = CompileFragmentShader(FSID);
		fsCache_[FSID] = fs;
		diskCacheDirty_ = true;
	} else {
		fs = fsIter->second;
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
		// Check if we can link these.
		ls = new LinkedShader(VSID, vs, FSID, fs, vs->UseHWTransform());
		ls->use(VSID, lastShader_);
		const LinkedShaderCacheEntry entry(vs, fs, ls);
		linkedShaderCache_.push_back(entry);
	} else {
		ls->use(VSID, lastShader_);
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
			for (auto iter : vsCache_) {
				iter.first.ToString(&id);
				ids.push_back(id);
			}
		}
		break;
	case SHADER_TYPE_FRAGMENT:
		{
			for (auto iter : fsCache_) {
				iter.first.ToString(&id);
				ids.push_back(id);
			}
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
		auto iter = vsCache_.find(shaderId);
		if (iter == vsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType, iter->first);
	}

	case SHADER_TYPE_FRAGMENT:
	{
		auto iter = fsCache_.find(shaderId);
		if (iter == fsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType, iter->first);
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
#define CACHE_VERSION 3
struct CacheHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t featureFlags;
	uint32_t reserved;
	int numVertexShaders;
	int numFragmentShaders;
	int numLinkedPrograms;
};

void ShaderManagerGLES::LoadAndPrecompile(const std::string &filename) {
	File::IOFile f(filename, "rb");
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
	double start = time_now_d();

	for (int i = 0; i < header.numVertexShaders; i++) {
		ShaderID id;
		if (!f.ReadArray(&id, 1)) {
			ERROR_LOG(G3D, "Truncated shader cache file, aborting.");
			return;
		}
		Shader *vs = CompileVertexShader(id);
		if (vs->Failed()) {
			// Give up on using the cache, just bail. We can't safely create the fallback shaders here
			// without trying to deduce the vertType from the VSID.
			ERROR_LOG(G3D, "Failed to compile a vertex shader loading from cache. Skipping rest of shader cache.");
			delete vs;
			return;
		}
		vsCache_[id] = vs;
	}
	for (int i = 0; i < header.numFragmentShaders; i++) {
		ShaderID id;
		if (!f.ReadArray(&id, 1)) {
			ERROR_LOG(G3D, "Truncated shader cache file, aborting.");
			return;
		}
		fsCache_[id] = CompileFragmentShader(id);
	}
	for (int i = 0; i < header.numLinkedPrograms; i++) {
		ShaderID vsid, fsid;
		if (!f.ReadArray(&vsid, 1)) {
			ERROR_LOG(G3D, "Truncated shader cache file, aborting.");
			return;
		}
		if (!f.ReadArray(&fsid, 1)) {
			ERROR_LOG(G3D, "Truncated shader cache file, aborting.");
			return;
		}
		VSCache::iterator vs = vsCache_.find(vsid);
		FSCache::iterator fs = fsCache_.find(fsid);
		if (vs != vsCache_.end() && fs != fsCache_.end()) {
			LinkedShader *ls = new LinkedShader(vsid, vs->second, fsid, fs->second, vs->second->UseHWTransform());
			LinkedShaderCacheEntry entry(vs->second, fs->second, ls);
			linkedShaderCache_.push_back(entry);
		}
	}
	time_update();
	double end = time_now_d();

	NOTICE_LOG(G3D, "Compiled and linked %d programs (%d vertex, %d fragment) in %0.1f milliseconds", header.numLinkedPrograms, header.numVertexShaders, header.numFragmentShaders, 1000 * (end - start));
	NOTICE_LOG(G3D, "Loaded the shader cache from '%s'", filename.c_str());
	diskCacheDirty_ = false;
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
	for (auto iter : vsCache_) {
		ShaderID id = iter.first;
		fwrite(&id, 1, sizeof(id), f);
	}
	for (auto iter : fsCache_) {
		ShaderID id = iter.first;
		fwrite(&id, 1, sizeof(id), f);
	}
	for (auto iter : linkedShaderCache_) {
		ShaderID vsid, fsid;
		for (auto iter2 : vsCache_) {
			if (iter.vs == iter2.second)
				vsid = iter2.first;
		}
		for (auto iter2 : fsCache_) {
			if (iter.fs == iter2.second)
				fsid = iter2.first;
		}
		fwrite(&vsid, 1, sizeof(vsid), f);
		fwrite(&fsid, 1, sizeof(fsid), f);
	}
	fclose(f);
	diskCacheDirty_ = false;
}
