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
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

static const SamplerDef samplers[2] = {
	{ "tex" },
	{ "pal" },
};

TextureShaderCache::TextureShaderCache(Draw::DrawContext *draw) : draw_(draw) { }

TextureShaderCache::~TextureShaderCache() {
	DeviceLost();
}

void TextureShaderCache::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
}

void TextureShaderCache::DeviceLost() {
	Clear();
}

Draw::Texture *TextureShaderCache::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut) {
	// Simplistic, but works well enough.
	u32 clutId = clutHash ^ (uint32_t)clutFormat;

	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->texture;
	}

	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	ClutTexture *tex = new ClutTexture();

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
	case GEPaletteFormat::GE_CMODE_16BIT_BGR5650:
		ConvertRGB565ToRGBA8888((u32 *)convTemp, (const u16 *)rawClut, texturePixels);
		desc.initData.push_back(convTemp);
		break;
	case GEPaletteFormat::GE_CMODE_16BIT_ABGR5551:
		ConvertRGBA5551ToRGBA8888((u32 *)convTemp, (const u16 *)rawClut, texturePixels);
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

void TextureShaderCache::Clear() {
	for (auto shader = depalCache_.begin(); shader != depalCache_.end(); ++shader) {
		if (shader->second->pipeline) {
			shader->second->pipeline->Release();
		}
		delete shader->second;
	}
	depalCache_.clear();
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

void TextureShaderCache::Decimate() {
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

Draw::SamplerState *TextureShaderCache::GetSampler() {
	if (!nearestSampler_) {
		Draw::SamplerStateDesc desc{};
		desc.wrapU = Draw::TextureAddressMode::CLAMP_TO_EDGE;
		desc.wrapV = Draw::TextureAddressMode::CLAMP_TO_EDGE;
		desc.wrapW = Draw::TextureAddressMode::CLAMP_TO_EDGE;
		nearestSampler_ = draw_->CreateSamplerState(desc);
	}
	return nearestSampler_;
}

TextureShader *TextureShaderCache::CreateShader(const char *fs) {
	using namespace Draw;
	if (!vertexShader_) {
		char *buffer = new char[4096];
		GenerateVs(buffer, draw_->GetShaderLanguageDesc());
		vertexShader_ = draw_->CreateShaderModule(ShaderStage::Vertex, draw_->GetShaderLanguageDesc().shaderLanguage, (const uint8_t *)buffer, strlen(buffer), "depal_vs");
		delete[] buffer;
	}

	ShaderModule *fragShader = draw_->CreateShaderModule(ShaderStage::Fragment, draw_->GetShaderLanguageDesc().shaderLanguage, (const uint8_t *)fs, strlen(fs), "depal_fs");

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
	fragShader->Release();

	_assert_(pipeline);

	TextureShader *depal = new TextureShader();
	depal->pipeline = pipeline;
	depal->code = fs;  // to std::string
	return depal;
}

TextureShader *TextureShaderCache::GetDepalettizeShader(uint32_t clutMode, GETextureFormat textureFormat, GEBufferFormat bufferFormat) {
	using namespace Draw;

	// Generate an ID for depal shaders.
	u32 id = (clutMode & 0xFFFFFF) | (textureFormat << 24) | (bufferFormat << 28);

	auto shader = depalCache_.find(id);
	if (shader != depalCache_.end()) {
		TextureShader *depal = shader->second;
		return shader->second;
	}

	// TODO: Parse these out of clutMode some nice way, to become a bit more stateless.
	DepalConfig config;
	config.clutFormat = gstate.getClutPaletteFormat();
	config.startPos = gstate.getClutIndexStartPos();
	config.shift = gstate.getClutIndexShift();
	config.mask = gstate.getClutIndexMask();
	config.bufferFormat = bufferFormat;
	config.textureFormat = textureFormat;

	char *buffer = new char[4096];
	GenerateDepalFs(buffer, config, draw_->GetShaderLanguageDesc());
	TextureShader *ts = CreateShader(buffer);
	delete[] buffer;

	depalCache_[id] = ts;

	return ts->pipeline ? ts : nullptr;
}

std::vector<std::string> TextureShaderCache::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<std::string> ids;
	for (auto &iter : depalCache_) {
		ids.push_back(StringFromFormat("%08x", iter.first));
	}
	return ids;
}

std::string TextureShaderCache::DebugGetShaderString(std::string idstr, DebugShaderType type, DebugShaderStringType stringType) {
	uint32_t id;
	sscanf(idstr.c_str(), "%08x", &id);
	auto iter = depalCache_.find(id);
	if (iter == depalCache_.end())
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

void TextureShaderCache::ApplyShader(TextureShader *shader, float bufferW, float bufferH, int renderW, int renderH, const KnownVertexBounds &bounds, u32 uoff, u32 voff) {
	Draw2DVertex verts[4] = {
		{-1, -1, 0, 0 },
		{ 1, -1, 1, 0 },
		{-1,  1, 0, 1 },
		{ 1,  1, 1, 1 },
	};

	// If min is not < max, then we don't have values (wasn't set during decode.)
	if (bounds.minV < bounds.maxV) {
		const float invWidth = 1.0f / bufferW;
		const float invHeight = 1.0f / bufferH;
		// Inverse of half = double.
		const float invHalfWidth = invWidth * 2.0f;
		const float invHalfHeight = invHeight * 2.0f;

		const int u1 = bounds.minU + uoff;
		const int v1 = bounds.minV + voff;
		const int u2 = bounds.maxU + uoff;
		const int v2 = bounds.maxV + voff;

		const float left = u1 * invHalfWidth - 1.0f;
		const float right = u2 * invHalfWidth - 1.0f;
		const float top = v1 * invHalfHeight - 1.0f;
		const float bottom = v2 * invHalfHeight - 1.0f;

		const float uvleft = u1 * invWidth;
		const float uvright = u2 * invWidth;
		const float uvtop = v1 * invHeight;
		const float uvbottom = v2 * invHeight;

		// Points are: BL, BR, TR, TL.
		verts[0] = Draw2DVertex{ left, bottom, uvleft, uvbottom };
		verts[1] = Draw2DVertex{ right, bottom, uvright, uvbottom };
		verts[2] = Draw2DVertex{ left, top, uvleft, uvtop };
		verts[3] = Draw2DVertex{ right, top, uvright, uvtop };

		// We need to reapply the texture next time since we cropped UV.
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	}

	Draw::Viewport vp{ 0.0f, 0.0f, (float)renderW, (float)renderH, 0.0f, 1.0f };
	draw_->BindPipeline(shader->pipeline);
	draw_->SetViewports(1, &vp);
	draw_->SetScissorRect(0, 0, renderW, renderH);
	draw_->DrawUP((const uint8_t *)verts, 4);
}
