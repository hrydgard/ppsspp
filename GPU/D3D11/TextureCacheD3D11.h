// Copyright (c) 2017- PPSSPP Project.

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

#pragma once

#include "Common/CommonWindows.h"

#include <d3d11.h>

#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
#include "GPU/D3D11/TextureScalerD3D11.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;

class FramebufferManagerD3D11;
class DepalShaderCacheD3D11;
class ShaderManagerD3D11;

class SamplerCacheD3D11 {
public:
	SamplerCacheD3D11() {}
	~SamplerCacheD3D11();
	ID3D11SamplerState *GetOrCreateSampler(ID3D11Device *device, const SamplerCacheKey &key);

private:
	std::map<SamplerCacheKey, ID3D11SamplerState *> cache_;
};

class TextureCacheD3D11 : public TextureCacheCommon {
public:
	TextureCacheD3D11(Draw::DrawContext *draw);
	~TextureCacheD3D11();

	void StartFrame();

	void SetFramebufferManager(FramebufferManagerD3D11 *fbManager);
	void SetDepalShaderCache(DepalShaderCacheD3D11 *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerD3D11 *sm) {
		shaderManager_ = sm;
	}

	void ForgetLastTexture() override;
	void InvalidateLastTexture(TexCacheEntry *entry = nullptr) override;

	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key);
	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) override;

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;

private:
	void LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, int scaleFactor, DXGI_FORMAT dstFmt);
	DXGI_FORMAT GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	TexCacheEntry::TexStatus CheckAlpha(const u32 *pixelData, u32 dstFmt, int stride, int w, int h);
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;

	void ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) override;
	void BuildTexture(TexCacheEntry *const entry) override;

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;

	ID3D11Texture2D *&DxTex(TexCacheEntry *entry) {
		return (ID3D11Texture2D *&)entry->texturePtr;
	}
	ID3D11ShaderResourceView *DxView(TexCacheEntry *entry) {
		return (ID3D11ShaderResourceView *)entry->textureView;
	}

	TextureScalerD3D11 scaler;

	SamplerCacheD3D11 samplerCache_;

	ID3D11ShaderResourceView *lastBoundTexture;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManagerD3D11 *framebufferManagerD3D11_;
	DepalShaderCacheD3D11 *depalShaderCache_;
	ShaderManagerD3D11 *shaderManager_;
};

DXGI_FORMAT GetClutDestFormatD3D11(GEPaletteFormat format);
