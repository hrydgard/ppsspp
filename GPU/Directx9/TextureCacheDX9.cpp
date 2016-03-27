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


extern int g_iNumVideos;

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

TextureCacheDX9::TextureCacheDX9() : secondCacheSizeEstimate_(0), clearCacheNextFrame_(false), lowMemoryMode_(false), clutBuf_(NULL), texelsScaledThisFrame_(0) {
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

void *TextureCacheDX9::ReadIndexedTex(int level, const u8 *texptr, int bytesPerIndex, u32 dstFmt, int bufw) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	int length = bufw * h;
	void *buf = NULL;
	switch (gstate.getClutPaletteFormat()) {
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		{
		tmpTexBuf16.resize(std::max(bufw, w) * h);
		tmpTexBufRearrange.resize(std::max(bufw, w) * h);
		const u16 *clut = GetCurrentClut<u16>();
		if (!gstate.isTextureSwizzled()) {
			switch (bytesPerIndex) {
			case 1:
				DeIndexTexture(tmpTexBuf16.data(), (const u8 *)texptr, length, clut);
				break;

			case 2:
				DeIndexTexture(tmpTexBuf16.data(), (const u16_le *)texptr, length, clut);
				break;

			case 4:
				DeIndexTexture(tmpTexBuf16.data(), (const u32_le *)texptr, length, clut);
				break;
			}
		} else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			UnswizzleFromMem(tmpTexBuf32.data(), bufw * bytesPerIndex, texptr, bufw, h, bytesPerIndex);
			switch (bytesPerIndex) {
			case 1:
				DeIndexTexture(tmpTexBuf16.data(), (u8 *) tmpTexBuf32.data(), length, clut);
				break;

			case 2:
				DeIndexTexture(tmpTexBuf16.data(), (u16 *) tmpTexBuf32.data(), length, clut);
				break;

			case 4:
				DeIndexTexture(tmpTexBuf16.data(), (u32 *) tmpTexBuf32.data(), length, clut);
				break;
			}
		}
		buf = tmpTexBuf16.data();
		}
		break;

	case GE_CMODE_32BIT_ABGR8888:
		{
		tmpTexBuf32.resize(std::max(bufw, w) * h);
		tmpTexBufRearrange.resize(std::max(bufw, w) * h);
		const u32 *clut = GetCurrentClut<u32>();
		if (!gstate.isTextureSwizzled()) {
			switch (bytesPerIndex) {
			case 1:
				DeIndexTexture(tmpTexBuf32.data(), (const u8 *)texptr, length, clut);
				break;

			case 2:
				DeIndexTexture(tmpTexBuf32.data(), (const u16_le *)texptr, length, clut);
				break;

			case 4:
				DeIndexTexture(tmpTexBuf32.data(), (const u32_le *)texptr, length, clut);
				break;
			}
			buf = tmpTexBuf32.data();
		} else {
			UnswizzleFromMem(tmpTexBuf32.data(), bufw * bytesPerIndex, texptr, bufw, h, bytesPerIndex);
			// Since we had to unswizzle to tmpTexBuf32, let's output to tmpTexBuf16.
			tmpTexBuf16.resize(std::max(bufw, w) * h * 2);
			u32 *dest32 = (u32 *) tmpTexBuf16.data();
			switch (bytesPerIndex) {
			case 1:
				DeIndexTexture(dest32, (u8 *) tmpTexBuf32.data(), length, clut);
				buf = dest32;
				break;

			case 2:
				DeIndexTexture(dest32, (u16 *) tmpTexBuf32.data(), length, clut);
				buf = dest32;
				break;

			case 4:
				// TODO: If a game actually uses this mode, check if using dest32 or tmpTexBuf32 is faster.
				DeIndexTexture(tmpTexBuf32.data(), tmpTexBuf32.data(), length, clut);
				buf = tmpTexBuf32.data();
				break;
			}
		}
		}
		break;

	default:
		ERROR_LOG_REPORT(G3D, "Unhandled clut texture mode %d!!!", (gstate.clutformat & 3));
		break;
	}

	return buf;
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

static const u8 texByteAlignMap[] = {2, 2, 2, 4};

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
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, entry.maxLevel);

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
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0);

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

