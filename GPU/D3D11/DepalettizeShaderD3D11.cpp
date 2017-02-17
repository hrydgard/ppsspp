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
#include "Core/Reporting.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DepalettizeShaderD3D11.h"
#include "GPU/D3D11/D3D11Util.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

static const int DEPAL_TEXTURE_OLD_AGE = 120;

#ifdef _WIN32
#define SHADERLOG
#endif

static const char *depalVShaderHLSL =
"struct VS_IN {\n"
"  float3 a_position : POSITION;\n"
"  float2 a_texcoord0 : TEXCOORD0;\n"
"};\n"
"struct VS_OUT {\n"
"  float2 Texcoord : TEXCOORD0;\n"
"  float4 Position : SV_Position;\n"
"};\n"
"VS_OUT main(VS_IN input) {\n"
"  VS_OUT output;\n"
"  output.Texcoord = input.a_texcoord0;\n"
"  output.Position = float4(input.a_position, 1.0);\n"
"  return output;\n"
"}\n";

static const D3D11_INPUT_ELEMENT_DESC g_DepalVertexElements[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, },
};

DepalShaderCacheD3D11::DepalShaderCacheD3D11(ID3D11Device *device, ID3D11DeviceContext *context)
		: device_(device), context_(context) {
	std::string errorMessage;
	std::vector<uint8_t> vsByteCode;
	vertexShader_ = CreateVertexShaderD3D11(device, depalVShaderHLSL, strlen(depalVShaderHLSL), &vsByteCode);
	device_->CreateInputLayout(g_DepalVertexElements, ARRAY_SIZE(g_DepalVertexElements), vsByteCode.data(), vsByteCode.size(), &inputLayout_);
}

DepalShaderCacheD3D11::~DepalShaderCacheD3D11() {
	Clear();
	vertexShader_->Release();
	inputLayout_->Release();
}

u32 DepalShaderCacheD3D11::GenerateShaderID(GEPaletteFormat clutFormat, GEBufferFormat pixelFormat) {
	return (clutFormat & 0xFFFFFF) | (pixelFormat << 24);
}

ID3D11ShaderResourceView *DepalShaderCacheD3D11::GetClutTexture(GEPaletteFormat clutFormat, const u32 clutID, u32 *rawClut) {
	const u32 realClutID = clutID ^ clutFormat;

	auto oldtex = texCache_.find(realClutID);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->view;
	}

	DXGI_FORMAT dstFmt = getClutDestFormatD3D11(clutFormat);
	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	DepalTextureD3D11 *tex = new DepalTextureD3D11();
	// TODO: Look into 1D textures

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
	data.SysMemPitch = 1024;
	HRESULT hr = device_->CreateTexture2D(&desc, &data, &tex->texture);
	if (FAILED(hr)) {
		ERROR_LOG(G3D, "Failed to create D3D texture for depal");
		delete tex;
		return nullptr;
	}
	hr = device_->CreateShaderResourceView(tex->texture, nullptr, &tex->view);
	if (FAILED(hr)) {
		// ...
	}
	tex->lastFrame = gpuStats.numFlips;
	texCache_[realClutID] = tex;
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

ID3D11PixelShader *DepalShaderCacheD3D11::GetDepalettizePixelShader(GEPaletteFormat clutFormat, GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(clutFormat, pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second->pixelShader;
	}

	char *buffer = new char[2048];

	GenerateDepalShader(buffer, pixelFormat, HLSL_D3D11);

	ID3D11PixelShader *pshader = CreatePixelShaderD3D11(device_, buffer, strlen(buffer));

	if (!pshader) {
		ERROR_LOG(G3D, "Failed to compile depal pixel shader");
		delete[] buffer;
		return nullptr;
	}

	DepalShaderD3D11 *depal = new DepalShaderD3D11();
	depal->pixelShader = pshader;

	cache_[id] = depal;

	delete[] buffer;

	return depal->pixelShader;
}