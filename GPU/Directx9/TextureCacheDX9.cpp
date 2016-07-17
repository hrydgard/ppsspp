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
#include "GPU/Directx9/helper/dx_state.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "Core/Config.h"
#include "Core/Host.h"

#include "ext/xxhash.h"
#include "math/math_util.h"


namespace DX9 {

#define INVALID_TEX (LPDIRECT3DTEXTURE9)(-1)

// If a texture hasn't been seen for this many frames, get rid of it.
#define TEXTURE_KILL_AGE 200
#define TEXTURE_KILL_AGE_LOWMEM 60
// Not used in lowmem mode.
#define TEXTURE_SECOND_KILL_AGE 100

// Try to be prime to other decimation intervals.
#define TEXCACHE_DECIMATION_INTERVAL 13

// Changes more frequent than this will be considered "frequent" and prevent texture scaling.
#define TEXCACHE_FRAME_CHANGE_FREQUENT 6
// Note: only used when hash backoff is disabled.
#define TEXCACHE_FRAME_CHANGE_FREQUENT_REGAIN_TRUST 33

#define TEXCACHE_MAX_TEXELS_SCALED (256*256)  // Per frame

#define TEXCACHE_MIN_PRESSURE 16 * 1024 * 1024  // Total in VRAM
#define TEXCACHE_SECOND_MIN_PRESSURE 4 * 1024 * 1024

TextureCacheDX9::TextureCacheDX9() : secondCacheSizeEstimate_(0), clearCacheNextFrame_(false), lowMemoryMode_(false), texelsScaledThisFrame_(0) {
	timesInvalidatedAllThisFrame_ = 0;
	lastBoundTexture = INVALID_TEX;
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;

	D3DCAPS9 pCaps;
	ZeroMemory(&pCaps, sizeof(pCaps));
	HRESULT result = 0;
	if (pD3DdeviceEx) {
		result = pD3DdeviceEx->GetDeviceCaps(&pCaps);
	} else {
		result = pD3Ddevice->GetDeviceCaps(&pCaps);
	}
	if (FAILED(result)) {
		WARN_LOG(G3D, "Failed to get the device caps!");
		maxAnisotropyLevel = 16;
	} else {
		maxAnisotropyLevel = pCaps.MaxAnisotropy;
	}
	SetupTextureDecoder();

	nextTexture_ = nullptr;
}

TextureCacheDX9::~TextureCacheDX9() {
	Clear(true);
}

void TextureCacheDX9::Clear(bool delete_them) {
	pD3Ddevice->SetTexture(0, NULL);
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

void TextureCacheDX9::DeleteTexture(TexCache::iterator it) {
	ReleaseTexture(&it->second);
	auto fbInfo = fbTexInfo_.find(it->first);
	if (fbInfo != fbTexInfo_.end()) {
		fbTexInfo_.erase(fbInfo);
	}

	cacheSizeEstimate_ -= EstimateTexMemoryUsage(&it->second);
	cache.erase(it);
}

void TextureCacheDX9::ForgetLastTexture() {
	lastBoundTexture = INVALID_TEX;
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

// Removes old textures.
void TextureCacheDX9::Decimate() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	if (cacheSizeEstimate_ >= TEXCACHE_MIN_PRESSURE) {
		const u32 had = cacheSizeEstimate_;

		pD3Ddevice->SetTexture(0, NULL);
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

void TextureCacheDX9::Invalidate(u32 addr, int size, GPUInvalidationType type) {
	// If we're hashing every use, without backoff, then this isn't needed.
	if (!g_Config.bTextureBackoffCache) {
		return;
	}

	addr &= 0x0FFFFFFF;
	u32 addr_end = addr + size;

	// They could invalidate inside the texture, let's just give a bit of leeway.
	const int LARGEST_TEXTURE_SIZE = 512 * 512 * 4;
	const u64 startKey = (u64)(addr - LARGEST_TEXTURE_SIZE) << 32;
	u64 endKey = (u64)(addr + size + LARGEST_TEXTURE_SIZE) << 32;
	if (endKey < startKey) {
		endKey = (u64)-1;
	}

	for (TexCache::iterator iter = cache.lower_bound(startKey), end = cache.upper_bound(endKey); iter != end; ++iter) {
		u32 texAddr = iter->second.addr;
		u32 texEnd = iter->second.addr + iter->second.sizeInRAM;

		if (texAddr < addr_end && addr < texEnd) {
			if (iter->second.GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
				iter->second.SetHashStatus(TexCacheEntry::STATUS_HASHING);
			}
			if (type != GPU_INVALIDATE_ALL) {
				gpuStats.numTextureInvalidations++;
				// Start it over from 0 (unless it's safe.)
				iter->second.numFrames = type == GPU_INVALIDATE_SAFE ? 256 : 0;
				if (type == GPU_INVALIDATE_SAFE) {
					u32 diff = gpuStats.numFlips - iter->second.lastFrame;
					// We still need to mark if the texture is frequently changing, even if it's safely changing.
					if (diff < TEXCACHE_FRAME_CHANGE_FREQUENT) {
						iter->second.status |= TexCacheEntry::STATUS_CHANGE_FREQUENT;
					}
				}
				iter->second.framesUntilNextFullHash = 0;
			} else if (!iter->second.framebuffer) {
				iter->second.invalidHint++;
			}
		}
	}
}

void TextureCacheDX9::InvalidateAll(GPUInvalidationType /*unused*/) {
	// If we're hashing every use, without backoff, then this isn't needed.
	if (!g_Config.bTextureBackoffCache) {
		return;
	}

	if (timesInvalidatedAllThisFrame_ > 5) {
		return;
	}
	timesInvalidatedAllThisFrame_++;

	for (TexCache::iterator iter = cache.begin(), end = cache.end(); iter != end; ++iter) {
		if (iter->second.GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
			iter->second.SetHashStatus(TexCacheEntry::STATUS_HASHING);
		}
		if (!iter->second.framebuffer) {
			iter->second.invalidHint++;
		}
	}
}

void TextureCacheDX9::ClearNextFrame() {
	clearCacheNextFrame_ = true;
}

bool TextureCacheDX9::AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset) {
	static const u32 MAX_SUBAREA_Y_OFFSET_SAFE = 32;

	AttachedFramebufferInfo fbInfo = {0};

	const u64 mirrorMask = 0x00600000;
	// Must be in VRAM so | 0x04000000 it is.  Also, ignore memory mirrors.
	const u32 addr = (address | 0x04000000) & 0x3FFFFFFF & ~mirrorMask;
	const u32 texaddr = ((entry->addr + texaddrOffset) & ~mirrorMask);
	const bool noOffset = texaddr == addr;
	const bool exactMatch = noOffset && entry->format < 4;
	const u32 h = 1 << ((entry->dim >> 8) & 0xf);
	// 512 on a 272 framebuffer is sane, so let's be lenient.
	const u32 minSubareaHeight = h / 4;

	// If they match exactly, it's non-CLUT and from the top left.
	if (exactMatch) {
		// Apply to non-buffered and buffered mode only.
		if (!(g_Config.iRenderingMode == FB_NON_BUFFERED_MODE || g_Config.iRenderingMode == FB_BUFFERED_MODE))
			return false;

		DEBUG_LOG(G3D, "Render to texture detected at %08x!", address);
		if (framebuffer->fb_stride != entry->bufw) {
			WARN_LOG_REPORT_ONCE(diffStrides1, G3D, "Render to texture with different strides %d != %d", entry->bufw, framebuffer->fb_stride);
		}
		if (entry->format != framebuffer->format) {
			WARN_LOG_REPORT_ONCE(diffFormat1, G3D, "Render to texture with different formats %d != %d", entry->format, framebuffer->format);
			// Let's avoid using it when we know the format is wrong.  May be a video/etc. updating memory.
			// However, some games use a different format to clear the buffer.
			if (framebuffer->last_frame_attached + 1 < gpuStats.numFlips) {
				DetachFramebuffer(entry, address, framebuffer);
			}
		} else {
			AttachFramebufferValid(entry, framebuffer, fbInfo);
			return true;
		}
	} else {
		// Apply to buffered mode only.
		if (!(g_Config.iRenderingMode == FB_BUFFERED_MODE))
			return false;

		const bool clutFormat =
			(framebuffer->format == GE_FORMAT_8888 && entry->format == GE_TFMT_CLUT32) ||
			(framebuffer->format != GE_FORMAT_8888 && entry->format == GE_TFMT_CLUT16);

		const u32 bitOffset = (texaddr - addr) * 8;
		const u32 pixelOffset = bitOffset / std::max(1U, (u32)textureBitsPerPixel[entry->format]);
		fbInfo.yOffset = entry->bufw == 0 ? 0 : pixelOffset / entry->bufw;
		fbInfo.xOffset = entry->bufw == 0 ? 0 : pixelOffset % entry->bufw;

		if (framebuffer->fb_stride != entry->bufw) {
			if (noOffset) {
				WARN_LOG_REPORT_ONCE(diffStrides2, G3D, "Render to texture using CLUT with different strides %d != %d", entry->bufw, framebuffer->fb_stride);
			} else {
				// Assume any render-to-tex with different bufw + offset is a render from ram.
				DetachFramebuffer(entry, address, framebuffer);
				return false;
			}
		}

		if (fbInfo.yOffset + minSubareaHeight >= framebuffer->height) {
			// Can't be inside the framebuffer then, ram.  Detach to be safe.
			DetachFramebuffer(entry, address, framebuffer);
			return false;
		}
		// Trying to play it safe.  Below 0x04110000 is almost always framebuffers.
		// TODO: Maybe we can reduce this check and find a better way above 0x04110000?
		if (fbInfo.yOffset > MAX_SUBAREA_Y_OFFSET_SAFE && addr > 0x04110000) {
			WARN_LOG_REPORT_ONCE(subareaIgnored, G3D, "Ignoring possible render to texture at %08x +%dx%d / %dx%d", address, fbInfo.xOffset, fbInfo.yOffset, framebuffer->width, framebuffer->height);
			DetachFramebuffer(entry, address, framebuffer);
			return false;
		}

		// Check for CLUT. The framebuffer is always RGB, but it can be interpreted as a CLUT texture.
		// 3rd Birthday (and a bunch of other games) render to a 16 bit clut texture.
		if (clutFormat) {
			if (!noOffset) {
				WARN_LOG_REPORT_ONCE(subareaClut, G3D, "Render to texture using CLUT with offset at %08x +%dx%d", address, fbInfo.xOffset, fbInfo.yOffset);
			}
			AttachFramebufferValid(entry, framebuffer, fbInfo);
			entry->status |= TexCacheEntry::STATUS_DEPALETTIZE;
			// We'll validate it compiles later.
			return true;
		} else if (entry->format == GE_TFMT_CLUT8 || entry->format == GE_TFMT_CLUT4) {
			ERROR_LOG_REPORT_ONCE(fourEightBit, G3D, "4 and 8-bit CLUT format not supported for framebuffers");
		}

		// This is either normal or we failed to generate a shader to depalettize
		if (framebuffer->format == entry->format || clutFormat) {
			if (framebuffer->format != entry->format) {
				WARN_LOG_REPORT_ONCE(diffFormat2, G3D, "Render to texture with different formats %d != %d at %08x", entry->format, framebuffer->format, address);
				AttachFramebufferValid(entry, framebuffer, fbInfo);
				return true;
			} else {
				WARN_LOG_REPORT_ONCE(subarea, G3D, "Render to area containing texture at %08x +%dx%d", address, fbInfo.xOffset, fbInfo.yOffset);
				// If "AttachFramebufferValid" ,  God of War Ghost of Sparta/Chains of Olympus will be missing special effect.
				AttachFramebufferInvalid(entry, framebuffer, fbInfo);
				return true;
			}
		} else {
			WARN_LOG_REPORT_ONCE(diffFormat2, G3D, "Render to texture with incompatible formats %d != %d at %08x", entry->format, framebuffer->format, address);
		}
	}

	return false;
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
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
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
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, entry.maxLevel, entry.addr);

	if (entry.maxLevel != 0) {
		GETexLevelMode mode = gstate.getTexLevelMode();
		switch (mode) {
		case GE_TEXLEVEL_MODE_AUTO:
			// TODO
			break;
		case GE_TEXLEVEL_MODE_CONST:
			dxstate.texMipLodBias.set(lodBias);
			// TODO
			break;
		case GE_TEXLEVEL_MODE_SLOPE:
			// TODO
			break;
		}
		entry.lodBias = lodBias;
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
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0, 0);

	dxstate.texMinFilter.set(MinFilt[minFilt]);
	dxstate.texMipFilter.set(MipFilt[minFilt]);
	dxstate.texMagFilter.set(MagFilt[magFilt]);

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

	if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY)) {
		DWORD aniso = 1 << g_Config.iAnisotropyLevel;
		DWORD anisotropyLevel = aniso > maxAnisotropyLevel ? maxAnisotropyLevel : aniso;
		pD3Ddevice->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, anisotropyLevel);
	}

}

