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

#include <map>
#include <algorithm>
#include <cassert>
#include <cstring>

#include <d3d11.h>

#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/D3D11/FragmentShaderGeneratorD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/DepalettizeShaderD3D11.h"
#include "GPU/D3D11/D3D11Util.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "Core/Config.h"
#include "Core/Host.h"

#include "ext/xxhash.h"
#include "math/math_util.h"


#define INVALID_TEX (ID3D11ShaderResourceView *)(-1LL)

	// If a texture hasn't been seen for this many frames, get rid of it.
#define TEXTURE_KILL_AGE 200
#define TEXTURE_KILL_AGE_LOWMEM 60
	// Not used in lowmem mode.
#define TEXTURE_SECOND_KILL_AGE 100

	// Try to be prime to other decimation intervals.
#define TEXCACHE_DECIMATION_INTERVAL 13

#define TEXCACHE_MAX_TEXELS_SCALED (256*256)  // Per frame

#define TEXCACHE_MIN_PRESSURE 16 * 1024 * 1024  // Total in VRAM
#define TEXCACHE_SECOND_MIN_PRESSURE 4 * 1024 * 1024

static const D3D11_INPUT_ELEMENT_DESC g_FramebufferVertexElements[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,},
};

SamplerCacheD3D11::~SamplerCacheD3D11() {
	for (auto &iter : cache_) {
		iter.second->Release();
	}
}

ID3D11SamplerState *SamplerCacheD3D11::GetOrCreateSampler(ID3D11Device *device, const SamplerCacheKey &key) {
	auto iter = cache_.find(key);
	if (iter != cache_.end()) {
		return iter->second;
	}

	D3D11_SAMPLER_DESC samp{};
	samp.AddressU = key.sClamp ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
	samp.AddressV = key.tClamp ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
	samp.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY) && g_Config.iAnisotropyLevel > 0) {
		samp.MaxAnisotropy = (float)(1 << g_Config.iAnisotropyLevel);
	} else {
		samp.MaxAnisotropy = 1.0f;
	}
	int filterKey = ((int)key.minFilt << 2) | ((int)key.magFilt << 1) | ((int)key.mipFilt);
	static const D3D11_FILTER filters[8] = {
		D3D11_FILTER_MIN_MAG_MIP_POINT,
		D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR,
		D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
		D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR,
		D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,
		D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
		D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
		D3D11_FILTER_MIN_MAG_MIP_LINEAR,
	};
	samp.Filter = filters[filterKey];
	samp.MaxLOD = key.maxLevel;
	samp.MinLOD = 0.0f;
	samp.MipLODBias = 0.0f;

	ID3D11SamplerState *sampler;
	device->CreateSamplerState(&samp, &sampler);
	cache_[key] = sampler;
	return sampler;
}

TextureCacheD3D11::TextureCacheD3D11(Draw::DrawContext *draw)
	: TextureCacheCommon(draw) {
	lastBoundTexture = INVALID_TEX;
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;

	HRESULT result = 0;

	maxAnisotropyLevel = 16;
	SetupTextureDecoder();

	nextTexture_ = nullptr;
}

TextureCacheD3D11::~TextureCacheD3D11() {
	// pFramebufferVertexDecl->Release();
	Clear(true);
}

void TextureCacheD3D11::SetFramebufferManager(FramebufferManagerD3D11 *fbManager) {
	framebufferManagerD3D11_ = fbManager;
	framebufferManager_ = fbManager;
}

void TextureCacheD3D11::Clear(bool delete_them) {
	context_->PSSetShaderResources(0, 1, nullptr);
	lastBoundTexture = INVALID_TEX;
	if (delete_them) {
		for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %p", iter->second.texturePtr);
			ReleaseTexture(&iter->second);
		}
		for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %p", iter->second.texturePtr);
			ReleaseTexture(&iter->second);
		}
	}
	if (cache.size() + secondCache.size()) {
		INFO_LOG(G3D, "Texture cached cleared from %i textures", (int)(cache.size() + secondCache.size()));
		cache.clear();
		secondCache.clear();
		cacheSizeEstimate_ = 0;
		secondCacheSizeEstimate_ = 0;
	}
	fbTexInfo_.clear();
	videos_.clear();
}

void TextureCacheD3D11::DeleteTexture(TexCache::iterator it) {
	ReleaseTexture(&it->second);
	auto fbInfo = fbTexInfo_.find(it->first);
	if (fbInfo != fbTexInfo_.end()) {
		fbTexInfo_.erase(fbInfo);
	}

	cacheSizeEstimate_ -= EstimateTexMemoryUsage(&it->second);
	cache.erase(it);
}

void TextureCacheD3D11::ForgetLastTexture() {
	lastBoundTexture = INVALID_TEX;
	gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
}

