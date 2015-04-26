// Copyright (c) 2014- PPSSPP Project.

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

#include "base/logging.h"
#include "Common/Log.h"
#include "Core/Reporting.h"
#include "DepalettizeShader.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

static const int DEPAL_TEXTURE_OLD_AGE = 120;

#ifdef _WIN32
#define SHADERLOG
#endif

static const char *depalVShader100 =
#ifdef USING_GLES2
"#version 100\n"
"precision highp float;\n"
#endif
"attribute vec4 a_position;\n"
"attribute vec2 a_texcoord0;\n"
"varying vec2 v_texcoord0;\n"
"void main() {\n"
"  v_texcoord0 = a_texcoord0;\n"
"  gl_Position = a_position;\n"
"}\n";

static const char *depalVShader300 =
#ifdef USING_GLES2
"#version 300 es\n"
"precision highp float;\n"
#else
"#version 330\n"
#endif
"in vec4 a_position;\n"
"in vec2 a_texcoord0;\n"
"out vec2 v_texcoord0;\n"
"void main() {\n"
"  v_texcoord0 = a_texcoord0;\n"
"  gl_Position = a_position;\n"
"}\n";


static bool CheckShaderCompileSuccess(GLuint shader, const char *code) {
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
#ifdef SHADERLOG
		OutputDebugStringUTF8(infoLog);
#endif
		shader = 0;
		return false;
	} else {
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
#ifdef SHADERLOG
		OutputDebugStringUTF8(code);
#endif
		return true;
	}
}

DepalShaderCache::DepalShaderCache() {
	// Pre-build the vertex program
	useGL3_ = gl_extensions.GLES3 || gl_extensions.VersionGEThan(3, 3);

	vertexShaderFailed_ = false;
	vertexShader_ = 0;
}

DepalShaderCache::~DepalShaderCache() {
	Clear();
}

bool DepalShaderCache::CreateVertexShader() {
	if (vertexShaderFailed_) {
		return false;
	}

	vertexShader_ = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader_, 1, useGL3_ ? &depalVShader300 : &depalVShader100, 0);
	glCompileShader(vertexShader_);

	if (!CheckShaderCompileSuccess(vertexShader_, useGL3_ ? depalVShader300 : depalVShader100)) {
		glDeleteShader(vertexShader_);
		vertexShader_ = 0;
		// Don't try to recompile.
		vertexShaderFailed_ = true;
	}

	return !vertexShaderFailed_;
}

u32 DepalShaderCache::GenerateShaderID(GEBufferFormat pixelFormat) {
	return (gstate.clutformat & 0xFFFFFF) | (pixelFormat << 24);
}

GLuint DepalShaderCache::GetClutTexture(const u32 clutID, u32 *rawClut) {
	GEPaletteFormat palFormat = gstate.getClutPaletteFormat();
	const u32 realClutID = clutID ^ palFormat;

	auto oldtex = texCache_.find(realClutID);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->texture;
	}

	GLuint dstFmt = getClutDestFormat(palFormat);
	int texturePixels = palFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	bool useBGRA = UseBGRA8888() && dstFmt == GL_UNSIGNED_BYTE;

	DepalTexture *tex = new DepalTexture();
	glGenTextures(1, &tex->texture);
	glBindTexture(GL_TEXTURE_2D, tex->texture);
	GLuint components = dstFmt == GL_UNSIGNED_SHORT_5_6_5 ? GL_RGB : GL_RGBA;

	GLuint components2 = components;
	if (useBGRA) {
		components2 = GL_BGRA_EXT;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, components, texturePixels, 1, 0, components2, dstFmt, (void *)rawClut);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	tex->lastFrame = gpuStats.numFlips;
	texCache_[realClutID] = tex;
	return tex->texture;
}

void DepalShaderCache::Clear() {
	for (auto shader = cache_.begin(); shader != cache_.end(); ++shader) {
		glDeleteShader(shader->second->fragShader);
		if (shader->second->program) {
			glDeleteProgram(shader->second->program);
		}
		delete shader->second;
	}
	cache_.clear();
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		glDeleteTextures(1, &tex->second->texture);
		delete tex->second;
	}
	texCache_.clear();
	if (vertexShader_) {
		glDeleteShader(vertexShader_);
		vertexShader_ = 0;
	}
}

void DepalShaderCache::Decimate() {
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ) {
		if (tex->second->lastFrame + DEPAL_TEXTURE_OLD_AGE < gpuStats.numFlips) {
			glDeleteTextures(1, &tex->second->texture);
			delete tex->second;
			texCache_.erase(tex++);
		} else {
			++tex;
		}
	}
}

DepalShader *DepalShaderCache::GetDepalettizeShader(GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second;
	}

	if (vertexShader_ == 0) {
		if (!CreateVertexShader()) {
			// The vertex shader failed, no need to bother trying the fragment.
			return nullptr;
		}
	}

	char *buffer = new char[2048];

	GenerateDepalShader(buffer, pixelFormat, useGL3_ ? GLSL_300 : GLSL_140);

	GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);

	const char *buf = buffer;
	glShaderSource(fragShader, 1, &buf, 0);
	glCompileShader(fragShader);

	CheckShaderCompileSuccess(fragShader, buffer);

	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader_);
	glAttachShader(program, fragShader);
	
	glBindAttribLocation(program, 0, "a_position");
	glBindAttribLocation(program, 1, "a_texcoord0");

	glLinkProgram(program);
	glUseProgram(program);

	GLint u_tex = glGetUniformLocation(program, "tex");
	GLint u_pal = glGetUniformLocation(program, "pal");

	glUniform1i(u_tex, 0);
	glUniform1i(u_pal, 3);

	DepalShader *depal = new DepalShader();
	depal->program = program;
	depal->fragShader = fragShader;
	cache_[id] = depal;

	GLint linkStatus = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus != GL_TRUE) {
		GLint bufLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
		if (bufLength) {
			char* errorbuf = new char[bufLength];
			glGetProgramInfoLog(program, bufLength, NULL, errorbuf);
#ifdef SHADERLOG
			OutputDebugStringUTF8(buffer);
			OutputDebugStringUTF8(errorbuf);
#endif
			ERROR_LOG(G3D, "Could not link program:\n %s  \n\n %s", errorbuf, buf);
			delete[] errorbuf;	// we're dead!
		}

		// Since it failed, let's mark it in the cache so we don't keep retrying.
		// That will only make it slower.
		depal->program = 0;

		// We will delete the shader later in Clear().
		glDeleteProgram(program);
	} else {
		depal->a_position = glGetAttribLocation(program, "a_position");
		depal->a_texcoord0 = glGetAttribLocation(program, "a_texcoord0");
	}

	delete[] buffer;
	return depal->program ? depal : nullptr;
}
