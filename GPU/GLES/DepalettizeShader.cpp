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

	vertexShader_ = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader_, 1, useGL3_ ? &depalVShader300 : &depalVShader100, 0);
	glCompileShader(vertexShader_);

	if (!CheckShaderCompileSuccess(vertexShader_, useGL3_ ? depalVShader300 : depalVShader100)) {
		// ...
	}
}

DepalShaderCache::~DepalShaderCache() {
	Clear();
	glDeleteShader(vertexShader_);
}

#define WRITE p+=sprintf

// Uses integer instructions available since OpenGL 3.0. Suitable for ES 3.0 as well.
void GenerateDepalShader300(char *buffer, GEBufferFormat pixelFormat) {
	char *p = buffer;
#ifdef USING_GLES2
	WRITE(p, "#version 300 es\n");
	WRITE(p, "precision mediump float;\n");
#else
	WRITE(p, "#version 330\n");
#endif
	WRITE(p, "in vec2 v_texcoord0;\n");
	WRITE(p, "out vec4 fragColor0;\n");
	WRITE(p, "uniform sampler2D tex;\n");
	WRITE(p, "uniform sampler2D pal;\n");

	WRITE(p, "void main() {\n");
	WRITE(p, "  vec4 color = texture2D(tex, v_texcoord0);\n");

	int mask = gstate.getClutIndexMask();
	int shift = gstate.getClutIndexShift();
	int offset = gstate.getClutIndexStartPos();
	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	// Unfortunately sampling turned our texture into floating point. To avoid this, might be able
	// to declare them as isampler2D objects, but these require integer textures, which needs more work.
	// Anyhow, we simply work around this by converting back to integer. Hopefully there will be no loss of precision.
	// Use the mask to skip reading some components.
	int shiftedMask = mask << shift;
	switch (pixelFormat) {
	case GE_FORMAT_8888:
		if (shiftedMask & 0xFF) WRITE(p, "  int r = int(color.r * 255.99);\n"); else WRITE(p, "  int r = 0;\n");
		if (shiftedMask & 0xFF00) WRITE(p, "  int g = int(color.g * 255.99);\n"); else WRITE(p, "  int g = 0;\n");
		if (shiftedMask & 0xFF0000) WRITE(p, "  int b = int(color.b * 255.99);\n"); else WRITE(p, "  int b = 0;\n");
		if (shiftedMask & 0xFF000000) WRITE(p, "  int a = int(color.a * 255.99);\n"); else WRITE(p, "  int a = 0;\n");
		WRITE(p, "  int index = (a << 24) | (b << 16) | (g << 8) | (r);\n");
		break;
	case GE_FORMAT_4444:
		if (shiftedMask & 0xF) WRITE(p, "  int r = int(color.r * 15.99);\n"); else WRITE(p, "  int r = 0;\n");
		if (shiftedMask & 0xF0) WRITE(p, "  int g = int(color.g * 15.99);\n"); else WRITE(p, "  int g = 0;\n");
		if (shiftedMask & 0xF00) WRITE(p, "  int b = int(color.b * 15.99);\n"); else WRITE(p, "  int b = 0;\n");
		if (shiftedMask & 0xF000) WRITE(p, "  int a = int(color.a * 15.99);\n"); else WRITE(p, "  int a = 0;\n");
		WRITE(p, "  int index = (a << 12) | (b << 8) | (g << 4) | (r);\n");
		break;
	case GE_FORMAT_565:
		if (shiftedMask & 0x1F) WRITE(p, "  int r = int(color.r * 31.99);\n"); else WRITE(p, "  int r = 0;\n");
		if (shiftedMask & 0x7E0) WRITE(p, "  int g = int(color.g * 63.99);\n"); else WRITE(p, "  int g = 0;\n");
		if (shiftedMask & 0xF800) WRITE(p, "  int b = int(color.b * 31.99);\n"); else WRITE(p, "  int b = 0;\n");
		WRITE(p, "  int index = (b << 11) | (g << 5) | (r);");
		break;
	case GE_FORMAT_5551:
		if (shiftedMask & 0x1F) WRITE(p, "  int r = int(color.r * 31.99);\n"); else WRITE(p, "  int r = 0;\n");
		if (shiftedMask & 0x3E0) WRITE(p, "  int g = int(color.g * 31.99);\n"); else WRITE(p, "  int g = 0;\n");
		if (shiftedMask & 0x7C00) WRITE(p, "  int b = int(color.b * 31.99);\n"); else WRITE(p, "  int b = 0;\n");
		if (shiftedMask & 0x8000) WRITE(p, "  int a = int(color.a);\n"); else WRITE(p, "  int a = 0;\n");
		WRITE(p, "  int index = (a << 15) | (b << 10) | (g << 5) | (r);");
		break;
	default:
		break;
	}

	float texturePixels = 256;
	if (clutFormat != GE_CMODE_32BIT_ABGR8888)
		texturePixels = 512;

	if (shift) {
		WRITE(p, "  index = ((index >> %i) & 0x%02x)", shift, mask);
	} else {
		WRITE(p, "  index = (index & 0x%02x)", mask);
	}
	if (offset) {
		WRITE(p, " | %i;\n", offset);  // '|' matches what we have in gstate.h
	} else {
		WRITE(p, ";\n");
	}

	WRITE(p, "  fragColor0 = texture2D(pal, vec2((float(index) + 0.5) * (1.0 / %f), 0.0));\n", texturePixels);
	WRITE(p, "}\n");
}

