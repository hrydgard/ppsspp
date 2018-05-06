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

static const D3D11_INPUT_ELEMENT_DESC g_QuadVertexElements[] = {
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
	samp.AddressW = samp.AddressU;  // Mali benefits from all clamps being the same, and this one is irrelevant.
	if (key.aniso) {
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
	// Only switch to aniso if linear min and mag are set.
	if (key.aniso && key.magFilt != 0 && key.minFilt != 0)
		samp.Filter = D3D11_FILTER_ANISOTROPIC;
	else
		samp.Filter = filters[filterKey];
#if PPSSPP_PLATFORM(UWP) && PPSSPP_ARCH(ARM)
	// For some reason, can't set MaxLOD on mobile.
	samp.MaxLOD = FLT_MAX;
	samp.MinLOD = -FLT_MAX;
	samp.MipLODBias = 0.0f;
#else
	samp.MaxLOD = key.maxLevel / 256.0f;
	samp.MinLOD = key.minLevel / 256.0f;
	samp.MipLODBias = key.lodBias / 256.0f;
#endif
	samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
	for (int i = 0; i < 4; i++) {
		samp.BorderColor[i] = 1.0f;
	}

	ID3D11SamplerState *sampler;
	ASSERT_SUCCESS(device->CreateSamplerState(&samp, &sampler));
	cache_[key] = sampler;
	return sampler;
}

TextureCacheD3D11::TextureCacheD3D11(Draw::DrawContext *draw)
	: TextureCacheCommon(draw) {
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);

	isBgraBackend_ = true;
	lastBoundTexture = INVALID_TEX;

	HRESULT result = 0;

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

void TextureCacheD3D11::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	ID3D11Texture2D *texture = (ID3D11Texture2D *)entry->texturePtr;
	ID3D11ShaderResourceView *view = (ID3D11ShaderResourceView *)entry->textureView;
	if (texture) {
		texture->Release();
		entry->texturePtr = nullptr;
	}
	if (view) {
		view->Release();
		entry->textureView = nullptr;
	}
}

void TextureCacheD3D11::ForgetLastTexture() {
	InvalidateLastTexture();
	gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	ID3D11ShaderResourceView *nullTex = nullptr;
	context_->PSSetShaderResources(0, 1, &nullTex);
}

void TextureCacheD3D11::InvalidateLastTexture(TexCacheEntry *entry) {
	if (!entry || entry->texturePtr == lastBoundTexture) {
		lastBoundTexture = INVALID_TEX;
	}
}

void TextureCacheD3D11::SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GETexLevelMode mode;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0, 0, mode);

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
	InvalidateLastTexture();
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

void TextureCacheD3D11::BindTexture(TexCacheEntry *entry) {
	ID3D11ShaderResourceView *textureView = DxView(entry);
	if (textureView != lastBoundTexture) {
		context_->PSSetShaderResources(0, 1, &textureView);
		lastBoundTexture = textureView;
	}
	SamplerCacheKey key{};
	UpdateSamplingParams(*entry, key);
	ID3D11SamplerState *state = samplerCache_.GetOrCreateSampler(device_, key);
	context_->PSSetSamplers(0, 1, &state);
}

