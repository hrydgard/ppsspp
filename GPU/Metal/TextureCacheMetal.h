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

// Objective-C++ only.

#include <string>
#include <unordered_map>
#include <vector>

#include "Common/GPU/Metal/MetalRenderManager.h"
#include "Common/File/Path.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureShaderCommon.h"

class FramebufferManagerMetal;
class ShaderManagerMetal;
class DrawEngineMetal;

class TextureCacheMetal : public TextureCacheCommon {
public:
	TextureCacheMetal(Draw::DrawContext *draw, Draw2D *draw2D);
	~TextureCacheMetal();

	void SetFramebufferManager(FramebufferManagerMetal *fbManager);
	void SetDrawEngine(DrawEngineMetal *drawEngine) { drawEngine_ = drawEngine; }

	void ForgetLastTexture() override {}
	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) override;

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;
	void NotifyConfigChanged() override;

	void *GetNativeTextureView(const TexCacheEntry *entry, bool flat) const override;

	// What the draw engine should bind at METAL_DRAW_BINDING_TEXTURE. Can be nil. Unretained (no ARC
	// traffic per draw), kept alive by boundTexture_ and boundSampler_.
	__unsafe_unretained id<MTLTexture> curTexture = nil;
	__unsafe_unretained id<MTLSamplerState> curSampler = nil;

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;
	void BindAsClutTexture(Draw::Texture *tex, bool smooth) override;
	void ApplySamplerByKey(const SamplerCacheKey &key) override;
	void BoundFramebufferTexture() override;
	void BuildTexture(TexCacheEntry *const entry) override;

private:
	void SetBoundTexture(id<MTLTexture> texture) {
		boundTexture_ = texture;
		curTexture = texture;
	}
	void SetBoundSampler(id<MTLSamplerState> sampler) {
		boundSampler_ = sampler;
		curSampler = sampler;
	}

	static id<MTLTexture> Tex(const TexCacheEntry *entry) {
		return (__bridge id<MTLTexture>)entry->texturePtr;
	}

	// Hardware texture scaling, with the same compute shaders as the Vulkan backend.
	void CompileScalingShader();
	void ClearScalingShaders();
	bool HasScalingShader() const { return singlePassCS_ != nil || !multipassCS_.empty(); }
	id<MTLComputePipelineState> CompileComputeShader(const std::string &source, const char *tag);
	// Records the dispatches that scale a decoded level (RGBA8, packed) into dst, a single level view.
	bool ScaleBufferToTexture(MetalPushAlloc src, id<MTLTexture> dst, int srcWidth, int srcHeight, int dstWidth, int dstHeight);

	MetalRenderManager *rm_ = nullptr;
	DrawEngineMetal *drawEngine_ = nullptr;

	std::unordered_map<uint64_t, id<MTLSamplerState>> samplerCache_;

	id<MTLTexture> boundTexture_;
	id<MTLSamplerState> boundSampler_;

	std::string textureShader_;
	id<MTLComputePipelineState> singlePassCS_;
	std::vector<id<MTLComputePipelineState>> multipassCS_;
	std::vector<TextureScaleScratchDesc> multipassScratchDescs_;
	std::vector<TextureScaleStageDesc> multipassStageDescs_;
};
