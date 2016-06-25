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

#include "../Globals.h"
#include "helper/global.h"
#include "helper/dx_fbo.h"
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
	TextureCacheDX9();
	~TextureCacheDX9();

	void SetTexture(bool force = false);
	virtual bool SetOffsetTexture(u32 offset) override;

	void Clear(bool delete_them);
	void StartFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ClearNextFrame();

	void SetFramebufferManager(FramebufferManagerDX9 *fbManager) {
		framebufferManager_ = fbManager;
	}
	void SetDepalShaderCache(DepalShaderCacheDX9 *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerDX9 *sm) {
		shaderManager_ = sm;
	}

	size_t NumLoadedTextures() const {
		return cache.size();
	}

	// Only used by Qt UI?
	bool DecodeTexture(u8 *output, const GPUgstate &state);

	void ForgetLastTexture();

	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight);

	void ApplyTexture();

protected:
	void DownloadFramebufferForClut(u32 clutAddr, u32 bytes) override;

private:
	void Decimate();  // Run this once per frame to get rid of old textures.
	void DeleteTexture(TexCache::iterator it);
	void UpdateSamplingParams(TexCacheEntry &entry, bool force);
	void LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, bool replaceImages, int scaleFactor, u32 dstFmt);
	D3DFORMAT GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	TexCacheEntry::Status CheckAlpha(const u32 *pixelData, u32 dstFmt, int stride, int w, int h);
	u32 GetCurrentClutHash();
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple);
	bool AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset = 0) override;
	void SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer);
	void ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer);

	bool CheckFullHash(TexCacheEntry *const entry, bool &doDelete);
	bool HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete);
	void BuildTexture(TexCacheEntry *const entry, bool replaceImages);

	LPDIRECT3DTEXTURE9 &DxTex(TexCacheEntry *entry) {
		return *(LPDIRECT3DTEXTURE9 *)&entry->texturePtr;
	}
	void ReleaseTexture(TexCacheEntry *entry) {
		LPDIRECT3DTEXTURE9 &texture = DxTex(entry);
		if (texture) {
			texture->Release();
			texture = nullptr;
		}
	}

	TexCache secondCache;
	u32 secondCacheSizeEstimate_;

	bool clearCacheNextFrame_;
	bool lowMemoryMode_;
	TextureScalerDX9 scaler;

	u32 clutHash_;

	LPDIRECT3DTEXTURE9 lastBoundTexture;
	float maxAnisotropyLevel;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManagerDX9 *framebufferManager_;
	DepalShaderCacheDX9 *depalShaderCache_;
	ShaderManagerDX9 *shaderManager_;

	const char *nextChangeReason_;
	bool nextNeedsRehash_;
	bool nextNeedsChange_;
	bool nextNeedsRebuild_;
};

D3DFORMAT getClutDestFormat(GEPaletteFormat format);

};