void TextureCacheD3D11::Unbind() {
	ID3D11ShaderResourceView *nullView = nullptr;
	context_->PSSetShaderResources(0, 1, &nullView);
	InvalidateLastTexture();
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

	TextureShaderApplierD3D11(ID3D11DeviceContext *context, ID3D11PixelShader *pshader, ID3D11Buffer *dynamicBuffer, float bufferW, float bufferH, int renderW, int renderH, float xoff, float yoff)
		: context_(context), pshader_(pshader), bufferW_(bufferW), bufferH_(bufferH), renderW_(renderW), renderH_(renderH) {
		static const Pos pos[4] = {
			{ -1,  1, 0 },
			{ 1,  1, 0 },
			{ -1, -1, 0 },
			{ 1, -1, 0 },
		};
		static const UV uv[4] = {
			{ 0, 0 },
			{ 1, 0 },
			{ 0, 1 },
			{ 1, 1 },
		};

		for (int i = 0; i < 4; ++i) {
			verts_[i].pos = pos[i];
			verts_[i].pos.x += xoff;
			verts_[i].pos.y += yoff;
			verts_[i].uv = uv[i];
		}
		D3D11_MAPPED_SUBRESOURCE map;
		context->Map(dynamicBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
		memcpy(map.pData, &verts_[0], 4 * 5 * sizeof(float));
		context->Unmap(dynamicBuffer, 0);
		vbuffer_ = dynamicBuffer;
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
			float z = 0.0f;
			// Points are: BL, BR, TL, TR.
			verts_[0].pos = Pos(left, bottom, z);
			verts_[1].pos = Pos(right, bottom, z);
			verts_[2].pos = Pos(left, top, z);
			verts_[3].pos = Pos(right, top, z);

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			verts_[0].uv = UV(uvleft, uvbottom);
			verts_[1].uv = UV(uvright, uvbottom);
			verts_[2].uv = UV(uvleft, uvtop);
			verts_[3].uv = UV(uvright, uvtop);

			// We need to reapply the texture next time since we cropped UV.
			gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
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
		context_->Draw(4, 0);
		gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
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
	uint32_t clutMode = gstate.clutformat & 0xFFFFFF;
	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		pshader = depalShaderCache_->GetDepalettizePixelShader(clutMode, framebuffer->drawnFormat);
	}

	if (pshader) {
		bool expand32 = !gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS);
		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		ID3D11ShaderResourceView *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_, expand32);

		Draw::Framebuffer *depalFBO = framebufferManagerD3D11_->GetTempFBO(TempFBO::DEPAL, framebuffer->renderWidth, framebuffer->renderHeight, Draw::FBO_8888);
		shaderManager_->DirtyLastShader();
		draw_->BindPipeline(nullptr);

		float xoff = -0.5f / framebuffer->renderWidth;
		float yoff = 0.5f / framebuffer->renderHeight;

		TextureShaderApplierD3D11 shaderApply(context_, pshader, framebufferManagerD3D11_->GetDynamicQuadBuffer(), framebuffer->bufferWidth, framebuffer->bufferHeight, framebuffer->renderWidth, framebuffer->renderHeight, xoff, yoff);
		shaderApply.ApplyBounds(gstate_c.vertBounds, gstate_c.curTextureXOffset, gstate_c.curTextureYOffset, xoff, yoff);
		shaderApply.Use(depalShaderCache_->GetDepalettizeVertexShader(), depalShaderCache_->GetInputLayout());

		context_->PSSetShaderResources(3, 1, &clutTexture);
		context_->PSSetSamplers(3, 1, &stockD3D11.samplerPoint2DWrap);
		framebufferManagerD3D11_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_SKIP_COPY);
		context_->PSSetSamplers(0, 1, &stockD3D11.samplerPoint2DWrap);
		draw_->BindFramebufferAsRenderTarget(depalFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
		shaderApply.Shade();

		framebufferManagerD3D11_->RebindFramebuffer();
		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::FB_COLOR_BIT, 0);

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TexCacheEntry::TexStatus alphaStatus = CheckAlpha(clutBuf_, GetClutDestFormatD3D11(clutFormat), clutTotalColors, clutTotalColors, 1);
		gstate_c.SetTextureFullAlpha(alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL);
	} else {
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;

		framebufferManagerD3D11_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);

		gstate_c.SetTextureFullAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
		framebufferManagerD3D11_->RebindFramebuffer();  // Probably not necessary.
	}
	SamplerCacheKey samplerKey{};
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight, samplerKey);
	ID3D11SamplerState *state = samplerCache_.GetOrCreateSampler(device_, samplerKey);
	context_->PSSetSamplers(0, 1, &state);
	InvalidateLastTexture();

	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_BLEND_STATE | DIRTY_FRAGMENTSHADER_STATE);
}


