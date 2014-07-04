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
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/FramebufferDX9.h"
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

TextureCacheDX9::TextureCacheDX9() : clearCacheNextFrame_(false), lowMemoryMode_(false), clutBuf_(NULL) {
	lastBoundTexture = INVALID_TEX;
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	// This is 5MB of temporary storage. Might be possible to shrink it.
	tmpTexBuf32.resize(1024 * 512);  // 2MB
	tmpTexBuf16.resize(1024 * 512);  // 1MB
	tmpTexBufRearrange.resize(1024 * 512);   // 2MB
	clutBufConverted_ = (u32 *)AllocateAlignedMemory(4096 * sizeof(u32), 16);  // 16KB
	clutBufRaw_ = (u32 *)AllocateAlignedMemory(4096 * sizeof(u32), 16);  // 16KB
	// glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyLevel);
	maxAnisotropyLevel = 16;
	SetupTextureDecoder();
#ifdef _XBOX
	// TODO: Maybe not?  This decimates more often, but it may be speed harmful if unnecessary.
	lowMemoryMode_ = true;
#endif
}

TextureCacheDX9::~TextureCacheDX9() {
	FreeAlignedMemory(clutBufConverted_);
	FreeAlignedMemory(clutBufRaw_);
}

void TextureCacheDX9::Clear(bool delete_them) {
	pD3Ddevice->SetTexture(0, NULL);
	lastBoundTexture = INVALID_TEX;
	if (delete_them) {
		for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %i", iter->second.texture);
			iter->second.texture->Release();
		}
		for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %i", iter->second.texture);
			iter->second.texture->Release();
		}
	}
	if (cache.size() + secondCache.size()) {
		INFO_LOG(G3D, "Texture cached cleared from %i textures", (int)(cache.size() + secondCache.size()));
		cache.clear();
		secondCache.clear();
	}
}

