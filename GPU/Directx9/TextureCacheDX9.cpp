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

#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/DepalettizeShaderDX9.h"
#include "gfx/d3d9_state.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "Core/Config.h"
#include "Core/Host.h"

#include "ext/xxhash.h"
#include "math/math_util.h"


namespace DX9 {

#define INVALID_TEX (LPDIRECT3DTEXTURE9)(-1)

static const D3DVERTEXELEMENT9 g_FramebufferVertexElements[] = {
	{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	D3DDECL_END()
};

TextureCacheDX9::TextureCacheDX9(Draw::DrawContext *draw)
	: TextureCacheCommon(draw) {
	lastBoundTexture = INVALID_TEX;
	isBgraBackend_ = true;

	device_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	deviceEx_ = (LPDIRECT3DDEVICE9EX)draw->GetNativeObject(Draw::NativeObject::DEVICE_EX);
	D3DCAPS9 pCaps;
	ZeroMemory(&pCaps, sizeof(pCaps));
	HRESULT result = 0;
	if (deviceEx_) {
		result = deviceEx_->GetDeviceCaps(&pCaps);
	} else {
		result = device_->GetDeviceCaps(&pCaps);
	}
	if (FAILED(result)) {
		WARN_LOG(G3D, "Failed to get the device caps!");
		maxAnisotropyLevel = 16;
	} else {
		maxAnisotropyLevel = pCaps.MaxAnisotropy;
	}
	SetupTextureDecoder();

	nextTexture_ = nullptr;
	device_->CreateVertexDeclaration(g_FramebufferVertexElements, &pFramebufferVertexDecl);
}

TextureCacheDX9::~TextureCacheDX9() {
	pFramebufferVertexDecl->Release();
	Clear(true);
}

void TextureCacheDX9::SetFramebufferManager(FramebufferManagerDX9 *fbManager) {
	framebufferManagerDX9_ = fbManager;
	framebufferManager_ = fbManager;
}

void TextureCacheDX9::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	DEBUG_LOG(G3D, "Deleting texture %p", entry->texturePtr);
	LPDIRECT3DTEXTURE9 &texture = DxTex(entry);
	if (texture) {
		texture->Release();
		texture = nullptr;
	}
}

void TextureCacheDX9::ForgetLastTexture() {
	InvalidateLastTexture();
	gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
}

void TextureCacheDX9::InvalidateLastTexture(TexCacheEntry *entry) {
	if (!entry || entry->texturePtr == lastBoundTexture) {
		lastBoundTexture = INVALID_TEX;
	}
}

D3DFORMAT getClutDestFormat(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return D3DFMT_A4R4G4B4;
	case GE_CMODE_16BIT_ABGR5551:
		return D3DFMT_A1R5G5B5;
	case GE_CMODE_16BIT_BGR5650:
		return D3DFMT_R5G6B5;
	case GE_CMODE_32BIT_ABGR8888:
		return D3DFMT_A8R8G8B8;
	}
	// Should never be here !
	return D3DFMT_A8R8G8B8;
}

static const u8 MinFilt[8] = {
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
	D3DTEXF_POINT,	// GL_NEAREST_MIPMAP_NEAREST,
	D3DTEXF_LINEAR,	// GL_LINEAR_MIPMAP_NEAREST,
	D3DTEXF_POINT,	// GL_NEAREST_MIPMAP_LINEAR,
	D3DTEXF_LINEAR,	// GL_LINEAR_MIPMAP_LINEAR,
};

static const u8 MipFilt[8] = {
	D3DTEXF_NONE,
	D3DTEXF_NONE,
	D3DTEXF_NONE,
	D3DTEXF_NONE,
	D3DTEXF_POINT,	// GL_NEAREST_MIPMAP_NEAREST,
	D3DTEXF_POINT,	// GL_LINEAR_MIPMAP_NEAREST,
	D3DTEXF_LINEAR,	// GL_NEAREST_MIPMAP_LINEAR,
	D3DTEXF_LINEAR,	// GL_LINEAR_MIPMAP_LINEAR,
};

