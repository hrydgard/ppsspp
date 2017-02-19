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

#include <d3d11.h>

#include "../Globals.h"
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

	void SetTexture(bool force = false);

	void StartFrame();

	void SetFramebufferManager(FramebufferManagerD3D11 *fbManager);
	void SetDepalShaderCache(DepalShaderCacheD3D11 *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerD3D11 *sm) {
		shaderManager_ = sm;
	}

	// Only used by Qt UI?
	bool DecodeTexture(u8 *output, const GPUgstate &state);

	void ForgetLastTexture() override;

	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key);

	void ApplyTexture();

protected:
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry) override;

private:
	void Decimate();  // Run this once per frame to get rid of old textures.
	void UpdateSamplingParams(TexCacheEntry &entry, SamplerCacheKey &key);
	void LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, bool replaceImages, int scaleFactor, u32 dstFmt);
	DXGI_FORMAT GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	TexCacheEntry::Status CheckAlpha(const u32 *pixelData, u32 dstFmt, int stride, int w, int h);
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple);
	void ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer);

	bool CheckFullHash(TexCacheEntry *const entry, bool &doDelete);
	bool HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete);
	void BuildTexture(TexCacheEntry *const entry, bool replaceImages);

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;

	ID3D11Texture2D *DxTex(TexCacheEntry *entry) {
		return (ID3D11Texture2D *)entry->texturePtr;
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

DXGI_FORMAT getClutDestFormatD3D11(GEPaletteFormat format);
