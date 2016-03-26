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

#include "Globals.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/FBO.h"
#include "GPU/GLES/TextureScaler.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;
class FramebufferManager;
class DepalShaderCache;
class ShaderManager;
class TransformDrawEngine;

inline bool UseBGRA8888() {
	// TODO: Other platforms?  May depend on vendor which is faster?
#ifdef _WIN32
	return gl_extensions.EXT_bgra;
#endif
	return false;
}

class TextureCache : public TextureCacheCommon {
public:
	TextureCache();
	~TextureCache();

	void SetTexture(bool force = false);
	virtual bool SetOffsetTexture(u32 offset) override;

	void Clear(bool delete_them);
	void StartFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ClearNextFrame();

	void SetFramebufferManager(FramebufferManager *fbManager) {
		framebufferManager_ = fbManager;
	}
	void SetDepalShaderCache(DepalShaderCache *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManager *sm) {
		shaderManager_ = sm;
	}
	void SetTransformDrawEngine(TransformDrawEngine *td) {
		transformDraw_ = td;
	}

	size_t NumLoadedTextures() const {
		return cache.size();
	}

	void ForgetLastTexture() {
		lastBoundTexture = -1;
		gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	}

	u32 AllocTextureName();

	// Only used by Qt UI?
	bool DecodeTexture(u8 *output, const GPUgstate &state);

	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight);

	void ApplyTexture();

protected:
	void DownloadFramebufferForClut(u32 clutAddr, u32 bytes) override;

private:
	void Decimate();  // Run this once per frame to get rid of old textures.
	void DeleteTexture(TexCache::iterator it);
	void *ReadIndexedTex(int level, const u8 *texptr, int bytesPerIndex, GLuint dstFmt, int bufw);
	void UpdateSamplingParams(TexCacheEntry &entry, bool force);
	void LoadTextureLevel(TexCacheEntry &entry, int level, bool replaceImages, int scaleFactor, GLenum dstFmt);
	GLenum GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	void *DecodeTextureLevel(GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, GLenum dstFmt, int scaleFactor, int *bufw = 0);
	TexCacheEntry::Status CheckAlpha(const u32 *pixelData, GLenum dstFmt, int stride, int w, int h);
	template <typename T>
	const T *GetCurrentClut();
	u32 GetCurrentClutHash();
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple);
	bool AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset = 0) override;
	void SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer);
	void ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer);

	std::vector<u32> nameCache_;
	TexCache secondCache;
	u32 secondCacheSizeEstimate_;

	bool clearCacheNextFrame_;
	bool lowMemoryMode_;

	TextureScalerGL scaler;

	u32 *clutBuf_;
	u32 clutHash_;
	// True if the clut is just alpha values in the same order (RGBA4444-bit only.)
	bool clutAlphaLinear_;
	u16 clutAlphaLinearColor_;

	u32 lastBoundTexture;
	float maxAnisotropyLevel;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManager *framebufferManager_;
	DepalShaderCache *depalShaderCache_;
	ShaderManager *shaderManager_;
	TransformDrawEngine *transformDraw_;
};

GLenum getClutDestFormat(GEPaletteFormat format);