// Removes old textures.
void TextureCacheD3D11::Decimate() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	if (cacheSizeEstimate_ >= TEXCACHE_MIN_PRESSURE) {
		const u32 had = cacheSizeEstimate_;

		context_->PSSetShaderResources(0, 1, nullptr);
		lastBoundTexture = INVALID_TEX;
		int killAge = lowMemoryMode_ ? TEXTURE_KILL_AGE_LOWMEM : TEXTURE_KILL_AGE;
		for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ) {
			if (iter->second.lastFrame + killAge < gpuStats.numFlips) {
				DeleteTexture(iter++);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(G3D, "Decimated texture cache, saved %d estimated bytes - now %d bytes", had - cacheSizeEstimate_, cacheSizeEstimate_);
	}

	if (g_Config.bTextureSecondaryCache && secondCacheSizeEstimate_ >= TEXCACHE_SECOND_MIN_PRESSURE) {
		const u32 had = secondCacheSizeEstimate_;

		for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ) {
			// In low memory mode, we kill them all.
			if (lowMemoryMode_ || iter->second.lastFrame + TEXTURE_KILL_AGE < gpuStats.numFlips) {
				ReleaseTexture(&iter->second);
				secondCache.erase(iter++);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(G3D, "Decimated second texture cache, saved %d estimated bytes - now %d bytes", had - secondCacheSizeEstimate_, secondCacheSizeEstimate_);
	}

	DecimateVideos();
}

DXGI_FORMAT getClutDestFormatD3D11(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return DXGI_FORMAT_B4G4R4A4_UNORM;
	case GE_CMODE_16BIT_ABGR5551:
		return DXGI_FORMAT_B5G5R5A1_UNORM;
	case GE_CMODE_16BIT_BGR5650:
		return DXGI_FORMAT_B5G6R5_UNORM;
	case GE_CMODE_32BIT_ABGR8888:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
	// Should never be here !
	return DXGI_FORMAT_R8G8B8A8_UNORM;
}

void TextureCacheD3D11::UpdateSamplingParams(TexCacheEntry &entry, SamplerCacheKey &key) {
	// TODO: Make GetSamplingParams write SamplerCacheKey directly
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, entry.maxLevel, entry.addr);
	key.minFilt = minFilt & 1;
	key.mipEnable = (minFilt >> 2) & 1;
	key.mipFilt = (minFilt >> 1) & 1;
	key.magFilt = magFilt & 1;
	key.sClamp = sClamp;
	key.tClamp = tClamp;
	key.maxLevel = entry.maxLevel;
	/*
	if (entry.maxLevel != 0) {
	if (force || entry.lodBias != lodBias) {
	if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
	GETexLevelMode mode = gstate.getTexLevelMode();
	switch (mode) {
	case GE_TEXLEVEL_MODE_AUTO:
	// TODO
	break;
	case GE_TEXLEVEL_MODE_CONST:
	// Sigh, LOD_BIAS is not even in ES 3.0..
	break;
	case GE_TEXLEVEL_MODE_SLOPE:
	// TODO
	break;
	}
	}
	entry.lodBias = lodBias;
	}
	}
	*/

	if (entry.framebuffer) {
		WARN_LOG_REPORT_ONCE(wrongFramebufAttach, G3D, "Framebuffer still attached in UpdateSamplingParams()?");
	}
}

void TextureCacheD3D11::SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0, 0);

	key.minFilt = minFilt & 1;
	key.mipFilt = 0;
	key.magFilt = magFilt & 1;
	key.sClamp = sClamp;
	key.tClamp = tClamp;

	// Often the framebuffer will not match the texture size.  We'll wrap/clamp in the shader in that case.
	// This happens whether we have OES_texture_npot or not.
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (w != bufferWidth || h != bufferHeight) {
		key.sClamp = true;
		key.tClamp = true;
	}
}

void TextureCacheD3D11::StartFrame() {
	lastBoundTexture = INVALID_TEX;
	timesInvalidatedAllThisFrame_ = 0;

	if (texelsScaledThisFrame_) {
		// INFO_LOG(G3D, "Scaled %i texels", texelsScaledThisFrame_);
	}
	texelsScaledThisFrame_ = 0;
	if (clearCacheNextFrame_) {
		Clear(true);
		clearCacheNextFrame_ = false;
	} else {
		Decimate();
	}
}

static inline u32 MiniHash(const u32 *ptr) {
	return ptr[0];
}

void TextureCacheD3D11::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
	const u32 clutBaseBytes = clutBase * (clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	//
	// TODO: Actually, this seems like a hack.  The game can upload part of a CLUT and reference other data.
	// clutTotalBytes_ is the last amount uploaded.  We should hash clutMaxBytes_, but this will often hash
	// unrelated old entries for small palettes.
	// Adding clutBaseBytes may just be mitigating this for some usage patterns.
	const u32 clutExtendedBytes = std::min(clutTotalBytes_ + clutBaseBytes, clutMaxBytes_);

	clutHash_ = DoReliableHash32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);
	clutBuf_ = clutBufRaw_;

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (clutFormat == GE_CMODE_16BIT_ABGR4444 && clutIndexIsSimple) {
		const u16_le *clut = GetCurrentClut<u16_le>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0x0FFF;
		for (int i = 0; i < 16; ++i) {
			u16 step = clutAlphaLinearColor_ | (i << 12);
			if (clut[i] != step) {
				clutAlphaLinear_ = false;
				break;
			}
		}
	}

	clutLastFormat_ = gstate.clutformat;
}

inline u32 TextureCacheD3D11::GetCurrentClutHash() {
	return clutHash_;
}

void TextureCacheD3D11::Unbind() {
	context_->PSSetShaderResources(0, 1, nullptr);
}