void TextureCacheD3D11::BuildTexture(TexCacheEntry *const entry) {
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

		// If size reaches 1, stop, and override maxlevel.
		int tw = gstate.getTextureWidth(i);
		int th = gstate.getTextureHeight(i);
		if (tw == 1 || th == 1) {
			maxLevel = i;
			break;
		}

		if (i > 0 && gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
			if (tw != 1 && tw != (gstate.getTextureWidth(i - 1) >> 1))
				badMipSizes = true;
			else if (th != 1 && th != (gstate.getTextureHeight(i - 1) >> 1))
				badMipSizes = true;
		}
	}

	int scaleFactor = standardScaleFactor_;

	// Rachet down scale factor in low-memory mode.
	if (lowMemoryMode_) {
		// Keep it even, though, just in case of npot troubles.
		scaleFactor = scaleFactor > 4 ? 4 : (scaleFactor > 2 ? 2 : 1);
	}

	u64 cachekey = replacer_.Enabled() ? entry->CacheKey() : 0;
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	ReplacedTexture &replaced = replacer_.FindReplacement(cachekey, entry->fullhash, w, h);
	if (replaced.GetSize(0, w, h)) {
		// We're replacing, so we won't scale.
		scaleFactor = 1;
		entry->status |= TexCacheEntry::STATUS_IS_SCALED;
		maxLevel = replaced.MaxLevel();
		badMipSizes = false;
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

	// Seems to cause problems in Tactics Ogre.
	if (badMipSizes) {
		maxLevel = 0;
	}

	DXGI_FORMAT dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

	if (IsFakeMipmapChange()) {
		// NOTE: Since the level is not part of the cache key, we assume it never changes.
		u8 level = std::max(0, gstate.getTexLevelOffset16() / 16);
		LoadTextureLevel(*entry, replaced, level, maxLevel, scaleFactor, dstFmt);
	} else {
		LoadTextureLevel(*entry, replaced, 0, maxLevel, scaleFactor, dstFmt);
	}

	ID3D11ShaderResourceView *textureView = DxView(entry);
	if (!textureView) {
		return;
	}

	// Mipmapping is only enabled when texture scaling is disabled.
	if (maxLevel > 0 && scaleFactor == 1) {
		for (int i = 1; i <= maxLevel; i++) {
			LoadTextureLevel(*entry, replaced, i, maxLevel, scaleFactor, dstFmt);
		}
	}

	if (maxLevel == 0) {
		entry->status |= TexCacheEntry::STATUS_BAD_MIPS;
	} else {
		entry->status &= ~TexCacheEntry::STATUS_BAD_MIPS;
	}
	if (replaced.Valid()) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(replaced.AlphaStatus()));
	}
}

DXGI_FORMAT GetClutDestFormatD3D11(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return DXGI_FORMAT_B4G4R4A4_UNORM;
	case GE_CMODE_16BIT_ABGR5551:
		return DXGI_FORMAT_B5G5R5A1_UNORM;
	case GE_CMODE_16BIT_BGR5650:
		return DXGI_FORMAT_B5G6R5_UNORM;
	case GE_CMODE_32BIT_ABGR8888:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	}
	// Should never be here !
	return DXGI_FORMAT_B8G8R8A8_UNORM;
}

DXGI_FORMAT TextureCacheD3D11::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	if (!gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS)) {
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	}

	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return GetClutDestFormatD3D11(clutFormat);
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
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	}
}

TexCacheEntry::TexStatus TextureCacheD3D11::CheckAlpha(const u32 *pixelData, u32 dstFmt, int stride, int w, int h) {
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

	return (TexCacheEntry::TexStatus)res;
}

ReplacedTextureFormat FromD3D11Format(u32 fmt) {
	switch (fmt) {
	case DXGI_FORMAT_B5G6R5_UNORM: return ReplacedTextureFormat::F_5650;
	case DXGI_FORMAT_B5G5R5A1_UNORM: return ReplacedTextureFormat::F_5551;
	case DXGI_FORMAT_B4G4R4A4_UNORM: return ReplacedTextureFormat::F_4444;
	case DXGI_FORMAT_B8G8R8A8_UNORM: default: return ReplacedTextureFormat::F_8888;
	}
}

