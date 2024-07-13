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

#include <algorithm>
#include <cstring>
#include <cfloat>

#include <d3d11.h>

#include "Common/TimeUtil.h"
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/D3D11/D3D11Util.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "Core/Config.h"

#include "ext/xxhash.h"
#include "Common/Math/math_util.h"

// For depth depal
struct DepthPushConstants {
	float z_scale;
	float z_offset;
	float pad[2];
};

#define INVALID_TEX (ID3D11ShaderResourceView *)(-1LL)

static const D3D11_INPUT_ELEMENT_DESC g_QuadVertexElements[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,},
};

// NOTE: In the D3D backends, we flip R and B in the shaders, so while these look wrong, they're OK.

static Draw::DataFormat FromD3D11Format(u32 fmt) {
	switch (fmt) {
	case DXGI_FORMAT_B4G4R4A4_UNORM: return Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
	case DXGI_FORMAT_B5G5R5A1_UNORM: return Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
	case DXGI_FORMAT_B5G6R5_UNORM: return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case DXGI_FORMAT_R8_UNORM: return Draw::DataFormat::R8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_UNORM: default: return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

static DXGI_FORMAT ToDXGIFormat(Draw::DataFormat fmt) {
	switch (fmt) {
	case Draw::DataFormat::BC1_RGBA_UNORM_BLOCK: return DXGI_FORMAT_BC1_UNORM;
	case Draw::DataFormat::BC2_UNORM_BLOCK: return DXGI_FORMAT_BC2_UNORM;
	case Draw::DataFormat::BC3_UNORM_BLOCK: return DXGI_FORMAT_BC3_UNORM;
	case Draw::DataFormat::BC4_UNORM_BLOCK: return DXGI_FORMAT_BC4_UNORM;
	case Draw::DataFormat::BC5_UNORM_BLOCK: return DXGI_FORMAT_BC5_UNORM;
	case Draw::DataFormat::BC7_UNORM_BLOCK: return DXGI_FORMAT_BC7_UNORM;
	case Draw::DataFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
	default: _dbg_assert_(false); return DXGI_FORMAT_UNKNOWN;
	}
}

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
	// Can't set MaxLOD on Feature Level <= 9_3.
	if (device->GetFeatureLevel() <= D3D_FEATURE_LEVEL_9_3) {
		samp.MaxLOD = FLT_MAX;
		samp.MinLOD = -FLT_MAX;
		samp.MipLODBias = 0.0f;
	} else {
		samp.MaxLOD = key.maxLevel / 256.0f;
		samp.MinLOD = key.minLevel / 256.0f;
		samp.MipLODBias = key.lodBias / 256.0f;
	}
	samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
	for (int i = 0; i < 4; i++) {
		samp.BorderColor[i] = 1.0f;
	}

	ID3D11SamplerState *sampler;
	ASSERT_SUCCESS(device->CreateSamplerState(&samp, &sampler));
	cache_[key] = sampler;
	return sampler;
}

TextureCacheD3D11::TextureCacheD3D11(Draw::DrawContext *draw, Draw2D *draw2D)
	: TextureCacheCommon(draw, draw2D) {
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);

	lastBoundTexture = INVALID_TEX;

	D3D11_BUFFER_DESC desc{ sizeof(DepthPushConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
	HRESULT hr = device_->CreateBuffer(&desc, nullptr, &depalConstants_);
	_dbg_assert_(SUCCEEDED(hr));

	HRESULT result = 0;

	nextTexture_ = nullptr;
}

TextureCacheD3D11::~TextureCacheD3D11() {
	depalConstants_->Release();

	// pFramebufferVertexDecl->Release();
	Clear(true);
}

void TextureCacheD3D11::SetFramebufferManager(FramebufferManagerD3D11 *fbManager) {
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
	lastBoundTexture = INVALID_TEX;

	ID3D11ShaderResourceView *nullTex[4]{};
	context_->PSSetShaderResources(0, 4, nullTex);
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

	if (replacer_.Enabled())
		clutHash_ = XXH32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);
	else
		clutHash_ = XXH3_64bits((const char *)clutBufRaw_, clutExtendedBytes) & 0xFFFFFFFF;
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
	if (!entry) {
		ID3D11ShaderResourceView *textureView = nullptr;
		context_->PSSetShaderResources(0, 1, &textureView);
		return;
	}
	ID3D11ShaderResourceView *textureView = DxView(entry);
	if (textureView != lastBoundTexture) {
		context_->PSSetShaderResources(0, 1, &textureView);
		lastBoundTexture = textureView;
	}
	int maxLevel = (entry->status & TexCacheEntry::STATUS_NO_MIPS) ? 0 : entry->maxLevel;
	SamplerCacheKey samplerKey = GetSamplingParams(maxLevel, entry);
	ID3D11SamplerState *state = samplerCache_.GetOrCreateSampler(device_, samplerKey);
	context_->PSSetSamplers(0, 1, &state);
	gstate_c.SetUseShaderDepal(ShaderDepalMode::OFF);
}

void TextureCacheD3D11::ApplySamplingParams(const SamplerCacheKey &key) {
	ID3D11SamplerState *state = samplerCache_.GetOrCreateSampler(device_, key);
	context_->PSSetSamplers(0, 1, &state);
}

void TextureCacheD3D11::Unbind() {
	ForgetLastTexture();
}

void TextureCacheD3D11::BindAsClutTexture(Draw::Texture *tex, bool smooth) {
	ID3D11ShaderResourceView *clutTexture = (ID3D11ShaderResourceView *)draw_->GetNativeObject(Draw::NativeObject::TEXTURE_VIEW, tex);
	context_->PSSetShaderResources(TEX_SLOT_CLUT, 1, &clutTexture);
	context_->PSSetSamplers(3, 1, smooth ? &stockD3D11.samplerLinear2DClamp : &stockD3D11.samplerPoint2DClamp);
}

void TextureCacheD3D11::BuildTexture(TexCacheEntry *const entry) {
	BuildTexturePlan plan;
	if (!PrepareBuildTexture(plan, entry)) {
		// We're screwed?
		return;
	}

	DXGI_FORMAT dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());
	if (plan.doReplace) {
		dstFmt = ToDXGIFormat(plan.replaced->Format());
	} else if (plan.scaleFactor > 1 || plan.saveTexture) {
		dstFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
	} else if (plan.decodeToClut8) {
		dstFmt = DXGI_FORMAT_R8_UNORM;
	}

	int levels;

	ID3D11ShaderResourceView *view;
	ID3D11Resource *texture = DxTex(entry);
	_assert_(texture == nullptr);

	// The PSP only supports 8 mip levels, but we support more in the texture replacer. 20 will never run out.
	D3D11_SUBRESOURCE_DATA subresData[20]{};

	if (plan.depth == 1) {
		// We don't yet have mip generation, so clamp the number of levels to the ones we can load directly.
		levels = std::min(plan.levelsToCreate, plan.levelsToLoad);
	} else {
		levels = plan.depth;
	}

	Draw::DataFormat texFmt = FromD3D11Format(dstFmt);

	for (int i = 0; i < levels; i++) {
		int srcLevel = (i == 0) ? plan.baseLevelSrc : i;

		int mipWidth;
		int mipHeight;
		plan.GetMipSize(i, &mipWidth, &mipHeight);

		u8 *data = nullptr;
		int stride = 0;

		int dataSize;
		if (plan.doReplace) {
			int blockSize = 0;
			if (Draw::DataFormatIsBlockCompressed(plan.replaced->Format(), &blockSize)) {
				stride = ((mipWidth + 3) & ~3) * blockSize / 4;  // Number of blocks * 4 * Size of a block / 4
				dataSize = plan.replaced->GetLevelDataSizeAfterCopy(i);
			} else {
				int bpp = (int)Draw::DataFormatSizeInBytes(plan.replaced->Format());
				stride = std::max(mipWidth * bpp, 16);
				dataSize = stride * mipHeight;
			}
		} else {
			int bpp = 0;
			if (plan.scaleFactor > 1) {
				bpp = 4;
			} else {
				bpp = dstFmt == DXGI_FORMAT_B8G8R8A8_UNORM ? 4 : 2;
			}
			stride = std::max(mipWidth * bpp, 16);
			dataSize = stride * mipHeight;
		}

		if (plan.depth == 1) {
			data = (u8 *)AllocateAlignedMemory(dataSize, 16);
			subresData[i].pSysMem = data;
			subresData[i].SysMemPitch = stride;
			subresData[i].SysMemSlicePitch = 0;
		} else {
			if (i == 0) {
				subresData[0].pSysMem = AllocateAlignedMemory(stride * mipHeight * plan.depth, 16);
				subresData[0].SysMemPitch = stride;
				subresData[0].SysMemSlicePitch = stride * mipHeight;
			}
			data = (uint8_t *)subresData[0].pSysMem + stride * mipHeight * i;
		}

		if (!data) {
			ERROR_LOG(Log::G3D, "Ran out of RAM trying to allocate a temporary texture upload buffer (%dx%d)", mipWidth, mipHeight);
			return;
		}

		LoadTextureLevel(*entry, data, 0, stride, plan, srcLevel, texFmt, TexDecodeFlags{});
	}

	int tw;
	int th;
	plan.GetMipSize(0, &tw, &th);
	if (tw > 16384)
		tw = 16384;
	if (th > 16384)
		th = 16384;

	// NOTE: For block-compressed textures, we'll force the size up to the closest 4x4. This is due to an
	// unfortunate restriction in D3D11 (and early D3D12). We'll warn about it in the log to give texture pack
	// authors notice to fix it.
	if (plan.doReplace && Draw::DataFormatIsBlockCompressed(plan.replaced->Format(), nullptr)) {
		tw = (tw + 3) & ~3;
		th = (th + 3) & ~3;
	}

	if (plan.depth == 1) {
		// We don't yet have mip generation, so clamp the number of levels to the ones we can load directly.
		levels = std::min(plan.levelsToCreate, plan.levelsToLoad);

		ID3D11Texture2D *tex = nullptr;
		D3D11_TEXTURE2D_DESC desc{};
		desc.CPUAccessFlags = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Width = tw;
		desc.Height = th;
		desc.Format = dstFmt;
		desc.MipLevels = levels;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ASSERT_SUCCESS(device_->CreateTexture2D(&desc, subresData, &tex));
		texture = tex;
	} else {
		ID3D11Texture3D *tex = nullptr;
		D3D11_TEXTURE3D_DESC desc{};
		desc.CPUAccessFlags = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.Width = tw;
		desc.Height = th;
		desc.Depth = plan.depth;
		desc.Format = dstFmt;
		desc.MipLevels = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ASSERT_SUCCESS(device_->CreateTexture3D(&desc, subresData, &tex));
		texture = tex;

		levels = plan.depth;
	}

	ASSERT_SUCCESS(device_->CreateShaderResourceView(texture, nullptr, &view));
	entry->texturePtr = texture;
	entry->textureView = view;

	for (int i = 0; i < 12; i++) {
		if (subresData[i].pSysMem) {
			FreeAlignedMemory((void *)subresData[i].pSysMem);
		}
	}

	// Signal that we support depth textures so use it as one.
	if (plan.depth > 1) {
		entry->status |= TexCacheEntry::STATUS_3D;
	}

	if (levels == 1) {
		entry->status |= TexCacheEntry::STATUS_NO_MIPS;
	} else {
		entry->status &= ~TexCacheEntry::STATUS_NO_MIPS;
	}

	if (plan.doReplace) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(plan.replaced->AlphaStatus()));

		if (!Draw::DataFormatIsBlockCompressed(plan.replaced->Format(), nullptr)) {
			entry->status |= TexCacheEntry::STATUS_BGRA;
		}
	} else {
		entry->status |= TexCacheEntry::STATUS_BGRA;
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
	if (!gstate_c.Use(GPU_USE_16BIT_FORMATS)) {
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

bool TextureCacheD3D11::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) {
	SetTexture();
	if (!nextTexture_) {
		return GetCurrentFramebufferTextureDebug(buffer, isFramebuffer);
	}

	// Apply texture may need to rebuild the texture if we're about to render, or bind a framebuffer.
	TexCacheEntry *entry = nextTexture_;
	ApplyTexture();

	ID3D11Texture2D *texture = (ID3D11Texture2D *)entry->texturePtr;
	if (!texture)
		return false;

	D3D11_TEXTURE2D_DESC desc;
	texture->GetDesc(&desc);

	int width = desc.Width >> level;
	int height = desc.Height >> level;

	switch (desc.Format) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		buffer.Allocate(width, height, GPU_DBG_FORMAT_8888);
		break;

	case DXGI_FORMAT_B5G6R5_UNORM:
		buffer.Allocate(width, height, GPU_DBG_FORMAT_565);
		break;

	case DXGI_FORMAT_B4G4R4A4_UNORM:
		buffer.Allocate(width, height, GPU_DBG_FORMAT_4444);
		break;

	case DXGI_FORMAT_B5G5R5A1_UNORM:
		buffer.Allocate(width, height, GPU_DBG_FORMAT_5551);
		break;

	case DXGI_FORMAT_R8_UNORM:
		buffer.Allocate(width, height, GPU_DBG_FORMAT_8BIT);
		break;

	default:
		return false;
	}

	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D *stagingCopy = nullptr;
	device_->CreateTexture2D(&desc, nullptr, &stagingCopy);
	if (!stagingCopy)
		return false;
	context_->CopyResource(stagingCopy, texture);

	D3D11_MAPPED_SUBRESOURCE map;
	if (FAILED(context_->Map(stagingCopy, level, D3D11_MAP_READ, 0, &map))) {
		stagingCopy->Release();
		return false;
	}

	int bufferRowSize = buffer.PixelSize() * width;
	for (int y = 0; y < height; y++) {
		memcpy(buffer.GetData() + bufferRowSize * y, (const uint8_t *)map.pData + map.RowPitch * y, bufferRowSize);
	}

	context_->Unmap(stagingCopy, level);
	stagingCopy->Release();
	*isFramebuffer = false;
	return true;
}

void *TextureCacheD3D11::GetNativeTextureView(const TexCacheEntry *entry) {
	ID3D11ShaderResourceView *textureView = DxView(entry);
	return (void *)textureView;
}