static const u8 MagFilt[2] = {
	D3DTEXF_POINT,
	D3DTEXF_LINEAR
};

void TextureCacheDX9::UpdateSamplingParams(TexCacheEntry &entry, bool force) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GETexLevelMode mode;
	u8 maxLevel = (entry.status & TexCacheEntry::STATUS_BAD_MIPS) ? 0 : entry.maxLevel;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, maxLevel, entry.addr, mode);

	if (maxLevel != 0) {
		if (mode == GE_TEXLEVEL_MODE_AUTO) {
			dxstate.texMaxMipLevel.set(0);
			dxstate.texMipLodBias.set(lodBias);
		} else if (mode == GE_TEXLEVEL_MODE_CONST) {
			// TODO: This is just an approximation - texMaxMipLevel sets the lowest numbered mip to use.
			// Unfortunately, this doesn't support a const 1.5 or etc.
			dxstate.texMaxMipLevel.set(std::max(0, std::min((int)maxLevel, (int)lodBias)));
			dxstate.texMipLodBias.set(-1000.0f);
		} else {  // if (mode == GE_TEXLEVEL_MODE_SLOPE{
			dxstate.texMaxMipLevel.set(0);
			dxstate.texMipLodBias.set(0.0f);
		}
	} else {
		dxstate.texMaxMipLevel.set(0);
		dxstate.texMipLodBias.set(0.0f);
	}

	D3DTEXTUREFILTERTYPE minf = (D3DTEXTUREFILTERTYPE)MinFilt[minFilt];
	D3DTEXTUREFILTERTYPE mipf = (D3DTEXTUREFILTERTYPE)MipFilt[minFilt];
	D3DTEXTUREFILTERTYPE magf = (D3DTEXTUREFILTERTYPE)MagFilt[magFilt];

	if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY) && g_Config.iAnisotropyLevel > 0 && minf == D3DTEXF_LINEAR) {
		minf = D3DTEXF_ANISOTROPIC;
	}

	dxstate.texMinFilter.set(minf);
	dxstate.texMipFilter.set(mipf);
	dxstate.texMagFilter.set(magf);
	dxstate.texAddressU.set(sClamp ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
	dxstate.texAddressV.set(tClamp ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
}

void TextureCacheDX9::SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GETexLevelMode mode;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0, 0, mode);

	dxstate.texMinFilter.set(MinFilt[minFilt]);
	dxstate.texMipFilter.set(MipFilt[minFilt]);
	dxstate.texMagFilter.set(MagFilt[magFilt]);
	dxstate.texMipLodBias.set(0.0f);
	dxstate.texMaxMipLevel.set(0.0f);

	// Often the framebuffer will not match the texture size.  We'll wrap/clamp in the shader in that case.
	// This happens whether we have OES_texture_npot or not.
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (w != bufferWidth || h != bufferHeight) {
		return;
	}

	dxstate.texAddressU.set(sClamp ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
	dxstate.texAddressV.set(tClamp ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
}

void TextureCacheDX9::StartFrame() {
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

	if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY)) {
		DWORD aniso = 1 << g_Config.iAnisotropyLevel;
		DWORD anisotropyLevel = aniso > maxAnisotropyLevel ? maxAnisotropyLevel : aniso;
		device_->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, anisotropyLevel);
	}

}

void TextureCacheDX9::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
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

void TextureCacheDX9::BindTexture(TexCacheEntry *entry) {
	LPDIRECT3DTEXTURE9 texture = DxTex(entry);
	if (texture != lastBoundTexture) {
		device_->SetTexture(0, texture);
		lastBoundTexture = texture;
	}
	UpdateSamplingParams(*entry, false);
}

void TextureCacheDX9::Unbind() {
	device_->SetTexture(0, NULL);
	InvalidateLastTexture();
}

