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

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Core/Reporting.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/DepalettizeShaderCommon.h"
#include "GPU/Common/DepalettizeCommon.h"

static const InputDef vsInputs[2] = {
	{ "vec2", "a_position", Draw::SEM_POSITION, },
	{ "vec2", "a_texcoord0", Draw::SEM_TEXCOORD0, },
};

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord0", Draw::SEM_TEXCOORD0, 0, "highp" },
};

static const SamplerDef samplers[2] = {
	{ "tex" },
	{ "pal" },
};

DepalShaderCache::DepalShaderCache(Draw::DrawContext *draw) : draw_(draw) { }

DepalShaderCache::~DepalShaderCache() {
	DeviceLost();
}

void DepalShaderCache::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
}

void DepalShaderCache::DeviceLost() {
	Clear();
}

bool DepalShaderCache::GenerateVertexShader(char *buffer, const ShaderLanguageDesc &lang) {
	ShaderWriter writer(buffer, lang, ShaderStage::Vertex, nullptr, 0);
	writer.BeginVSMain(vsInputs, Slice<UniformDef>::empty(), varyings);
	writer.C("  v_texcoord0 = a_texcoord0;\n");
	writer.C("  gl_Position = vec4(a_position, 0.0, 1.0);\n");
	if (strlen(lang.viewportYSign)) {
		writer.F("  gl_Position.y *= %s1.0;\n", lang.viewportYSign);
	}
	writer.EndVSMain(varyings);
	return true;
}

Draw::Texture *DepalShaderCache::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut) {
	u32 clutId = GetClutID(clutFormat, clutHash);

	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->texture;
	}

	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	DepalTexture *tex = new DepalTexture();

	Draw::TextureDesc desc{};
	desc.width = texturePixels;
	desc.height = 1;
	desc.depth = 1;
	desc.mipLevels = 1;
	desc.tag = "clut";
	desc.type = Draw::TextureType::LINEAR2D;  // TODO: Try LINEAR1D?
	desc.format = Draw::DataFormat::R8G8B8A8_UNORM;  // TODO: Also support an BGR format. We won't bother with the 16-bit formats here.

	uint8_t convTemp[2048]{};

	switch (clutFormat) {
	case GEPaletteFormat::GE_CMODE_32BIT_ABGR8888:
		desc.initData.push_back((const uint8_t *)rawClut);
		break;

	// TODO: The 16-bit CLUTs might be pre-reversed for OpenGL! :/
	case GEPaletteFormat::GE_CMODE_16BIT_BGR5650:
		ConvertRGBA5551ToRGBA8888((u32 *)convTemp, (const u16 *)rawClut, texturePixels);
		desc.initData.push_back(convTemp);
		break;
	case GEPaletteFormat::GE_CMODE_16BIT_ABGR5551:
		ConvertRGB565ToRGBA8888((u32 *)convTemp, (const u16 *)rawClut, texturePixels);
		desc.initData.push_back(convTemp);
		break;
	case GEPaletteFormat::GE_CMODE_16BIT_ABGR4444:
		ConvertRGBA4444ToRGBA8888((u32 *)convTemp, (const u16 *)rawClut, texturePixels);
		desc.initData.push_back(convTemp);
		break;
	}

	tex->texture = draw_->CreateTexture(desc);
	tex->lastFrame = gpuStats.numFlips;

	texCache_[clutId] = tex;
	return tex->texture;
}

void DepalShaderCache::Clear() {
	for (auto shader = cache_.begin(); shader != cache_.end(); ++shader) {
		shader->second->fragShader->Release();
		if (shader->second->pipeline) {
			shader->second->pipeline->Release();
		}
		delete shader->second;
	}
	cache_.clear();
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		tex->second->texture->Release();
		delete tex->second;
	}
	texCache_.clear();
	if (vertexShader_) {
		vertexShader_->Release();
		vertexShader_ = nullptr;
	}
	if (nearestSampler_) {
		nearestSampler_->Release();
		nearestSampler_ = nullptr;
	}
}