// FP only, to suit GL(ES) 2.0
void GenerateDepalShader100(char *buffer, GEBufferFormat pixelFormat) {
	char *p = buffer;

	char lookupMethod[128] = "index.r";
	char offset[128] = "";

	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	const u32 clutBase = gstate.getClutIndexStartPos();

	const int shift = gstate.getClutIndexShift();
	const int mask = gstate.getClutIndexMask();

	float index_multiplier = 1.0f;
	// pixelformat is the format of the texture we are sampling.
	bool formatOK = true;
	switch (pixelFormat) {
	case GE_FORMAT_8888:
		if ((mask & (mask + 1)) == 0) {
			// If the value has all bits contiguous (bitmask check above), we can mod by it + 1.
			const char *rgba = "rrrrrrrrggggggggbbbbbbbbaaaaaaaa";
			const u8 rgba_shift = shift & 7;
			if (rgba_shift == 0 && mask == 0xFF) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
			} else {
				sprintf(lookupMethod, "mod(index.%c * %f, %d.0)", rgba[shift], 255.99f / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				// Format was OK if there weren't bits from another component.
				formatOK = mask <= 255 - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	case GE_FORMAT_4444:
		if ((mask & (mask + 1)) == 0 && shift < 16) {
			const char *rgba = "rrrrggggbbbbaaaa";
			const u8 rgba_shift = shift & 3;
			if (rgba_shift == 0 && mask == 0xF) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
				index_multiplier = 15.0f / 256.0f;
			} else {
				// Let's divide and mod to get the right bits.  A common case is shift=0, mask=01.
				sprintf(lookupMethod, "mod(index.%c * %f, %d.0)", rgba[shift], 15.99f / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				formatOK = mask <= 15 - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	case GE_FORMAT_565:
		if ((mask & (mask + 1)) == 0 && shift < 16) {
			const u8 shifts[16] = {0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4};
			const int multipliers[16] = {31, 31, 31, 31, 31, 63, 63, 63, 63, 63, 63, 31, 31, 31, 31, 31};
			const char *rgba = "rrrrrggggggbbbbb";
			const u8 rgba_shift = shifts[shift];
			if (rgba_shift == 0 && mask == multipliers[shift]) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
				index_multiplier = multipliers[shift] / 256.0f;
			} else {
				// We just need to divide the right component by the right value, and then mod against the mask.
				// A common case is shift=1, mask=0f.
				sprintf(lookupMethod, "mod(index.%c * %f, %d.0)", rgba[shift], ((float)multipliers[shift] + 0.99f) / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				formatOK = mask <= multipliers[shift] - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	case GE_FORMAT_5551:
		if ((mask & (mask + 1)) == 0 && shift < 16) {
			const char *rgba = "rrrrrgggggbbbbba";
			const u8 rgba_shift = shift % 5;
			if (rgba_shift == 0 && mask == 0x1F) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
				index_multiplier = 31.0f / 256.0f;
			} else if (shift == 15 && mask == 1) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
				index_multiplier = 1.0f / 256.0f;
			} else {
				// A isn't possible here.
				sprintf(lookupMethod, "mod(index.%c * %f, %d.0)", rgba[shift], 31.99f / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				formatOK = mask <= 31 - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	default:
		break;
	}

	float texturePixels = 256.f;
	if (clutFormat != GE_CMODE_32BIT_ABGR8888) {
		texturePixels = 512.f;
		index_multiplier *= 0.5f;
	}

	// Adjust index_multiplier, similar to the use of 15.99 instead of 16 in the ES 3 path.
	// index_multiplier -= 0.01f / texturePixels;

	if (!formatOK) {
		ERROR_LOG_REPORT_ONCE(depal, G3D, "%i depal unsupported: shift=%i mask=%02x offset=%d", pixelFormat, shift, mask, clutBase);
	}

	// Offset by half a texel (plus clutBase) to turn NEAREST filtering into FLOOR.
	float texel_offset = ((float)clutBase + 0.5f) / texturePixels;
	sprintf(offset, " + %f", texel_offset);

#ifdef USING_GLES2
	WRITE(p, "#version 100\n");
	WRITE(p, "precision mediump float;\n");
#else
	WRITE(p, "#version 110\n");
#endif
	WRITE(p, "varying vec2 v_texcoord0;\n");
	WRITE(p, "uniform sampler2D tex;\n");
	WRITE(p, "uniform sampler2D pal;\n");
	WRITE(p, "void main() {\n");
	WRITE(p, "  vec4 index = texture2D(tex, v_texcoord0);\n");
	WRITE(p, "  float coord = (%s * %f)%s;\n", lookupMethod, index_multiplier, offset);
	WRITE(p, "  gl_FragColor = texture2D(pal, vec2(coord, 0.0));\n");
	WRITE(p, "}\n");
}

#undef WRITE


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

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
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
		glDeleteProgram(shader->second->program);
		delete shader->second;
	}
	cache_.clear();
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		glDeleteTextures(1, &tex->second->texture);
		delete tex->second;
	}
	texCache_.clear();
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

GLuint DepalShaderCache::GetDepalettizeShader(GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second->program;
	}

	char *buffer = new char[2048];

	if (useGL3_) {
		GenerateDepalShader300(buffer, pixelFormat);
	} else {
		GenerateDepalShader100(buffer, pixelFormat);
	}

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
	glUniform1i(u_pal, 1);

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

		delete[] buffer;
		return 0;
	}

	DepalShader *depal = new DepalShader();
	depal->program = program;
	depal->fragShader = fragShader;

	cache_[id] = depal;

	delete[] buffer;

	return depal->program;
}
