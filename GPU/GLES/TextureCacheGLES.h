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

#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureScalerGLES.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;
class FramebufferManagerGLES;
class DepalShaderCacheGLES;
class ShaderManagerGLES;
class DrawEngineGLES;
class GLRTexture;

class TextureCacheGLES : public TextureCacheCommon {
public:
	TextureCacheGLES(Draw::DrawContext *draw);
	~TextureCacheGLES();

	void Clear(bool delete_them) override;
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
		lastBoundTexture = nullptr;
	}
	void InvalidateLastTexture() override {
		lastBoundTexture = nullptr;
	}

	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) override;

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;

private:
	void ApplySamplingParams(const SamplerCacheKey &key);
	void LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int scaleFactor, Draw::DataFormat dstFmt);
	Draw::DataFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;

	static CheckAlphaResult CheckAlpha(const uint8_t *pixelData, Draw::DataFormat dstFmt, int w);
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;
	void ApplyTextureFramebuffer(VirtualFramebuffer *framebuffer, GETextureFormat texFormat, FramebufferNotificationChannel channel) override;

	void BuildTexture(TexCacheEntry *const entry) override;

	GLRenderManager *render_;

	TextureScalerGLES scaler;

	GLRTexture *lastBoundTexture = nullptr;

	FramebufferManagerGLES *framebufferManagerGL_;
	DepalShaderCacheGLES *depalShaderCache_;
	ShaderManagerGLES *shaderManager_;
	DrawEngineGLES *drawEngine_;

	GLRInputLayout *shadeInputLayout_ = nullptr;

	enum { INVALID_TEX = -1 };
};

Draw::DataFormat getClutDestFormat(GEPaletteFormat format);
