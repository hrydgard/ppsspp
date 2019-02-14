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
#include <d3d11.h>

#include "base/basictypes.h"
#include "base/logging.h"
#include "Common/Log.h"
#include "Common/ColorConv.h"
#include "Common/StringUtils.h"
#include "Core/Reporting.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DepalettizeShaderD3D11.h"
#include "GPU/D3D11/D3D11Util.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

#ifdef _WIN32
#define SHADERLOG
#endif

static const char *depalVShaderHLSL = R"(
struct VS_IN {
  float3 a_position : POSITION;
  float2 a_texcoord0 : TEXCOORD0;
};
struct VS_OUT {
  float2 Texcoord : TEXCOORD0;
  float4 Position : SV_Position;
};
VS_OUT main(VS_IN input) {
  VS_OUT output;
  output.Texcoord = input.a_texcoord0;
  output.Position = float4(input.a_position, 1.0);
  return output;
}
)";

static const D3D11_INPUT_ELEMENT_DESC g_DepalVertexElements[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, },
};

DepalShaderCacheD3D11::DepalShaderCacheD3D11(Draw::DrawContext *draw) {
	std::string errorMessage;
	std::vector<uint8_t> vsByteCode;
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	featureLevel_ = (D3D_FEATURE_LEVEL)draw->GetNativeObject(Draw::NativeObject::FEATURE_LEVEL);
	vertexShader_ = CreateVertexShaderD3D11(device_, depalVShaderHLSL, strlen(depalVShaderHLSL), &vsByteCode, featureLevel_);
	ASSERT_SUCCESS(device_->CreateInputLayout(g_DepalVertexElements, ARRAY_SIZE(g_DepalVertexElements), vsByteCode.data(), vsByteCode.size(), &inputLayout_));
}

DepalShaderCacheD3D11::~DepalShaderCacheD3D11() {
	Clear();
	vertexShader_->Release();
	inputLayout_->Release();
}

ID3D11ShaderResourceView *DepalShaderCacheD3D11::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut, bool expandTo32bit) {
	const u32 clutId = GetClutID(clutFormat, clutHash);

	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->view;
	}

	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;
	int bpp = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	DXGI_FORMAT dstFmt;
	uint32_t *expanded = nullptr;
	if (expandTo32bit && clutFormat != GE_CMODE_32BIT_ABGR8888) {
		expanded = new uint32_t[texturePixels];
		switch (clutFormat) {
		case GE_CMODE_16BIT_ABGR4444:
			ConvertRGBA4444ToRGBA8888(expanded, (const uint16_t *)rawClut, texturePixels);
			break;
		case GE_CMODE_16BIT_ABGR5551:
			ConvertRGBA5551ToRGBA8888(expanded, (const uint16_t *)rawClut, texturePixels);
			break;
		case GE_CMODE_16BIT_BGR5650:
			ConvertRGBA565ToRGBA8888(expanded, (const uint16_t *)rawClut, texturePixels);
			break;
		}
		rawClut = expanded;
		dstFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
		bpp = 4;
	}
	else {
		dstFmt = GetClutDestFormatD3D11(clutFormat);
	}

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = texturePixels;
	desc.Height = 1;
	desc.ArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = dstFmt;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA data{};
	data.pSysMem = rawClut;
	// Regardless of format, the CLUT should always be 1024 bytes.
	data.SysMemPitch = texturePixels * bpp;

	DepalTextureD3D11 *tex = new DepalTextureD3D11();
	// TODO: Look into 1D textures
	ASSERT_SUCCESS(device_->CreateTexture2D(&desc, &data, &tex->texture));
	ASSERT_SUCCESS(device_->CreateShaderResourceView(tex->texture, nullptr, &tex->view));
	tex->lastFrame = gpuStats.numFlips;
	texCache_[clutId] = tex;

	if (expandTo32bit) {
		delete[] expanded;
	}
	return tex->view;
}

void DepalShaderCacheD3D11::Clear() {
	for (auto shader = cache_.begin(); shader != cache_.end(); ++shader) {
		shader->second->pixelShader->Release();
		delete shader->second;
	}
	cache_.clear();

	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		delete tex->second;
	}
	texCache_.clear();
}

void DepalShaderCacheD3D11::Decimate() {
	for (auto tex = texCache_.begin(); tex != texCache_.end();) {
		if (tex->second->lastFrame + DEPAL_TEXTURE_OLD_AGE < gpuStats.numFlips) {
			delete tex->second;
			texCache_.erase(tex++);
		} else {
			++tex;
		}
	}
}

ID3D11PixelShader *DepalShaderCacheD3D11::GetDepalettizePixelShader(uint32_t clutMode, GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(clutMode, pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second->pixelShader;
	}

	char *buffer = new char[2048];

	GenerateDepalShader(buffer, pixelFormat, HLSL_D3D11);

	ID3D11PixelShader *pshader = CreatePixelShaderD3D11(device_, buffer, strlen(buffer), featureLevel_);

	if (!pshader) {
		ERROR_LOG(G3D, "Failed to compile depal pixel shader");
		delete[] buffer;
		return nullptr;
	}

	DepalShaderD3D11 *depal = new DepalShaderD3D11();
	depal->pixelShader = pshader;
	depal->code = buffer;

	cache_[id] = depal;


	delete[] buffer;

	return depal->pixelShader;
}

std::vector<std::string> DepalShaderCacheD3D11::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<std::string> ids;
	for (auto &iter : cache_) {
		ids.push_back(StringFromFormat("%08x", iter.first));
	}
	return ids;
}

std::string DepalShaderCacheD3D11::DebugGetShaderString(std::string idstr, DebugShaderType type, DebugShaderStringType stringType) {
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