class TextureShaderApplierDX9 {
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

	TextureShaderApplierDX9(LPDIRECT3DDEVICE9 device, LPDIRECT3DPIXELSHADER9 pshader, LPDIRECT3DVERTEXDECLARATION9 decl, float bufferW, float bufferH, int renderW, int renderH, float xoff, float yoff)
		: device_(device), pshader_(pshader), decl_(decl), bufferW_(bufferW), bufferH_(bufferH), renderW_(renderW), renderH_(renderH) {
		static const Pos pos[4] = {
			{-1,  1, 0},
			{ 1,  1, 0},
			{ 1, -1, 0},
			{-1, -1, 0},
		};
		static const UV uv[4] = {
			{0, 0},
			{1, 0},
			{1, 1},
			{0, 1},
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

			float z = 0.0f;
			// Points are: BL, BR, TR, TL.
			verts_[0].pos = Pos(left, bottom, z);
			verts_[1].pos = Pos(right, bottom, z);
			verts_[2].pos = Pos(right, top, z);
			verts_[3].pos = Pos(left, top, z);

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

	void Use(LPDIRECT3DVERTEXSHADER9 vshader) {
		device_->SetPixelShader(pshader_);
		device_->SetVertexShader(vshader);
		device_->SetVertexDeclaration(decl_);
	}

	void Shade() {
		device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		device_->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
		device_->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
		device_->SetRenderState(D3DRS_ZENABLE, FALSE);
		device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		device_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

		D3DVIEWPORT9 vp{ 0, 0, (DWORD)renderW_, (DWORD)renderH_, 0.0f, 1.0f };
		device_->SetViewport(&vp);
		HRESULT hr = device_->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, verts_, (3 + 2) * sizeof(float));
		if (FAILED(hr)) {
			ERROR_LOG_REPORT(G3D, "Depal render failed: %08x", hr);
		}

		dxstate.Restore();
	}

protected:
	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DPIXELSHADER9 pshader_;
	LPDIRECT3DVERTEXDECLARATION9 decl_;
	PosUV verts_[4];
	float bufferW_;
	float bufferH_;
	int renderW_;
	int renderH_;
};

void TextureCacheDX9::ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	LPDIRECT3DPIXELSHADER9 pshader = nullptr;
	uint32_t clutMode = gstate.clutformat & 0xFFFFFF;
	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		pshader = depalShaderCache_->GetDepalettizePixelShader(clutMode, framebuffer->drawnFormat);
	}

	if (pshader) {
		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		LPDIRECT3DTEXTURE9 clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);

		Draw::Framebuffer *depalFBO = framebufferManagerDX9_->GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, Draw::FBO_8888);
		draw_->BindFramebufferAsRenderTarget(depalFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
		shaderManager_->DirtyLastShader();

		float xoff = -0.5f / framebuffer->renderWidth;
		float yoff = 0.5f / framebuffer->renderHeight;

		TextureShaderApplierDX9 shaderApply(device_, pshader, pFramebufferVertexDecl, framebuffer->bufferWidth, framebuffer->bufferHeight, framebuffer->renderWidth, framebuffer->renderHeight, xoff, yoff);
		shaderApply.ApplyBounds(gstate_c.vertBounds, gstate_c.curTextureXOffset, gstate_c.curTextureYOffset, xoff, yoff);
		shaderApply.Use(depalShaderCache_->GetDepalettizeVertexShader());

		device_->SetTexture(1, clutTexture);
		device_->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		device_->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		device_->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

		framebufferManagerDX9_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_SKIP_COPY);
		device_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		device_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		device_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		device_->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0);
		device_->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);

		shaderApply.Shade();

		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::FB_COLOR_BIT, 0);

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TexCacheEntry::TexStatus alphaStatus = CheckAlpha(clutBuf_, getClutDestFormat(clutFormat), clutTotalColors, clutTotalColors, 1);
		gstate_c.SetTextureFullAlpha(alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL);
	} else {
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;

		framebufferManagerDX9_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);

		gstate_c.SetTextureFullAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
	}

	framebufferManagerDX9_->RebindFramebuffer();
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);

	InvalidateLastTexture();
}