static inline u32 MiniHash(const u32 *ptr) {
	return ptr[0];
}

static inline u32 QuickTexHash(TextureReplacer &replacer, u32 addr, int bufw, int w, int h, GETextureFormat format, TextureCacheDX9::TexCacheEntry *entry) {
	if (replacer.Enabled()) {
		return replacer.ComputeHash(addr, bufw, w, h, format, entry->maxSeenV);
	}

	if (h == 512 && entry->maxSeenV < 512 && entry->maxSeenV != 0) {
		h = (int)entry->maxSeenV;
	}

	const u32 sizeInRAM = (textureBitsPerPixel[format] * bufw * h) / 8;
	const u32 *checkp = (const u32 *) Memory::GetPointer(addr);

	return DoQuickTexHash(checkp, sizeInRAM);
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

inline u32 TextureCacheDX9::GetCurrentClutHash() {
	return clutHash_;
}

void TextureCacheDX9::SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	_dbg_assert_msg_(G3D, framebuffer != nullptr, "Framebuffer must not be null.");

	framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		const u64 cachekey = entry->CacheKey();
		const auto &fbInfo = fbTexInfo_[cachekey];

		// Keep the framebuffer alive.
		framebuffer->last_frame_used = gpuStats.numFlips;

		// We need to force it, since we may have set it on a texture before attaching.
		gstate_c.curTextureWidth = framebuffer->bufferWidth;
		gstate_c.curTextureHeight = framebuffer->bufferHeight;
		gstate_c.bgraTexture = false;
		gstate_c.curTextureXOffset = fbInfo.xOffset;
		gstate_c.curTextureYOffset = fbInfo.yOffset;
		gstate_c.needShaderTexClamp = gstate_c.curTextureWidth != (u32)gstate.getTextureWidth(0) || gstate_c.curTextureHeight != (u32)gstate.getTextureHeight(0);
		if (gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0) {
			gstate_c.needShaderTexClamp = true;
		}

		nextTexture_ = entry;
	} else {
		if (framebuffer->fbo_dx9) {
			fbo_destroy(framebuffer->fbo_dx9);
			framebuffer->fbo_dx9 = 0;
		}
		pD3Ddevice->SetTexture(0, NULL);
		gstate_c.needShaderTexClamp = false;
	}

	nextNeedsRehash_ = false;
	nextNeedsChange_ = false;
	nextNeedsRebuild_ = false;
}