void TextureCacheD3D11::ApplyTexture() {
	TexCacheEntry *entry = nextTexture_;
	if (entry == nullptr) {
		return;
	}
	nextTexture_ = nullptr;

	UpdateMaxSeenV(entry, gstate.isModeThrough());

	bool replaceImages = false;
	if (nextNeedsRebuild_) {
		if (nextNeedsRehash_) {
			// Update the hash on the texture.
			int w = gstate.getTextureWidth(0);
			int h = gstate.getTextureHeight(0);
			entry->fullhash = QuickTexHash(replacer, entry->addr, entry->bufw, w, h, GETextureFormat(entry->format), entry);
		}
		if (nextNeedsChange_) {
			// This texture existed previously, let's handle the change.
			replaceImages = HandleTextureChange(entry, nextChangeReason_, false, true);
		}

		// We actually build afterward (shared with rehash rebuild.)
	} else if (nextNeedsRehash_) {
		// Okay, this matched and didn't change - but let's check the hash.  Maybe it will change.
		bool doDelete = true;
		if (!CheckFullHash(entry, doDelete)) {
			replaceImages = HandleTextureChange(entry, "hash fail", true, doDelete);
			nextNeedsRebuild_ = true;
		} else if (nextTexture_ != nullptr) {
			// Secondary cache picked a different texture, use it.
			entry = nextTexture_;
			nextTexture_ = nullptr;
			UpdateMaxSeenV(entry, gstate.isModeThrough());
		}
	}

	// Okay, now actually rebuild the texture if needed.
	if (nextNeedsRebuild_) {
		BuildTexture(entry, replaceImages);
	}

	entry->lastFrame = gpuStats.numFlips;
	if (entry->framebuffer) {
		ApplyTextureFramebuffer(entry, entry->framebuffer);
	} else {
		ID3D11ShaderResourceView *textureView = DxView(entry);
		if (textureView != lastBoundTexture) {
			context_->PSSetShaderResources(0, 1, &textureView);
			lastBoundTexture = textureView;
		}
		SamplerCacheKey key;
		UpdateSamplingParams(*entry, key);
		ID3D11SamplerState *state = samplerCache_.GetOrCreateSampler(device_, key);
		context_->PSSetSamplers(0, 1, &state);
		gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
		gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;
	}
}

class TextureShaderApplierD3D11 {
public:
	struct Pos {
		Pos(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {
		}
		Pos() {
		}

		float x;
		float y;
		float z;
	};
	struct UV {
		UV(float u_, float v_) : u(u_), v(v_) {
		}
		UV() {
		}

		float u;
		float v;
	};

	struct PosUV {
		Pos pos;
		UV uv;
	};

	TextureShaderApplierD3D11(ID3D11DeviceContext *context, ID3D11PixelShader *pshader, float bufferW, float bufferH, int renderW, int renderH, float xoff, float yoff)
		: context_(context), pshader_(pshader), bufferW_(bufferW), bufferH_(bufferH), renderW_(renderW), renderH_(renderH) {
		static const Pos pos[4] = {
			{ -1,  1, 0 },
			{ 1,  1, 0 },
			{ 1, -1, 0 },
			{ -1, -1, 0 },
		};
		static const UV uv[4] = {
			{ 0, 0 },
			{ 1, 0 },
			{ 1, 1 },
			{ 0, 1 },
		};

		for (int i = 0; i < 4; ++i) {
			verts_[i].pos = pos[i];
			verts_[i].pos.x += xoff;
			verts_[i].pos.y += yoff;
			verts_[i].uv = uv[i];
		}
	}

	void ApplyBounds(const KnownVertexBounds &bounds, u32 uoff, u32 voff, float xoff, float yoff) {
		// If min is not < max, then we don't have values (wasn't set during decode.)
		if (bounds.minV < bounds.maxV) {
			const float invWidth = 1.0f / bufferW_;
			const float invHeight = 1.0f / bufferH_;
			// Inverse of half = double.
			const float invHalfWidth = invWidth * 2.0f;
			const float invHalfHeight = invHeight * 2.0f;

			const int u1 = bounds.minU + uoff;
			const int v1 = bounds.minV + voff;
			const int u2 = bounds.maxU + uoff;
			const int v2 = bounds.maxV + voff;

			const float left = u1 * invHalfWidth - 1.0f + xoff;
			const float right = u2 * invHalfWidth - 1.0f + xoff;
			const float top = v1 * invHalfHeight - 1.0f + yoff;
			const float bottom = v2 * invHalfHeight - 1.0f + yoff;
			// Points are: BL, BR, TR, TL.
			verts_[0].pos = Pos(left, bottom, -1.0f);
			verts_[1].pos = Pos(right, bottom, -1.0f);
			verts_[2].pos = Pos(right, top, -1.0f);
			verts_[3].pos = Pos(left, top, -1.0f);

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			verts_[0].uv = UV(uvleft, uvbottom);
			verts_[1].uv = UV(uvright, uvbottom);
			verts_[2].uv = UV(uvright, uvtop);
			verts_[3].uv = UV(uvleft, uvtop);
		}
	}

	void Use(ID3D11VertexShader *vshader, ID3D11InputLayout *decl) {
		context_->PSSetShader(pshader_, 0, 0);
		context_->VSSetShader(vshader, 0, 0);
		context_->IASetInputLayout(decl);
	}

	void Shade() {
		D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)renderW_, (float)renderH_, 0.0f, 1.0f };
		context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0xF], nullptr, 0xFFFFFFFF);
		context_->OMSetDepthStencilState(stockD3D11.depthStencilDisabled, 0xFF);
		context_->RSSetState(stockD3D11.rasterStateNoCull);
		context_->RSSetViewports(1, &vp);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context_->IASetVertexBuffers(0, 1, &vbuffer_, &stride_, &offset_);
		context_->Draw(2, 0);
	}

