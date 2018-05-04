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
#include <wiiu/gx2/shaders.h>

#include "Common/Log.h"
#include "Common/ColorConv.h"
#include "Common/StringUtils.h"
#include "Core/Reporting.h"
#include "GPU/GX2/TextureCacheGX2.h"
#include "GPU/GX2/DepalettizeShaderGX2.h"
#include "GPU/GX2/GX2Util.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

DepalShaderCacheGX2::DepalShaderCacheGX2(Draw::DrawContext *draw) {
	static const GX2AttribStream depalAttribStream[] = {
		{ 0, 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_COMP_SEL(_x, _y, _z, _1), GX2_ENDIAN_SWAP_DEFAULT },
		{ 1, 0, 12, GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_COMP_SEL(_x, _y, _0, _0), GX2_ENDIAN_SWAP_DEFAULT },
	};
	fetchShader_.size = GX2CalcFetchShaderSizeEx(countof(depalAttribStream), GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
	fetchShader_.program = (u8 *)MEM2_alloc(fetchShader_.size, GX2_SHADER_ALIGNMENT);
	GX2InitFetchShaderEx(&fetchShader_, fetchShader_.program, countof(depalAttribStream), depalAttribStream, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, fetchShader_.program, fetchShader_.size);

	context_ = (GX2ContextState *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
}

DepalShaderCacheGX2::~DepalShaderCacheGX2() {
	Clear();
	MEM2_free(fetchShader_.program);
}

GX2Texture *DepalShaderCacheGX2::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32_le *rawClut, bool expandTo32bit) {
	const u32 clutId = GetClutID(clutFormat, clutHash);

	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second;
	}

	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;
	int bpp = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	GX2SurfaceFormat dstFmt;
	u32_le *expanded = nullptr;
	if (expandTo32bit && clutFormat != GE_CMODE_32BIT_ABGR8888) {
		expanded = new u32_le[texturePixels];
		switch (clutFormat) {
		case GE_CMODE_16BIT_ABGR4444:
			ConvertRGBA4444ToRGBA8888(expanded, (const u16_le *)rawClut, texturePixels);
			break;
		case GE_CMODE_16BIT_ABGR5551:
			ConvertRGBA5551ToRGBA8888(expanded, (const u16_le *)rawClut, texturePixels);
			break;
		case GE_CMODE_16BIT_BGR5650:
			ConvertRGB565ToRGBA8888(expanded, (const u16_le *)rawClut, texturePixels);
			break;
		}
		rawClut = expanded;
		dstFmt = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
		bpp = 4;
	}
	else {
		dstFmt = GetClutDestFormatGX2(clutFormat);
	}

	DepalTextureGX2 *tex = new DepalTextureGX2();

	tex->surface.width = texturePixels;
	tex->surface.height = 1;
	tex->surface.depth = 1;
	tex->surface.dim = GX2_SURFACE_DIM_TEXTURE_1D;
	tex->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	tex->surface.use = GX2_SURFACE_USE_TEXTURE;
	tex->viewNumSlices = 1;

	tex->surface.format = dstFmt;
	tex->compMap = GX2_COMP_SEL(_a, _r, _g, _b);

	GX2CalcSurfaceSizeAndAlignment(&tex->surface);
	GX2InitTextureRegs(tex);

	tex->surface.image = MEM2_alloc(tex->surface.imageSize, tex->surface.alignment);
	_assert_(tex->surface.image);

	if (bpp == 2) {
		const u16_le *src = (const u16_le *)rawClut;
		u16_le *dst = (u16_le *)tex->surface.image;
		while (src < (u16_le *)rawClut + texturePixels) {
			*dst++ = (*src++);
		}
	} else {
		const u32_le *src = (const u32_le *)rawClut;
		u32_le *dst = (u32_le *)tex->surface.image;
		while (src < (u32_le *)rawClut + texturePixels) {
			*dst++ = (*src++);
		}
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex->surface.image, tex->surface.imageSize);

	tex->lastFrame = gpuStats.numFlips;
	texCache_[clutId] = tex;

	if (expandTo32bit) {
		delete[] expanded;
	}
	return tex;
}

void DepalShaderCacheGX2::Clear() {
	for (auto shader = cache_.begin(); shader != cache_.end(); ++shader) {
		delete shader->second;
	}
	cache_.clear();

	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		delete tex->second;
	}
	texCache_.clear();
}

void DepalShaderCacheGX2::Decimate() {
	for (auto tex = texCache_.begin(); tex != texCache_.end();) {
		if (tex->second->lastFrame + DEPAL_TEXTURE_OLD_AGE < gpuStats.numFlips) {
			delete tex->second;
			texCache_.erase(tex++);
		} else {
			++tex;
		}
	}
}

extern "C" GX2PixelShader GX2_fsCol;
DepalShaderCacheGX2::DepalShaderGX2::DepalShaderGX2(GEBufferFormat pixelFormat) : GX2PixelShader(GX2_fsCol) {
	// TODO;
	program = (u8*)MEM2_alloc(size, GX2_SHADER_ALIGNMENT);
	memcpy(program, GX2_fsCol.program, size);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, program, size);
}

GX2PixelShader *DepalShaderCacheGX2::GetDepalettizePixelShader(u32 clutMode, GEBufferFormat pixelFormat) {
	// TODO:
	return nullptr;
	u32 id = GenerateShaderID(clutMode, pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second;
	}

	DepalShaderGX2 *depal = new DepalShaderGX2(pixelFormat);
	cache_[id] = depal;

	return depal;
}

std::vector<std::string> DepalShaderCacheGX2::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<std::string> ids;
	for (auto &iter : cache_) {
		ids.push_back(StringFromFormat("%08x", iter.first));
	}
	return ids;
}

std::string DepalShaderCacheGX2::DebugGetShaderString(std::string idstr, DebugShaderType type, DebugShaderStringType stringType) {
	u32 id;
	sscanf(idstr.c_str(), "%08x", &id);
	auto iter = cache_.find(id);
	if (iter == cache_.end())
		return "";
	switch (stringType) {
	case SHADER_STRING_SHORT_DESC:
		return idstr;
	case SHADER_STRING_SOURCE_CODE:
		// TODO: disassemble shader
		return "N/A";
	default:
		return "";
	}
}
