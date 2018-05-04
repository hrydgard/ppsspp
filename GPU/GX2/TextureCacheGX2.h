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

#include <wiiu/gx2/sampler.h>
#include <wiiu/gx2/texture.h>
#include <wiiu/gx2/context.h>

#include "Common/CommonWindows.h"

#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
#include "GPU/GX2/TextureScalerGX2.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;

class FramebufferManagerGX2;
class DepalShaderCacheGX2;
class ShaderManagerGX2;

class SamplerCacheGX2 {
public:
	SamplerCacheGX2() {}
	~SamplerCacheGX2();
	GX2Sampler* GetOrCreateSampler(const SamplerCacheKey &key);

private:
	std::map<SamplerCacheKey, GX2Sampler*> cache_;
};

class TextureCacheGX2 : public TextureCacheCommon {
public:
	TextureCacheGX2(Draw::DrawContext *draw);
	~TextureCacheGX2();

	void StartFrame();

	void SetFramebufferManager(FramebufferManagerGX2 *fbManager);
	void SetDepalShaderCache(DepalShaderCacheGX2 *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerGX2 *sm) {
		shaderManager_ = sm;
	}

	void ForgetLastTexture() override;
	void InvalidateLastTexture() override;

	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) override;

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;

private:
	void LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, int scaleFactor, GX2SurfaceFormat dstFmt);
	GX2SurfaceFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	TexCacheEntry::TexStatus CheckAlpha(const u32_le *pixelData, u32 dstFmt, int stride, int w, int h);
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;

	void ApplyTextureFramebuffer(VirtualFramebuffer *framebuffer, GETextureFormat texFormat, FramebufferNotificationChannel channel) override;
	void BuildTexture(TexCacheEntry *const entry) override;

	GX2ContextState *context_;

	GX2Texture *&GX2Tex(TexCacheEntry *entry) {
		return (GX2Texture *&)entry->texturePtr;
	}

	TextureScalerGX2 scaler;

	SamplerCacheGX2 samplerCache_;

	GX2Texture *lastBoundTexture;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManagerGX2 *framebufferManagerGX2_;
	DepalShaderCacheGX2 *depalShaderCache_;
	ShaderManagerGX2 *shaderManager_;
};

GX2SurfaceFormat GetClutDestFormatGX2(GEPaletteFormat format);
