// Copyright (c) 2012- PPSSPP Project.

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

#include <map>

#include <d3d9.h>

#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
#include "GPU/Directx9/TextureScalerDX9.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;

namespace DX9 {

class FramebufferManagerDX9;
class DepalShaderCacheDX9;
class ShaderManagerDX9;

class TextureCacheDX9 : public TextureCacheCommon {
public:
	TextureCacheDX9(Draw::DrawContext *draw);
	~TextureCacheDX9();

	void StartFrame();

	void SetFramebufferManager(FramebufferManagerDX9 *fbManager);
	void SetDepalShaderCache(DepalShaderCacheDX9 *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerDX9 *sm) {
		shaderManager_ = sm;
	}

	void ForgetLastTexture() override;
	void InvalidateLastTexture(TexCacheEntry *entry = nullptr) override;

	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight);

	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) override;

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;

private:
	void UpdateSamplingParams(TexCacheEntry &entry, bool force);
	void LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, int scaleFactor, u32 dstFmt);
	D3DFORMAT GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	TexCacheEntry::TexStatus CheckAlpha(const u32 *pixelData, u32 dstFmt, int stride, int w, int h);
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;

	void ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) override;
	void BuildTexture(TexCacheEntry *const entry) override;

	LPDIRECT3DTEXTURE9 &DxTex(TexCacheEntry *entry) {
		return *(LPDIRECT3DTEXTURE9 *)&entry->texturePtr;
	}

	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;

	TextureScalerDX9 scaler;

	LPDIRECT3DVERTEXDECLARATION9 pFramebufferVertexDecl;

	LPDIRECT3DTEXTURE9 lastBoundTexture;
	float maxAnisotropyLevel;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManagerDX9 *framebufferManagerDX9_;
	DepalShaderCacheDX9 *depalShaderCache_;
	ShaderManagerDX9 *shaderManager_;
};

D3DFORMAT getClutDestFormat(GEPaletteFormat format);

};
