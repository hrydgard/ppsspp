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
#include <d3d9.h>

#include "base/logging.h"
#include "thin3d/thin3d.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Reporting.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/DepalettizeShaderDX9.h"
#include "GPU/Common/DepalettizeShaderCommon.h"
#include "gfx/d3d9_shader.h"

namespace DX9 {

static const int DEPAL_TEXTURE_OLD_AGE = 120;

#ifdef _WIN32
#define SHADERLOG
#endif

static const char *depalVShaderHLSL = R"(
struct VS_IN {
  float3 a_position : POSITION;
  float2 a_texcoord0 : TEXCOORD0;
};
struct VS_OUT {
  float4 Position : POSITION;
  float2 Texcoord : TEXCOORD0;
};
VS_OUT main(VS_IN input) {
  VS_OUT output;
  output.Texcoord = input.a_texcoord0;
  output.Position = float4(input.a_position, 1.0);
  return output;
}
)";

DepalShaderCacheDX9::DepalShaderCacheDX9(Draw::DrawContext *draw) : vertexShader_(nullptr) {
	device_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	std::string errorMessage;
	if (!DX9::CompileVertexShader(device_, depalVShaderHLSL, &vertexShader_, nullptr, errorMessage)) {
		ERROR_LOG(G3D, "error compling depal vshader: %s", errorMessage.c_str());
	}
}

DepalShaderCacheDX9::~DepalShaderCacheDX9() {
	Clear();
	if (vertexShader_) {
		vertexShader_->Release();
	}
}

LPDIRECT3DTEXTURE9 DepalShaderCacheDX9::GetClutTexture(GEPaletteFormat clutFormat, u32 clutHash, u32 *rawClut) {
	u32 clutId = GetClutID(clutFormat, clutHash);

	auto oldtex = texCache_.find(clutId);
	if (oldtex != texCache_.end()) {
		oldtex->second->lastFrame = gpuStats.numFlips;
		return oldtex->second->texture;
	}

	D3DFORMAT dstFmt = DX9::getClutDestFormat(clutFormat);
	int texturePixels = clutFormat == GE_CMODE_32BIT_ABGR8888 ? 256 : 512;

	DepalTextureDX9 *tex = new DepalTextureDX9();

	// Create texture
	D3DPOOL pool = D3DPOOL_MANAGED;
	int usage = 0;
	if (device_) {
		pool = D3DPOOL_DEFAULT;
		usage = D3DUSAGE_DYNAMIC;  // TODO: Switch to using a staging texture?
	}

	HRESULT hr = device_->CreateTexture(texturePixels, 1, 1, usage, dstFmt, pool, &tex->texture, NULL);
	if (FAILED(hr)) {
		ERROR_LOG(G3D, "Failed to create D3D texture for depal");
		delete tex;
		return nullptr;
	}

	D3DLOCKED_RECT rect;
	hr = tex->texture->LockRect(0, &rect, NULL, 0);
	if (FAILED(hr)) {
		ERROR_LOG(G3D, "Failed to lock D3D texture for depal");
		delete tex;
		return nullptr;
	}
	// Regardless of format, the CLUT should always be 1024 bytes.
	memcpy(rect.pBits, rawClut, 1024);
	tex->texture->UnlockRect(0);

	device_->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	device_->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device_->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	device_->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

	tex->lastFrame = gpuStats.numFlips;
	texCache_[clutId] = tex;
	return tex->texture;
}

void DepalShaderCacheDX9::Clear() {
	for (auto shader = cache_.begin(); shader != cache_.end(); ++shader) {
		shader->second->pixelShader->Release();
		delete shader->second;
	}
	cache_.clear();
	for (auto tex = texCache_.begin(); tex != texCache_.end(); ++tex) {
		tex->second->texture->Release();
		delete tex->second;
	}
	texCache_.clear();
}

void DepalShaderCacheDX9::Decimate() {
	for (auto tex = texCache_.begin(); tex != texCache_.end();) {
		if (tex->second->lastFrame + DEPAL_TEXTURE_OLD_AGE < gpuStats.numFlips) {
			tex->second->texture->Release();
			delete tex->second;
			texCache_.erase(tex++);
		} else {
			++tex;
		}
	}
}

LPDIRECT3DPIXELSHADER9 DepalShaderCacheDX9::GetDepalettizePixelShader(uint32_t clutMode, GEBufferFormat pixelFormat) {
	u32 id = GenerateShaderID(clutMode, pixelFormat);

	auto shader = cache_.find(id);
	if (shader != cache_.end()) {
		return shader->second->pixelShader;
	}

	char *buffer = new char[2048];

	GenerateDepalShader(buffer, pixelFormat, HLSL_DX9);

	LPDIRECT3DPIXELSHADER9 pshader;
	std::string errorMessage;
	if (!CompilePixelShader(device_, buffer, &pshader, NULL, errorMessage)) {
		ERROR_LOG(G3D, "Failed to compile depal pixel shader: %s\n\n%s", buffer, errorMessage.c_str());
		delete[] buffer;
		return nullptr;
	}

	DepalShaderDX9 *depal = new DepalShaderDX9();
	depal->pixelShader = pshader;
	depal->code = buffer;

	cache_[id] = depal;

	delete[] buffer;

	return depal->pixelShader;
}

std::vector<std::string> DepalShaderCacheDX9::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<std::string> ids;
	for (auto &iter : cache_) {
		ids.push_back(StringFromFormat("%08x", iter.first));
	}
	return ids;
}

std::string DepalShaderCacheDX9::DebugGetShaderString(std::string idstr, DebugShaderType type, DebugShaderStringType stringType) {
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

}  // namespace