void TextureCacheDX9::ApplyTexture() {
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
		LPDIRECT3DTEXTURE9 texture = DxTex(entry);
		if (texture != lastBoundTexture) {
			pD3Ddevice->SetTexture(0, texture);
			lastBoundTexture = texture;
		}
		UpdateSamplingParams(*entry, false);

		gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
		gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;
	}
}

void TextureCacheDX9::DownloadFramebufferForClut(u32 clutAddr, u32 bytes) {
	framebufferManager_->DownloadFramebufferForClut(clutAddr, bytes);
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

	TextureShaderApplierDX9(LPDIRECT3DPIXELSHADER9 pshader, float bufferW, float bufferH, int renderW, int renderH, float xoff, float yoff)
		: pshader_(pshader), bufferW_(bufferW), bufferH_(bufferH), renderW_(renderW), renderH_(renderH) {
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

	void Use(LPDIRECT3DVERTEXSHADER9 vshader) {
		pD3Ddevice->SetPixelShader(pshader_);
		pD3Ddevice->SetVertexShader(vshader);
		pD3Ddevice->SetVertexDeclaration(pFramebufferVertexDecl);
	}

	void Shade() {
		pD3Ddevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		pD3Ddevice->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
		pD3Ddevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
		pD3Ddevice->SetRenderState(D3DRS_ZENABLE, FALSE);
		pD3Ddevice->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		pD3Ddevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		pD3Ddevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

		DXSetViewport(0, 0, renderW_, renderH_);
		HRESULT hr = pD3Ddevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, verts_, (3 + 2) * sizeof(float));
		if (FAILED(hr)) {
			ERROR_LOG_REPORT(G3D, "Depal render failed: %08x", hr);
		}

		dxstate.Restore();
	}

protected:
	LPDIRECT3DPIXELSHADER9 pshader_;
	PosUV verts_[4];
	float bufferW_;
	float bufferH_;
	int renderW_;
	int renderH_;
};