// Removes old textures.
void TextureCacheDX9::Decimate() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	pD3Ddevice->SetTexture(0, NULL);
	lastBoundTexture = INVALID_TEX;
	int killAge = lowMemoryMode_ ? TEXTURE_KILL_AGE_LOWMEM : TEXTURE_KILL_AGE;
	for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ) {
		if (iter->second.lastFrame + killAge < gpuStats.numFlips) {
			iter->second.texture->Release();
			cache.erase(iter++);
		} else {
			++iter;
		}
	}

	if (g_Config.bTextureSecondaryCache) {
		for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ) {
			// In low memory mode, we kill them all.
			if (lowMemoryMode_ || iter->second.lastFrame + TEXTURE_KILL_AGE < gpuStats.numFlips) {
				iter->second.texture->Release();
				secondCache.erase(iter++);
			} else {
				++iter;
			}
		}
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
	u64 startKey = addr - LARGEST_TEXTURE_SIZE;
	u64 endKey = addr + size + LARGEST_TEXTURE_SIZE;
	for (TexCache::iterator iter = cache.lower_bound(startKey), end = cache.upper_bound(endKey); iter != end; ++iter) {
		u32 texAddr = iter->second.addr;
		u32 texEnd = iter->second.addr + iter->second.sizeInRAM;

		if (texAddr < addr_end && addr < texEnd) {
			if ((iter->second.status & TexCacheEntry::STATUS_MASK) == TexCacheEntry::STATUS_RELIABLE) {
				// Clear status -> STATUS_HASHING.
				iter->second.status &= ~TexCacheEntry::STATUS_MASK;
			}
			if (type != GPU_INVALIDATE_ALL) {
				gpuStats.numTextureInvalidations++;
				// Start it over from 0 (unless it's safe.)
				iter->second.numFrames = type == GPU_INVALIDATE_SAFE ? 256 : 0;
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

	for (TexCache::iterator iter = cache.begin(), end = cache.end(); iter != end; ++iter) {
		if ((iter->second.status & TexCacheEntry::STATUS_MASK) == TexCacheEntry::STATUS_RELIABLE) {
			// Clear status -> STATUS_HASHING.
			iter->second.status &= ~TexCacheEntry::STATUS_MASK;
		}
		if (!iter->second.framebuffer) {
			iter->second.invalidHint++;
		}
	}
}

void TextureCacheDX9::ClearNextFrame() {
	clearCacheNextFrame_ = true;
}


template <typename T>
inline void AttachFramebufferValid(T &entry, VirtualFramebufferDX9 *framebuffer) {
	const bool hasInvalidFramebuffer = entry->framebuffer == 0 || entry->invalidHint == -1;
	const bool hasOlderFramebuffer = entry->framebuffer != 0 && entry->framebuffer->last_frame_render < framebuffer->last_frame_render;
	if (hasInvalidFramebuffer || hasOlderFramebuffer) {
		entry->framebuffer = framebuffer;
		entry->invalidHint = 0;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

template <typename T>
inline void AttachFramebufferInvalid(T &entry, VirtualFramebufferDX9 *framebuffer) {
	if (entry->framebuffer == 0 || entry->framebuffer == framebuffer) {
		entry->framebuffer = framebuffer;
		entry->invalidHint = -1;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

inline void TextureCacheDX9::AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebufferDX9 *framebuffer, bool exactMatch) {
		// If they match exactly, it's non-CLUT and from the top left.
	if (exactMatch) {
		DEBUG_LOG(G3D, "Render to texture detected at %08x!", address);
		if (!entry->framebuffer || entry->invalidHint == -1) {
				if (entry->format != framebuffer->format) {
				WARN_LOG_REPORT_ONCE(diffFormat1, G3D, "Render to texture with different formats %d != %d", entry->format, framebuffer->format);
				// If it already has one, let's hope that one is correct.
				// If "AttachFramebufferValid" , Evangelion Jo and Kurohyou 2 will be 'blue background' in-game
				AttachFramebufferInvalid(entry, framebuffer);
			} else {
				AttachFramebufferValid(entry, framebuffer);
			}
		// TODO: Delete the original non-fbo texture too.
	}
		} else if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE || g_Config.iRenderingMode == FB_BUFFERED_MODE) {
			// 3rd Birthday (and possibly other games) render to a 16 bit clut texture.
			const bool compatFormat = framebuffer->format == entry->format
				|| (framebuffer->format == GE_FORMAT_8888 && entry->format == GE_TFMT_CLUT32)
				|| (framebuffer->format != GE_FORMAT_8888 && entry->format == GE_TFMT_CLUT16);

			// Is it at least the right stride?
			if (framebuffer->fb_stride == entry->bufw && compatFormat) {
				if (framebuffer->format != entry->format) {
				WARN_LOG_REPORT_ONCE(diffFormat2, G3D, "Render to texture with different formats %d != %d at %08x", entry->format, framebuffer->format, address);
					// TODO: Use an FBO to translate the palette?
				// If 'AttachFramebufferInvalid' , Kurohyou 2 will be missing battle scene in-game and FF Type-0 will have black box shadow/'blue fog' and 3rd birthday will have 'blue fog'
				// If 'AttachFramebufferValid' , DBZ VS Tag will have 'burning effect' , 
					AttachFramebufferValid(entry, framebuffer);
				} else if ((entry->addr - address) / entry->bufw < framebuffer->height) {
				WARN_LOG_REPORT_ONCE(subarea, G3D, "Render to area containing texture at %08x", address);
					// TODO: Keep track of the y offset.
				// If "AttachFramebufferValid" ,  God of War Ghost of Sparta/Chains of Olympus will be missing special effect.
				AttachFramebufferInvalid(entry, framebuffer);
			}
		}
	}
}

inline void TextureCacheDX9::DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebufferDX9 *framebuffer) {
	if (entry->framebuffer == framebuffer) {
		entry->framebuffer = 0;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

void TextureCacheDX9::NotifyFramebuffer(u32 address, VirtualFramebufferDX9 *framebuffer, FramebufferNotification msg) {
	// This is a rough heuristic, because sometimes our framebuffers are too tall.
	static const u32 MAX_SUBAREA_Y_OFFSET = 32;

	// Must be in VRAM so | 0x04000000 it is.
	const u64 cacheKey = (u64)(address | 0x04000000) << 32;
	// If it has a clut, those are the low 32 bits, so it'll be inside this range.
	// Also, if it's a subsample of the buffer, it'll also be within the FBO.
	const u64 cacheKeyEnd = cacheKey + ((u64)(framebuffer->fb_stride * MAX_SUBAREA_Y_OFFSET) << 32);

	switch (msg) {
	case NOTIFY_FB_CREATED:
	case NOTIFY_FB_UPDATED:
		// Ensure it's in the framebuffer cache.
		if (std::find(fbCache_.begin(), fbCache_.end(), framebuffer) == fbCache_.end()) {
			fbCache_.push_back(framebuffer);
		}
		for (auto it = cache.lower_bound(cacheKey), end = cache.upper_bound(cacheKeyEnd); it != end; ++it) {
			AttachFramebuffer(&it->second, address | 0x04000000, framebuffer, it->first == cacheKey);
		}
		break;

	case NOTIFY_FB_DESTROYED:
		fbCache_.erase(std::remove(fbCache_.begin(), fbCache_.end(),  framebuffer), fbCache_.end());
		for (auto it = cache.lower_bound(cacheKey), end = cache.upper_bound(cacheKeyEnd); it != end; ++it) {
			DetachFramebuffer(&it->second, address | 0x04000000, framebuffer);
		}
		break;
	}
}

void *TextureCacheDX9::UnswizzleFromMem(u32 texaddr, u32 bufw, u32 bytesPerPixel, u32 level) {
	const u32 rowWidth = (bytesPerPixel > 0) ? (bufw * bytesPerPixel) : (bufw / 2);
	const u32 pitch = rowWidth / 4;
	const int bxc = rowWidth / 16;
	int byc = (gstate.getTextureHeight(level) + 7) / 8;
	if (byc == 0)
		byc = 1;

	u32 ydest = 0;
	if (rowWidth >= 16) {
		u32 *ydestp = tmpTexBuf32.data();
		// The most common one, so it gets an optimized implementation.
		DoUnswizzleTex16(Memory::GetPointer(texaddr), ydestp, bxc, byc, pitch, rowWidth);
	} else if (rowWidth == 8) {
		const u32 *src = (u32 *) Memory::GetPointer(texaddr);
		for (int by = 0; by < byc; by++) {
			for (int n = 0; n < 8; n++, ydest += 2) {
				tmpTexBuf32[ydest + 0] = *src++;
				tmpTexBuf32[ydest + 1] = *src++;
				src += 2; // skip two u32
			}
		}
	} else if (rowWidth == 4) {
		const u32 *src = (u32 *) Memory::GetPointer(texaddr);
		for (int by = 0; by < byc; by++) {
			for (int n = 0; n < 8; n++, ydest++) {
				tmpTexBuf32[ydest] = *src++;
				src += 3;
			}
		}
	} else if (rowWidth == 2) {
		const u16 *src = (u16 *) Memory::GetPointer(texaddr);
		for (int by = 0; by < byc; by++) {
			for (int n = 0; n < 4; n++, ydest++) {
				u16 n1 = src[0];
				u16 n2 = src[8];
				tmpTexBuf32[ydest] = (u32)n1 | ((u32)n2 << 16);
				src += 16;
			}
		}
	} else if (rowWidth == 1) {
		const u8 *src = (u8 *) Memory::GetPointer(texaddr);
		for (int by = 0; by < byc; by++) {
			for (int n = 0; n < 2; n++, ydest++) {
				u8 n1 = src[ 0];
				u8 n2 = src[16];
				u8 n3 = src[32];
				u8 n4 = src[48];
				tmpTexBuf32[ydest] = (u32)n1 | ((u32)n2 << 8) | ((u32)n3 << 16) | ((u32)n4 << 24);
				src += 64;
			}
		}
	}
	return tmpTexBuf32.data();
}

void *TextureCacheDX9::ReadIndexedTex(int level, u32 texaddr, int bytesPerIndex, u32 dstFmt, int bufw) {
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
				DeIndexTexture<u8>(tmpTexBuf16.data(), texaddr, length, clut);
				break;

			case 2:
				DeIndexTexture<u16_le>(tmpTexBuf16.data(), texaddr, length, clut);
				break;

			case 4:
				DeIndexTexture<u32_le>(tmpTexBuf16.data(), texaddr, length, clut);
				break;
			}
		} else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			UnswizzleFromMem(texaddr, bufw, bytesPerIndex, level);
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
				DeIndexTexture<u8>(tmpTexBuf32.data(), texaddr, length, clut);
				break;

			case 2:
				DeIndexTexture<u16_le>(tmpTexBuf32.data(), texaddr, length, clut);
				break;

			case 4:
				DeIndexTexture<u32_le>(tmpTexBuf32.data(), texaddr, length, clut);
				break;
			}
			buf = tmpTexBuf32.data();
		} else {
			UnswizzleFromMem(texaddr, bufw, bytesPerIndex, level);
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

static const u32 MinFilt[8] = {
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
	D3DTEXF_POINT,	// GL_NEAREST_MIPMAP_NEAREST,
	D3DTEXF_LINEAR,	// GL_LINEAR_MIPMAP_NEAREST,
	D3DTEXF_POINT,	// GL_NEAREST_MIPMAP_LINEAR,
	D3DTEXF_LINEAR,	// GL_LINEAR_MIPMAP_LINEAR,
};

static const u32 MipFilt[8] = {
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
	D3DTEXF_POINT,	// GL_NEAREST_MIPMAP_NEAREST,
	D3DTEXF_POINT,	// GL_LINEAR_MIPMAP_NEAREST,
	D3DTEXF_LINEAR,	// GL_NEAREST_MIPMAP_LINEAR,
	D3DTEXF_LINEAR,	// GL_LINEAR_MIPMAP_LINEAR,
};

static const u32 MagFilt[2] = {
	D3DTEXF_POINT,
	D3DTEXF_LINEAR
};

// This should not have to be done per texture! OpenGL is silly yo
// TODO: Dirty-check this against the current texture.
void TextureCacheDX9::UpdateSamplingParams(TexCacheEntry &entry, bool force) {
	int minFilt = gstate.texfilter & 0x7;
	int magFilt = (gstate.texfilter>>8) & 1;
	bool sClamp = gstate.isTexCoordClampedS();
	bool tClamp = gstate.isTexCoordClampedT();

	// Always force !!
	force = true;

	bool noMip = (gstate.texlevel & 0xFFFFFF) == 0x000001 || (gstate.texlevel & 0xFFFFFF) == 0x100001 ;  // Fix texlevel at 0

	if (entry.maxLevel == 0) {
		// Enforce no mip filtering, for safety.
		minFilt &= 1; // no mipmaps yet
	} else {
		// TODO: Is this a signed value? Which direction?
		float lodBias = 0.0; // -(float)((gstate.texlevel >> 16) & 0xFF) / 16.0f;
		if (force || entry.lodBias != lodBias) {
			entry.lodBias = lodBias;
		}
	}

	if ((g_Config.iTexFiltering == LINEAR || (g_Config.iTexFiltering == LINEARFMV && g_iNumVideos)) && !gstate.isColorTestEnabled()) {
		magFilt |= 1;
		minFilt |= 1;
	}

	if (g_Config.iTexFiltering == NEAREST) {
		magFilt &= ~1;
		minFilt &= ~1;
	}

	if (!g_Config.bMipMap || noMip) {
		magFilt &= 1;
		minFilt &= 1;
	}

	if (force || entry.minFilt != minFilt) {
		pD3Ddevice->SetSamplerState(0, D3DSAMP_MINFILTER, MinFilt[minFilt]);
		pD3Ddevice->SetSamplerState(0, D3DSAMP_MIPFILTER, MipFilt[minFilt]);
		entry.minFilt = minFilt;
	}
	if (force || entry.magFilt != magFilt) {
		pD3Ddevice->SetSamplerState(0, D3DSAMP_MAGFILTER, MagFilt[magFilt]);
		entry.magFilt = magFilt;
	}
	if (force || entry.sClamp != sClamp) {
		pD3Ddevice->SetSamplerState(0, D3DSAMP_ADDRESSU, sClamp ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
		entry.sClamp = sClamp;
	}
	if (force || entry.tClamp != tClamp) {
		pD3Ddevice->SetSamplerState(0, D3DSAMP_ADDRESSV, tClamp ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
		entry.tClamp = tClamp;
	}
	
#ifdef _XBOX
	pD3Ddevice->SetRenderState(D3DRS_HALFPIXELOFFSET, TRUE );
#endif
}

static inline u32 ABGR2RGBA(u32 src) {
#ifndef _XBOX
	return ((src & 0xFF000000)) | 
        ((src & 0x00FF0000) >>  16) | 
        ((src & 0x0000FF00)) | 
        ((src & 0x000000FF) << 16);  
#else
	return (src >> 8) | (src << 24); 
#endif
}

static void ClutConvertColors(void *dstBuf, const void *srcBuf, u32 dstFmt, int numPixels) {
	// TODO: All these can be further sped up with SSE or NEON.
	switch (dstFmt) {
	case D3DFMT_A1R5G5B5:
		{
			const u16_le *src = (const u16_le *)srcBuf;
			u16 *dst = (u16 *)dstBuf;
			for (int i = 0; i < numPixels; i++) {
				u16 rgb = (src[i]);
				((uint16_t *)dst)[i] = (rgb & 0x83E0) | ((rgb & 0x1F) << 10) | ((rgb & 0x7C00) >> 10);
			}
		}
		break;
	case D3DFMT_A4R4G4B4:
		{
			const u16_le *src = (const u16_le *)srcBuf;
			u16_le *dst = (u16_le *)dstBuf;
			for (int i = 0; i < numPixels; i++) {
				// Already good format
				u16 rgb = src[i];
				dst[i] = (rgb & 0xF) | (rgb & 0xF0)<<8 | ( rgb & 0xF00) | ((rgb & 0xF000)>>8);
			}
		}
		break;
	case D3DFMT_R5G6B5:
		{
			const u16_le *src = (const u16_le *)srcBuf;
			u16 *dst = (u16 *)dstBuf;
			for (int i = 0; i < numPixels; i++) {
				u16 rgb = src[i];
				dst[i] = ((rgb & 0x1f) << 11) | ( rgb & 0x7e0)  | ((rgb & 0xF800) >>11 );
			}
		}
		break;
	default:
		{
			const u32 *src = (const u32 *)srcBuf;
			u32 *dst = (u32*)dstBuf;
			for (int i = 0; i < numPixels; i++) {
				dst[i] = ABGR2RGBA(src[i]);
			}
		}
		break;
	}
}

void TextureCacheDX9::StartFrame() {
	lastBoundTexture = INVALID_TEX;
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

static inline u32 QuickClutHash(const u8 *clut, u32 bytes) {
	// CLUTs always come in multiples of 32 bytes, can't load them any other way.
	_dbg_assert_msg_(G3D, (bytes & 31) == 0, "CLUT should always have a multiple of 32 bytes.");

	const u32 prime = 2246822519U;
	u32 hash = 0;
#ifdef _M_SSE
	if ((((u32)(intptr_t)clut) & 0xf) == 0) {
		__m128i cursor = _mm_set1_epi32(0);
		const __m128i mult = _mm_set1_epi32(prime);
		const __m128i *p = (const __m128i *)clut;
		for (u32 i = 0; i < bytes / 16; ++i) {
			cursor = _mm_add_epi32(cursor, _mm_mul_epu32(_mm_load_si128(&p[i]), mult));
		}
		// Add the four parts into the low i32.
		cursor = _mm_add_epi32(cursor, _mm_srli_si128(cursor, 8));
		cursor = _mm_add_epi32(cursor, _mm_srli_si128(cursor, 4));
		hash = _mm_cvtsi128_si32(cursor);
	} else {
#else
	// TODO: ARM NEON implementation (using CPUDetect to be sure it has NEON.)
	{
#endif
		for (const u32 *p = (u32 *)clut, *end = (u32 *)(clut + bytes); p < end; ) {
			hash += *p++ * prime;
		}
	}

	return hash;
}

static inline u32 QuickTexHash(u32 addr, int bufw, int w, int h, GETextureFormat format) {
	const u32 sizeInRAM = (textureBitsPerPixel[format] * bufw * h) / 8;
	const u32 *checkp = (const u32 *) Memory::GetPointer(addr);
	u32 check = 0;

#ifndef _XBOX
	return DoQuickTexHash(checkp, sizeInRAM);
#else
	// TODO: Move this to common tex hash code?  Use hash similar to new texhash?
	if ((((u32)(intptr_t)checkp | sizeInRAM) & 0x1f) == 0) {
		__vector4 add, xor;
		__vector4 cur = __vzero();
		const __vector4 * ptr = (const __vector4*)checkp;
		for (u32 i = 0; i < sizeInRAM / 16; i+=2) {
			add = __lvx(&ptr[i],0);
			xor = __lvx(&ptr[i+1],0);
			cur = __vadduwm(cur, add);
			cur = __vxor(cur, xor);
		}
		// Add the four parts into the low i32. // is it possible with vmx ?
		check = cur.u[0]+cur.u[1]+cur.u[2]+cur.u[3];
	} else {
		for (u32 i = 0; i < sizeInRAM / 8; ++i) {
			check += *checkp++;
			check ^= *checkp++;
		}
	}

	return check;
#endif
}

inline bool TextureCacheDX9::TexCacheEntry::Matches(u16 dim2, u8 format2, int maxLevel2) {
	return dim == dim2 && format == format2 && maxLevel == maxLevel2;
}

void TextureCacheDX9::LoadClut() {
	u32 clutAddr = gstate.getClutAddress();
	clutTotalBytes_ = gstate.getClutLoadBytes();
	if (Memory::IsValidAddress(clutAddr)) {
		Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, clutTotalBytes_);
	} else {
		memset(clutBufRaw_, 0xFF, clutTotalBytes_);
	}
	// Reload the clut next time.
	clutLastFormat_ = 0xFFFFFFFF;
}

void TextureCacheDX9::UpdateCurrentClut() {
	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	const u32 clutBase = gstate.getClutIndexStartPos();
	const u32 clutBaseBytes = clutBase * (clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	const u32 clutExtendedBytes = clutTotalBytes_ + clutBaseBytes;

	clutHash_ = DoReliableHash((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);
	
	/*
	// Avoid a copy when we don't need to convert colors.
	if (clutFormat != GE_CMODE_32BIT_ABGR8888) {
		ClutConvertColors(clutBufConverted_, clutBufRaw_, getClutDestFormat(clutFormat), clutExtendedBytes / sizeof(u16));
		clutBuf_ = clutBufConverted_;
	} else {
		clutBuf_ = clutBufRaw_;
	}
	*/
	ClutConvertColors(clutBufConverted_, clutBufRaw_, getClutDestFormat(clutFormat), clutExtendedBytes / sizeof(u16));
	clutBuf_ = clutBufConverted_;
	//clutBuf_ = clutBufRaw_;

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (gstate.getClutPaletteFormat() == GE_CMODE_16BIT_ABGR4444 && gstate.isClutIndexSimple()) {
		const u16_le *clut = (const u16_le*)GetCurrentClut<u16>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0xFFF0;
		for (int i = 0; i < 16; ++i) {
			if ((clut[i] & 0xf) != i) {
				clutAlphaLinear_ = false;
				break;
			}
			// Alpha 0 doesn't matter.
			if (i != 0 && (clut[i] & 0xFFF0) != clutAlphaLinearColor_) {
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

// #define DEBUG_TEXTURES

#ifdef DEBUG_TEXTURES
bool SetDebugTexture() {
	static const int highlightFrames = 30;

	static int numTextures = 0;
	static int lastFrames = 0;
	static int mostTextures = 1;

	if (lastFrames != gpuStats.numFlips) {
		mostTextures = std::max(mostTextures, numTextures);
		numTextures = 0;
		lastFrames = gpuStats.numFlips;
	}

	static GLuint solidTexture = 0;

	bool changed = false;
	if (((gpuStats.numFlips / highlightFrames) % mostTextures) == numTextures) {
		if (gpuStats.numFlips % highlightFrames == 0) {
			NOTICE_LOG(G3D, "Highlighting texture # %d / %d", numTextures, mostTextures);
		}
		static const u32 solidTextureData[] = {0x99AA99FF};

		if (solidTexture == 0) {
			glGenTextures(1, &solidTexture);
			glBindTexture(GL_TEXTURE_2D, solidTexture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, solidTextureData);
		} else {
			glBindTexture(GL_TEXTURE_2D, solidTexture);
		}
		changed = true;
	}

	++numTextures;
	return changed;
}
#endif

void TextureCacheDX9::SetTextureFramebuffer(TexCacheEntry *entry)
{
	entry->framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		// For now, let's not bind FBOs that we know are off (invalidHint will be -1.)
		// But let's still not use random memory.
		if (entry->framebuffer->fbo && entry->invalidHint != -1) {
			fbo_bind_color_as_texture(entry->framebuffer->fbo, 0);
			// Keep the framebuffer alive.
			// TODO: Dangerous if it sets a new one?
			entry->framebuffer->last_frame_used = gpuStats.numFlips;
		} else {
			pD3Ddevice->SetTexture(0, NULL);
			gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		}
		// We need to force it, since we may have set it on a texture before attaching.
		UpdateSamplingParams(*entry, true);
		gstate_c.curTextureWidth = entry->framebuffer->width;
		gstate_c.curTextureHeight = entry->framebuffer->height;
		gstate_c.flipTexture = true;
		gstate_c.textureFullAlpha = entry->framebuffer->format == GE_FORMAT_565;
	} else {
		if (entry->framebuffer->fbo)
			entry->framebuffer->fbo = 0;
		pD3Ddevice->SetTexture(0, NULL);
	}
}

void TextureCacheDX9::SetTexture() {
#ifdef DEBUG_TEXTURES
	if (SetDebugTexture()) {
		// A different texture was bound, let's rebind next time.
		lastBoundTexture = -1;
		return;
	}
#endif

	u32 texaddr = gstate.getTextureAddress(0);
	if (!Memory::IsValidAddress(texaddr)) {
		// Bind a null texture and return.
		pD3Ddevice->SetTexture(0, NULL);
		lastBoundTexture = INVALID_TEX;
		return;
	}

	GETextureFormat format = gstate.getTextureFormat();
	if (format >= 11) {
		ERROR_LOG_REPORT(G3D, "Unknown texture format %i", format);
		// TODO: Better assumption?
		format = GE_TFMT_5650;
	}
	bool hasClut = gstate.isTextureFormatIndexed();

	u64 cachekey = (u64)texaddr << 32;
	u32 cluthash;
	if (hasClut) {
		if (clutLastFormat_ != gstate.clutformat) {
			// We update here because the clut format can be specified after the load.
			UpdateCurrentClut();
		}
		cluthash = GetCurrentClutHash() ^ gstate.clutformat;
		cachekey |= cluthash;
	} else {
		cluthash = 0;
	}
	
	int bufw = GetTextureBufw(0, texaddr, format);
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	int maxLevel = gstate.getTextureMaxLevel();

	u32 texhash = MiniHash((const u32 *)Memory::GetPointer(texaddr));
	u32 fullhash = 0;

	TexCache::iterator iter = cache.find(cachekey);
	TexCacheEntry *entry = NULL;
	gstate_c.flipTexture = false;
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	bool replaceImages = false;
	
	if (iter != cache.end()) {
		entry = &iter->second;
		// Validate the texture still matches the cache entry.
		u16 dim = gstate.getTextureDimension(0);
		bool match = entry->Matches(dim, format, maxLevel);
#ifndef _XBOX
		match &= host->GPUAllowTextureCache(texaddr);
#endif

		// Check for FBO - slow!
		if (entry->framebuffer) {
			if (match) {
				SetTextureFramebuffer(entry);
				lastBoundTexture = INVALID_TEX;
				entry->lastFrame = gpuStats.numFlips;
				return;
			} else {
				// Make sure we re-evaluate framebuffers.
				DetachFramebuffer(entry, texaddr, entry->framebuffer);
				match = false;
			}
		}

		bool rehash = (entry->status & TexCacheEntry::STATUS_MASK) == TexCacheEntry::STATUS_UNRELIABLE;
		bool doDelete = true;

		if (match) {
			if (entry->lastFrame != gpuStats.numFlips) {
				u32 diff = gpuStats.numFlips - entry->lastFrame;
				entry->numFrames++;

				if (entry->framesUntilNextFullHash < diff) {
					// Exponential backoff up to 512 frames.  Textures are often reused.
					if (entry->numFrames > 32) {
						// Also, try to add some "randomness" to avoid rehashing several textures the same frame.
						entry->framesUntilNextFullHash = std::min(512, entry->numFrames) + ((intptr_t)entry->texture & 15);
					} else {
						entry->framesUntilNextFullHash = entry->numFrames;
					}
					rehash = true;
				} else {
					entry->framesUntilNextFullHash -= diff;
				}
			}

			// If it's not huge or has been invalidated many times, recheck the whole texture.
			if (entry->invalidHint > 180 || (entry->invalidHint > 15 && dim <= 0x909)) {
				entry->invalidHint = 0;
				rehash = true;
			}

			bool hashFail = false;
			if (texhash != entry->hash) {
				fullhash = QuickTexHash(texaddr, bufw, w, h, format);
				hashFail = true;
				rehash = false;
			}

			if (rehash && (entry->status & TexCacheEntry::STATUS_MASK) != TexCacheEntry::STATUS_RELIABLE) {
				fullhash = QuickTexHash(texaddr, bufw, w, h, format);
				if (fullhash != entry->fullhash) {
					hashFail = true;
				} else if ((entry->status & TexCacheEntry::STATUS_MASK) == TexCacheEntry::STATUS_UNRELIABLE && entry->numFrames > TexCacheEntry::FRAMES_REGAIN_TRUST) {
					// Reset to STATUS_HASHING.
					if (g_Config.bTextureBackoffCache) {
						entry->status &= ~TexCacheEntry::STATUS_MASK;
					}
				}
			}

			if (hashFail) {
				match = false;
				entry->status |= TexCacheEntry::STATUS_UNRELIABLE;
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
							secondKey = entry->fullhash | (u64)entry->cluthash << 32;
							secondCache[secondKey] = *entry;
							doDelete = false;
						}
					}
				}
			}
		}

		if (match) {
			// TODO: Mark the entry reliable if it's been safe for long enough?
			//got one!
			entry->lastFrame = gpuStats.numFlips;
			if (entry->texture != lastBoundTexture) {
				pD3Ddevice->SetTexture(0, entry->texture);
				lastBoundTexture = entry->texture;
				gstate_c.textureFullAlpha = (entry->status & TexCacheEntry::STATUS_ALPHA_MASK) == TexCacheEntry::STATUS_ALPHA_FULL;
			}
			UpdateSamplingParams(*entry, false);
			VERBOSE_LOG(G3D, "Texture at %08x Found in Cache, applying", texaddr);
			return; //Done!
		} else {
			entry->numInvalidated++;
			gpuStats.numTextureInvalidations++;
			DEBUG_LOG(G3D, "Texture different or overwritten, reloading at %08x", texaddr);
			if (doDelete) {
				if (entry->maxLevel == maxLevel && entry->dim == gstate.getTextureDimension(0) && entry->format == format && g_Config.iTexScalingLevel <= 1) {
					// Actually, if size and number of levels match, let's try to avoid deleting and recreating.
					// Instead, let's use glTexSubImage to replace the images.
					replaceImages = true;
				} else {
					if (entry->texture == lastBoundTexture) {
						lastBoundTexture = INVALID_TEX;
					}
					entry->texture->Release();
				}
			}
			if (entry->status == TexCacheEntry::STATUS_RELIABLE) {
				entry->status = TexCacheEntry::STATUS_HASHING;
			}
		}
	} else {
		VERBOSE_LOG(G3D, "No texture in cache, decoding...");
		TexCacheEntry entryNew = {0};
		cache[cachekey] = entryNew;

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
	
	entry->dim = gstate.getTextureDimension(0);
	entry->bufw = bufw;

	// This would overestimate the size in many case so we underestimate instead
	// to avoid excessive clearing caused by cache invalidations.
	entry->sizeInRAM = (textureBitsPerPixel[format] * bufw * h / 2) / 8;

	entry->fullhash = fullhash == 0 ? QuickTexHash(texaddr, bufw, w, h, format) : fullhash;
	entry->cluthash = cluthash;

	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;

	gstate_c.curTextureWidth = w;
	gstate_c.curTextureHeight = h;

	// TODO: If a framebuffer is attached here, might end up with a bad entry.texture.
	// Should just always create one here or something (like GLES.)

	// Before we go reading the texture from memory, let's check for render-to-texture.
	for (size_t i = 0, n = fbCache_.size(); i < n; ++i) {
		auto framebuffer = fbCache_[i];
		// This is a rough heuristic, because sometimes our framebuffers are too tall.
		static const u32 MAX_SUBAREA_Y_OFFSET = 32;

		// Must be in VRAM so | 0x04000000 it is.
		const u64 cacheKeyStart = (u64)(framebuffer->fb_address | 0x04000000) << 32;
		// If it has a clut, those are the low 32 bits, so it'll be inside this range.
		// Also, if it's a subsample of the buffer, it'll also be within the FBO.
		const u64 cacheKeyEnd = cacheKeyStart + ((u64)(framebuffer->fb_stride * MAX_SUBAREA_Y_OFFSET) << 32);

		if (cachekey >= cacheKeyStart && cachekey < cacheKeyEnd) {
			AttachFramebuffer(entry, framebuffer->fb_address | 0x04000000, framebuffer, cachekey == cacheKeyStart);
		}
	}

	// If we ended up with a framebuffer, attach it - no texture decoding needed.
	if (entry->framebuffer) {
		SetTextureFramebuffer(entry);
		lastBoundTexture = INVALID_TEX;
		entry->lastFrame = gpuStats.numFlips;
		return;
	}

	// Adjust maxLevel to actually present levels..
	for (int i = 0; i <= maxLevel; i++) {
		// If encountering levels pointing to nothing, adjust max level.
		u32 levelTexaddr = gstate.getTextureAddress(i);
		if (!Memory::IsValidAddress(levelTexaddr)) {
			maxLevel = i - 1;
			break;
		}
	}

	LoadTextureLevel(*entry, 0, replaceImages);

	pD3Ddevice->SetTexture(0, entry->texture);
	lastBoundTexture = entry->texture;

	DWORD anisotropyLevel = (DWORD) g_Config.iAnisotropyLevel > maxAnisotropyLevel ? maxAnisotropyLevel : g_Config.iAnisotropyLevel;
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropyLevel);
	pD3Ddevice->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, anisotropyLevel); 

	UpdateSamplingParams(*entry, true);

	gstate_c.textureFullAlpha = (entry->status & TexCacheEntry::STATUS_ALPHA_MASK) == TexCacheEntry::STATUS_ALPHA_FULL;
}

void *TextureCacheDX9::DecodeTextureLevel(GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, u32 &dstFmt) {
	void *finalBuf = NULL;

	u32 texaddr = gstate.getTextureAddress(level);

	int bufw = GetTextureBufw(level, texaddr, format);

	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	const u8 *texptr = Memory::GetPointer(texaddr);

	switch (format) {
	case GE_TFMT_CLUT4:
		{
		dstFmt = getClutDestFormat(clutformat);

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
			if (!gstate.isTextureSwizzled()) {
				if (clutAlphaLinear_ && mipmapShareClut) {
					DeIndexTexture4Optimal(tmpTexBuf16.data(), texaddr, bufw * h, clutAlphaLinearColor_);
				} else {
					DeIndexTexture4(tmpTexBuf16.data(), texaddr, bufw * h, clut);
				}
			} else {
				tmpTexBuf32.resize(std::max(bufw, w) * h);
				UnswizzleFromMem(texaddr, bufw, 0, level);
				if (clutAlphaLinear_ && mipmapShareClut) {
					DeIndexTexture4Optimal(tmpTexBuf16.data(), (u8 *)tmpTexBuf32.data(), bufw * h, clutAlphaLinearColor_);
				} else {
					DeIndexTexture4(tmpTexBuf16.data(), (u8 *)tmpTexBuf32.data(), bufw * h, clut);
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
			if (!gstate.isTextureSwizzled()) {
				DeIndexTexture4(tmpTexBuf32.data(), texaddr, bufw * h, clut);
				finalBuf = tmpTexBuf32.data();
			} else {
				UnswizzleFromMem(texaddr, bufw, 0, level);
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
		dstFmt = getClutDestFormat(gstate.getClutPaletteFormat());
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texaddr, 1, dstFmt, bufw);
		break;

	case GE_TFMT_CLUT16:
		dstFmt = getClutDestFormat(gstate.getClutPaletteFormat());
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texaddr, 2, dstFmt, bufw);
		break;

	case GE_TFMT_CLUT32:
		dstFmt = getClutDestFormat(gstate.getClutPaletteFormat());
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texaddr, 4, dstFmt, bufw);
		break;

	case GE_TFMT_4444:
	case GE_TFMT_5551:
	case GE_TFMT_5650:
		if (format == GE_TFMT_4444)
			dstFmt = D3DFMT_A4R4G4B4;
		else if (format == GE_TFMT_5551)
			dstFmt = D3DFMT_A1R5G5B5;
		else if (format == GE_TFMT_5650)
			dstFmt = D3DFMT_R5G6B5;
		texByteAlign = 2;

		if (!gstate.isTextureSwizzled()) {
			int len = std::max(bufw, w) * h;
			tmpTexBuf16.resize(len);
			tmpTexBufRearrange.resize(len);
			Memory::Memcpy(tmpTexBuf16.data(), texaddr, len * sizeof(u16));
			finalBuf = tmpTexBuf16.data();
		}
		else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			finalBuf = UnswizzleFromMem(texaddr, bufw, 2, level);
		}
		ClutConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
		break;

	case GE_TFMT_8888:
		dstFmt = D3DFMT_A8R8G8B8;
		if (!gstate.isTextureSwizzled()) {
			// Special case: if we don't need to deal with packing, we don't need to copy.
			//if (w == bufw) {
			//	finalBuf = Memory::GetPointer(texaddr);
			//} else 
			{
				int len = bufw * h;
				tmpTexBuf32.resize(std::max(bufw, w) * h);
				tmpTexBufRearrange.resize(std::max(bufw, w) * h);
				Memory::Memcpy(tmpTexBuf32.data(), texaddr, len * sizeof(u32));
				finalBuf = tmpTexBuf32.data();
			}
		}
		else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			finalBuf = UnswizzleFromMem(texaddr, bufw, 4, level);
		}
		ClutConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
		break;

	case GE_TFMT_DXT1:
		dstFmt = D3DFMT_A8R8G8B8;
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
		dstFmt = D3DFMT_A8R8G8B8;
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

	case GE_TFMT_DXT5:  // These work fine now
		dstFmt = D3DFMT_A8R8G8B8;
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

	if (w != bufw) {
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
		int inRowBytes = bufw * pixelSize;
		int outRowBytes = w * pixelSize;
		const u8 *read = (const u8 *)finalBuf;
		u8 *write = 0;
		if (w > bufw) {
			write = (u8 *)tmpTexBufRearrange.data();
			finalBuf = tmpTexBufRearrange.data();
		} else {
			write = (u8 *)finalBuf;
		}
		for (int y = 0; y < h; y++) {
			memmove(write, read, outRowBytes);
			read += inRowBytes;
			write += outRowBytes;
		}
	}

	return finalBuf;
}

void TextureCacheDX9::CheckAlpha(TexCacheEntry &entry, u32 *pixelData, u32 dstFmt, int w, int h) {
	// TODO: Could probably be optimized more.
	u32 hitZeroAlpha = 0;
	u32 hitSomeAlpha = 0;

	switch (dstFmt) {
	case D3DFMT_A4R4G4B4:
		{
			const u32 *p = pixelData;
			for (int i = 0; i < (w * h + 1) / 2; ++i) {
#ifndef BIG_ENDIAN
				u32 a = p[i] & 0x000F000F;
				hitZeroAlpha |= a ^ 0x000F000F;
				if (a != 0x000F000F && a != 0x0000000F && a != 0x000F0000 && a != 0) {
					hitSomeAlpha = 1;
					break;
				}
#else
				u32 a = p[i] & 0xF000F000;
				hitZeroAlpha |= a ^ 0xF000F000;
				if (a != 0xF000F000 && a != 0x0000F000 && a != 0xF0000000 && a != 0) {
					hitSomeAlpha = 1;
					break;
				}
#endif
			}
		}
		break;
	case D3DFMT_A1R5G5B5:
		{
			const u32 *p = pixelData;
			for (int i = 0; i < (w * h + 1) / 2; ++i) {
#ifndef BIG_ENDIAN
				u32 a = p[i] & 0x00010001;
				hitZeroAlpha |= a ^ 0x00010001;
#else
				u32 a = p[i] & 0x10001000;
				hitZeroAlpha |= a ^ 0x10001000;
#endif
			}
		}
		break;
	case D3DFMT_R5G6B5:
		{
			// Never has any alpha.
		}
		break;
	default:
		{
			const u32 *p = pixelData;
			for (int i = 0; i < w * h; ++i) {
				u32 a = p[i] & 0xFF000000;
				hitZeroAlpha |= a ^ 0xFF000000;
				if (a != 0xFF000000 && a != 0) {
					hitSomeAlpha = 1;
					break;
				}
			}
		}
		break;
	}

	if (hitSomeAlpha != 0)
		entry.status |= TexCacheEntry::STATUS_ALPHA_UNKNOWN;
	else if (hitZeroAlpha != 0)
		entry.status |= TexCacheEntry::STATUS_ALPHA_SIMPLE;
	else
		entry.status |= TexCacheEntry::STATUS_ALPHA_FULL;
}

static inline void copyTexture(int xoffset, int yoffset, int w, int h, int pitch, int srcfmt, int fmt, void * pSrc, void * pDst) {
	// Swap color
	switch(fmt) {
		case D3DFMT_R5G6B5:
		case D3DFMT_A4R4G4B4:
		case D3DFMT_A1R5G5B5:
			// Really needed ?
			for(int y = 0; y < h; y++) {
				const u16 *src = (const u16 *)((u8*)pSrc + (w*2) * y);
				u16 *dst = (u16*)((u8*)pDst + pitch * y);
				memcpy(dst, src, w * sizeof(u16));
			}
			break;
				
		// 32 bit texture
		case D3DFMT_A8R8G8B8:
			for(int y = 0; y < h; y++) {
				const u32 *src = (const u32 *)((u8*)pSrc + (w*4) * y);
				u32 *dst = (u32*)((u8*)pDst + pitch * y);
				
				/*
				// todo use memcpy
				for(int x = 0; x < w; x++) {
					unsigned int rgb = src[x];
					dst[x] = rgb;
				}
				*/
				memcpy(dst, src, w * sizeof(u32));
			}
			break;

	}

}

void TextureCacheDX9::LoadTextureLevel(TexCacheEntry &entry, int level, bool replaceImages) {
	// TODO: only do this once
	u32 texByteAlign = 1;

	// TODO: Look into using BGRA for 32-bit textures when the GL_EXT_texture_format_BGRA8888 extension is available, as it's faster than RGBA on some chips.
	u32 dstFmt = 0;

	GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
	void *finalBuf = DecodeTextureLevel(GETextureFormat(entry.format), clutformat, level, texByteAlign, dstFmt);
	if (finalBuf == NULL) {
		return;
	}

	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	gpuStats.numTexturesDecoded++;

	u32 *pixelData = (u32 *)finalBuf;

	int scaleFactor = g_Config.iTexScalingLevel;

	// Don't scale the PPGe texture.
	if (entry.addr > 0x05000000 && entry.addr < 0x08800000)
		scaleFactor = 1;

	if (scaleFactor > 1 && entry.numInvalidated == 0) 
		scaler.Scale(pixelData, dstFmt, w, h, scaleFactor);
	// Or always?
	if (entry.numInvalidated == 0)
		CheckAlpha(entry, pixelData, dstFmt, w, h);
	else
		entry.status |= TexCacheEntry::STATUS_ALPHA_UNKNOWN;

	// Ignore mip map atm
	if (level == 0) { 
		if (replaceImages) {	
			// Unset texture
			pD3Ddevice->SetTexture(0, NULL);

			D3DLOCKED_RECT rect;
			entry.texture->LockRect(level, &rect, NULL, 0);

			copyTexture(0, 0, w, h, rect.Pitch, entry.format, dstFmt, pixelData, rect.pBits);

			entry.texture->UnlockRect(level);

			// Rebind texture
			pD3Ddevice->SetTexture(0, entry.texture);
		} else {
			// Create texture
#ifdef _XBOX
			pD3Ddevice->CreateTexture(w, h, 1, 0, (D3DFORMAT)D3DFMT(dstFmt), NULL, &entry.texture, NULL);
#else
			pD3Ddevice->CreateTexture(w, h, 1, 0, (D3DFORMAT)D3DFMT(dstFmt), D3DPOOL_MANAGED, &entry.texture, NULL);
#endif
			D3DLOCKED_RECT rect;
			entry.texture->LockRect(level, &rect, NULL, 0);

			copyTexture(0, 0, w, h, rect.Pitch, entry.format, dstFmt, pixelData, rect.pBits);

			entry.texture->UnlockRect(level);
		}
		
//#ifdef _DEBUG
#if 0
		// Hack save to disk ...
		char fname[256];
		int fmt = 0;
		static int ipic = 0;
		switch(dstFmt) {
		case D3DFMT_A4R4G4B4:
			fmt = 0x4444;
			break;
		case D3DFMT_A1R5G5B5:
			fmt = 0x5551;
			break;
		case D3DFMT_R5G6B5:
			fmt = 0x5650;
			break;
		case D3DFMT_A8R8G8B8:
			fmt = 0x8888;
			break;
		default:
			fmt = 0xDEAD;
			break;
		}
		sprintf(fname, "game:\\pic\\pic.%02x.%04x.%08x.%08x.png", ipic++, fmt, entry.format, clutformat);
		D3DXSaveTextureToFile(fname, D3DXIFF_PNG, entry.texture, NULL);
#endif
	}
}

// Only used by Qt UI?
bool TextureCacheDX9::DecodeTexture(u8* output, GPUgstate state)
{
	OutputDebugStringA("TextureCache::DecodeTexture : FixMe\r\n");
	return true;
}

};