DXGI_FORMAT ToDXGIFormat(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650: return DXGI_FORMAT_B5G6R5_UNORM;
	case ReplacedTextureFormat::F_5551: return DXGI_FORMAT_B5G5R5A1_UNORM;
	case ReplacedTextureFormat::F_4444: return DXGI_FORMAT_B4G4R4A4_UNORM;
	case ReplacedTextureFormat::F_8888: default: return DXGI_FORMAT_B8G8R8A8_UNORM;
	}
}

void TextureCacheD3D11::LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, int scaleFactor, DXGI_FORMAT dstFmt) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	ID3D11Texture2D *texture = DxTex(&entry);
	if ((level == 0 || IsFakeMipmapChange()) && texture == nullptr) {
		// Create texture
		int levels = scaleFactor == 1 ? maxLevel + 1 : 1;
		int tw = w, th = h;
		DXGI_FORMAT tfmt = dstFmt;
		if (replaced.GetSize(level, tw, th)) {
			tfmt = ToDXGIFormat(replaced.Format(level));
		} else {
			tw *= scaleFactor;
			th *= scaleFactor;
			if (scaleFactor > 1) {
				tfmt = DXGI_FORMAT_B8G8R8A8_UNORM;
			}
		}

		D3D11_TEXTURE2D_DESC desc{};
		// TODO: Make it DEFAULT or immutable, required for multiple mip levels. Will require some code restructuring though.
		desc.CPUAccessFlags = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Width = tw;
		desc.Height = th;
		desc.Format = tfmt;
		desc.MipLevels = IsFakeMipmapChange() ? 1 : levels;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		ASSERT_SUCCESS(device_->CreateTexture2D(&desc, nullptr, &texture));
		ID3D11ShaderResourceView *view;
		ASSERT_SUCCESS(device_->CreateShaderResourceView(texture, nullptr, &view));
		entry.texturePtr = texture;
		entry.textureView = view;
	}

	gpuStats.numTexturesDecoded++;
	// For UpdateSubresource, we can't decode directly into the texture so we allocate a buffer :(
	u32 *mapData = nullptr;
	int mapRowPitch = 0;
	if (replaced.GetSize(level, w, h)) {
		mapData = (u32 *)AllocateAlignedMemory(w * h * sizeof(u32), 16);
		mapRowPitch = w * 4;
		replaced.Load(level, mapData, mapRowPitch);
		dstFmt = ToDXGIFormat(replaced.Format(level));
	} else {
		GETextureFormat tfmt = (GETextureFormat)entry.format;
		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		u32 texaddr = gstate.getTextureAddress(level);
		int bufw = GetTextureBufw(level, texaddr, tfmt);
		int bpp = dstFmt == DXGI_FORMAT_B8G8R8A8_UNORM ? 4 : 2;
		u32 *pixelData;
		int decPitch;
		if (scaleFactor > 1) {
			tmpTexBufRearrange_.resize(std::max(bufw, w) * h);
			pixelData = tmpTexBufRearrange_.data();
			// We want to end up with a neatly packed texture for scaling.
			decPitch = w * bpp;
			mapData = (u32 *)AllocateAlignedMemory(sizeof(u32) * (w * scaleFactor) * (h * scaleFactor), 16);
			mapRowPitch = w * scaleFactor * 4;
		} else {
			mapRowPitch = std::max(bufw, w) * bpp;
			size_t bufSize = sizeof(u32) * (mapRowPitch / bpp) * h;
			mapData = (u32 *)AllocateAlignedMemory(bufSize, 16);
			if (!mapData) {
				ERROR_LOG(G3D, "Ran out of RAM trying to allocate a temporary texture upload buffer (alloc size: %d, %dx%d)", bufSize, mapRowPitch / sizeof(u32), h);
				return;
			}
			pixelData = (u32 *)mapData;
			decPitch = mapRowPitch;
		}

		bool expand32 = !gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS);
		DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, level, bufw, false, false, expand32);

		// We check before scaling since scaling shouldn't invent alpha from a full alpha texture.
		if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
			TexCacheEntry::TexStatus alphaStatus = CheckAlpha(pixelData, dstFmt, decPitch / bpp, w, h);
			entry.SetAlphaStatus(alphaStatus, level);
		} else {
			entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
		}

		if (scaleFactor > 1) {
			u32 scaleFmt = (u32)dstFmt;
			scaler.ScaleAlways((u32 *)mapData, pixelData, scaleFmt, w, h, scaleFactor);
			pixelData = (u32 *)mapData;

			// We always end up at 8888.  Other parts assume this.
			assert(scaleFmt == DXGI_FORMAT_B8G8R8A8_UNORM);
			bpp = sizeof(u32);
			decPitch = w * bpp;

			if (decPitch != mapRowPitch) {
				// Rearrange in place to match the requested pitch.
				// (it can only be larger than w * bpp, and a match is likely.)
				// Note! This is bad because it reads the mapped memory! TODO: Look into if DX9 does this right.
				for (int y = h - 1; y >= 0; --y) {
					memcpy((u8 *)mapData + mapRowPitch * y, (u8 *)mapData + decPitch * y, w * bpp);
				}
				decPitch = mapRowPitch;
			}
		}

		if (replacer_.Enabled()) {
			ReplacedTextureDecodeInfo replacedInfo;
			replacedInfo.cachekey = entry.CacheKey();
			replacedInfo.hash = entry.fullhash;
			replacedInfo.addr = entry.addr;
			replacedInfo.isVideo = videos_.find(entry.addr & 0x3FFFFFFF) != videos_.end();
			replacedInfo.isFinal = (entry.status & TexCacheEntry::STATUS_TO_SCALE) == 0;
			replacedInfo.scaleFactor = scaleFactor;
			replacedInfo.fmt = FromD3D11Format(dstFmt);

			replacer_.NotifyTextureDecoded(replacedInfo, pixelData, decPitch, level, w, h);
		}
	}

	if (IsFakeMipmapChange())
		context_->UpdateSubresource(texture, 0, nullptr, mapData, mapRowPitch, 0);
	else
		context_->UpdateSubresource(texture, level, nullptr, mapData, mapRowPitch, 0);
	FreeAlignedMemory(mapData);
}

