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

#define SHADERLOG

#include "base/logging.h"
#include "Common/Log.h"
#include "DepalettizeShader.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureCache.h"

static const char *depalVShader =
"#version 100\n"
"precision highp float;\n"
"// Depal shader\n"
"attribute vec4 a_position;\n"
"attribute vec2 a_texcoord0;\n"
"varying vec2 v_texcoord0;\n"
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
		return true;
	}
}

DepalShaderCache::DepalShaderCache() {
	// Pre-build the vertex program
	vertexShader_ = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader_, 1, &depalVShader, 0);
	glCompileShader(vertexShader_);

	if (CheckShaderCompileSuccess(vertexShader_, depalVShader)) {
		// ...
	}
}

DepalShaderCache::~DepalShaderCache() {
	Clear();
	glDeleteShader(vertexShader_);
}

void GenerateDepalShader(char *buffer, GEBufferFormat pixelFormat) {
	char *p = buffer;
#define WRITE p+=sprintf

	WRITE(p, "#version 100\n");
	WRITE(p, "precision mediump float;\n");
	WRITE(p, "varying vec2 v_texcoord0;\n");
	WRITE(p, "uniform sampler2D tex;\n");
	WRITE(p, "uniform sampler2D pal;\n");
	WRITE(p, "void main() {\n");
	WRITE(p, "  vec4 index = texture2D(tex, v_texcoord0);\n");

	char lookupMethod[128] = "index.r";
	char offset[128] = "";

	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	const u32 clutBase = gstate.getClutIndexStartPos();

	int shift = gstate.getClutIndexShift();
	int mask = gstate.getClutIndexMask();

	float multiplier = 1.0f;
	// pixelformat is the format of the texture we are sampling.
	switch (pixelFormat) {
	case GE_FORMAT_8888:
		if (mask == 0xFF) {
			switch (shift) {  // bgra?
			case 0: strcpy(lookupMethod, "index.r"); break;
			case 8: strcpy(lookupMethod, "index.g"); break;
			case 16: strcpy(lookupMethod, "index.b"); break;
			default:
			case 24: strcpy(lookupMethod, "index.a"); break;
			}
		} else {
			// Ugh
		}
		break;
	case GE_FORMAT_4444:
		if ((mask & 0xF) == 0xF) {
			switch (shift) {  // bgra?
			case 0: strcpy(lookupMethod, "index.r"); break;
			case 4: strcpy(lookupMethod, "index.g"); break;
			case 8: strcpy(lookupMethod, "index.b"); break;
			default:
			case 12: strcpy(lookupMethod, "index.a"); break;
			}
			multiplier = 1.0f / 15.0f;
		} else {
			// Ugh
		}
		break;
	case GE_FORMAT_565:
		if ((mask & 0x3f) == 0x3F) {
			switch (shift) {  // bgra?
			case 0: strcpy(lookupMethod, "index.r"); break;
			case 5: strcpy(lookupMethod, "index.g"); break;
			default:
			case 11: strcpy(lookupMethod, "index.b"); break;
			}
			multiplier = 1.0f / 31.0f;
		} else {
			// Ugh
		}
		break;
	case GE_FORMAT_5551:
		if ((mask & 0x1F) == 0x1F) {
			switch (shift) {  // bgra?
			case 0: strcpy(lookupMethod, "index.r"); break;
			case 4: strcpy(lookupMethod, "index.g"); break;
			case 8: strcpy(lookupMethod, "index.b"); break;
			default:
			case 15: strcpy(lookupMethod, "index.a"); break;
			}
		} else {
			// Ugh
		}
		break;
	}

	if (clutBase != 0) {
		sprintf(offset, " + %.0f", (float)clutBase / 255.0f);   // 256?
	}

	if (true) {
		
		WRITE(p, "  gl_FragColor = vec4(index.r);\n", lookupMethod, offset);
		//WRITE(p, "  gl_FragColor = vec4(index) + texture2D(pal, vec2(v_texcoord0.x, 0));\n", lookupMethod, offset);
	} else {
		WRITE(p, "  gl_FragColor = texture2D(pal, vec2((%s * %f)%s, 0.0));\n", lookupMethod, multiplier, offset);
	}
	WRITE(p, "}\n");
}

u32 DepalShaderCache::GenerateShaderID(GEBufferFormat pixelFormat) {
	return (gstate.clutformat & 0xFFFFFF) | (pixelFormat << 24);
}

GLuint DepalShaderCache::GetClutTexture(const u32 clutID, u32 *rawClut) {
	auto oldtex = texCache_.find(clutID);
	if (oldtex != texCache_.end()) {
		return oldtex->second->texture;
	}

	GLuint dstFmt = getClutDestFormat(gstate.getClutPaletteFormat());
	
	DepalTexture *tex = new DepalTexture();
	glGenTextures(1, &tex->texture);
	glBindTexture(GL_TEXTURE_2D, tex->texture);
	GLuint components = dstFmt == GL_UNSIGNED_SHORT_5_6_5 ? GL_RGB : GL_RGBA;
	glTexImage2D(GL_TEXTURE_2D, 0, components, 256, 1, 0, components, dstFmt, (void *)rawClut);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	texCache_[clutID] = tex;
	return tex->texture;
}

void DepalShaderCache::Clear() {
	for (auto shader : cache_) {
		glDeleteShader(shader.second->fragShader);
		glDeleteProgram(shader.second->program);
		delete shader.second;
	}
	for (auto tex : texCache_) {
		glDeleteTextures(1, &tex.second->texture);
		delete tex.second;
	}
}

void DepalShaderCache::Decimate() {
	// TODO
}

GLuint DepalShaderCache::GetDepalettizeShader(GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second->program;
	}

	char *buffer = new char[2048];

	GenerateDepalShader(buffer, pixelFormat);

	GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);

	const char *buf = buffer;
	glShaderSource(fragShader, 1, &buf, 0);
	glCompileShader(fragShader);

	CheckShaderCompileSuccess(fragShader, buffer);

	delete[] buffer;

	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader_);
	glAttachShader(program, fragShader);
	
	glBindAttribLocation(program, 0, "a_position");
	glBindAttribLocation(program, 1, "a_texcoord0");

	glLinkProgram(program);
	glUseProgram(program);

	GLint u_tex = glGetUniformLocation(program, "tex");
	GLint u_pal = glGetUniformLocation(program, "pal");

	glUniform1d(u_tex, 0);
	glUniform1d(u_pal, 1);

	GLint linkStatus = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus != GL_TRUE) {
		GLint bufLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
		if (bufLength) {
			char* buf = new char[bufLength];
			glGetProgramInfoLog(program, bufLength, NULL, buf);
			ERROR_LOG(G3D, "Could not link program:\n %s", buf);
			delete[] buf;	// we're dead!
		}
		return 0;
	}

	DepalShader *depal = new DepalShader();
	depal->program = program;
	depal->fragShader = fragShader;

	cache_[id] = depal;
	
	return depal->program;
}