protected:
	ID3D11DeviceContext *context_;
	ID3D11PixelShader *pshader_;
	ID3D11Buffer *vbuffer_;
	PosUV verts_[4];
	UINT stride_ = sizeof(PosUV);
	UINT offset_ = 0;
	float bufferW_;
	float bufferH_;
	int renderW_;
	int renderH_;
};

void TextureCacheD3D11::ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	ID3D11PixelShader *pshader = nullptr;
	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		pshader = depalShaderCache_->GetDepalettizePixelShader(clutFormat, framebuffer->drawnFormat);
	}

	if (pshader) {
		ID3D11ShaderResourceView *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);

		Draw::Framebuffer *depalFBO = framebufferManagerD3D11_->GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, Draw::FBO_8888);
		draw_->BindFramebufferAsRenderTarget(depalFBO);
		shaderManager_->DirtyLastShader();
		draw_->BindPipeline(nullptr);

		float xoff = -0.5f / framebuffer->renderWidth;
		float yoff = 0.5f / framebuffer->renderHeight;

		TextureShaderApplierD3D11 shaderApply(context_, pshader, framebuffer->bufferWidth, framebuffer->bufferHeight, framebuffer->renderWidth, framebuffer->renderHeight, xoff, yoff);
		shaderApply.ApplyBounds(gstate_c.vertBounds, gstate_c.curTextureXOffset, gstate_c.curTextureYOffset, xoff, yoff);
		shaderApply.Use(depalShaderCache_->GetDepalettizeVertexShader(), depalShaderCache_->GetInputLayout());

		context_->PSSetShaderResources(1, 1, &clutTexture);
		context_->PSSetSamplers(1, 1, &stockD3D11.samplerPoint2DWrap);
		framebufferManagerD3D11_->BindFramebufferColor(0, framebuffer, BINDFBCOLOR_SKIP_COPY);
		context_->PSSetSamplers(0, 1, &stockD3D11.samplerPoint2DWrap);
		shaderApply.Shade();

		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::FB_COLOR_BIT, 0);

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TexCacheEntry::Status alphaStatus = CheckAlpha(clutBuf_, getClutDestFormatD3D11(clutFormat), clutTotalColors, clutTotalColors, 1);
		gstate_c.textureFullAlpha = alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL;
		gstate_c.textureSimpleAlpha = alphaStatus == TexCacheEntry::STATUS_ALPHA_SIMPLE;
	} else {
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;

		framebufferManagerD3D11_->BindFramebufferColor(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);

		gstate_c.textureFullAlpha = gstate.getTextureFormat() == GE_TFMT_5650;
		gstate_c.textureSimpleAlpha = gstate_c.textureFullAlpha;
	}

	framebufferManagerD3D11_->RebindFramebuffer();
	SamplerCacheKey samplerKey;
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight, samplerKey);
	ID3D11SamplerState *state = samplerCache_.GetOrCreateSampler(device_, samplerKey);
	context_->PSSetSamplers(0, 1, &state);
	lastBoundTexture = INVALID_TEX;
}