void TextureCacheDX9::BuildTexture(TexCacheEntry *const entry, bool replaceImages) {
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

	// If GLES3 is available, we can preallocate the storage, which makes texture loading more efficient.
	D3DFORMAT dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

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
		if (replaceImages) {
			// Since we're replacing the texture, we can't replace the image inside.
			ReleaseTexture(entry, true);
			replaceImages = false;
		}
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

	if (replaceImages) {
		// Make sure it's not currently set.
		device_->SetTexture(0, NULL);
	}
	// Seems to cause problems in Tactics Ogre.
	if (badMipSizes) {
		maxLevel = 0;
	}

	if (IsFakeMipmapChange()) {
		// NOTE: Since the level is not part of the cache key, we assume it never changes.
		u8 level = std::max(0, gstate.getTexLevelOffset16() / 16);
		LoadTextureLevel(*entry, replaced, level, maxLevel, replaceImages, scaleFactor, dstFmt);
	} else {
		LoadTextureLevel(*entry, replaced, 0, maxLevel, replaceImages, scaleFactor, dstFmt);
	}
	LPDIRECT3DTEXTURE9 &texture = DxTex(entry);
	if (!texture) {
		return;
	}

	// Mipmapping is only enabled when texture scaling is disabled.
	if (maxLevel > 0 && scaleFactor == 1) {
		for (int i = 1; i <= maxLevel; i++) {
			LoadTextureLevel(*entry, replaced, i, maxLevel, replaceImages, scaleFactor, dstFmt);
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

D3DFORMAT TextureCacheDX9::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormat(clutFormat);
	case GE_TFMT_4444:
		return D3DFMT_A4R4G4B4;
	case GE_TFMT_5551:
		return D3DFMT_A1R5G5B5;
	case GE_TFMT_5650:
		return D3DFMT_R5G6B5;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return D3DFMT_A8R8G8B8;
	}
}

TexCacheEntry::TexStatus TextureCacheDX9::CheckAlpha(const u32 *pixelData, u32 dstFmt, int stride, int w, int h) {
	CheckAlphaResult res;
	switch (dstFmt) {
	case D3DFMT_A4R4G4B4:
		res = CheckAlphaRGBA4444Basic(pixelData, stride, w, h);
		break;
	case D3DFMT_A1R5G5B5:
		res = CheckAlphaRGBA5551Basic(pixelData, stride, w, h);
		break;
	case D3DFMT_R5G6B5:
		// Never has any alpha.
		res = CHECKALPHA_FULL;
		break;
	default:
		res = CheckAlphaRGBA8888Basic(pixelData, stride, w, h);
		break;
	}

	return (TexCacheEntry::TexStatus)res;
}

ReplacedTextureFormat FromD3D9Format(u32 fmt) {
	switch (fmt) {
	case D3DFMT_R5G6B5: return ReplacedTextureFormat::F_5650;
	case D3DFMT_A1R5G5B5: return ReplacedTextureFormat::F_5551;
	case D3DFMT_A4R4G4B4: return ReplacedTextureFormat::F_4444;
	case D3DFMT_A8R8G8B8: default: return ReplacedTextureFormat::F_8888;
	}
}

D3DFORMAT ToD3D9Format(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650: return D3DFMT_R5G6B5;
	case ReplacedTextureFormat::F_5551: return D3DFMT_A1R5G5B5;
	case ReplacedTextureFormat::F_4444: return D3DFMT_A4R4G4B4;
	case ReplacedTextureFormat::F_8888: default: return D3DFMT_A8R8G8B8;
	}
}

