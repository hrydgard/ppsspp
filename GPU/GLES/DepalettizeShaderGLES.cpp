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
#include "Common/StringUtils.h"
#include "Core/Reporting.h"
#include "DepalettizeShaderGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

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

DepalShaderCacheGLES::DepalShaderCacheGLES(Draw::DrawContext *draw) {
	render_ = (GLRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	// Pre-build the vertex program
	useGL3_ = gl_extensions.GLES3 || gl_extensions.VersionGEThan(3, 3);

	vertexShaderFailed_ = false;
	vertexShader_ = 0;
}

DepalShaderCacheGLES::~DepalShaderCacheGLES() {
	Clear();
}

bool DepalShaderCacheGLES::CreateVertexShader() {
	std::string src(useGL3_ ? depalVShader300 : depalVShader100);
	vertexShader_ = render_->CreateShader(GL_VERTEX_SHADER, src);
	return true;
}

GLRTexture *DepalShaderCacheGLES::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut) {
	u32 clutId = GetClutID(clutFormat, clutHash);

	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->texture;
	}

	GLuint dstFmt = getClutDestFormat(clutFormat);
	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	DepalTexture *tex = new DepalTexture();
	tex->texture = render_->CreateTexture(GL_TEXTURE_2D);
	GLuint components = dstFmt == GL_UNSIGNED_SHORT_5_6_5 ? GL_RGB : GL_RGBA;
	GLuint components2 = components;

	uint8_t *clutCopy = new uint8_t[1024];
	memcpy(clutCopy, rawClut, 1024);
	render_->TextureImage(tex->texture, 0, texturePixels, 1, components, components2, dstFmt, clutCopy, false);

	tex->lastFrame = gpuStats.numFlips;
	texCache_[clutId] = tex;
	return tex->texture;
}

void DepalShaderCacheGLES::Clear() {
	for (auto shader = cache_.begin(); shader != cache_.end(); ++shader) {
		render_->DeleteShader(shader->second->fragShader);
		if (shader->second->program) {
			render_->DeleteProgram(shader->second->program);
		}
		delete shader->second;
	}
	cache_.clear();
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		render_->DeleteTexture(tex->second->texture);
		delete tex->second;
	}
	texCache_.clear();
	if (vertexShader_) {
		render_->DeleteShader(vertexShader_);
		vertexShader_ = 0;
	}
}

void DepalShaderCacheGLES::Decimate() {
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ) {
		if (tex->second->lastFrame + DEPAL_TEXTURE_OLD_AGE < gpuStats.numFlips) {
			render_->DeleteTexture(tex->second->texture);
			delete tex->second;
			texCache_.erase(tex++);
		} else {
			++tex;
		}
	}
}

DepalShader *DepalShaderCacheGLES::GetDepalettizeShader(uint32_t clutMode, GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(clutMode, pixelFormat);

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
	
	std::string src(buffer);
	GLRShader *fragShader = render_->CreateShader(GL_FRAGMENT_SHADER, src);

	DepalShader *depal = new DepalShader();

	std::vector<GLRProgram::Semantic> semantics;
	semantics.push_back({ 0, "a_position" });
	semantics.push_back({ 1, "a_texcoord0" });

	std::vector<GLRProgram::UniformLocQuery> queries;
	queries.push_back({ &depal->u_tex, "tex" });
	queries.push_back({ &depal->u_pal, "pal" });

	std::vector<GLRProgram::Initializer> initializer;
	initializer.push_back({ &depal->u_tex, 0, 0 });
	initializer.push_back({ &depal->u_pal, 0, 3 });

	std::vector<GLRShader *> shaders{ vertexShader_, fragShader };

	GLRProgram *program = render_->CreateProgram(shaders, semantics, queries, initializer, false);

	depal->program = program;
	depal->fragShader = fragShader;
	depal->code = buffer;
	cache_[id] = depal;

	delete[] buffer;
	return depal->program ? depal : nullptr;
}

std::vector<std::string> DepalShaderCacheGLES::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<std::string> ids;
	for (auto &iter : cache_) {
		ids.push_back(StringFromFormat("%08x", iter.first));
	}
	return ids;
}

std::string DepalShaderCacheGLES::DebugGetShaderString(std::string idstr, DebugShaderType type, DebugShaderStringType stringType) {
	uint32_t id;
	sscanf(idstr.c_str(), "%08x", &id);
	auto iter = cache_.find(id);
	if (iter == cache_.end())
		return "";
	switch (stringType) {
	case SHADER_STRING_SHORT_DESC:
		return idstr;
	case SHADER_STRING_SOURCE_CODE:
		return iter->second->code;
	default:
		return "";
	}
}