void DepalShaderCache::Decimate() {
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ) {
		if (tex->second->lastFrame + DEPAL_TEXTURE_OLD_AGE < gpuStats.numFlips) {
			tex->second->texture->Release();
			delete tex->second;
			texCache_.erase(tex++);
		} else {
			++tex;
		}
	}
}

Draw::SamplerState *DepalShaderCache::GetSampler() {
	if (!nearestSampler_) {
		Draw::SamplerStateDesc desc{};
		desc.wrapU = Draw::TextureAddressMode::CLAMP_TO_EDGE;
		desc.wrapV = Draw::TextureAddressMode::CLAMP_TO_EDGE;
		desc.wrapW = Draw::TextureAddressMode::CLAMP_TO_EDGE;
		nearestSampler_ = draw_->CreateSamplerState(desc);
	}
	return nearestSampler_;
}

DepalShader *DepalShaderCache::GetDepalettizeShader(uint32_t clutMode, GEBufferFormat pixelFormat) {
	using namespace Draw;

	u32 id = GenerateShaderID(clutMode, pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		DepalShader *depal = shader->second;
		return shader->second;
	}
	
	char *buffer = new char[4096];

	if (!vertexShader_) {
		if (!GenerateVertexShader(buffer, draw_->GetShaderLanguageDesc())) {
			// The vertex shader failed, no need to bother trying the fragment.
			delete[] buffer;
			return nullptr;
		}
		vertexShader_ = draw_->CreateShaderModule(ShaderStage::Vertex, draw_->GetShaderLanguageDesc().shaderLanguage, (const uint8_t *)buffer, strlen(buffer), "depal_vs");
	}

	// TODO: Replace with ShaderWriter-based implementation.
	GenerateDepalShader(buffer, pixelFormat, draw_->GetShaderLanguageDesc().shaderLanguage);
	
	std::string src(buffer);
	ShaderModule *fragShader = draw_->CreateShaderModule(ShaderStage::Fragment, draw_->GetShaderLanguageDesc().shaderLanguage, (const uint8_t *)buffer, strlen(buffer), "depal_fs");

	DepalShader *depal = new DepalShader();

	static const InputLayoutDesc desc = {
		{
			{ 16, false },
		},
		{
			{ 0, SEM_POSITION, DataFormat::R32G32_FLOAT, 0 },
			{ 0, SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 8 },
		},
	};
	InputLayout *inputLayout = draw_->CreateInputLayout(desc);
	BlendState *blendOff = draw_->CreateBlendState({ false, 0xF });
	DepthStencilStateDesc dsDesc{};
	DepthStencilState *noDepthStencil = draw_->CreateDepthStencilState(dsDesc);
	RasterState *rasterNoCull = draw_->CreateRasterState({});

	PipelineDesc depalPipelineDesc{
		Primitive::TRIANGLE_STRIP,   // Could have use a single triangle too (in which case we'd use LIST here) but want to be prepared to do subrectangles.
		{ vertexShader_, fragShader },
		inputLayout, noDepthStencil, blendOff, rasterNoCull, nullptr, samplers
	};
	
	Pipeline *pipeline = draw_->CreateGraphicsPipeline(depalPipelineDesc);

	inputLayout->Release();
	blendOff->Release();
	noDepthStencil->Release();
	rasterNoCull->Release();

	_assert_(pipeline);

	depal->pipeline = pipeline;
	depal->fragShader = fragShader;
	depal->code = buffer;
	cache_[id] = depal;

	delete[] buffer;
	return depal->pipeline ? depal : nullptr;
}

std::vector<std::string> DepalShaderCache::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<std::string> ids;
	for (auto &iter : cache_) {
		ids.push_back(StringFromFormat("%08x", iter.first));
	}
	return ids;
}

std::string DepalShaderCache::DebugGetShaderString(std::string idstr, DebugShaderType type, DebugShaderStringType stringType) {
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