void TextureCacheD3D11::SetTexture(bool force) {
#ifdef DEBUG_TEXTURES
	if (SetDebugTexture()) {
		// A different texture was bound, let's rebind next time.
		lastBoundTexture = INVALID_TEX;
		return;
	}
#endif

	if (force) {
		lastBoundTexture = INVALID_TEX;
	}

	u32 texaddr = gstate.getTextureAddress(0);
	if (!Memory::IsValidAddress(texaddr)) {
		// Bind a null texture and return.
		context_->PSSetShaderResources(0, 1, nullptr);
		lastBoundTexture = INVALID_TEX;
		return;
	}

	const u16 dim = gstate.getTextureDimension(0);
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);

	GETextureFormat format = gstate.getTextureFormat();
	if (format >= 11) {
		ERROR_LOG_REPORT(G3D, "Unknown texture format %i", format);
		// TODO: Better assumption?
		format = GE_TFMT_5650;
	}
	bool hasClut = gstate.isTextureFormatIndexed();

	// Ignore uncached/kernel when caching.
	u32 cluthash;
	if (hasClut) {
		if (clutLastFormat_ != gstate.clutformat) {
			// We update here because the clut format can be specified after the load.
			UpdateCurrentClut(gstate.getClutPaletteFormat(), gstate.getClutIndexStartPos(), gstate.isClutIndexSimple());
		}
		cluthash = GetCurrentClutHash() ^ gstate.clutformat;
	} else {
		cluthash = 0;
	}
	u64 cachekey = TexCacheEntry::CacheKey(texaddr, format, dim, cluthash);

	int bufw = GetTextureBufw(0, texaddr, format);
	u8 maxLevel = gstate.getTextureMaxLevel();

	u32 texhash = MiniHash((const u32 *)Memory::GetPointer(texaddr));

	TexCache::iterator iter = cache.find(cachekey);
	TexCacheEntry *entry = NULL;
	gstate_c.needShaderTexClamp = false;
	gstate_c.bgraTexture = true;
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

	if (iter != cache.end()) {
		entry = &iter->second;
		// Validate the texture still matches the cache entry.
		bool match = entry->Matches(dim, format, maxLevel);
		const char *reason = "different params";

		// Check for FBO - slow!
		if (entry->framebuffer) {
			if (match) {
				if (hasClut && clutRenderAddress_ != 0xFFFFFFFF) {
					WARN_LOG_REPORT_ONCE(clutAndTexRender, G3D, "Using rendered texture with rendered CLUT: texfmt=%d, clutfmt=%d", gstate.getTextureFormat(), gstate.getClutPaletteFormat());
				}

				SetTextureFramebuffer(entry, entry->framebuffer);
				return;
			} else {
				// Make sure we re-evaluate framebuffers.
				DetachFramebuffer(entry, texaddr, entry->framebuffer);
				reason = "detached framebuf";
				match = false;
			}
		}

		bool rehash = entry->GetHashStatus() == TexCacheEntry::STATUS_UNRELIABLE;

		// First let's see if another texture with the same address had a hashfail.
		if (entry->status & TexCacheEntry::STATUS_CLUT_RECHECK) {
			// Always rehash in this case, if one changed the rest all probably did.
			rehash = true;
			entry->status &= ~TexCacheEntry::STATUS_CLUT_RECHECK;
		} else if (!gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE)) {
			// Okay, just some parameter change - the data didn't change, no need to rehash.
			rehash = false;
		}

		if (match) {
			if (entry->lastFrame != gpuStats.numFlips) {
				u32 diff = gpuStats.numFlips - entry->lastFrame;
				entry->numFrames++;

				if (entry->framesUntilNextFullHash < diff) {
					// Exponential backoff up to 512 frames.  Textures are often reused.
					if (entry->numFrames > 32) {
						// Also, try to add some "randomness" to avoid rehashing several textures the same frame.
						entry->framesUntilNextFullHash = std::min(512, entry->numFrames) + ((intptr_t)entry->texturePtr & 15);
					} else {
						entry->framesUntilNextFullHash = entry->numFrames;
					}
					rehash = true;
				} else {
					entry->framesUntilNextFullHash -= diff;
				}
			}

			// If it's not huge or has been invalidated many times, recheck the whole texture.
			if (entry->invalidHint > 180 || (entry->invalidHint > 15 && (dim >> 8) < 9 && (dim & 0xF) < 9)) {
				entry->invalidHint = 0;
				rehash = true;
			}

			if (texhash != entry->hash) {
				match = false;
			} else if (entry->GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
				rehash = false;
			}
		}

		if (match && (entry->status & TexCacheEntry::STATUS_TO_SCALE) && standardScaleFactor_ != 1 && texelsScaledThisFrame_ < TEXCACHE_MAX_TEXELS_SCALED) {
			if ((entry->status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
				// INFO_LOG(G3D, "Reloading texture to do the scaling we skipped..");
				match = false;
				reason = "scaling";
			}
		}

		if (match) {
			// TODO: Mark the entry reliable if it's been safe for long enough?
			//got one!
			if (entry->texturePtr != lastBoundTexture) {
				gstate_c.curTextureWidth = w;
				gstate_c.curTextureHeight = h;
			}
			if (rehash) {
				// Update in case any of these changed.
				entry->sizeInRAM = (textureBitsPerPixel[format] * bufw * h / 2) / 8;
				entry->bufw = bufw;
				entry->cluthash = cluthash;
			}

			nextTexture_ = entry;
			nextNeedsRehash_ = rehash;
			nextNeedsChange_ = false;
			// Might need a rebuild if the hash fails.
			nextNeedsRebuild_ = false;
			VERBOSE_LOG(G3D, "Texture at %08x Found in Cache, applying", texaddr);
			return; //Done!
		} else {
			nextChangeReason_ = reason;
			nextNeedsChange_ = true;
		}
	} else {
		VERBOSE_LOG(G3D, "No texture in cache, decoding...");
		TexCacheEntry entryNew = { 0 };
		cache[cachekey] = entryNew;

		if (hasClut && clutRenderAddress_ != 0xFFFFFFFF) {
			WARN_LOG_REPORT_ONCE(clutUseRender, G3D, "Using texture with rendered CLUT: texfmt=%d, clutfmt=%d", gstate.getTextureFormat(), gstate.getClutPaletteFormat());
		}

		entry = &cache[cachekey];
		if (g_Config.bTextureBackoffCache) {
			entry->status = TexCacheEntry::STATUS_HASHING;
		} else {
			entry->status = TexCacheEntry::STATUS_UNRELIABLE;
		}

		nextNeedsChange_ = false;
	}

	// We have to decode it, let's setup the cache entry first.
	entry->addr = texaddr;
	entry->hash = texhash;
	entry->dim = dim;
	entry->format = format;
	entry->maxLevel = maxLevel;

	// This would overestimate the size in many case so we underestimate instead
	// to avoid excessive clearing caused by cache invalidations.
	entry->sizeInRAM = (textureBitsPerPixel[format] * bufw * h / 2) / 8;
	entry->bufw = bufw;

	entry->cluthash = cluthash;

	gstate_c.curTextureWidth = w;
	gstate_c.curTextureHeight = h;

	// Before we go reading the texture from memory, let's check for render-to-texture.
	// We must do this early so we have the right w/h.
	entry->framebuffer = 0;
	for (size_t i = 0, n = fbCache_.size(); i < n; ++i) {
		auto framebuffer = fbCache_[i];
		AttachFramebuffer(entry, framebuffer->fb_address, framebuffer);
	}

	// If we ended up with a framebuffer, attach it - no texture decoding needed.
	if (entry->framebuffer) {
		SetTextureFramebuffer(entry, entry->framebuffer);
	}

	nextTexture_ = entry;
	nextNeedsRehash_ = entry->framebuffer == nullptr;
	// We still need to rebuild, to allocate a texture.  But we'll bail early.
	nextNeedsRebuild_ = true;
}

