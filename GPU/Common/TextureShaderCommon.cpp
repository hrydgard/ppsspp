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
#include "GPU/Common/Draw2D.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

static const SamplerDef samplers[2] = {
	{ 0, "tex", SamplerFlags::ARRAY_ON_VULKAN },
	{ 1, "pal" },
};

TextureShaderCache::TextureShaderCache(Draw::DrawContext *draw, Draw2D *draw2D) : draw_(draw), draw2D_(draw2D) { }

TextureShaderCache::~TextureShaderCache() {
	DeviceLost();
}

void TextureShaderCache::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
}

void TextureShaderCache::DeviceLost() {
	Clear();
	draw_ = nullptr;
}

ClutTexture TextureShaderCache::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, const u32 *rawClut) {
	// Simplistic, but works well enough.
	u32 clutId = clutHash ^ (uint32_t)clutFormat;

	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return *oldtex->second;
	}

	int maxClutEntries = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	ClutTexture *tex = new ClutTexture();

	Draw::TextureDesc desc{};
	desc.width = 512;  // We always use 512-sized textures here for simplicity, though the most common is that only up to 256 entries are used.
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
		ConvertRGB565ToRGBA8888((u32 *)convTemp, (const u16 *)rawClut, maxClutEntries);
		desc.initData.push_back(convTemp);
		break;
	case GEPaletteFormat::GE_CMODE_16BIT_ABGR5551:
		ConvertRGBA5551ToRGBA8888((u32 *)convTemp, (const u16 *)rawClut, maxClutEntries);
		desc.initData.push_back(convTemp);
		break;
	case GEPaletteFormat::GE_CMODE_16BIT_ABGR4444:
		ConvertRGBA4444ToRGBA8888((u32 *)convTemp, (const u16 *)rawClut, maxClutEntries);
		desc.initData.push_back(convTemp);
		break;
	}


	for (int i = 0; i < 3; i++) {
		tex->rampLengths[i] = 0;
		tex->rampStarts[i] = 0;
	}
	// Quick check for how many continuously growing entries we have at the start.
	// Bilinearly filtering CLUTs only really makes sense for this kind of ramp.
	int i = 0;
	for (int j = 0; j < ClutTexture::MAX_RAMPS; j++) {
		tex->rampStarts[j] = i;
		int lastR = 0;
		int lastG = 0;
		int lastB = 0;
		int lastA = 0;
		for (; i < maxClutEntries; i++) {
			int r = desc.initData[0][i * 4];
			int g = desc.initData[0][i * 4 + 1];
			int b = desc.initData[0][i * 4 + 2];
			int a = desc.initData[0][i * 4 + 3];
			if (r < lastR || g < lastG || b < lastB || a < lastA) {
				lastR = r; lastG = g; lastB = b; lastA = a;
				break;
			} else {
				lastR = r;
				lastG = g;
				lastB = b;
				lastA = a;
			}
		}
		tex->rampLengths[j] = i - tex->rampStarts[j];
		if (i >= maxClutEntries) {
			break;
		}
	}

	tex->texture = draw_->CreateTexture(desc);
	tex->lastFrame = gpuStats.numFlips;

	texCache_[clutId] = tex;
	return *tex;
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
	if (nearestSampler_) {
		nearestSampler_->Release();
		nearestSampler_ = nullptr;
	}
	if (linearSampler_) {
		linearSampler_->Release();
		linearSampler_ = nullptr;
	}
}

Draw::SamplerState *TextureShaderCache::GetSampler(bool linearFilter) {
	if (linearFilter) {
		if (!linearSampler_) {
			Draw::SamplerStateDesc desc{};
			desc.magFilter = Draw::TextureFilter::LINEAR;
			desc.minFilter = Draw::TextureFilter::LINEAR;
			desc.wrapU = Draw::TextureAddressMode::CLAMP_TO_EDGE;
			desc.wrapV = Draw::TextureAddressMode::CLAMP_TO_EDGE;
			desc.wrapW = Draw::TextureAddressMode::CLAMP_TO_EDGE;
			linearSampler_ = draw_->CreateSamplerState(desc);
		}
		return linearSampler_;
	} else {
		if (!nearestSampler_) {
			Draw::SamplerStateDesc desc{};
			desc.wrapU = Draw::TextureAddressMode::CLAMP_TO_EDGE;
			desc.wrapV = Draw::TextureAddressMode::CLAMP_TO_EDGE;
			desc.wrapW = Draw::TextureAddressMode::CLAMP_TO_EDGE;
			nearestSampler_ = draw_->CreateSamplerState(desc);
		}
		return nearestSampler_;
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

Draw2DPipeline *TextureShaderCache::GetDepalettizeShader(uint32_t clutMode, GETextureFormat textureFormat, GEBufferFormat bufferFormat, bool smoothedDepal, u32 depthUpperBits) {
	using namespace Draw;

	// Generate an ID for depal shaders.
	u64 id = ((u64)depthUpperBits << 32) | (clutMode & 0xFFFFFF) | (textureFormat << 24) | (bufferFormat << 28);

	auto shader = depalCache_.find(id);
	if (shader != depalCache_.end()) {
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
	config.smoothedDepal = smoothedDepal;
	config.depthUpperBits = depthUpperBits;

	Draw2DPipeline *ts = draw2D_->Create2DPipeline([=](ShaderWriter &writer) -> Draw2DPipelineInfo {
		GenerateDepalFs(writer, config);
		return Draw2DPipelineInfo{
			"depal",
			config.bufferFormat == GE_FORMAT_DEPTH16 ? RASTER_DEPTH : RASTER_COLOR,
			RASTER_COLOR,
			samplers
		};
	});

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

std::string TextureShaderCache::DebugGetShaderString(const std::string &idstr, DebugShaderType type, DebugShaderStringType stringType) {
	uint32_t id = 0;
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