void TextureCacheDX9::ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	LPDIRECT3DPIXELSHADER9 pshader = nullptr;
	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		pshader = depalShaderCache_->GetDepalettizePixelShader(clutFormat, framebuffer->drawnFormat);
	}

	if (pshader) {
		LPDIRECT3DTEXTURE9 clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);

		FBO_DX9 *depalFBO = framebufferManager_->GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, FBO_8888);
		fbo_bind_as_render_target(depalFBO);
		shaderManager_->DirtyLastShader();

		float xoff = -0.5f / framebuffer->renderWidth;
		float yoff = 0.5f / framebuffer->renderHeight;

		TextureShaderApplierDX9 shaderApply(pshader, framebuffer->bufferWidth, framebuffer->bufferHeight, framebuffer->renderWidth, framebuffer->renderHeight, xoff, yoff);
		shaderApply.ApplyBounds(gstate_c.vertBounds, gstate_c.curTextureXOffset, gstate_c.curTextureYOffset, xoff, yoff);
		shaderApply.Use(depalShaderCache_->GetDepalettizeVertexShader());

		pD3Ddevice->SetTexture(1, clutTexture);
		pD3Ddevice->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		pD3Ddevice->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		pD3Ddevice->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

		framebufferManager_->BindFramebufferColor(0, framebuffer, BINDFBCOLOR_SKIP_COPY);
		pD3Ddevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		pD3Ddevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		pD3Ddevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

		shaderApply.Shade();

		fbo_bind_color_as_texture(depalFBO, 0);

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TexCacheEntry::Status alphaStatus = CheckAlpha(clutBuf_, getClutDestFormat(clutFormat), clutTotalColors, clutTotalColors, 1);
		gstate_c.textureFullAlpha = alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL;
		gstate_c.textureSimpleAlpha = alphaStatus == TexCacheEntry::STATUS_ALPHA_SIMPLE;
	} else {
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;

		framebufferManager_->BindFramebufferColor(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);

		gstate_c.textureFullAlpha = gstate.getTextureFormat() == GE_TFMT_5650;
		gstate_c.textureSimpleAlpha = gstate_c.textureFullAlpha;
	}

	framebufferManager_->RebindFramebuffer();
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);

	lastBoundTexture = INVALID_TEX;
}

