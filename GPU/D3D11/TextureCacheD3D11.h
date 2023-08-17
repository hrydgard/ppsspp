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
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;

class FramebufferManagerD3D11;
class TextureShaderCache;
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
	TextureCacheD3D11(Draw::DrawContext *draw, Draw2D *draw2D);
	~TextureCacheD3D11();

	void SetFramebufferManager(FramebufferManagerD3D11 *fbManager);

	void ForgetLastTexture() override;

	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) override;

	void DeviceLost() override { draw_ = nullptr; }
	void DeviceRestore(Draw::DrawContext *draw) override { draw_ = draw; }

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;
	void BindAsClutTexture(Draw::Texture *tex, bool smooth) override;
	void ApplySamplingParams(const SamplerCacheKey &key) override;
	void *GetNativeTextureView(const TexCacheEntry *entry) override;

private:
	DXGI_FORMAT GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;

	void BuildTexture(TexCacheEntry *const entry) override;

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;

	ID3D11Resource *&DxTex(const TexCacheEntry *entry) {
		return (ID3D11Resource *&)entry->texturePtr;
	}
	ID3D11ShaderResourceView *DxView(const TexCacheEntry *entry) {
		return (ID3D11ShaderResourceView *)entry->textureView;
	}

	SamplerCacheD3D11 samplerCache_;

	ID3D11ShaderResourceView *lastBoundTexture;
	ID3D11Buffer *depalConstants_;
};

DXGI_FORMAT GetClutDestFormatD3D11(GEPaletteFormat format);