void TextureCacheDX9::LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, bool replaceImages, int scaleFactor, u32 dstFmt) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	LPDIRECT3DTEXTURE9 &texture = DxTex(&entry);
	if ((level == 0 || IsFakeMipmapChange()) && (!replaceImages || texture == nullptr)) {
		// Create texture
		D3DPOOL pool = D3DPOOL_MANAGED;
		int usage = 0;
		pool = D3DPOOL_DEFAULT;
		usage = D3DUSAGE_DYNAMIC;  // TODO: Switch to using a staging texture?
		int levels = scaleFactor == 1 ? maxLevel + 1 : 1;
		int tw = w, th = h;
		D3DFORMAT tfmt = (D3DFORMAT)(dstFmt);
		if (replaced.GetSize(level, tw, th)) {
			tfmt = ToD3D9Format(replaced.Format(level));
		} else {
			tw *= scaleFactor;
			th *= scaleFactor;
			if (scaleFactor > 1) {
				tfmt = D3DFMT_A8R8G8B8;
			}
		}
		HRESULT hr;
		if (IsFakeMipmapChange())
			hr = device_->CreateTexture(tw, th, 1, usage, tfmt, pool, &texture, NULL);
		else
			hr = device_->CreateTexture(tw, th, levels, usage, tfmt, pool, &texture, NULL);
		if (FAILED(hr)) {
			INFO_LOG(G3D, "Failed to create D3D texture: %dx%d", tw, th);
			ReleaseTexture(&entry, true);
			return;
		}
	}

	D3DLOCKED_RECT rect;

	HRESULT result;
	uint32_t lockFlag = level == 0 ? D3DLOCK_DISCARD : 0;  // Can only discard the top level
	if (IsFakeMipmapChange())
		result = texture->LockRect(0, &rect, NULL, lockFlag);
	else
		result = texture->LockRect(level, &rect, NULL, lockFlag);
	if (FAILED(result)) {
		ERROR_LOG(G3D, "Failed to lock D3D texture: %dx%d", w, h);
		return;
	}

	gpuStats.numTexturesDecoded++;
	if (replaced.GetSize(level, w, h)) {
		replaced.Load(level, rect.pBits, rect.Pitch);
		dstFmt = ToD3D9Format(replaced.Format(level));
	} else {
		GETextureFormat tfmt = (GETextureFormat)entry.format;
		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		u32 texaddr = gstate.getTextureAddress(level);
		int bufw = GetTextureBufw(level, texaddr, tfmt);
		int bpp = dstFmt == D3DFMT_A8R8G8B8 ? 4 : 2;

		u32 *pixelData = (u32 *)rect.pBits;
		int decPitch = rect.Pitch;
		if (scaleFactor > 1) {
			tmpTexBufRearrange_.resize(std::max(bufw, w) * h);
			pixelData = tmpTexBufRearrange_.data();
			// We want to end up with a neatly packed texture for scaling.
			decPitch = w * bpp;
		}

		DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, level, bufw, false, false, false);

		// We check before scaling since scaling shouldn't invent alpha from a full alpha texture.
		if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
			TexCacheEntry::TexStatus alphaStatus = CheckAlpha(pixelData, dstFmt, decPitch / bpp, w, h);
			entry.SetAlphaStatus(alphaStatus, level);
		} else {
			entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
		}

		if (scaleFactor > 1) {
			scaler.ScaleAlways((u32 *)rect.pBits, pixelData, dstFmt, w, h, scaleFactor);
			pixelData = (u32 *)rect.pBits;

			// We always end up at 8888.  Other parts assume this.
			assert(dstFmt == D3DFMT_A8R8G8B8);
			bpp = sizeof(u32);
			decPitch = w * bpp;

			if (decPitch != rect.Pitch) {
				// Rearrange in place to match the requested pitch.
				// (it can only be larger than w * bpp, and a match is likely.)
				for (int y = h - 1; y >= 0; --y) {
					memcpy((u8 *)rect.pBits + rect.Pitch * y, (u8 *)rect.pBits + decPitch * y, w * bpp);
				}
				decPitch = rect.Pitch;
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
			replacedInfo.fmt = FromD3D9Format(dstFmt);

			replacer_.NotifyTextureDecoded(replacedInfo, pixelData, decPitch, level, w, h);
		}
	}

	if (IsFakeMipmapChange())
		texture->UnlockRect(0);
	else
		texture->UnlockRect(level);
}