static inline u32 QuickTexHash(u32 addr, int bufw, int w, int h, GETextureFormat format, TextureCacheDX9::TexCacheEntry *entry) {
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

template <typename T>
inline const T *TextureCacheDX9::GetCurrentClut() {
	return (const T *)clutBuf_;
}

inline u32 TextureCacheDX9::GetCurrentClutHash() {
	return clutHash_;
}

void TextureCacheDX9::SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	_dbg_assert_msg_(G3D, framebuffer != nullptr, "Framebuffer must not be null.");

	framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		const u64 cachekey = entry->CacheKey();
		const auto &fbInfo = fbTexInfo_[cachekey];

		LPDIRECT3DPIXELSHADER9 pshader = nullptr;
		if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
			pshader = depalShaderCache_->GetDepalettizePixelShader(clutFormat, framebuffer->drawnFormat);
		}
		if (pshader) {
			const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
			const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

			TexCacheEntry::Status alphaStatus = CheckAlpha(clutBuf_, getClutDestFormat(clutFormat), clutTotalColors, clutTotalColors, 1);
			gstate_c.textureFullAlpha = alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL;
			gstate_c.textureSimpleAlpha = alphaStatus == TexCacheEntry::STATUS_ALPHA_SIMPLE;
		} else {
			entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;

			gstate_c.textureFullAlpha = gstate.getTextureFormat() == GE_TFMT_5650;
			gstate_c.textureSimpleAlpha = gstate_c.textureFullAlpha;
		}

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
}