bool TextureCacheD3D11::CheckFullHash(TexCacheEntry *const entry, bool &doDelete) {
	bool hashFail = false;
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	u32 fullhash = QuickTexHash(replacer, entry->addr, entry->bufw, w, h, GETextureFormat(entry->format), entry);
	if (fullhash != entry->fullhash) {
		hashFail = true;
	} else {
		if (g_Config.bTextureBackoffCache) {
			if (entry->GetHashStatus() != TexCacheEntry::STATUS_HASHING && entry->numFrames > TexCacheEntry::FRAMES_REGAIN_TRUST) {
				// Reset to STATUS_HASHING.
				entry->SetHashStatus(TexCacheEntry::STATUS_HASHING);
				entry->status &= ~TexCacheEntry::STATUS_CHANGE_FREQUENT;
			}
		} else if (entry->numFrames > TEXCACHE_FRAME_CHANGE_FREQUENT_REGAIN_TRUST) {
			entry->status &= ~TexCacheEntry::STATUS_CHANGE_FREQUENT;
		}
	}

	if (hashFail) {
		entry->status |= TexCacheEntry::STATUS_UNRELIABLE;
		if (entry->numFrames < TEXCACHE_FRAME_CHANGE_FREQUENT) {
			if (entry->status & TexCacheEntry::STATUS_FREE_CHANGE) {
				entry->status &= ~TexCacheEntry::STATUS_FREE_CHANGE;
			} else {
				entry->status |= TexCacheEntry::STATUS_CHANGE_FREQUENT;
			}
		}
		entry->numFrames = 0;

		// Don't give up just yet.  Let's try the secondary cache if it's been invalidated before.
		// If it's failed a bunch of times, then the second cache is just wasting time and VRAM.
		if (g_Config.bTextureSecondaryCache) {
			if (entry->numInvalidated > 2 && entry->numInvalidated < 128 && !lowMemoryMode_) {
				u64 secondKey = fullhash | (u64)entry->cluthash << 32;
				TexCache::iterator secondIter = secondCache.find(secondKey);
				if (secondIter != secondCache.end()) {
					TexCacheEntry *secondEntry = &secondIter->second;
					if (secondEntry->Matches(entry->dim, entry->format, entry->maxLevel)) {
						// Reset the numInvalidated value lower, we got a match.
						if (entry->numInvalidated > 8) {
							--entry->numInvalidated;
						}
						nextTexture_ = secondEntry;
						return true;
					}
				} else {
					secondKey = entry->fullhash | ((u64)entry->cluthash << 32);
					secondCacheSizeEstimate_ += EstimateTexMemoryUsage(entry);
					secondCache[secondKey] = *entry;
					doDelete = false;
				}
			}
		}

		// We know it failed, so update the full hash right away.
		entry->fullhash = fullhash;
		return false;
	}

	return true;
}

bool TextureCacheD3D11::HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete) {
	bool replaceImages = false;

	cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
	entry->numInvalidated++;
	gpuStats.numTextureInvalidations++;
	DEBUG_LOG(G3D, "Texture different or overwritten, reloading at %08x: %s", entry->addr, reason);
	if (doDelete) {
		if (initialMatch && standardScaleFactor_ == 1 && (entry->status & TexCacheEntry::STATUS_IS_SCALED) == 0) {
			// Actually, if size and number of levels match, let's try to avoid deleting and recreating.
			// Instead, let's use glTexSubImage to replace the images.
			replaceImages = true;
		} else {
			if (entry->texturePtr == lastBoundTexture) {
				lastBoundTexture = INVALID_TEX;
			}
			ReleaseTexture(entry);
			entry->status &= ~TexCacheEntry::STATUS_IS_SCALED;
		}
	}
	// Clear the reliable bit if set.
	if (entry->GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
		entry->SetHashStatus(TexCacheEntry::STATUS_HASHING);
	}

	// Also, mark any textures with the same address but different clut.  They need rechecking.
	if (entry->cluthash != 0) {
		const u64 cachekeyMin = (u64)(entry->addr & 0x3FFFFFFF) << 32;
		const u64 cachekeyMax = cachekeyMin + (1ULL << 32);
		for (auto it = cache.lower_bound(cachekeyMin), end = cache.upper_bound(cachekeyMax); it != end; ++it) {
			if (it->second.cluthash != entry->cluthash) {
				it->second.status |= TexCacheEntry::STATUS_CLUT_RECHECK;
			}
		}
	}

	return replaceImages;
}

