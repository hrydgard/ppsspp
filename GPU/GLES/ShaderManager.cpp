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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <map>

#include "math/lin/matrix4x4.h"

#include "Core/Reporting.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TransformPipeline.h"
#include "UI/OnScreenDisplay.h"

Shader::Shader(const char *code, uint32_t shaderType, bool useHWTransform) : failed_(false), useHWTransform_(useHWTransform) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugString(code);
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
		ERROR_LOG(G3D, "Error in shader compilation!\n");
		ERROR_LOG(G3D, "Info log: %s\n", infoLog);
		ERROR_LOG(G3D, "Shader source:\n%s\n", (const char *)code);
		Reporting::ReportMessage("Error in shader compilation: info: %s / code: %s", infoLog, (const char *)code);
#ifdef SHADERLOG
		OutputDebugString(infoLog);
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

LinkedShader::LinkedShader(Shader *vs, Shader *fs, bool useHWTransform)
		: program(0), dirtyUniforms(0), useHWTransform_(useHWTransform) {
	program = glCreateProgram();
	glAttachShader(program, vs->shader);
	glAttachShader(program, fs->shader);
	glLinkProgram(program);

	GLint linkStatus;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus != GL_TRUE) {
		GLint bufLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
		if (bufLength) {
			char* buf = new char[bufLength];
			glGetProgramInfoLog(program, bufLength, NULL, buf);
			ERROR_LOG(G3D, "Could not link program:\n %s", buf);
			ERROR_LOG(G3D, "VS:\n%s", vs->source().c_str());
			ERROR_LOG(G3D, "FS:\n%s", fs->source().c_str());
#ifdef SHADERLOG
			OutputDebugString(buf);
			OutputDebugString(vs->source().c_str());
			OutputDebugString(fs->source().c_str());
#endif
			delete [] buf;	// we're dead!
		}
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
	u_colormask = glGetUniformLocation(program, "u_colormask");

	// Transform
	u_view = glGetUniformLocation(program, "u_view");
	u_world = glGetUniformLocation(program, "u_world");
	u_texmtx = glGetUniformLocation(program, "u_texmtx");
	numBones = gstate.getNumBoneWeights();
#ifdef USE_BONE_ARRAY
	u_bone = glGetUniformLocation(program, "u_bone");
#else
	for (int i = 0; i < numBones; i++) {
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

	a_position = glGetAttribLocation(program, "a_position");
	a_color0 = glGetAttribLocation(program, "a_color0");
	a_color1 = glGetAttribLocation(program, "a_color1");
	a_texcoord = glGetAttribLocation(program, "a_texcoord");
	a_normal = glGetAttribLocation(program, "a_normal");
	a_weight0123 = glGetAttribLocation(program, "a_w1");
	a_weight4567 = glGetAttribLocation(program, "a_w2");

	glUseProgram(program);

	// Default uniform values
	glUniform1i(u_tex, 0);
	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_ALL;
	use();
}

LinkedShader::~LinkedShader() {
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
	const float col[4] = {
		(float)((color & 0xFF)),
		(float)((color & 0xFF00) >> 8),
		(float)((color & 0xFF0000) >> 16),
		(float)alpha
	};
	glUniform4fv(uniform, 1, col);
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

static void ConvertMatrix4x3To4x4(const float *m4x3, float *m4x4) {
	m4x4[0] = m4x3[0];
	m4x4[1] = m4x3[1];
	m4x4[2] = m4x3[2];
	m4x4[3] = 0.0f;
	m4x4[4] = m4x3[3];
	m4x4[5] = m4x3[4];
	m4x4[6] = m4x3[5];
	m4x4[7] = 0.0f;
	m4x4[8] = m4x3[6];
	m4x4[9] = m4x3[7];
	m4x4[10] = m4x3[8];
	m4x4[11] = 0.0f;
	m4x4[12] = m4x3[9];
	m4x4[13] = m4x3[10];
	m4x4[14] = m4x3[11];
	m4x4[15] = 1.0f;
}

static void SetMatrix4x3(int uniform, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4(m4x3, m4x4);
	glUniformMatrix4fv(uniform, 1, GL_FALSE, m4x4);
}

void LinkedShader::use() {
	glUseProgram(program);
	updateUniforms();
	glEnableVertexAttribArray(a_position);
	if (a_texcoord != -1) glEnableVertexAttribArray(a_texcoord);
	if (a_color0 != -1) glEnableVertexAttribArray(a_color0);
	if (a_color1 != -1) glEnableVertexAttribArray(a_color1);
	if (a_normal != -1) glEnableVertexAttribArray(a_normal);
	if (a_weight0123 != -1) glEnableVertexAttribArray(a_weight0123);
	if (a_weight4567 != -1) glEnableVertexAttribArray(a_weight4567);
}

void LinkedShader::stop() {
	glDisableVertexAttribArray(a_position);
	if (a_texcoord != -1) glDisableVertexAttribArray(a_texcoord);
	if (a_color0 != -1) glDisableVertexAttribArray(a_color0);
	if (a_color1 != -1) glDisableVertexAttribArray(a_color1);
	if (a_normal != -1) glDisableVertexAttribArray(a_normal);
	if (a_weight0123 != -1) glDisableVertexAttribArray(a_weight0123);
	if (a_weight4567 != -1) glDisableVertexAttribArray(a_weight4567);
}

void LinkedShader::updateUniforms() {
	if (!dirtyUniforms)
		return;

	// Update any dirty uniforms before we draw
	if (u_proj != -1 && (dirtyUniforms & DIRTY_PROJMATRIX)) {
		glUniformMatrix4fv(u_proj, 1, GL_FALSE, gstate.projMatrix);
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
	if (u_proj_through != -1 && (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX))
	{
		Matrix4x4 proj_through;
		proj_through.setOrtho(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);
		glUniformMatrix4fv(u_proj_through, 1, GL_FALSE, proj_through.getReadPtr());
	}
	if (u_texenv != -1 && (dirtyUniforms & DIRTY_TEXENV)) {
		SetColorUniform3(u_texenv, gstate.texenvcolor);
	}
	if (u_alphacolorref != -1 && (dirtyUniforms & DIRTY_ALPHACOLORREF)) {
		SetColorUniform3Alpha255(u_alphacolorref, gstate.colorref, (gstate.alphatest >> 8) & 0xFF);
	}
	if (u_colormask != -1 && (dirtyUniforms & DIRTY_COLORMASK)) {
		SetColorUniform3(u_colormask, gstate.colormask);
	}
	if (u_fogcolor != -1 && (dirtyUniforms & DIRTY_FOGCOLOR)) {
		SetColorUniform3(u_fogcolor, gstate.fogcolor);
	}
	if (u_fogcoef != -1 && (dirtyUniforms & DIRTY_FOGCOEF)) {
		const float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		glUniform2fv(u_fogcoef, 1, fogcoef);
	}

	// Texturing
	if (u_uvscaleoffset != -1 && (dirtyUniforms & DIRTY_UVSCALEOFFSET)) {
		float uvscaleoff[4] = {gstate_c.uScale, gstate_c.vScale, gstate_c.uOff, gstate_c.vOff};
		if (gstate.isModeThrough()) {
			// We never get here because we don't use HW transform with through mode.
			// Although - why don't we?
			uvscaleoff[0] /= gstate_c.curTextureWidth;
			uvscaleoff[1] /= gstate_c.curTextureHeight;
			uvscaleoff[2] /= gstate_c.curTextureWidth;
			uvscaleoff[3] /= gstate_c.curTextureHeight;
		} else {
			static const float rescale[4] = {2.0f, 2*127.5f/128.f, 2*32767.5f/32768.f, 2.0f};
			float factor = rescale[(gstate.vertType & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT];
			uvscaleoff[0] *= factor;
			uvscaleoff[1] *= factor;
		}
		glUniform4fv(u_uvscaleoffset, 1, uvscaleoff);
	}

	// Transform
	if (u_world != -1 && (dirtyUniforms & DIRTY_WORLDMATRIX)) {
		SetMatrix4x3(u_world, gstate.worldMatrix);
	}
	if (u_view != -1 && (dirtyUniforms & DIRTY_VIEWMATRIX)) {
		SetMatrix4x3(u_view, gstate.viewMatrix);
	}
	if (u_texmtx != -1 && (dirtyUniforms & DIRTY_TEXMATRIX)) {
		SetMatrix4x3(u_texmtx, gstate.tgenMatrix);
	}

	// TODO: Could even set all bones in one go if they're all dirty.
#ifdef USE_BONE_ARRAY
	if (u_bone != -1) {
		float allBones[8 * 16];

		bool allDirty = true;
		for (int i = 0; i < numBones; i++) {
			if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
				ConvertMatrix4x3To4x4(gstate.boneMatrix + 12 * i, allBones + 16 * i);
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
				if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
					glUniformMatrix4fv(u_bone + i, 1, GL_FALSE, allBones + 16 * i);
				}
			}
		}
	}
#else
	float bonetemp[16];
	for (int i = 0; i < numBones; i++) {
		if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To4x4(gstate.boneMatrix + 12 * i, bonetemp);
			glUniformMatrix4fv(u_bone[i], 1, GL_FALSE, bonetemp);
		}
	}
#endif

	// Lighting
	if (u_ambient != -1 && (dirtyUniforms & DIRTY_AMBIENT)) {
		SetColorUniform3Alpha(u_ambient, gstate.ambientcolor, gstate.ambientalpha & 0xFF);
	}
	if (u_matambientalpha != -1 && (dirtyUniforms & DIRTY_MATAMBIENTALPHA)) {
		SetColorUniform3Alpha(u_matambientalpha, gstate.materialambient, gstate.materialalpha & 0xFF);
	}
	if (u_matdiffuse != -1 && (dirtyUniforms & DIRTY_MATDIFFUSE)) {
		SetColorUniform3(u_matdiffuse, gstate.materialdiffuse);
	}
	if (u_matemissive != -1 && (dirtyUniforms & DIRTY_MATEMISSIVE)) {
		SetColorUniform3(u_matemissive, gstate.materialemissive);
	}
	if (u_matspecular != -1 && (dirtyUniforms & DIRTY_MATSPECULAR)) {
		SetColorUniform3ExtraFloat(u_matspecular, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
	}

	for (int i = 0; i < 4; i++) {
		if (dirtyUniforms & (DIRTY_LIGHT0 << i)) {
			GELightType type = (GELightType)((gstate.ltype[i] >> 8) & 3);
			if (type == GE_LIGHTTYPE_DIRECTIONAL) {
				// Prenormalize
				float x = gstate_c.lightpos[i][0];
				float y = gstate_c.lightpos[i][1];
				float z = gstate_c.lightpos[i][2];
				float len = sqrtf(x*x+y*y+z*z);
				if (len == 0.0f) 
					len = 1.0f;
				else
					len = 1.0f / len;
				float vec[3] = { x * len, y * len, z * len };
				if (u_lightpos[i] != -1) glUniform3fv(u_lightpos[i], 1, vec);
			} else {
				if (u_lightpos[i] != -1) glUniform3fv(u_lightpos[i], 1, gstate_c.lightpos[i]);
			}
			if (u_lightdir[i] != -1) glUniform3fv(u_lightdir[i], 1, gstate_c.lightdir[i]);
			if (u_lightatt[i] != -1) glUniform3fv(u_lightatt[i], 1, gstate_c.lightatt[i]);
			if (u_lightangle[i] != -1) glUniform1f(u_lightangle[i], gstate_c.lightangle[i]);
			if (u_lightspotCoef[i] != -1) glUniform1f(u_lightspotCoef[i], gstate_c.lightspotCoef[i]);
			if (u_lightambient[i] != -1) glUniform3fv(u_lightambient[i], 1, gstate_c.lightColor[0][i]);
			if (u_lightdiffuse[i] != -1) glUniform3fv(u_lightdiffuse[i], 1, gstate_c.lightColor[1][i]);
			if (u_lightspecular[i] != -1) glUniform3fv(u_lightspecular[i], 1, gstate_c.lightColor[2][i]);
		}
	}

	dirtyUniforms = 0;
}

ShaderManager::ShaderManager() : lastShader(NULL), globalDirty(0xFFFFFFFF), shaderSwitchDirty(0) {
	codeBuffer_ = new char[16384];
}

ShaderManager::~ShaderManager() {
	delete [] codeBuffer_;
}


void ShaderManager::DirtyUniform(u32 what) {
	globalDirty |= what;
}

void ShaderManager::Clear() {
	for (LinkedShaderCache::iterator iter = linkedShaderCache.begin(); iter != linkedShaderCache.end(); ++iter) {
		delete iter->second;
	}
	for (FSCache::iterator iter = fsCache.begin(); iter != fsCache.end(); ++iter)	{
		delete iter->second;
	}
	for (VSCache::iterator iter = vsCache.begin(); iter != vsCache.end(); ++iter)	{
		delete iter->second;
	}
	linkedShaderCache.clear();
	fsCache.clear();
	vsCache.clear();
	globalDirty = 0xFFFFFFFF;
	DirtyShader();
}

void ShaderManager::ClearCache(bool deleteThem) {
	Clear();
}


void ShaderManager::DirtyShader() {
	// Forget the last shader ID
	lastFSID.clear();
	lastVSID.clear();
	lastShader = 0;
}

void ShaderManager::EndFrame() { // disables vertex arrays
	if (lastShader)
		lastShader->stop();
	lastShader = 0;
}


LinkedShader *ShaderManager::ApplyShader(int prim) {
	if (globalDirty) {
		if (lastShader)
			lastShader->dirtyUniforms |= globalDirty;
		shaderSwitchDirty |= globalDirty;
		globalDirty = 0;
	}

	bool useHWTransform = CanUseHardwareTransform(prim);

	VertexShaderID VSID;
	FragmentShaderID FSID;
	ComputeVertexShaderID(&VSID, prim, useHWTransform);
	ComputeFragmentShaderID(&FSID);

	// Just update uniforms if this is the same shader as last time.
	if (lastShader != 0 && VSID == lastVSID && FSID == lastFSID) {
		lastShader->updateUniforms();
		return lastShader;	// Already all set.
	}

	if (lastShader != 0) {
		// There was a previous shader and we're switching.
		lastShader->stop();
	}

	// Deferred dirtying! Let's see if we can make this even more clever later.
	for (LinkedShaderCache::iterator iter = linkedShaderCache.begin(); iter != linkedShaderCache.end(); ++iter) {
		iter->second->dirtyUniforms |= shaderSwitchDirty;
	}
	shaderSwitchDirty = 0;

	lastVSID = VSID;
	lastFSID = FSID;

	VSCache::iterator vsIter = vsCache.find(VSID);
	Shader *vs;
	if (vsIter == vsCache.end())	{
		// Vertex shader not in cache. Let's compile it.
		GenerateVertexShader(prim, codeBuffer_, useHWTransform);
		vs = new Shader(codeBuffer_, GL_VERTEX_SHADER, useHWTransform);

		if (vs->Failed()) {
			ERROR_LOG(HLE, "Shader compilation failed, falling back to software transform");
			osm.Show("hardware transform error - falling back to software", 2.5f, 0xFF3030FF, -1, true);
			delete vs;

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			GenerateVertexShader(prim, codeBuffer_, false);
			vs = new Shader(codeBuffer_, GL_VERTEX_SHADER, false);
		}

		vsCache[VSID] = vs;
	} else {
		vs = vsIter->second;
	}

	FSCache::iterator fsIter = fsCache.find(FSID);
	Shader *fs;
	if (fsIter == fsCache.end())	{
		// Fragment shader not in cache. Let's compile it.
		GenerateFragmentShader(codeBuffer_);
		fs = new Shader(codeBuffer_, GL_FRAGMENT_SHADER, useHWTransform);
		fsCache[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	// Okay, we have both shaders. Let's see if there's a linked one.
	std::pair<Shader*, Shader*> linkedID(vs, fs);

	LinkedShaderCache::iterator iter = linkedShaderCache.find(linkedID);
	LinkedShader *ls;
	if (iter == linkedShaderCache.end()) {
		ls = new LinkedShader(vs, fs, vs->UseHWTransform());	// This does "use" automatically
		linkedShaderCache[linkedID] = ls;
	} else {
		ls = iter->second;
		ls->use();
	}

	lastShader = ls;
	return ls;
}
