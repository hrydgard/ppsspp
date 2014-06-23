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

#ifdef _WIN32
#define SHADERLOG
#endif

#ifdef SHADERLOG
#include "Common/CommonWindows.h"
#endif

#include <map>

#include "base/logging.h"
#include "math/math_util.h"
#include "gfx_es2/gl_state.h"
#include "math/lin/matrix4x4.h"

#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TransformPipeline.h"
#include "UI/OnScreenDisplay.h"
#include "Framebuffer.h"

Shader::Shader(const char *code, uint32_t shaderType, bool useHWTransform) : failed_(false), useHWTransform_(useHWTransform) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugStringUTF8(code);
#endif
	shader = glCreateShader(shaderType);
	glShaderSource(shader, 1, &code, 0);
	glCompileShader(shader);
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
#define MAX_INFO_LOG_SIZE 2048
		GLchar infoLog[MAX_INFO_LOG_SIZE];
		GLsizei len;
		glGetShaderInfoLog(shader, MAX_INFO_LOG_SIZE, &len, infoLog);
		infoLog[len] = '\0';
#ifdef ANDROID
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
}

Shader::~Shader() {
	if (shader)
		glDeleteShader(shader);
}

LinkedShader::LinkedShader(Shader *vs, Shader *fs, u32 vertType, bool useHWTransform, LinkedShader *previous)
		: useHWTransform_(useHWTransform), program(0), dirtyUniforms(0) {
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

#ifndef USING_GLES2
	if (gl_extensions.ARB_blend_func_extended) {
		// Dual source alpha
		glBindFragDataLocationIndexed(program, 0, 0, "fragColor0");
		glBindFragDataLocationIndexed(program, 0, 1, "fragColor1");
	} else if (gl_extensions.VersionGEThan(3, 3, 0)) {
		glBindFragDataLocation(program, 0, "fragColor0");
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
#ifdef ANDROID
			ELOG("Could not link program:\n %s", buf);
#endif
			ERROR_LOG(G3D, "Could not link program:\n %s", buf);
			ERROR_LOG(G3D, "VS:\n%s", vs->source().c_str());
			ERROR_LOG(G3D, "FS:\n%s", fs->source().c_str());
			Reporting::ReportMessage("Error in shader program link: info: %s / fs: %s / vs: %s", buf, fs->source().c_str(), vs->source().c_str());
#ifdef SHADERLOG
			OutputDebugStringUTF8(buf);
			OutputDebugStringUTF8(vs->source().c_str());
			OutputDebugStringUTF8(fs->source().c_str());
#endif
			delete [] buf;	// we're dead!
		}
		// Prevent a buffer overflow.
		numBones = 0;
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

	u_fbotex = glGetUniformLocation(program, "fbotex");
	u_blendFixA = glGetUniformLocation(program, "u_blendFixA");
	u_blendFixB = glGetUniformLocation(program, "u_blendFixB");
	u_fbotexSize = glGetUniformLocation(program, "u_fbotexSize");

	// Transform
	u_view = glGetUniformLocation(program, "u_view");
	u_world = glGetUniformLocation(program, "u_world");
	u_texmtx = glGetUniformLocation(program, "u_texmtx");
	if (vertTypeGetWeightMask(vertType) != GE_VTYPE_WEIGHT_NONE)
		numBones = TranslateNumBones(vertTypeGetNumBoneWeights(vertType));
	else
		numBones = 0;

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

	// Looping up to numBones lets us avoid checking u_bone[i]
	for (int i = 0; i < numBones; i++) {
		if (u_bone[i] != -1)
			availableUniforms |= DIRTY_BONEMATRIX0 << i;
	}
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

	glUseProgram(program);

	// Default uniform values
	glUniform1i(u_tex, 0);
	glUniform1i(u_fbotex, 1);
	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_ALL;
	use(vertType, previous);
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

static void SetMatrix4x3(int uniform, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4(m4x4, m4x3);
	glUniformMatrix4fv(uniform, 1, GL_FALSE, m4x4);
}

void LinkedShader::use(u32 vertType, LinkedShader *previous) {
	glUseProgram(program);
	UpdateUniforms(vertType);
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

void LinkedShader::UpdateUniforms(u32 vertType) {
	u32 dirty = dirtyUniforms & availableUniforms;
	dirtyUniforms = 0;
	if (!dirty)
		return;

	// Update any dirty uniforms before we draw
	if (dirty & DIRTY_PROJMATRIX) {
		float flippedMatrix[16];
		memcpy(flippedMatrix, gstate.projMatrix, 16 * sizeof(float));
		if (gstate_c.vpHeight < 0) {
			flippedMatrix[5] = -flippedMatrix[5];
			flippedMatrix[13] = -flippedMatrix[13];
		}
		if (gstate_c.vpWidth < 0) {
			flippedMatrix[0] = -flippedMatrix[0];
			flippedMatrix[12] = -flippedMatrix[12];
		}
		glUniformMatrix4fv(u_proj, 1, GL_FALSE, flippedMatrix);
	}
	if (dirty & DIRTY_PROJTHROUGHMATRIX)
	{
		Matrix4x4 proj_through;
		proj_through.setOrtho(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);
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
		} else if (my_isnan(fogcoef[1]))	{
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

	// Texturing

	// If this dirty check is changed to true, Frontier Gate Boost works in texcoord speedhack mode.
	// This means that it's not a flushing issue.
	// It uses GE_TEXMAP_TEXTURE_MATRIX with GE_PROJMAP_UV a lot.
	// Can't figure out why it doesn't dirty at the right points though...
	if (dirty & DIRTY_UVSCALEOFFSET) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		static const float rescale[4] = {1.0f, 2*127.5f/128.f, 2*32767.5f/32768.f, 1.0f};
		const float factor = rescale[(vertType & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT];

		float uvscaleoff[4];

		switch (gstate.getUVGenMode()) {
		case GE_TEXMAP_TEXTURE_COORDS:
			// Not sure what GE_TEXMAP_UNKNOWN is, but seen in Riviera.  Treating the same as GE_TEXMAP_TEXTURE_COORDS works.
		case GE_TEXMAP_UNKNOWN:
			if (g_Config.bPrescaleUV) {
				// Shouldn't even get here as we won't use the uniform in the shader.
				// We are here but are prescaling UV in the decoder? Let's do the same as in the other case
				// except consider *Scale and *Off to be 1 and 0.
				uvscaleoff[0] = widthFactor;
				uvscaleoff[1] = heightFactor;
				uvscaleoff[2] = 0.0f;
				uvscaleoff[3] = 0.0f;
			} else {
				uvscaleoff[0] = gstate_c.uv.uScale * factor * widthFactor;
				uvscaleoff[1] = gstate_c.uv.vScale * factor * heightFactor;
				uvscaleoff[2] = gstate_c.uv.uOff * widthFactor;
				uvscaleoff[3] = gstate_c.uv.vOff * heightFactor;
			}
			break;

		// These two work the same whether or not we prescale UV.

		case GE_TEXMAP_TEXTURE_MATRIX:
			// We cannot bake the UV coord scale factor in here, as we apply a matrix multiplication
			// before this is applied, and the matrix multiplication may contain translation. In this case
			// the translation will be scaled which breaks faces in Hexyz Force for example.
			// So I've gone back to applying the scale factor in the shader.
			uvscaleoff[0] = widthFactor;
			uvscaleoff[1] = heightFactor;
			uvscaleoff[2] = 0.0f;
			uvscaleoff[3] = 0.0f;
			break;

		case GE_TEXMAP_ENVIRONMENT_MAP:
			// In this mode we only use uvscaleoff to scale to the texture size.
			uvscaleoff[0] = widthFactor;
			uvscaleoff[1] = heightFactor;
			uvscaleoff[2] = 0.0f;
			uvscaleoff[3] = 0.0f;
			break;

		default:
			ERROR_LOG_REPORT(G3D, "Unexpected UV gen mode: %d", gstate.getUVGenMode());
		}
		glUniform4fv(u_uvscaleoffset, 1, uvscaleoff);
	}

	if (dirty & DIRTY_TEXCLAMP) {
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
			if (u_lightpos[i] != -1) {
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
}

ShaderManager::ShaderManager() : lastShader_(NULL), globalDirty_(0xFFFFFFFF), shaderSwitchDirty_(0) {
	codeBuffer_ = new char[16384];
}

ShaderManager::~ShaderManager() {
	delete [] codeBuffer_;
}

void ShaderManager::Clear() {
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
	globalDirty_ = 0xFFFFFFFF;
	lastFSID_.clear();
	lastVSID_.clear();
	DirtyShader();
}

void ShaderManager::ClearCache(bool deleteThem) {
	Clear();
}

void ShaderManager::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.clear();
	lastVSID_.clear();
	DirtyLastShader();
	globalDirty_ = 0xFFFFFFFF;
	shaderSwitchDirty_ = 0;
}

void ShaderManager::DirtyLastShader() { // disables vertex arrays
	if (lastShader_)
		lastShader_->stop();
	lastShader_ = 0;
}

Shader *ShaderManager::ApplyVertexShader(int prim, u32 vertType) {
	// This doesn't work - we miss some events that really do need to dirty the prescale.
	// like changing the texmapmode.
	// if (g_Config.bPrescaleUV)
	//	 globalDirty_ &= ~DIRTY_UVSCALEOFFSET;

	if (globalDirty_) {
		if (lastShader_)
			lastShader_->dirtyUniforms |= globalDirty_;
		shaderSwitchDirty_ |= globalDirty_;
		globalDirty_ = 0;
	}

	bool useHWTransform = CanUseHardwareTransform(prim);

	VertexShaderID VSID;
	ComputeVertexShaderID(&VSID, vertType, prim, useHWTransform);

	// Just update uniforms if this is the same shader as last time.
	if (lastShader_ != 0 && VSID == lastVSID_) {
		lastVShaderSame_ = true;
		return lastShader_->vs_;	// Already all set.
	} else {
		lastVShaderSame_ = false;
	}

	lastVSID_ = VSID;

	VSCache::iterator vsIter = vsCache_.find(VSID);
	Shader *vs;
	if (vsIter == vsCache_.end())	{
		// Vertex shader not in cache. Let's compile it.
		GenerateVertexShader(prim, vertType, codeBuffer_, useHWTransform);
		vs = new Shader(codeBuffer_, GL_VERTEX_SHADER, useHWTransform);

		if (vs->Failed()) {
			ERROR_LOG(G3D, "Shader compilation failed, falling back to software transform");
			osm.Show("hardware transform error - falling back to software", 2.5f, 0xFF3030FF, -1, true);
			delete vs;

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			GenerateVertexShader(prim, vertType, codeBuffer_, false);
			vs = new Shader(codeBuffer_, GL_VERTEX_SHADER, false);
		}

		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	return vs;
}

LinkedShader *ShaderManager::ApplyFragmentShader(Shader *vs, int prim, u32 vertType) {
	FragmentShaderID FSID;
	ComputeFragmentShaderID(&FSID);
	if (lastVShaderSame_ && FSID == lastFSID_) {
		lastShader_->UpdateUniforms(vertType);
		return lastShader_;
	}

	lastFSID_ = FSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	Shader *fs;
	if (fsIter == fsCache_.end())	{
		// Fragment shader not in cache. Let's compile it.
		GenerateFragmentShader(codeBuffer_);
		fs = new Shader(codeBuffer_, GL_FRAGMENT_SHADER, vs->UseHWTransform());
		fsCache_[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	// Okay, we have both shaders. Let's see if there's a linked one.
	LinkedShader *ls = NULL;

	for (auto iter = linkedShaderCache_.begin(); iter != linkedShaderCache_.end(); ++iter) {
		// Deferred dirtying! Let's see if we can make this even more clever later.
		iter->ls->dirtyUniforms |= shaderSwitchDirty_;

		if (iter->vs == vs && iter->fs == fs) {
			ls = iter->ls;
		}
	}
	shaderSwitchDirty_ = 0;

	if (ls == NULL) {
		ls = new LinkedShader(vs, fs, vertType, vs->UseHWTransform(), lastShader_);  // This does "use" automatically
		const LinkedShaderCacheEntry entry(vs, fs, ls);
		linkedShaderCache_.push_back(entry);
	} else {
		ls->use(vertType, lastShader_);
	}

	lastShader_ = ls;
	return ls;
}