void TextureCacheD3D11::BuildTexture(TexCacheEntry *const entry, bool replaceImages) {
	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;

	// For the estimate, we assume cluts always point to 8888 for simplicity.
	cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

	// TODO: If a framebuffer is attached here, might end up with a bad entry.texture.
	// Should just always create one here or something (like GLES.)

	if (entry->framebuffer) {
		// Nothing else to do here.
		return;
	}

	if ((entry->bufw == 0 || (gstate.texbufwidth[0] & 0xf800) != 0) && entry->addr >= PSP_GetKernelMemoryEnd()) {
		ERROR_LOG_REPORT(G3D, "Texture with unexpected bufw (full=%d)", gstate.texbufwidth[0] & 0xffff);
		// Proceeding here can cause a crash.
		return;
	}

	// Adjust maxLevel to actually present levels..
	bool badMipSizes = false;
	int maxLevel = entry->maxLevel;
	for (int i = 0; i <= maxLevel; i++) {
		// If encountering levels pointing to nothing, adjust max level.
		u32 levelTexaddr = gstate.getTextureAddress(i);
		if (!Memory::IsValidAddress(levelTexaddr)) {
			maxLevel = i - 1;
			break;
		}

		if (i > 0 && gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
			int tw = gstate.getTextureWidth(i);
			int th = gstate.getTextureHeight(i);
			if (tw != 1 && tw != (gstate.getTextureWidth(i - 1) >> 1))
				badMipSizes = true;
			else if (th != 1 && th != (gstate.getTextureHeight(i - 1) >> 1))
				badMipSizes = true;
		}
	}

	// In addition, simply don't load more than level 0 if g_Config.bMipMap is false.
	if (!g_Config.bMipMap) {
		maxLevel = 0;
	}

	// If GLES3 is available, we can preallocate the storage, which makes texture loading more efficient.
	DXGI_FORMAT dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

	int scaleFactor = standardScaleFactor_;

	// Rachet down scale factor in low-memory mode.
	if (lowMemoryMode_) {
		// Keep it even, though, just in case of npot troubles.
		scaleFactor = scaleFactor > 4 ? 4 : (scaleFactor > 2 ? 2 : 1);
	}

	u64 cachekey = replacer.Enabled() ? entry->CacheKey() : 0;
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	ReplacedTexture &replaced = replacer.FindReplacement(cachekey, entry->fullhash, w, h);
	if (replaced.GetSize(0, w, h)) {
		if (replaceImages) {
			// Since we're replacing the texture, we can't replace the image inside.
			ReleaseTexture(entry);
			replaceImages = false;
		}
		// We're replacing, so we won't scale.
		scaleFactor = 1;
		entry->status |= TexCacheEntry::STATUS_IS_SCALED;
		if (g_Config.bMipMap) {
			maxLevel = replaced.MaxLevel();
			badMipSizes = false;
		}
	}

	// Don't scale the PPGe texture.
	if (entry->addr > 0x05000000 && entry->addr < PSP_GetKernelMemoryEnd())
		scaleFactor = 1;
	if ((entry->status & TexCacheEntry::STATUS_CHANGE_FREQUENT) != 0 && scaleFactor != 1) {
		// Remember for later that we /wanted/ to scale this texture.
		entry->status |= TexCacheEntry::STATUS_TO_SCALE;
		scaleFactor = 1;
	}

	if (scaleFactor != 1) {
		if (texelsScaledThisFrame_ >= TEXCACHE_MAX_TEXELS_SCALED) {
			entry->status |= TexCacheEntry::STATUS_TO_SCALE;
			scaleFactor = 1;
		} else {
			entry->status &= ~TexCacheEntry::STATUS_TO_SCALE;
			entry->status |= TexCacheEntry::STATUS_IS_SCALED;
			texelsScaledThisFrame_ += w * h;
		}
	}

	if (replaceImages) {
		// Make sure it's not currently set.
		context_->PSSetShaderResources(0, 1, nullptr);
	}

	// Seems to cause problems in Tactics Ogre.
	if (badMipSizes) {
		maxLevel = 0;
	}

	LoadTextureLevel(*entry, replaced, 0, maxLevel, replaceImages, scaleFactor, dstFmt);
	ID3D11ShaderResourceView *textureView = DxView(entry);
	if (!textureView) {
		return;
	}

	// Mipmapping is only enabled when texture scaling is disabled.
	if (maxLevel > 0 && scaleFactor == 1) {
		for (int i = 1; i <= maxLevel; i++) {
			LoadTextureLevel(*entry, replaced, i, maxLevel, replaceImages, scaleFactor, dstFmt);
		}
	}

	if (replaced.Valid()) {
		entry->SetAlphaStatus(TexCacheEntry::Status(replaced.AlphaStatus()));
	}
}

DXGI_FORMAT TextureCacheD3D11::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormatD3D11(clutFormat);
	case GE_TFMT_4444:
		return DXGI_FORMAT_B4G4R4A4_UNORM;
	case GE_TFMT_5551:
		return DXGI_FORMAT_B5G5R5A1_UNORM;
	case GE_TFMT_5650:
		return DXGI_FORMAT_B5G6R5_UNORM;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}

TextureCacheD3D11::TexCacheEntry::Status TextureCacheD3D11::CheckAlpha(const u32 *pixelData, u32 dstFmt, int stride, int w, int h) {
	CheckAlphaResult res;
	switch (dstFmt) {
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		res = CheckAlphaRGBA4444Basic(pixelData, stride, w, h);
		break;
	case DXGI_FORMAT_B5G5R5A1_UNORM:
		res = CheckAlphaRGBA5551Basic(pixelData, stride, w, h);
		break;
	case DXGI_FORMAT_B5G6R5_UNORM:
		// Never has any alpha.
		res = CHECKALPHA_FULL;
		break;
	default:
		res = CheckAlphaRGBA8888Basic(pixelData, stride, w, h);
		break;
	}

	return (TexCacheEntry::Status)res;
}