bool TextureCacheDX9::SetOffsetTexture(u32 offset) {
	if (g_Config.iRenderingMode != FB_BUFFERED_MODE) {
		return false;
	}
	u32 texaddr = gstate.getTextureAddress(0);
	if (!Memory::IsValidAddress(texaddr) || !Memory::IsValidAddress(texaddr + offset)) {
		return false;
	}

	const u16 dim = gstate.getTextureDimension(0);
	u64 cachekey = TexCacheEntry::CacheKey(texaddr, gstate.getTextureFormat(), dim, 0);
	TexCache::iterator iter = cache.find(cachekey);
	if (iter == cache.end()) {
		return false;
	}
	TexCacheEntry *entry = &iter->second;

	bool success = false;
	for (size_t i = 0, n = fbCache_.size(); i < n; ++i) {
		auto framebuffer = fbCache_[i];
		if (AttachFramebuffer(entry, framebuffer->fb_address, framebuffer, offset)) {
			success = true;
		}
	}

	if (success && entry->framebuffer) {
		SetTextureFramebuffer(entry, entry->framebuffer);
		return true;
	}

	return false;
}

void TextureCacheDX9::SetTexture(bool force) {
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
		pD3Ddevice->SetTexture(0, NULL);
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
		} else if ((gstate_c.textureChanged & TEXCHANGE_UPDATED) == 0) {
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
			nextNeedsRebuild_= false;
			VERBOSE_LOG(G3D, "Texture at %08x Found in Cache, applying", texaddr);
			return; //Done!
		} else {
			nextChangeReason_ = reason;
			nextNeedsChange_ = true;
		}
	} else {
		VERBOSE_LOG(G3D, "No texture in cache, decoding...");
		TexCacheEntry entryNew = {0};
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
	nextNeedsRebuild_= true;
}

