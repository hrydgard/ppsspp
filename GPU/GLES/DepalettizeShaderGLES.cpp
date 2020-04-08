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

static const char *depalVShader100 = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec4 a_position;
attribute vec2 a_texcoord0;
varying vec2 v_texcoord0;
void main() {
  v_texcoord0 = a_texcoord0;
  gl_Position = a_position;
}
)";

static const char *depalVShader300 = R"(
#ifdef GL_ES
precision highp float;
#endif
in vec4 a_position;
in vec2 a_texcoord0;
out vec2 v_texcoord0;
void main() {
  v_texcoord0 = a_texcoord0;
  gl_Position = a_position;
}
)";

DepalShaderCacheGLES::DepalShaderCacheGLES(Draw::DrawContext *draw) {
	_assert_(draw);
	render_ = (GLRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	useGL3_ = gl_extensions.GLES3 || gl_extensions.VersionGEThan(3, 3);
}

void DepalShaderCacheGLES::Init() {
	if (!gstate_c.Supports(GPU_SUPPORTS_32BIT_INT_FSHADER)) {
		// Use the floating point path, it just can't handle the math.
		useGL3_ = false;
	}
}

DepalShaderCacheGLES::~DepalShaderCacheGLES() {
	Clear();
}

void DepalShaderCacheGLES::DeviceRestore(Draw::DrawContext *draw) {
	render_ = (GLRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
}

bool DepalShaderCacheGLES::CreateVertexShader() {
	std::string src(useGL3_ ? depalVShader300 : depalVShader100);
	std::string prelude;
	if (gl_extensions.IsGLES) {
		prelude = useGL3_ ? "#version 300 es\n" : "#version 100\n";
	} else {
		// We need to add a corresponding #version.  Apple drivers fail without an exact match.
		prelude = StringFromFormat("#version %d\n", gl_extensions.GLSLVersion());
	}
	vertexShader_ = render_->CreateShader(GL_VERTEX_SHADER, prelude + src, "depal");
	return true;
}

GLRTexture *DepalShaderCacheGLES::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut) {
	u32 clutId = GetClutID(clutFormat, clutHash);

	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->texture;
	}

	Draw::DataFormat dstFmt = getClutDestFormat(clutFormat);
	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	DepalTexture *tex = new DepalTexture();
	tex->texture = render_->CreateTexture(GL_TEXTURE_2D);

	uint8_t *clutCopy = new uint8_t[1024];
	memcpy(clutCopy, rawClut, 1024);
	render_->TextureImage(tex->texture, 0, texturePixels, 1, dstFmt, clutCopy, GLRAllocType::NEW, false);

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
		DepalShader *depal = shader->second;
		// If compile failed previously, try to recover.
		if (depal->fragShader->failed || vertexShader_->failed)
			return nullptr;
		return shader->second;
	}

	if (!vertexShader_) {
		if (!CreateVertexShader()) {
			// The vertex shader failed, no need to bother trying the fragment.
			return nullptr;
		}
	}

	char *buffer = new char[2048];

	GenerateDepalShader(buffer, pixelFormat, useGL3_ ? GLSL_300 : GLSL_140);
	
	std::string src(buffer);
	GLRShader *fragShader = render_->CreateShader(GL_FRAGMENT_SHADER, src, "depal");

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