bool TextureCacheD3D11::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) {
	SetTexture(false);
	if (!nextTexture_)
		return false;

	// Apply texture may need to rebuild the texture if we're about to render, or bind a framebuffer.
	TexCacheEntry *entry = nextTexture_;
	ApplyTexture();

	// TODO: Centralize.
	if (entry->framebuffer) {
		VirtualFramebuffer *vfb = entry->framebuffer;
		buffer.Allocate(vfb->bufferWidth, vfb->bufferHeight, GPU_DBG_FORMAT_8888, false);
		bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, 0, 0, vfb->bufferWidth, vfb->bufferHeight, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), vfb->bufferWidth);
		// Vulkan requires us to re-apply all dynamic state for each command buffer, and the above will cause us to start a new cmdbuf.
		// So let's dirty the things that are involved in Vulkan dynamic state. Readbacks are not frequent so this won't hurt other backends.
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
		// We may have blitted to a temp FBO.
		framebufferManager_->RebindFramebuffer();
		return retval;
	}

	ID3D11Texture2D *texture = (ID3D11Texture2D *)entry->texturePtr;
	if (!texture)
		return false;

	D3D11_TEXTURE2D_DESC desc;
	texture->GetDesc(&desc);

	if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
		// TODO: Support the other formats
		return false;
	}

	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D *stagingCopy = nullptr;
	device_->CreateTexture2D(&desc, nullptr, &stagingCopy);
	context_->CopyResource(stagingCopy, texture);

	int width = desc.Width >> level;
	int height = desc.Height >> level;
	buffer.Allocate(width, height, GPU_DBG_FORMAT_8888);

	D3D11_MAPPED_SUBRESOURCE map;
	if (FAILED(context_->Map(stagingCopy, level, D3D11_MAP_READ, 0, &map))) {
		stagingCopy->Release();
		return false;
	}

	for (int y = 0; y < height; y++) {
		memcpy(buffer.GetData() + 4 * width * y, (const uint8_t *)map.pData + map.RowPitch * y, 4 * width);
	}

	context_->Unmap(stagingCopy, level);
	stagingCopy->Release();
	return true;
}