ReplacedTextureFormat FromD3D11Format(u32 fmt) {
	switch (fmt) {
	case DXGI_FORMAT_B5G6R5_UNORM: return ReplacedTextureFormat::F_5650;
	case DXGI_FORMAT_B5G5R5A1_UNORM: return ReplacedTextureFormat::F_5551;
	case DXGI_FORMAT_B4G4R4A4_UNORM: return ReplacedTextureFormat::F_4444;
	case DXGI_FORMAT_R8G8B8A8_UNORM: default: return ReplacedTextureFormat::F_8888;
	}
}

DXGI_FORMAT ToDXGIFormat(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650: return DXGI_FORMAT_B5G6R5_UNORM;
	case ReplacedTextureFormat::F_5551: return DXGI_FORMAT_B5G5R5A1_UNORM;
	case ReplacedTextureFormat::F_4444: return DXGI_FORMAT_B4G4R4A4_UNORM;
	case ReplacedTextureFormat::F_8888: default: return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}

void TextureCacheD3D11::LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, bool replaceImages, int scaleFactor, u32 dstFmt) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	ID3D11Texture2D *texture = DxTex(&entry);
	if (level == 0 && (!replaceImages || texture == nullptr)) {
		// Create texture
		int levels = scaleFactor == 1 ? maxLevel + 1 : 1;
		int tw = w, th = h;
		DXGI_FORMAT tfmt = (DXGI_FORMAT)(dstFmt);
		if (replaced.GetSize(level, tw, th)) {
			tfmt = ToDXGIFormat(replaced.Format(level));
		} else {
			tw *= scaleFactor;
			th *= scaleFactor;
			if (scaleFactor > 1) {
				tfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
			}
		}
		D3D11_TEXTURE2D_DESC desc{};
		// TODO: Make it immutable, will requires some code restructuring though.
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Width = tw;
		desc.Height = th;
		desc.MipLevels = levels;

		HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &texture);
		if (FAILED(hr)) {
			INFO_LOG(G3D, "Failed to create D3D texture");
			ReleaseTexture(&entry);
			return;
		}
		ID3D11ShaderResourceView *view;
		hr = device_->CreateShaderResourceView(texture, nullptr, &view);

		entry.texturePtr = texture;
		entry.textureView = view;
	}

	D3D11_MAPPED_SUBRESOURCE map;
	context_->Map(texture, level, D3D11_MAP_WRITE_DISCARD, 0, &map);

	gpuStats.numTexturesDecoded++;
	if (replaced.GetSize(level, w, h)) {
		replaced.Load(level, map.pData, map.RowPitch);
		dstFmt = ToDXGIFormat(replaced.Format(level));
	} else {
		GETextureFormat tfmt = (GETextureFormat)entry.format;
		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		u32 texaddr = gstate.getTextureAddress(level);
		int bufw = GetTextureBufw(level, texaddr, tfmt);
		int bpp = dstFmt == DXGI_FORMAT_R8G8B8A8_UNORM ? 4 : 2;

		u32 *pixelData = (u32 *)map.pData;
		int decPitch = map.RowPitch;
		if (scaleFactor > 1) {
			tmpTexBufRearrange.resize(std::max(bufw, w) * h);
			pixelData = tmpTexBufRearrange.data();
			// We want to end up with a neatly packed texture for scaling.
			decPitch = w * bpp;
		}

		bool decSuccess = DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, level, bufw, false);
		if (!decSuccess) {
			memset(pixelData, 0, decPitch * h);
		}

		if (scaleFactor > 1) {
			scaler.ScaleAlways((u32 *)map.pData, pixelData, dstFmt, w, h, scaleFactor);
			pixelData = (u32 *)map.pData;

			// We always end up at 8888.  Other parts assume this.
			assert(dstFmt == DXGI_FORMAT_R8G8B8A8_UNORM);
			bpp = sizeof(u32);
			decPitch = w * bpp;

			if (decPitch != map.RowPitch) {
				// Rearrange in place to match the requested pitch.
				// (it can only be larger than w * bpp, and a match is likely.)
				for (int y = h - 1; y >= 0; --y) {
					memcpy((u8 *)map.pData + map.RowPitch * y, (u8 *)map.pData + decPitch * y, w * bpp);
				}
				decPitch = map.RowPitch;
			}
		}

		if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
			TexCacheEntry::Status alphaStatus = CheckAlpha(pixelData, dstFmt, decPitch / bpp, w, h);
			entry.SetAlphaStatus(alphaStatus, level);
		} else {
			entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
		}

		if (replacer.Enabled()) {
			ReplacedTextureDecodeInfo replacedInfo;
			replacedInfo.cachekey = entry.CacheKey();
			replacedInfo.hash = entry.fullhash;
			replacedInfo.addr = entry.addr;
			replacedInfo.isVideo = videos_.find(entry.addr & 0x3FFFFFFF) != videos_.end();
			replacedInfo.isFinal = (entry.status & TexCacheEntry::STATUS_TO_SCALE) == 0;
			replacedInfo.scaleFactor = scaleFactor;
			replacedInfo.fmt = FromD3D11Format(dstFmt);

			replacer.NotifyTextureDecoded(replacedInfo, pixelData, decPitch, level, w, h);
		}
	}
	context_->Unmap(texture, level);
}

bool TextureCacheD3D11::DecodeTexture(u8 *output, const GPUgstate &state) {
	OutputDebugStringA("TextureCache::DecodeTexture : FixMe\r\n");
	return true;
}