void TextureCacheDX9::ApplyTexture() {
	if (nextTexture_ == nullptr) {
		return;
	}

	if (nextTexture_->framebuffer) {
		ApplyTextureFramebuffer(nextTexture_, nextTexture_->framebuffer);
	} else {
		UpdateMaxSeenV(gstate.isModeThrough());

		LPDIRECT3DTEXTURE9 texture = DxTex(nextTexture_);
		pD3Ddevice->SetTexture(0, texture);
		lastBoundTexture = texture;
		UpdateSamplingParams(*nextTexture_, false);
	}

	nextTexture_ = nullptr;
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
	} else {
		framebufferManager_->BindFramebufferColor(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);
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
		entry->lastFrame = gpuStats.numFlips;
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
	u32 fullhash = 0;

	TexCache::iterator iter = cache.find(cachekey);
	TexCacheEntry *entry = NULL;
	gstate_c.needShaderTexClamp = false;
	gstate_c.bgraTexture = true;
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	bool replaceImages = false;
	
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
				entry->lastFrame = gpuStats.numFlips;
				return;
			} else {
				// Make sure we re-evaluate framebuffers.
				DetachFramebuffer(entry, texaddr, entry->framebuffer);
				reason = "detached framebuf";
				match = false;
			}
		}

		bool rehash = entry->GetHashStatus() == TexCacheEntry::STATUS_UNRELIABLE;
		bool doDelete = true;

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

			bool hashFail = false;
			if (texhash != entry->hash) {
				fullhash = QuickTexHash(texaddr, bufw, w, h, format, entry);
				hashFail = true;
				rehash = false;
			}

			if (rehash && entry->GetHashStatus() != TexCacheEntry::STATUS_RELIABLE) {
				fullhash = QuickTexHash(texaddr, bufw, w, h, format, entry);
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
			}

			if (hashFail) {
				match = false;
				reason = "hash fail";
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
						u64 secondKey = fullhash | (u64)cluthash << 32;
						TexCache::iterator secondIter = secondCache.find(secondKey);
						if (secondIter != secondCache.end()) {
							TexCacheEntry *secondEntry = &secondIter->second;
							if (secondEntry->Matches(dim, format, maxLevel)) {
								// Reset the numInvalidated value lower, we got a match.
								if (entry->numInvalidated > 8) {
									--entry->numInvalidated;
								}
								entry = secondEntry;
								match = true;
							}
						} else {
							secondKey = entry->fullhash | ((u64)entry->cluthash << 32);
							secondCacheSizeEstimate_ += EstimateTexMemoryUsage(entry);
							secondCache[secondKey] = *entry;
							doDelete = false;
						}
					}
				}
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
			entry->lastFrame = gpuStats.numFlips;
			LPDIRECT3DTEXTURE9 texture = DxTex(entry);
			if (texture != lastBoundTexture) {
				nextTexture_ = entry;
				gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
				gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;
			}
			UpdateSamplingParams(*entry, false);
			VERBOSE_LOG(G3D, "Texture at %08x Found in Cache, applying", texaddr);
			return; //Done!
		} else {
			cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
			entry->numInvalidated++;
			gpuStats.numTextureInvalidations++;
			DEBUG_LOG(G3D, "Texture different or overwritten, reloading at %08x: %s", texaddr, reason);
			if (doDelete) {
				if (entry->maxLevel == maxLevel && entry->dim == gstate.getTextureDimension(0) && entry->format == format && standardScaleFactor_ == 1) {
					// Actually, if size and number of levels match, let's try to avoid deleting and recreating.
					// Instead, let's use glTexSubImage to replace the images.
					replaceImages = true;
				} else {
					if (entry->texturePtr == lastBoundTexture) {
						lastBoundTexture = INVALID_TEX;
					}
					ReleaseTexture(entry);
				}
			}
			// Clear the reliable bit if set.
			if (entry->GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
				entry->SetHashStatus(TexCacheEntry::STATUS_HASHING);
			}

			// Also, mark any textures with the same address but different clut.  They need rechecking.
			if (cluthash != 0) {
				const u64 cachekeyMin = (u64)(texaddr & 0x3FFFFFFF) << 32;
				const u64 cachekeyMax = cachekeyMin + (1ULL << 32);
				for (auto it = cache.lower_bound(cachekeyMin), end = cache.upper_bound(cachekeyMax); it != end; ++it) {
					if (it->second.cluthash != cluthash) {
						it->second.status |= TexCacheEntry::STATUS_CLUT_RECHECK;
					}
				}
			}
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
	}

	if ((bufw == 0 || (gstate.texbufwidth[0] & 0xf800) != 0) && texaddr >= PSP_GetKernelMemoryEnd()) {
		ERROR_LOG_REPORT(G3D, "Texture with unexpected bufw (full=%d)", gstate.texbufwidth[0] & 0xffff);
	}

	// We have to decode it, let's setup the cache entry first.
	entry->addr = texaddr;
	entry->hash = texhash;
	entry->format = format;
	entry->lastFrame = gpuStats.numFlips;
	entry->framebuffer = 0;
	entry->maxLevel = maxLevel;
	entry->lodBias = 0.0f;
	
	entry->dim = dim;
	entry->bufw = bufw;

	// This would overestimate the size in many case so we underestimate instead
	// to avoid excessive clearing caused by cache invalidations.
	entry->sizeInRAM = (textureBitsPerPixel[format] * bufw * h / 2) / 8;

	entry->fullhash = fullhash == 0 ? QuickTexHash(texaddr, bufw, w, h, format, entry) : fullhash;
	entry->cluthash = cluthash;

	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;

	gstate_c.curTextureWidth = w;
	gstate_c.curTextureHeight = h;

	// For the estimate, we assume cluts always point to 8888 for simplicity.
	cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

	// TODO: If a framebuffer is attached here, might end up with a bad entry.texture.
	// Should just always create one here or something (like GLES.)

	// Before we go reading the texture from memory, let's check for render-to-texture.
	for (size_t i = 0, n = fbCache_.size(); i < n; ++i) {
		auto framebuffer = fbCache_[i];
		AttachFramebuffer(entry, framebuffer->fb_address, framebuffer);
	}

	// If we ended up with a framebuffer, attach it - no texture decoding needed.
	if (entry->framebuffer) {
		SetTextureFramebuffer(entry, entry->framebuffer);
		entry->lastFrame = gpuStats.numFlips;
		return;
	}

	// Adjust maxLevel to actually present levels..
	bool badMipSizes = false;
	for (u32 i = 0; i <= maxLevel; i++) {
		// If encountering levels pointing to nothing, adjust max level.
		u32 levelTexaddr = gstate.getTextureAddress(i);
		if (!Memory::IsValidAddress(levelTexaddr)) {
			maxLevel = i - 1;
			break;
		}

		if (i > 0) {
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
	D3DFORMAT dstFmt = GetDestFormat(format, gstate.getClutPaletteFormat());

	int scaleFactor = standardScaleFactor_;

	// Rachet down scale factor in low-memory mode.
	if (lowMemoryMode_) {
		// Keep it even, though, just in case of npot troubles.
		scaleFactor = scaleFactor > 4 ? 4 : (scaleFactor > 2 ? 2 : 1);
	}

	// Don't scale the PPGe texture.
	if (entry->addr > 0x05000000 && entry->addr < 0x08800000)
		scaleFactor = 1;
	if ((entry->status & TexCacheEntry::STATUS_CHANGE_FREQUENT) != 0) {
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

	LoadTextureLevel(*entry, 0, maxLevel, replaceImages, scaleFactor, dstFmt);
	LPDIRECT3DTEXTURE9 &texture = DxTex(entry);
	if (!texture) {
		return;
	}

	// Mipmapping is only enabled when texture scaling is disabled.
	if (maxLevel > 0 && scaleFactor == 1) {
		for (u32 i = 1; i <= maxLevel; i++) {
			LoadTextureLevel(*entry, i, maxLevel, replaceImages, scaleFactor, dstFmt);
		}
	}

	gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
	gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;

	nextTexture_ = entry;
	UpdateSamplingParams(*nextTexture_, true);
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

void *TextureCacheDX9::DecodeTextureLevel(GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, u32 &dstFmt, int *bufwout) {
	void *finalBuf = NULL;

	u32 texaddr = gstate.getTextureAddress(level);
	bool swizzled = gstate.isTextureSwizzled();
	if ((texaddr & 0x00600000) != 0 && Memory::IsVRAMAddress(texaddr)) {
		// This means it's in a mirror, possibly a swizzled mirror.  Let's report.
		WARN_LOG_REPORT_ONCE(texmirror, G3D, "Decoding texture from VRAM mirror at %08x swizzle=%d", texaddr, swizzled ? 1 : 0);
		if ((texaddr & 0x00200000) == 0x00200000) {
			// Technically 2 and 6 are slightly different, but this is better than nothing probably.
			swizzled = !swizzled;
		}
		// Note that (texaddr & 0x00600000) == 0x00600000 is very likely to be depth texturing.
	}

	int bufw = GetTextureBufw(level, texaddr, format);
	if (bufwout)
		*bufwout = bufw;
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	const u8 *texptr = Memory::GetPointer(texaddr);

	switch (format) {
	case GE_TFMT_CLUT4:
		{
		const bool mipmapShareClut = gstate.isClutSharedForMipmaps();
		const int clutSharingOffset = mipmapShareClut ? 0 : level * 16;

		switch (clutformat) {
		case GE_CMODE_16BIT_BGR5650:
		case GE_CMODE_16BIT_ABGR5551:
		case GE_CMODE_16BIT_ABGR4444:
			{
			tmpTexBuf16.resize(std::max(bufw, w) * h);
			tmpTexBufRearrange.resize(std::max(bufw, w) * h);
			const u16 *clut = GetCurrentClut<u16>() + clutSharingOffset;
			texByteAlign = 2;
			if (!swizzled) {
				if (clutAlphaLinear_ && mipmapShareClut) {
					DeIndexTexture4OptimalRev(tmpTexBuf16.data(), texptr, bufw * h, clutAlphaLinearColor_);
				} else {
					DeIndexTexture4(tmpTexBuf16.data(), texptr, bufw * h, clut);
				}
			} else {
				tmpTexBuf32.resize(std::max(bufw, w) * h);
				UnswizzleFromMem(tmpTexBuf32.data(), bufw / 2, texptr, bufw, h, 0);
				if (clutAlphaLinear_ && mipmapShareClut) {
					DeIndexTexture4OptimalRev(tmpTexBuf16.data(), (const u8 *)tmpTexBuf32.data(), bufw * h, clutAlphaLinearColor_);
				} else {
					DeIndexTexture4(tmpTexBuf16.data(), (const u8 *)tmpTexBuf32.data(), bufw * h, clut);
				}
			}
			finalBuf = tmpTexBuf16.data();
			}
			break;

		case GE_CMODE_32BIT_ABGR8888:
			{
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			tmpTexBufRearrange.resize(std::max(bufw, w) * h);
			const u32 *clut = GetCurrentClut<u32>() + clutSharingOffset;
			if (!swizzled) {
				DeIndexTexture4(tmpTexBuf32.data(), texptr, bufw * h, clut);
				finalBuf = tmpTexBuf32.data();
			} else {
				UnswizzleFromMem(tmpTexBuf32.data(), bufw / 2, texptr, bufw, h, 0);
				// Let's reuse tmpTexBuf16, just need double the space.
				tmpTexBuf16.resize(std::max(bufw, w) * h * 2);
				DeIndexTexture4((u32 *)tmpTexBuf16.data(), (u8 *)tmpTexBuf32.data(), bufw * h, clut);
				finalBuf = tmpTexBuf16.data();
			}
			}
			break;

		default:
			ERROR_LOG_REPORT(G3D, "Unknown CLUT4 texture mode %d", gstate.getClutPaletteFormat());
			return NULL;
		}
		}
		break;

	case GE_TFMT_CLUT8:
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texptr, 1, dstFmt, bufw);
		break;

	case GE_TFMT_CLUT16:
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texptr, 2, dstFmt, bufw);
		break;

	case GE_TFMT_CLUT32:
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texptr, 4, dstFmt, bufw);
		break;

	case GE_TFMT_4444:
	case GE_TFMT_5551:
	case GE_TFMT_5650:
		texByteAlign = 2;

		if (!swizzled) {
			int len = std::max(bufw, w) * h;
			tmpTexBuf16.resize(len);
			tmpTexBufRearrange.resize(len);
			Memory::MemcpyUnchecked(tmpTexBuf16.data(), texaddr, len * sizeof(u16));
			finalBuf = tmpTexBuf16.data();
		}
		else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			UnswizzleFromMem(tmpTexBuf32.data(), bufw * 2, texptr, bufw, h, 2);
			finalBuf = tmpTexBuf32.data();
		}
		break;

	case GE_TFMT_8888:
		if (!swizzled) {
			// Special case: if we don't need to deal with packing, we don't need to copy.
			//if (w == bufw) {
			//	finalBuf = Memory::GetPointer(texaddr);
			//} else 
			{
				int len = bufw * h;
				tmpTexBuf32.resize(std::max(bufw, w) * h);
				tmpTexBufRearrange.resize(std::max(bufw, w) * h);
				Memory::MemcpyUnchecked(tmpTexBuf32.data(), texaddr, len * sizeof(u32));
				finalBuf = tmpTexBuf32.data();
			}
		} else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			UnswizzleFromMem(tmpTexBuf32.data(), bufw * 4, texptr, bufw, h, 4);
			finalBuf = tmpTexBuf32.data();
		}
		break;

	case GE_TFMT_DXT1:
		{
			int minw = std::min(bufw, w);
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			tmpTexBufRearrange.resize(std::max(bufw, w) * h);
			u32 *dst = tmpTexBuf32.data();
			DXT1Block *src = (DXT1Block*)texptr;

			for (int y = 0; y < h; y += 4) {
				u32 blockIndex = (y / 4) * (bufw / 4);
				for (int x = 0; x < minw; x += 4) {
					DecodeDXT1Block(dst + bufw * y + x, src + blockIndex, bufw);
					blockIndex++;
				}
			}
			finalBuf = tmpTexBuf32.data();
			w = (w + 3) & ~3;
		}
		break;

	case GE_TFMT_DXT3:
		{
			int minw = std::min(bufw, w);
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			tmpTexBufRearrange.resize(std::max(bufw, w) * h);
			u32 *dst = tmpTexBuf32.data();
			DXT3Block *src = (DXT3Block*)texptr;

			for (int y = 0; y < h; y += 4) {
				u32 blockIndex = (y / 4) * (bufw / 4);
				for (int x = 0; x < minw; x += 4) {
					DecodeDXT3Block(dst + bufw * y + x, src + blockIndex, bufw);
					blockIndex++;
				}
			}
			w = (w + 3) & ~3;
			finalBuf = tmpTexBuf32.data();
		}
		break;

	case GE_TFMT_DXT5:
		{
			int minw = std::min(bufw, w);
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			tmpTexBufRearrange.resize(std::max(bufw, w) * h);
			u32 *dst = tmpTexBuf32.data();
			DXT5Block *src = (DXT5Block*)texptr;

			for (int y = 0; y < h; y += 4) {
				u32 blockIndex = (y / 4) * (bufw / 4);
				for (int x = 0; x < minw; x += 4) {
					DecodeDXT5Block(dst + bufw * y + x, src + blockIndex, bufw);
					blockIndex++;
				}
			}
			w = (w + 3) & ~3;
			finalBuf = tmpTexBuf32.data();
		}
		break;

	default:
		ERROR_LOG_REPORT(G3D, "Unknown Texture Format %d!!!", format);
		return NULL;
	}

	if (!finalBuf) {
		ERROR_LOG_REPORT(G3D, "NO finalbuf! Will crash!");
	}

	if (!(standardScaleFactor_ == 1 && gstate_c.Supports(GPU_SUPPORTS_UNPACK_SUBIMAGE)) && w != bufw) {
		int pixelSize;
		switch (dstFmt) {
		case D3DFMT_A4R4G4B4:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_R5G6B5:
			pixelSize = 2;
			break;
		default:
			pixelSize = 4;
			break;
		}
		// Need to rearrange the buffer to simulate GL_UNPACK_ROW_LENGTH etc.
		finalBuf = RearrangeBuf(finalBuf, bufw * pixelSize, w * pixelSize, h);
	}

	return finalBuf;
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

