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

#include <map>

#include "math/lin/matrix4x4.h"

#include "../GPUState.h"
#include "../ge_constants.h"
#include "ShaderManager.h"
#include "TransformPipeline.h"

Shader::Shader(const char *code, uint32_t shaderType) {
#ifdef _DEBUG
	source_ = code;
#endif
#ifdef _WIN32
	// OutputDebugString(code);
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
	} else {
		//NOTICE_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
	}
}

LinkedShader::LinkedShader(Shader *vs, Shader *fs)
		: program(0), dirtyUniforms(0) {
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
			delete [] buf;	// we're dead!

			//_dbg_assert_msg_(HLE,0,"Could not link program");
		}
		return;
	}

	NOTICE_LOG(G3D, "Linked shader!");

	u_tex		= glGetUniformLocation(program, "tex");
	u_proj	 = glGetUniformLocation(program, "u_proj");
	u_texenv = glGetUniformLocation(program, "u_texenv");
	u_fogcolor = glGetUniformLocation(program, "u_fogcolor");
	u_fogparam = glGetUniformLocation(program, "u_fogparam");
	u_alpharef = glGetUniformLocation(program, "u_alpharef");

	a_position	= glGetAttribLocation(program, "a_position");
	a_color0		= glGetAttribLocation(program, "a_color0");
	a_color1		= glGetAttribLocation(program, "a_color1");
	a_texcoord	= glGetAttribLocation(program, "a_texcoord");

	glUseProgram(program);
	// Default uniform values
	glUniform1i(u_tex, 0);
	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_PROJMATRIX | DIRTY_TEXENV | DIRTY_ALPHAREF;
}

LinkedShader::~LinkedShader() {
	glDeleteProgram(program);
}

void LinkedShader::use() {
	glUseProgram(program);	
	glUniform1i(u_tex, 0);
	// Update any dirty uniforms before we draw
	if (dirtyUniforms & DIRTY_PROJMATRIX) {
		if (gstate.vertType & GE_VTYPE_THROUGH_MASK) {
			Matrix4x4 proj;
			proj.setOrtho(0.0f, 480, 272, 0, -1, 0);	// TODO: Store this somewhere instead of regenerating! And not in each LinkedShader object!
			glUniformMatrix4fv(u_proj, 1, GL_FALSE, proj.getReadPtr());
		}
		else
		{
			glUniformMatrix4fv(u_proj, 1, GL_FALSE, gstate.projMatrix);
		}

	}
	if (u_texenv != -1 && dirtyUniforms & DIRTY_TEXENV) {
		glUniform4f(u_texenv, 1.0, 1.0, 1.0, 1.0);	// TODO
	}
	if (u_alpharef != -1 && dirtyUniforms & DIRTY_ALPHAREF) {
		glUniform4f(u_alpharef, ((float)((gstate.alphatest >> 8) & 0xFF)) / 255.0f, 0.0f, 0.0f, 0.0f);
	}

	dirtyUniforms = 0;
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
}

void ShaderManager::ClearCache(bool deleteThem)
{
	Clear();
}


void ShaderManager::DirtyShader()
{
	// Forget the last shader ID
	lastFSID.clear();
	lastVSID.clear();
}


LinkedShader *ShaderManager::ApplyShader()
{
	if (globalDirty) {
		// Deferred dirtying! Let's see if we can make this even more clever later.
		for (LinkedShaderCache::iterator iter = linkedShaderCache.begin(); iter != linkedShaderCache.end(); ++iter) {
			iter->second->dirtyUniforms |= globalDirty;
		}
		globalDirty = 0;
	}

	VertexShaderID VSID;
	FragmentShaderID FSID;
	ComputeVertexShaderID(&VSID);
	ComputeFragmentShaderID(&FSID);

	// Bail quickly in the no-op case. TODO: why does it cause trouble?
	//	if (VSID == lastVSID && FSID == lastFSID) return lastShader;	// Already all set.

	lastVSID = VSID;
	lastFSID = FSID;

	VSCache::iterator vsIter = vsCache.find(VSID);
	Shader *vs;
	if (vsIter == vsCache.end())	{
		// Vertex shader not in cache. Let's compile it.
		char *shaderCode = GenerateVertexShader();
		vs = new Shader(shaderCode, GL_VERTEX_SHADER);
		vsCache[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	
	FSCache::iterator fsIter = fsCache.find(FSID);
	Shader *fs;
	if (fsIter == fsCache.end())	{
		// Fragment shader not in cache. Let's compile it.
		char *shaderCode = GenerateFragmentShader();
		fs = new Shader(shaderCode, GL_FRAGMENT_SHADER);
		fsCache[FSID] = fs;
	} else {
		fs = fsIter->second;
	}
	// Okay, we have both shaders. Let's see if there's a linked one.
	std::pair<Shader*, Shader*> linkedID(vs, fs);

	LinkedShaderCache::iterator iter = linkedShaderCache.find(linkedID);
	LinkedShader *ls;
	if (iter == linkedShaderCache.end()) {
		ls = new LinkedShader(vs, fs);	// This does "use" automatically
		linkedShaderCache[linkedID] = ls;
	} else {
		ls = iter->second;
	}

	ls->use();

	lastShader = ls;
	return ls;
}