bool TextureCacheDX9::DecodeTexture(u8 *output, const GPUgstate &state)
{
	OutputDebugStringA("TextureCache::DecodeTexture : FixMe\r\n");
	return true;
}

bool TextureCacheDX9::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) {
	SetTexture(true);
	ApplyTexture();
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	LPDIRECT3DBASETEXTURE9 baseTex;
	LPDIRECT3DTEXTURE9 tex;
	LPDIRECT3DSURFACE9 offscreen = nullptr;
	HRESULT hr;

	bool success = false;
	hr = device_->GetTexture(0, &baseTex);
	if (SUCCEEDED(hr) && baseTex != NULL) {
		hr = baseTex->QueryInterface(IID_IDirect3DTexture9, (void **)&tex);
		if (SUCCEEDED(hr)) {
			D3DSURFACE_DESC desc;
			D3DLOCKED_RECT locked;
			tex->GetLevelDesc(level, &desc);
			RECT rect = { 0, 0, (LONG)desc.Width, (LONG)desc.Height };
			hr = tex->LockRect(level, &locked, &rect, D3DLOCK_READONLY);

			// If it fails, this means it's a render-to-texture, so we have to get creative.
			if (FAILED(hr)) {
				LPDIRECT3DSURFACE9 renderTarget = nullptr;
				hr = tex->GetSurfaceLevel(level, &renderTarget);
				if (renderTarget && SUCCEEDED(hr)) {
					hr = device_->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &offscreen, NULL);
					if (SUCCEEDED(hr)) {
						hr = device_->GetRenderTargetData(renderTarget, offscreen);
						if (SUCCEEDED(hr)) {
							hr = offscreen->LockRect(&locked, &rect, D3DLOCK_READONLY);
						}
					}
					renderTarget->Release();
				}
			}

			if (SUCCEEDED(hr)) {
				GPUDebugBufferFormat fmt;
				int pixelSize;
				switch (desc.Format) {
				case D3DFMT_A1R5G5B5:
					fmt = gstate_c.bgraTexture ? GPU_DBG_FORMAT_5551 : GPU_DBG_FORMAT_5551_BGRA;
					pixelSize = 2;
					break;
				case D3DFMT_A4R4G4B4:
					fmt = gstate_c.bgraTexture ? GPU_DBG_FORMAT_4444 : GPU_DBG_FORMAT_4444_BGRA;
					pixelSize = 2;
					break;
				case D3DFMT_R5G6B5:
					fmt = gstate_c.bgraTexture ? GPU_DBG_FORMAT_565 : GPU_DBG_FORMAT_565_BGRA;
					pixelSize = 2;
					break;
				case D3DFMT_A8R8G8B8:
					fmt = gstate_c.bgraTexture ? GPU_DBG_FORMAT_8888 : GPU_DBG_FORMAT_8888_BGRA;
					pixelSize = 4;
					break;
				default:
					fmt = GPU_DBG_FORMAT_INVALID;
					break;
				}

				if (fmt != GPU_DBG_FORMAT_INVALID) {
					buffer.Allocate(locked.Pitch / pixelSize, desc.Height, fmt, false);
					memcpy(buffer.GetData(), locked.pBits, locked.Pitch * desc.Height);
					success = true;
				} else {
					success = false;
				}
				if (offscreen) {
					offscreen->UnlockRect();
					offscreen->Release();
				} else {
					tex->UnlockRect(level);
				}
			}
			tex->Release();
		}
		baseTex->Release();
	}

	return success;
}

};