// TODO: xoffset and yoffset are unused - bug?
static inline void copyTexture(int xoffset, int yoffset, int w, int h, int pitch, int srcfmt, int fmt, void * pSrc, void * pDst) {
	int y;
	switch(fmt) {
		case D3DFMT_R5G6B5:
		case D3DFMT_A4R4G4B4:
		case D3DFMT_A1R5G5B5:
			for(y = 0; y < h; y++) {
				const u16 *src = (const u16 *)((u8*)pSrc + (w*2) * y);
				u16 *dst = (u16*)((u8*)pDst + pitch * y);
				memcpy(dst, src, w * sizeof(u16));
			}
			break;
				
		// 32 bit texture
		case D3DFMT_A8R8G8B8:
			for(y = 0; y < h; y++) {
				const u32 *src = (const u32 *)((u8*)pSrc + (w*4) * y);
				u32 *dst = (u32*)((u8*)pDst + pitch * y);
				memcpy(dst, src, w * sizeof(u32));
			}
			break;
	}
}

void TextureCacheDX9::LoadTextureLevel(TexCacheEntry &entry, int level, int maxLevel, bool replaceImages, int scaleFactor, u32 dstFmt) {
	// TODO: only do this once
	u32 texByteAlign = 1;

	GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
	int bufw;
	void *finalBuf = DecodeTextureLevel(GETextureFormat(entry.format), clutformat, level, texByteAlign, dstFmt, &bufw);
	if (finalBuf == NULL) {
		return;
	}

	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	gpuStats.numTexturesDecoded++;

	u32 *pixelData = (u32 *)finalBuf;
	if (scaleFactor > 1 && (entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0)
		scaler.Scale(pixelData, dstFmt, w, h, scaleFactor);

	if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
		TexCacheEntry::Status alphaStatus = CheckAlpha(pixelData, dstFmt, w, w, h);
		entry.SetAlphaStatus(alphaStatus, level);
	} else {
		entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
	}

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
		HRESULT hr = pD3Ddevice->CreateTexture(w, h, levels, usage, (D3DFORMAT)D3DFMT(dstFmt), pool, &texture, NULL);
		if (FAILED(hr)) {
			INFO_LOG(G3D, "Failed to create D3D texture");
			ReleaseTexture(&entry);
			return;
		}
	}

	D3DLOCKED_RECT rect;
	texture->LockRect(level, &rect, NULL, 0);

	copyTexture(0, 0, w, h, rect.Pitch, entry.format, dstFmt, pixelData, rect.pBits);

	texture->UnlockRect(level);
}

bool TextureCacheDX9::DecodeTexture(u8 *output, const GPUgstate &state)
{
	OutputDebugStringA("TextureCache::DecodeTexture : FixMe\r\n");
	return true;
}

};
