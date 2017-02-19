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

#include "gfx_es2/gpu_features.h"
#include "gfx/gl_common.h"
#include "Globals.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureScalerGLES.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;
class FramebufferManagerGLES;
class DepalShaderCacheGLES;
class ShaderManagerGLES;
class DrawEngineGLES;

inline bool UseBGRA8888() {
	// TODO: Other platforms?  May depend on vendor which is faster?
#ifdef _WIN32
	return gl_extensions.EXT_bgra;
#endif
	return false;
}

class TextureCacheGLES : public TextureCacheCommon {
public:
	TextureCacheGLES(Draw::DrawContext *draw);
	~TextureCacheGLES();

	void Clear(bool delete_them);
	void StartFrame();

	void SetFramebufferManager(FramebufferManagerGLES *fbManager);
	void SetDepalShaderCache(DepalShaderCacheGLES *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerGLES *sm) {
		shaderManager_ = sm;
	}
	void SetDrawEngine(DrawEngineGLES *td) {
		drawEngine_ = td;
	}

	void ForgetLastTexture() override {
		lastBoundTexture = INVALID_TEX;
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	}
	void InvalidateLastTexture(TexCacheEntry *entry = nullptr) override {
		if (!entry || entry->textureName == lastBoundTexture) {
			lastBoundTexture = INVALID_TEX;
		}
	}

	u32 AllocTextureName();

	// Only used by Qt UI?
	bool DecodeTexture(u8 *output, const GPUgstate &state);

	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight);

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry) override;

private:
	void UpdateSamplingParams(TexCacheEntry &entry, bool force);
	void LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, bool replaceImages, int scaleFactor, GLenum dstFmt);
	GLenum GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	void *DecodeTextureLevelOld(GETextureFormat format, GEPaletteFormat clutformat, int level, GLenum dstFmt, int scaleFactor, int *bufw = 0);
	TexCacheEntry::Status CheckAlpha(const u32 *pixelData, GLenum dstFmt, int stride, int w, int h);
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;
	void ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) override;

	void BuildTexture(TexCacheEntry *const entry, bool replaceImages) override;

	std::vector<u32> nameCache_;

	TextureScalerGLES scaler;

	u32 lastBoundTexture;
	float maxAnisotropyLevel;

	FramebufferManagerGLES *framebufferManagerGL_;
	DepalShaderCacheGLES *depalShaderCache_;
	ShaderManagerGLES *shaderManager_;
	DrawEngineGLES *drawEngine_;

	enum { INVALID_TEX = -1 };
};

GLenum getClutDestFormat(GEPaletteFormat format);