bool TextureCacheDX9::CheckFullHash(TexCacheEntry *const entry, bool &doDelete) {
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

bool TextureCacheDX9::HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete) {
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
	D3DFORMAT dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

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
	if (entry->addr > 0x05000000 && entry->addr < 0x08800000)
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
		pD3Ddevice->SetTexture(0, NULL);
	}
	// Seems to cause problems in Tactics Ogre.
	if (badMipSizes) {
		maxLevel = 0;
	}

	LoadTextureLevel(*entry, replaced, 0, maxLevel, replaceImages, scaleFactor, dstFmt);
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

	if (replaced.Valid()) {
		entry->SetAlphaStatus(TexCacheEntry::Status(replaced.AlphaStatus()));
	}

	UpdateSamplingParams(*entry, true);
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

TextureCacheDX9::TexCacheEntry::Status TextureCacheDX9::CheckAlpha(const u32 *pixelData, u32 dstFmt, int stride, int w, int h) {
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

	return (TexCacheEntry::Status)res;
}

ReplacedTextureFormat FromD3D9Format(u32 fmt) {
	switch (fmt) {
	case D3DFMT_R5G6B5:
		return ReplacedTextureFormat::F_5650;
	case D3DFMT_A1R5G5B5:
		return ReplacedTextureFormat::F_5551;
	case D3DFMT_A4R4G4B4:
		return ReplacedTextureFormat::F_4444;
	case D3DFMT_A8R8G8B8:
	default:
		return ReplacedTextureFormat::F_8888;
	}
}

u32 ToD3D9Format(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650:
		return D3DFMT_R5G6B5;
	case ReplacedTextureFormat::F_5551:
		return D3DFMT_A1R5G5B5;
	case ReplacedTextureFormat::F_4444:
		return D3DFMT_A4R4G4B4;
	case ReplacedTextureFormat::F_8888:
	default:
		return D3DFMT_A8R8G8B8;
	}
}

void TextureCacheDX9::LoadTextureLevel(TexCacheEntry &entry, ReplacedTexture &replaced, int level, int maxLevel, bool replaceImages, int scaleFactor, u32 dstFmt) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	LPDIRECT3DTEXTURE9 &texture = DxTex(&entry);
	if (level == 0 && (!replaceImages || texture == nullptr)) {
		// Create texture
		D3DPOOL pool = D3DPOOL_MANAGED;
		int usage = 0;
		if (pD3DdeviceEx) {
			pool = D3DPOOL_DEFAULT;
			usage = D3DUSAGE_DYNAMIC;  // TODO: Switch to using a staging texture?
		}
		int levels = scaleFactor == 1 ? maxLevel + 1 : 1;
		int tw = w, th = h;
		D3DFORMAT tfmt = (D3DFORMAT)D3DFMT(dstFmt);
		if (replaced.GetSize(level, tw, th)) {
			tfmt = (D3DFORMAT)D3DFMT(ToD3D9Format(replaced.Format(level)));
		} else {
			tw *= scaleFactor;
			th *= scaleFactor;
			if (scaleFactor > 1) {
				tfmt = D3DFMT_A8R8G8B8;
			}
		}
		HRESULT hr = pD3Ddevice->CreateTexture(tw, th, levels, usage, tfmt, pool, &texture, NULL);
		if (FAILED(hr)) {
			INFO_LOG(G3D, "Failed to create D3D texture");
			ReleaseTexture(&entry);
			return;
		}
	}

	D3DLOCKED_RECT rect;
	texture->LockRect(level, &rect, NULL, 0);

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
			replacedInfo.fmt = FromD3D9Format(dstFmt);

			replacer.NotifyTextureDecoded(replacedInfo, pixelData, decPitch, level, w, h);
		}
	}

	texture->UnlockRect(level);
}

bool TextureCacheDX9::DecodeTexture(u8 *output, const GPUgstate &state)
{
	OutputDebugStringA("TextureCache::DecodeTexture : FixMe\r\n");
	return true;
}

};
