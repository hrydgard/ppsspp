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

#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/FragmentShaderGenerator.h"
#include "GPU/GLES/DepalettizeShader.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/Common/TextureDecoder.h"
#include "Core/Config.h"
#include "Core/Host.h"

#include "ext/xxhash.h"
#include "math/math_util.h"
#include "native/gfx_es2/gl_state.h"

#ifdef _M_SSE
#include <xmmintrin.h>
#endif

// If a texture hasn't been seen for this many frames, get rid of it.
#define TEXTURE_KILL_AGE 200
#define TEXTURE_KILL_AGE_LOWMEM 60
// Not used in lowmem mode.
#define TEXTURE_SECOND_KILL_AGE 100

// Try to be prime to other decimation intervals.
#define TEXCACHE_DECIMATION_INTERVAL 13

// Changes more frequent than this will be considered "frequent" and prevent texture scaling.
#define TEXCACHE_FRAME_CHANGE_FREQUENT 6

#define TEXCACHE_NAME_CACHE_SIZE 16

#define TEXCACHE_MAX_TEXELS_SCALED (256*256)  // Per frame

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

extern int g_iNumVideos;

TextureCache::TextureCache() : clearCacheNextFrame_(false), lowMemoryMode_(false), clutBuf_(NULL), texelsScaledThisFrame_(0) {
	timesInvalidatedAllThisFrame_ = 0;
	lastBoundTexture = -1;
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	// This is 5MB of temporary storage. Might be possible to shrink it.
	tmpTexBuf32.resize(1024 * 512);  // 2MB
	tmpTexBuf16.resize(1024 * 512);  // 1MB
	tmpTexBufRearrange.resize(1024 * 512);   // 2MB

	// Aren't these way too big?
	clutBufConverted_ = (u32 *)AllocateAlignedMemory(4096 * sizeof(u32), 16);  // 16KB
	clutBufRaw_ = (u32 *)AllocateAlignedMemory(4096 * sizeof(u32), 16);  // 16KB

	// Zap these so that reads from uninitialized parts of the CLUT look the same in
	// release and debug
	memset(clutBufConverted_, 0, 4096 * sizeof(u32));
	memset(clutBufRaw_, 0, 4096 * sizeof(u32));

	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyLevel);
	SetupTextureDecoder();
}

TextureCache::~TextureCache() {
	FreeAlignedMemory(clutBufConverted_);
	FreeAlignedMemory(clutBufRaw_);
}

void TextureCache::Clear(bool delete_them) {
	glBindTexture(GL_TEXTURE_2D, 0);
	lastBoundTexture = -1;
	if (delete_them) {
		for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %i", iter->second.texture);
			glDeleteTextures(1, &iter->second.texture);
		}
		for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %i", iter->second.texture);
			glDeleteTextures(1, &iter->second.texture);
		}
		if (!nameCache_.empty()) {
			glDeleteTextures((GLsizei)nameCache_.size(), &nameCache_[0]);
			nameCache_.clear();
		}
	}
	if (cache.size() + secondCache.size()) {
		INFO_LOG(G3D, "Texture cached cleared from %i textures", (int)(cache.size() + secondCache.size()));
		cache.clear();
		secondCache.clear();
	}
}

void TextureCache::DeleteTexture(TexCache::iterator it) {
	glDeleteTextures(1, &it->second.texture);
	auto fbInfo = fbTexInfo_.find(it->second.addr);
	if (fbInfo != fbTexInfo_.end()) {
		fbTexInfo_.erase(fbInfo);
	}
	cache.erase(it);
}

// Removes old textures.
void TextureCache::Decimate() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	lastBoundTexture = -1;
	int killAge = lowMemoryMode_ ? TEXTURE_KILL_AGE_LOWMEM : TEXTURE_KILL_AGE;
	for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ) {
		if (iter->second.lastFrame + killAge < gpuStats.numFlips) {
			DeleteTexture(iter++);
		} else {
			++iter;
		}
	}

	if (g_Config.bTextureSecondaryCache) {
		for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ) {
			// In low memory mode, we kill them all.
			if (lowMemoryMode_ || iter->second.lastFrame + TEXTURE_SECOND_KILL_AGE < gpuStats.numFlips) {
				glDeleteTextures(1, &iter->second.texture);
				secondCache.erase(iter++);
			} else {
				++iter;
			}
		}
	}
}

void TextureCache::Invalidate(u32 addr, int size, GPUInvalidationType type) {
	// If we're hashing every use, without backoff, then this isn't needed.
	if (!g_Config.bTextureBackoffCache) {
		return;
	}

	addr &= 0x3FFFFFFF;
	const u32 addr_end = addr + size;

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
				iter->second.framesUntilNextFullHash = 0;
			} else if (!iter->second.framebuffer) {
				iter->second.invalidHint++;
			}
		}
	}
}

void TextureCache::InvalidateAll(GPUInvalidationType /*unused*/) {
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

void TextureCache::ClearNextFrame() {
	clearCacheNextFrame_ = true;
}


void TextureCache::AttachFramebufferValid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo) {
	const bool hasInvalidFramebuffer = entry->framebuffer == 0 || entry->invalidHint == -1;
	const bool hasOlderFramebuffer = entry->framebuffer != 0 && entry->framebuffer->last_frame_render < framebuffer->last_frame_render;
	bool hasFartherFramebuffer = false;
	if (!hasInvalidFramebuffer && !hasOlderFramebuffer) {
		// If it's valid, but the offset is greater, then we still win.
		if (fbTexInfo_[entry->addr].yOffset == fbInfo.yOffset)
			hasFartherFramebuffer = fbTexInfo_[entry->addr].xOffset > fbInfo.xOffset;
		else
			hasFartherFramebuffer = fbTexInfo_[entry->addr].yOffset > fbInfo.yOffset;
	}
	if (hasInvalidFramebuffer || hasOlderFramebuffer || hasFartherFramebuffer) {
		entry->framebuffer = framebuffer;
		entry->invalidHint = 0;
		entry->status &= ~TextureCache::TexCacheEntry::STATUS_DEPALETTIZE;
		fbTexInfo_[entry->addr] = fbInfo;
		framebuffer->last_frame_attached = gpuStats.numFlips;
		host->GPUNotifyTextureAttachment(entry->addr);
	} else if (entry->framebuffer == framebuffer) {
		framebuffer->last_frame_attached = gpuStats.numFlips;
	}
}

void TextureCache::AttachFramebufferInvalid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo) {
	if (entry->framebuffer == 0 || entry->framebuffer == framebuffer) {
		entry->framebuffer = framebuffer;
		entry->invalidHint = -1;
		entry->status &= ~TextureCache::TexCacheEntry::STATUS_DEPALETTIZE;
		fbTexInfo_[entry->addr] = fbInfo;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

bool TextureCache::AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset) {
	static const u32 MIN_SUBAREA_HEIGHT = 8;
	static const u32 MAX_SUBAREA_Y_OFFSET_SAFE = 32;

	AttachedFramebufferInfo fbInfo = {0};

	const u64 mirrorMask = 0x00600000;
	// Must be in VRAM so | 0x04000000 it is.  Also, ignore memory mirrors.
	const u32 addr = (address | 0x04000000) & 0x3FFFFFFF & ~mirrorMask;
	const u32 texaddr = ((entry->addr + texaddrOffset) & ~mirrorMask);
	const bool noOffset = texaddr == addr;
	const bool exactMatch = noOffset && entry->format < 4;

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
		fbInfo.yOffset = pixelOffset / entry->bufw;
		fbInfo.xOffset = pixelOffset % entry->bufw;

		if (framebuffer->fb_stride != entry->bufw) {
			if (noOffset) {
				WARN_LOG_REPORT_ONCE(diffStrides2, G3D, "Render to texture using CLUT with different strides %d != %d", entry->bufw, framebuffer->fb_stride);
			} else {
				// Assume any render-to-tex with different bufw + offset is a render from ram.
				DetachFramebuffer(entry, address, framebuffer);
				return false;
			}
		}

		if (fbInfo.yOffset + MIN_SUBAREA_HEIGHT >= framebuffer->height) {
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

inline void TextureCache::DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer) {
	if (entry->framebuffer == framebuffer) {
		entry->framebuffer = 0;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

void TextureCache::NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg) {
	// Must be in VRAM so | 0x04000000 it is.  Also, ignore memory mirrors.
	// These checks are mainly to reduce scanning all textures.
	const u32 addr = (address | 0x04000000) & 0x3F9FFFFF;
	const u32 bpp = framebuffer->format == GE_FORMAT_8888 ? 4 : 2;
	const u64 cacheKey = (u64)addr << 32;
	// If it has a clut, those are the low 32 bits, so it'll be inside this range.
	// Also, if it's a subsample of the buffer, it'll also be within the FBO.
	const u64 cacheKeyEnd = cacheKey + ((u64)(framebuffer->fb_stride * framebuffer->height * bpp) << 32);

	// The first mirror starts at 0x04200000 and there are 3.  We search all for framebuffers.
	const u64 mirrorCacheKey = (u64)0x04200000 << 32;
	const u64 mirrorCacheKeyEnd = (u64)0x04800000 << 32;

	switch (msg) {
	case NOTIFY_FB_CREATED:
	case NOTIFY_FB_UPDATED:
		// Ensure it's in the framebuffer cache.
		if (std::find(fbCache_.begin(), fbCache_.end(), framebuffer) == fbCache_.end()) {
			fbCache_.push_back(framebuffer);
		}
		for (auto it = cache.lower_bound(cacheKey), end = cache.upper_bound(cacheKeyEnd); it != end; ++it) {
			AttachFramebuffer(&it->second, addr, framebuffer);
		}
		// Let's assume anything in mirrors is fair game to check.
		for (auto it = cache.lower_bound(mirrorCacheKey), end = cache.upper_bound(mirrorCacheKeyEnd); it != end; ++it) {
			AttachFramebuffer(&it->second, addr, framebuffer);
		}
		break;

	case NOTIFY_FB_DESTROYED:
		fbCache_.erase(std::remove(fbCache_.begin(), fbCache_.end(),  framebuffer), fbCache_.end());
		for (auto it = cache.lower_bound(cacheKey), end = cache.upper_bound(cacheKeyEnd); it != end; ++it) {
			DetachFramebuffer(&it->second, addr, framebuffer);
		}
		for (auto it = cache.lower_bound(mirrorCacheKey), end = cache.upper_bound(mirrorCacheKeyEnd); it != end; ++it) {
			DetachFramebuffer(&it->second, addr, framebuffer);
		}
		break;
	}
}

void *TextureCache::UnswizzleFromMem(const u8 *texptr, u32 bufw, u32 bytesPerPixel, u32 level) {
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
		DoUnswizzleTex16(texptr, ydestp, bxc, byc, pitch, rowWidth);
	} else if (rowWidth == 8) {
		const u32 *src = (const u32 *) texptr;
		for (int by = 0; by < byc; by++) {
			for (int n = 0; n < 8; n++, ydest += 2) {
				tmpTexBuf32[ydest + 0] = *src++;
				tmpTexBuf32[ydest + 1] = *src++;
				src += 2; // skip two u32
			}
		}
	} else if (rowWidth == 4) {
		const u32 *src = (const u32 *) texptr;
		for (int by = 0; by < byc; by++) {
			for (int n = 0; n < 8; n++, ydest++) {
				tmpTexBuf32[ydest] = *src++;
				src += 3;
			}
		}
	} else if (rowWidth == 2) {
		const u16 *src = (const u16 *) texptr;
		for (int by = 0; by < byc; by++) {
			for (int n = 0; n < 4; n++, ydest++) {
				u16 n1 = src[0];
				u16 n2 = src[8];
				tmpTexBuf32[ydest] = (u32)n1 | ((u32)n2 << 16);
				src += 16;
			}
		}
	} else if (rowWidth == 1) {
		const u8 *src = (const u8 *) texptr;
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

void *TextureCache::ReadIndexedTex(int level, const u8 *texptr, int bytesPerIndex, GLuint dstFmt, int bufw) {
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
			UnswizzleFromMem(texptr, bufw, bytesPerIndex, level);
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
			UnswizzleFromMem(texptr, bufw, bytesPerIndex, level);
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

GLenum getClutDestFormat(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return GL_UNSIGNED_SHORT_4_4_4_4;
	case GE_CMODE_16BIT_ABGR5551:
		return GL_UNSIGNED_SHORT_5_5_5_1;
	case GE_CMODE_16BIT_BGR5650:
		return GL_UNSIGNED_SHORT_5_6_5;
	case GE_CMODE_32BIT_ABGR8888:
		return GL_UNSIGNED_BYTE;
	}
	return 0;
}

static const u8 texByteAlignMap[] = {2, 2, 2, 4};

static const GLuint MinFiltGL[8] = {
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST_MIPMAP_NEAREST,
	GL_LINEAR_MIPMAP_NEAREST,
	GL_NEAREST_MIPMAP_LINEAR,
	GL_LINEAR_MIPMAP_LINEAR,
};

static const GLuint MagFiltGL[2] = {
	GL_NEAREST,
	GL_LINEAR
};

void TextureCache::GetSamplingParams(int &minFilt, int &magFilt, bool &sClamp, bool &tClamp, float &lodBias, int maxLevel) {
	minFilt = gstate.texfilter & 0x7;
	magFilt = (gstate.texfilter>>8) & 1;
	sClamp = gstate.isTexCoordClampedS();
	tClamp = gstate.isTexCoordClampedT();

	bool noMip = (gstate.texlevel & 0xFFFFFF) == 0x000001 || (gstate.texlevel & 0xFFFFFF) == 0x100001 ;  // Fix texlevel at 0

	if (maxLevel == 0) {
		// Enforce no mip filtering, for safety.
		minFilt &= 1; // no mipmaps yet
		lodBias = 0.0f;
	} else {
		// Texture lod bias should be signed.
		lodBias = (float)(int)(s8)((gstate.texlevel >> 16) & 0xFF) / 16.0f;
	}

	if (g_Config.iTexFiltering == LINEARFMV && g_iNumVideos > 0 && (gstate.getTextureDimension(0) & 0xF) >= 9) {
		magFilt |= 1;
		minFilt |= 1;
	}
	if (g_Config.iTexFiltering == LINEAR && (!gstate.isColorTestEnabled() || IsColorTestTriviallyTrue())) {
		if (!gstate.isAlphaTestEnabled() || IsAlphaTestTriviallyTrue()) {
			magFilt |= 1;
			minFilt |= 1;
		}
	}
	bool forceNearest = g_Config.iTexFiltering == NEAREST;
	// Force Nearest when color test enabled and rendering resolution greater than 480x272
	if ((gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue()) && g_Config.iInternalResolution != 1 && gstate.isModeThrough()) {
		// Some games use 0 as the color test color, which won't be too bad if it bleeds.
		// Fuchsia and green, etc. are the problem colors.
		if (gstate.getColorTestRef() != 0) {
			forceNearest = true;
		}
	}
	if (forceNearest) {
		magFilt &= ~1;
		minFilt &= ~1;
	}

	if (!g_Config.bMipMap || noMip) {
		minFilt &= 1;
	}
}

// This should not have to be done per texture! OpenGL is silly yo
void TextureCache::UpdateSamplingParams(TexCacheEntry &entry, bool force) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, entry.maxLevel);

	if (entry.maxLevel != 0) {
		if (force || entry.lodBias != lodBias) {
#ifndef USING_GLES2
			GETexLevelMode mode = gstate.getTexLevelMode();
			switch (mode) {
			case GE_TEXLEVEL_MODE_AUTO:
				// TODO
				break;
			case GE_TEXLEVEL_MODE_CONST:
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, lodBias);
				break;
			case GE_TEXLEVEL_MODE_SLOPE:
				// TODO
				break;
			}
#endif
			entry.lodBias = lodBias;
		}
	}

	if (force || entry.minFilt != minFilt) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, MinFiltGL[minFilt]);
		entry.minFilt = minFilt;
	}
	if (force || entry.magFilt != magFilt) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, MagFiltGL[magFilt]);
		entry.magFilt = magFilt;
	}

	if (entry.framebuffer) {
		WARN_LOG_REPORT_ONCE(wrongFramebufAttach, G3D, "Framebuffer still attached in UpdateSamplingParams()?");
	}

	if (force || entry.sClamp != sClamp) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		entry.sClamp = sClamp;
	}
	if (force || entry.tClamp != tClamp) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		entry.tClamp = tClamp;
	}
}

void TextureCache::SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, MinFiltGL[minFilt]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, MagFiltGL[magFilt]);

	// Often the framebuffer will not match the texture size.  We'll wrap/clamp in the shader in that case.
	// This happens whether we have OES_texture_npot or not.
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (w != bufferWidth || h != bufferHeight) {
		return;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
}

static void ConvertColors(void *dstBuf, const void *srcBuf, GLuint dstFmt, int numPixels) {
	const u32 *src = (const u32 *)srcBuf;
	u32 *dst = (u32 *)dstBuf;
	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		{
#ifdef _M_SSE
			const __m128i maskB = _mm_set1_epi16(0x00F0);
			const __m128i maskG = _mm_set1_epi16(0x0F00);

			__m128i *srcp = (__m128i *)src;
			__m128i *dstp = (__m128i *)dst;
			const int sseChunks = numPixels / 8;
			for (int i = 0; i < sseChunks; ++i) {
				__m128i c = _mm_load_si128(&srcp[i]);
				__m128i v = _mm_srli_epi16(c, 12);
				v = _mm_or_si128(v, _mm_and_si128(_mm_srli_epi16(c, 4), maskB));
				v = _mm_or_si128(v, _mm_and_si128(_mm_slli_epi16(c, 4), maskG));
				v = _mm_or_si128(v, _mm_slli_epi16(c, 12));
				_mm_store_si128(&dstp[i], v);
			}
			// The remainder is done in chunks of 2, SSE was chunks of 8.
			int i = sseChunks * 8 / 2;
#else
			int i = 0;
			// TODO: NEON.
#endif
			for (; i < (numPixels + 1) / 2; i++) {
				u32 c = src[i];
				dst[i] = ((c >> 12) & 0x000F000F) |
				       ((c >> 4)  & 0x00F000F0) |
				       ((c << 4)  & 0x0F000F00) |
				       ((c << 12) & 0xF000F000);
			}
		}
		break;
	// Final Fantasy 2 uses this heavily in animated textures.
	case GL_UNSIGNED_SHORT_5_5_5_1:
		{
#ifdef _M_SSE
			const __m128i maskB = _mm_set1_epi16(0x003E);
			const __m128i maskG = _mm_set1_epi16(0x07C0);

			__m128i *srcp = (__m128i *)src;
			__m128i *dstp = (__m128i *)dst;
			const int sseChunks = numPixels / 8;
			for (int i = 0; i < sseChunks; ++i) {
				__m128i c = _mm_load_si128(&srcp[i]);
				__m128i v = _mm_srli_epi16(c, 15);
				v = _mm_or_si128(v, _mm_and_si128(_mm_srli_epi16(c, 9), maskB));
				v = _mm_or_si128(v, _mm_and_si128(_mm_slli_epi16(c, 1), maskG));
				v = _mm_or_si128(v, _mm_slli_epi16(c, 11));
				_mm_store_si128(&dstp[i], v);
			}
			// The remainder is done in chunks of 2, SSE was chunks of 8.
			int i = sseChunks * 8 / 2;
#else
			int i = 0;
			// TODO: NEON.
#endif
			for (; i < (numPixels + 1) / 2; i++) {
				u32 c = src[i];
				dst[i] = ((c >> 15) & 0x00010001) |
				       ((c >> 9)  & 0x003E003E) |
				       ((c << 1)  & 0x07C007C0) |
				       ((c << 11) & 0xF800F800);
			}
		}
		break;
	case GL_UNSIGNED_SHORT_5_6_5:
		{
#ifdef _M_SSE
			const __m128i maskG = _mm_set1_epi16(0x07E0);

			__m128i *srcp = (__m128i *)src;
			__m128i *dstp = (__m128i *)dst;
			const int sseChunks = numPixels / 8;
			for (int i = 0; i < sseChunks; ++i) {
				__m128i c = _mm_load_si128(&srcp[i]);
				__m128i v = _mm_srli_epi16(c, 11);
				v = _mm_or_si128(v, _mm_and_si128(c, maskG));
				v = _mm_or_si128(v, _mm_slli_epi16(c, 11));
				_mm_store_si128(&dstp[i], v);
			}
			// The remainder is done in chunks of 2, SSE was chunks of 8.
			int i = sseChunks * 8 / 2;
#else
			int i = 0;
			// TODO: NEON.
#endif
			for (; i < (numPixels + 1) / 2; i++) {
				u32 c = src[i];
				dst[i] = ((c >> 11) & 0x001F001F) |
				       ((c >> 0)  & 0x07E007E0) |
				       ((c << 11) & 0xF800F800);
			}
		}
		break;
	default:
		if (UseBGRA8888()) {
#ifdef _M_SSE
			const __m128i maskGA = _mm_set1_epi32(0xFF00FF00);

			__m128i *srcp = (__m128i *)src;
			__m128i *dstp = (__m128i *)dst;
			const int sseChunks = numPixels / 4;
			for (int i = 0; i < sseChunks; ++i) {
				__m128i c = _mm_load_si128(&srcp[i]);
				__m128i rb = _mm_andnot_si128(maskGA, c);
				c = _mm_and_si128(c, maskGA);

				__m128i b = _mm_srli_epi32(rb, 16);
				__m128i r = _mm_slli_epi32(rb, 16);
				c = _mm_or_si128(_mm_or_si128(c, r), b);
				_mm_store_si128(&dstp[i], c);
			}
			// The remainder starts right after those done via SSE.
			int i = sseChunks * 4;
#else
			int i = 0;
#endif
			for (; i < numPixels; i++) {
				u32 c = src[i];
				dst[i] = ((c >> 16) & 0x000000FF) |
				         ((c >> 0)  & 0xFF00FF00) |
				         ((c << 16) & 0x00FF0000);
			}
		} else {
			// No need to convert RGBA8888, right order already
			if (dst != src)
				memcpy(dst, src, numPixels * sizeof(u32));
		}
		break;
	}
}

void TextureCache::StartFrame() {
	lastBoundTexture = -1;
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

static inline u32 QuickTexHash(u32 addr, int bufw, int w, int h, GETextureFormat format) {
	const u32 sizeInRAM = (textureBitsPerPixel[format] * bufw * h) / 8;
	const u32 *checkp = (const u32 *) Memory::GetPointer(addr);

	return DoQuickTexHash(checkp, sizeInRAM);
}

inline bool TextureCache::TexCacheEntry::Matches(u16 dim2, u8 format2, int maxLevel2) {
	return dim == dim2 && format == format2 && maxLevel == maxLevel2;
}

void TextureCache::LoadClut() {
	u32 clutAddr = gstate.getClutAddress();
	if (Memory::IsValidAddress(clutAddr)) {
#ifdef _M_SSE
		int numBlocks = gstate.getClutLoadBlocks(); 
		clutTotalBytes_ = numBlocks * 32;
		const __m128i *source = (const __m128i *)Memory::GetPointerUnchecked(clutAddr);
		__m128i *dest = (__m128i *)clutBufRaw_;
		for (int i = 0; i < numBlocks; i++, source += 2, dest += 2) {
			__m128i data1 = _mm_loadu_si128(source);
			__m128i data2 = _mm_loadu_si128(source + 1);
			_mm_store_si128(dest, data1);
			_mm_store_si128(dest + 1, data2);
		}
#else
		clutTotalBytes_ = gstate.getClutLoadBytes();
		Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, clutTotalBytes_);
#endif
	} else {
		clutTotalBytes_ = gstate.getClutLoadBytes();
		memset(clutBufRaw_, 0xFF, clutTotalBytes_);
	}
	// Reload the clut next time.
	clutLastFormat_ = 0xFFFFFFFF;
}

void TextureCache::UpdateCurrentClut() {
	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	const u32 clutBase = gstate.getClutIndexStartPos();
	const u32 clutBaseBytes = clutBase * (clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	const u32 clutExtendedBytes = clutTotalBytes_ + clutBaseBytes;

	clutHash_ = DoReliableHash((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);

	// Avoid a copy when we don't need to convert colors.
	if (UseBGRA8888() || clutFormat != GE_CMODE_32BIT_ABGR8888) {
		ConvertColors(clutBufConverted_, clutBufRaw_, getClutDestFormat(clutFormat), clutExtendedBytes / sizeof(u16));
		clutBuf_ = clutBufConverted_;
	} else {
		clutBuf_ = clutBufRaw_;
	}

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (gstate.getClutPaletteFormat() == GE_CMODE_16BIT_ABGR4444 && gstate.isClutIndexSimple()) {
		const u16 *clut = GetCurrentClut<u16>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0xFFF0;
		for (int i = 0; i < 16; ++i) {
			if ((clut[i] & 0xf) != i) {
				clutAlphaLinear_ = false;
				break;
			}
			// Alpha 0 doesn't matter. 
			// TODO: Well, depending on blend mode etc, it can actually matter, although unlikely.
			if (i != 0 && (clut[i] & 0xFFF0) != clutAlphaLinearColor_) {
				clutAlphaLinear_ = false;
				break;
			}
		}
	}

	clutLastFormat_ = gstate.clutformat;
}

template <typename T>
inline const T *TextureCache::GetCurrentClut() {
	return (const T *)clutBuf_;
}

inline u32 TextureCache::GetCurrentClutHash() {
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

void TextureCache::SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		GLuint program = 0;
		if (entry->status & TexCacheEntry::STATUS_DEPALETTIZE) {
			program = depalShaderCache_->GetDepalettizeShader(framebuffer->format);
		}
		if (program) {
			GLuint clutTexture = depalShaderCache_->GetClutTexture(clutHash_, clutBuf_);
			FBO *depalFBO = framebufferManager_->GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, FBO_8888);
			fbo_bind_as_render_target(depalFBO);
			static const float pos[12] = {
				-1, -1, -1,
				 1, -1, -1,
				 1,  1, -1,
				-1,  1, -1
			};
			static const float uv[8] = {
				0, 0,
				1, 0,
				1, 1,
				0, 1,
			};
			static const GLubyte indices[4] = { 0, 1, 3, 2 };

			shaderManager_->DirtyLastShader();

			glUseProgram(program);

			GLint a_position = glGetAttribLocation(program, "a_position");
			GLint a_texcoord0 = glGetAttribLocation(program, "a_texcoord0");

			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glEnableVertexAttribArray(a_position);
			glEnableVertexAttribArray(a_texcoord0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, clutTexture);
			glActiveTexture(GL_TEXTURE0);

			framebufferManager_->BindFramebufferColor(framebuffer, true);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

			glDisable(GL_BLEND);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDisable(GL_SCISSOR_TEST);
			glDisable(GL_CULL_FACE);
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_STENCIL_TEST);
#if !defined(USING_GLES2)
			glDisable(GL_LOGIC_OP);
#endif
			glViewport(0, 0, framebuffer->renderWidth, framebuffer->renderHeight);

			glVertexAttribPointer(a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
			glVertexAttribPointer(a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, uv);
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
			glDisableVertexAttribArray(a_position);
			glDisableVertexAttribArray(a_texcoord0);

			fbo_bind_color_as_texture(depalFBO, 0);
			glstate.Restore();
			framebufferManager_->RebindFramebuffer();

			const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
			const u32 clutBase = gstate.getClutIndexStartPos();
			const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
			const u32 clutExtendedColors = (clutTotalBytes_ / bytesPerColor) + clutBase;

			TexCacheEntry::Status alphaStatus = CheckAlpha(clutBuf_, getClutDestFormat(gstate.getClutPaletteFormat()), clutExtendedColors, clutExtendedColors, 1);
			gstate_c.textureFullAlpha = alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL;
			gstate_c.textureSimpleAlpha = alphaStatus == TexCacheEntry::STATUS_ALPHA_SIMPLE;
		} else {
			entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;
			framebufferManager_->BindFramebufferColor(framebuffer);

			gstate_c.textureFullAlpha = framebuffer->format == GE_FORMAT_565;
			gstate_c.textureSimpleAlpha = gstate_c.textureFullAlpha;
		}

		// Keep the framebuffer alive.
		framebuffer->last_frame_used = gpuStats.numFlips;

		// We need to force it, since we may have set it on a texture before attaching.
		gstate_c.curTextureWidth = framebuffer->bufferWidth;
		gstate_c.curTextureHeight = framebuffer->bufferHeight;
		gstate_c.flipTexture = true;
		gstate_c.curTextureXOffset = fbTexInfo_[entry->addr].xOffset;
		gstate_c.curTextureYOffset = fbTexInfo_[entry->addr].yOffset;
		gstate_c.needShaderTexClamp = gstate_c.curTextureWidth != (u32)gstate.getTextureWidth(0) || gstate_c.curTextureHeight != (u32)gstate.getTextureHeight(0);
		if (gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0) {
			gstate_c.needShaderTexClamp = true;
		}
		SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);
	} else {
		if (framebuffer->fbo)
			framebuffer->fbo = 0;
		glBindTexture(GL_TEXTURE_2D, 0);
		gstate_c.needShaderTexClamp = false;
	}
}

bool TextureCache::SetOffsetTexture(u32 offset) {
	if (g_Config.iRenderingMode != FB_BUFFERED_MODE) {
		return false;
	}
	u32 texaddr = gstate.getTextureAddress(0);
	if (!Memory::IsValidAddress(texaddr) || !Memory::IsValidAddress(texaddr + offset)) {
		return false;
	}

	u64 cachekey = (u64)(texaddr & 0x3FFFFFFF) << 32;
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

	if (success) {
		SetTextureFramebuffer(entry, entry->framebuffer);
		lastBoundTexture = -1;
		entry->lastFrame = gpuStats.numFlips;
	}

	return success;
}

void TextureCache::SetTexture(bool force) {
#ifdef DEBUG_TEXTURES
	if (SetDebugTexture()) {
		// A different texture was bound, let's rebind next time.
		lastBoundTexture = -1;
		return;
	}
#endif

	if (force) {
		lastBoundTexture = -1;
	}

	u32 texaddr = gstate.getTextureAddress(0);
	if (!Memory::IsValidAddress(texaddr)) {
		// Bind a null texture and return.
		glBindTexture(GL_TEXTURE_2D, 0);
		lastBoundTexture = -1;
		return;
	}

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
	u64 cachekey = (u64)(texaddr & 0x3FFFFFFF) << 32;
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
	int maxLevel = gstate.getTextureMaxLevel();

	u32 texhash = MiniHash((const u32 *)Memory::GetPointer(texaddr));
	u32 fullhash = 0;

	TexCache::iterator iter = cache.find(cachekey);
	TexCacheEntry *entry = NULL;
	gstate_c.flipTexture = false;
	gstate_c.needShaderTexClamp = false;
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	bool replaceImages = false;

	if (iter != cache.end()) {
		entry = &iter->second;
		// Validate the texture still matches the cache entry.
		u16 dim = gstate.getTextureDimension(0);
		bool match = entry->Matches(dim, format, maxLevel);
#ifndef MOBILE_DEVICE
		match = match && host->GPUAllowTextureCache(texaddr);
#endif

		// Check for FBO - slow!
		if (entry->framebuffer) {
			if (match) {
				SetTextureFramebuffer(entry, entry->framebuffer);
				lastBoundTexture = -1;
				entry->lastFrame = gpuStats.numFlips;
				return;
			} else {
				// Make sure we re-evaluate framebuffers.
				DetachFramebuffer(entry, texaddr, entry->framebuffer);
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
						entry->framesUntilNextFullHash = std::min(512, entry->numFrames) + (entry->texture & 15);
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
				fullhash = QuickTexHash(texaddr, bufw, w, h, format);
				hashFail = true;
				rehash = false;
			}

			if (rehash && entry->GetHashStatus() != TexCacheEntry::STATUS_RELIABLE) {
				fullhash = QuickTexHash(texaddr, bufw, w, h, format);
				if (fullhash != entry->fullhash) {
					hashFail = true;
				} else if (entry->GetHashStatus() != TexCacheEntry::STATUS_HASHING && entry->numFrames > TexCacheEntry::FRAMES_REGAIN_TRUST) {
					// Reset to STATUS_HASHING.
					if (g_Config.bTextureBackoffCache) {
						entry->SetHashStatus(TexCacheEntry::STATUS_HASHING);
					}
					entry->status &= ~TexCacheEntry::STATUS_CHANGE_FREQUENT;
				}
			}

			if (hashFail) {
				match = false;
				entry->status |= TexCacheEntry::STATUS_UNRELIABLE;
				if (entry->numFrames < TEXCACHE_FRAME_CHANGE_FREQUENT) {
					entry->status |= TexCacheEntry::STATUS_CHANGE_FREQUENT;
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
							secondCache[secondKey] = *entry;
							doDelete = false;
						}
					}
				}
			}
		}

		if (match && (entry->status & TexCacheEntry::STATUS_TO_SCALE) && g_Config.iTexScalingLevel != 1 && texelsScaledThisFrame_ < TEXCACHE_MAX_TEXELS_SCALED) {
			// INFO_LOG(G3D, "Reloading texture to do the scaling we skipped..");
			match = false;
		}

		if (match) {
			// TODO: Mark the entry reliable if it's been safe for long enough?
			//got one!
			entry->lastFrame = gpuStats.numFlips;
			if (entry->texture != lastBoundTexture) {
				glBindTexture(GL_TEXTURE_2D, entry->texture);
				lastBoundTexture = entry->texture;
				gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
				gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;
			}
			UpdateSamplingParams(*entry, false);
			VERBOSE_LOG(G3D, "Texture at %08x Found in Cache, applying", texaddr);
			return; //Done!
		} else {
			entry->numInvalidated++;
			gpuStats.numTextureInvalidations++;
			DEBUG_LOG(G3D, "Texture different or overwritten, reloading at %08x", texaddr);
			if (doDelete) {
				if (entry->maxLevel == maxLevel && entry->dim == gstate.getTextureDimension(0) && entry->format == format && g_Config.iTexScalingLevel == 1) {
					// Actually, if size and number of levels match, let's try to avoid deleting and recreating.
					// Instead, let's use glTexSubImage to replace the images.
					replaceImages = true;
				} else {
					if (entry->texture == lastBoundTexture) {
						lastBoundTexture = -1;
					}
					glDeleteTextures(1, &entry->texture);
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

	// Always generate a texture name, we might need it if the texture is replaced later.
	if (!replaceImages) {
		entry->texture = AllocTextureName();
	}

	// Before we go reading the texture from memory, let's check for render-to-texture.
	for (size_t i = 0, n = fbCache_.size(); i < n; ++i) {
		auto framebuffer = fbCache_[i];
		AttachFramebuffer(entry, framebuffer->fb_address, framebuffer);
	}

	// If we ended up with a framebuffer, attach it - no texture decoding needed.
	if (entry->framebuffer) {
		SetTextureFramebuffer(entry, entry->framebuffer);
		lastBoundTexture = -1;
		entry->lastFrame = gpuStats.numFlips;
		return;
	}
	glBindTexture(GL_TEXTURE_2D, entry->texture);
	lastBoundTexture = entry->texture;

	// Adjust maxLevel to actually present levels..
	bool badMipSizes = false;
	for (int i = 0; i <= maxLevel; i++) {
		// If encountering levels pointing to nothing, adjust max level.
		u32 levelTexaddr = gstate.getTextureAddress(i);
		if (!Memory::IsValidAddress(levelTexaddr)) {
			maxLevel = i - 1;
			break;
		}

#ifndef USING_GLES2
		if (i > 0) {
			int tw = gstate.getTextureWidth(i);
			int th = gstate.getTextureHeight(i);
			if (tw != 1 && tw != (gstate.getTextureWidth(i - 1) >> 1))
				badMipSizes = true;
			else if (th != 1 && th != (gstate.getTextureHeight(i - 1) >> 1))
				badMipSizes = true;
		}
#endif
	}

	// In addition, simply don't load more than level 0 if g_Config.bMipMap is false.
	if (!g_Config.bMipMap) {
		maxLevel = 0;
	}

	// If GLES3 is available, we can preallocate the storage, which makes texture loading more efficient.
	GLenum dstFmt = GetDestFormat(format, gstate.getClutPaletteFormat());

	int scaleFactor;
	// Auto-texture scale upto 5x rendering resolution
	if (g_Config.iTexScalingLevel == 0) {
		scaleFactor = g_Config.iInternalResolution;
		if (scaleFactor == 0) {
			scaleFactor = (PSP_CoreParameter().renderWidth + 479) / 480;
		}

#ifndef MOBILE_DEVICE
		scaleFactor = std::min(gl_extensions.OES_texture_npot ? 5 : 4, scaleFactor);
		if (!gl_extensions.OES_texture_npot && scaleFactor == 3) {
			scaleFactor = 2;
		}
#else
		scaleFactor = std::min(gl_extensions.OES_texture_npot ? 3 : 2, scaleFactor);
#endif
	} else {
		scaleFactor = g_Config.iTexScalingLevel;
	}

	// Don't scale the PPGe texture.
	if (entry->addr > 0x05000000 && entry->addr < 0x08800000)
		scaleFactor = 1;

	if (scaleFactor != 1 && (entry->status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
		if (texelsScaledThisFrame_ >= TEXCACHE_MAX_TEXELS_SCALED) {
			entry->status |= TexCacheEntry::STATUS_TO_SCALE;
			scaleFactor = 1;
			// INFO_LOG(G3D, "Skipped scaling for now..");
		} else {
			entry->status &= ~TexCacheEntry::STATUS_TO_SCALE;
			texelsScaledThisFrame_ += w * h;
		}
	}

	// Disabled this due to issue #6075: https://github.com/hrydgard/ppsspp/issues/6075
	// This breaks Dangan Ronpa 2 with mipmapping enabled. Why? No idea, it shouldn't.
	// glTexStorage2D probably has few benefits for us anyway.
	if (false && gl_extensions.GLES3 && maxLevel > 0) {
		// glTexStorage2D requires the use of sized formats.
		GLenum storageFmt = GL_RGBA8;
		switch (dstFmt) {
		case GL_UNSIGNED_BYTE: storageFmt = GL_RGBA8; break;
		case GL_UNSIGNED_SHORT_5_6_5: storageFmt = GL_RGB565; break;
		case GL_UNSIGNED_SHORT_4_4_4_4: storageFmt = GL_RGBA4; break;
		case GL_UNSIGNED_SHORT_5_5_5_1: storageFmt = GL_RGB5_A1; break;
		default:
			ERROR_LOG(G3D, "Unknown dstfmt %i", (int)dstFmt);
			break;
		}
		// TODO: This may cause bugs, since it hard-sets the texture w/h, and we might try to reuse it later with a different size.
		glTexStorage2D(GL_TEXTURE_2D, maxLevel + 1, storageFmt, w * scaleFactor, h * scaleFactor);
		// Make sure we don't use glTexImage2D after glTexStorage2D.
		replaceImages = true;
	}

	// GLES2 doesn't have support for a "Max lod" which is critical as PSP games often
	// don't specify mips all the way down. As a result, we either need to manually generate
	// the bottom few levels or rely on OpenGL's autogen mipmaps instead, which might not
	// be as good quality as the game's own (might even be better in some cases though).

	// Always load base level texture here 

	LoadTextureLevel(*entry, 0, replaceImages, scaleFactor, dstFmt);
	
	// Mipmapping only enable when texture scaling disable
	if (maxLevel > 0 && g_Config.iTexScalingLevel == 1) {
#ifndef USING_GLES2
		if (badMipSizes) {
			glGenerateMipmap(GL_TEXTURE_2D);
		} else {
			for (int i = 1; i <= maxLevel; i++) {
				LoadTextureLevel(*entry, i, replaceImages, scaleFactor, dstFmt);
			}
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, (float)maxLevel);
		}
#else 
		glGenerateMipmap(GL_TEXTURE_2D);
#endif
	} else {
#ifndef USING_GLES2
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
#else
		if (gl_extensions.GLES3) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		}
#endif
	}

	int aniso = 1 << g_Config.iAnisotropyLevel;
	float anisotropyLevel = (float) aniso > maxAnisotropyLevel ? maxAnisotropyLevel : (float) aniso;
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropyLevel);

	gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
	gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;

	UpdateSamplingParams(*entry, true);

	//glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	//glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
}

u32 TextureCache::AllocTextureName() {
	if (nameCache_.empty()) {
		nameCache_.resize(TEXCACHE_NAME_CACHE_SIZE);
		glGenTextures(TEXCACHE_NAME_CACHE_SIZE, &nameCache_[0]);
	}
	u32 name = nameCache_.back();
	nameCache_.pop_back();
	return name;
}

GLenum TextureCache::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormat(clutFormat);
	case GE_TFMT_4444:
		return GL_UNSIGNED_SHORT_4_4_4_4;
	case GE_TFMT_5551:
		return GL_UNSIGNED_SHORT_5_5_5_1;
	case GE_TFMT_5650:
		return GL_UNSIGNED_SHORT_5_6_5;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return GL_UNSIGNED_BYTE;
	}
}

void *TextureCache::DecodeTextureLevel(GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, GLenum dstFmt, int *bufwout) {
	void *finalBuf = NULL;

	u32 texaddr = gstate.getTextureAddress(level);
	if (texaddr & 0x00600000 && Memory::IsVRAMAddress(texaddr)) {
		// This means it's in a mirror, possibly a swizzled mirror.  Let's report.
		WARN_LOG_REPORT_ONCE(texmirror, G3D, "Decoding texture from VRAM mirror at %08x swizzle=%d", texaddr, gstate.isTextureSwizzled() ? 1 : 0);
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
			if (!gstate.isTextureSwizzled()) {
				if (clutAlphaLinear_ && mipmapShareClut) {
					DeIndexTexture4Optimal(tmpTexBuf16.data(), texptr, bufw * h, clutAlphaLinearColor_);
				} else {
					DeIndexTexture4(tmpTexBuf16.data(), texptr, bufw * h, clut);
				}
			} else {
				tmpTexBuf32.resize(std::max(bufw, w) * h);
				UnswizzleFromMem(texptr, bufw, 0, level);
				if (clutAlphaLinear_ && mipmapShareClut) {
					DeIndexTexture4Optimal(tmpTexBuf16.data(), (const u8 *)tmpTexBuf32.data(), bufw * h, clutAlphaLinearColor_);
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
			if (!gstate.isTextureSwizzled()) {
				DeIndexTexture4(tmpTexBuf32.data(), texptr, bufw * h, clut);
				finalBuf = tmpTexBuf32.data();
			} else {
				UnswizzleFromMem(texptr, bufw, 0, level);
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

		if (!gstate.isTextureSwizzled()) {
			int len = std::max(bufw, w) * h;
			tmpTexBuf16.resize(len);
			tmpTexBufRearrange.resize(len);
			finalBuf = tmpTexBuf16.data();
			ConvertColors(finalBuf, texptr, dstFmt, bufw * h);
		} else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			finalBuf = UnswizzleFromMem(texptr, bufw, 2, level);
			ConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
		}
		break;

	case GE_TFMT_8888:
		if (!gstate.isTextureSwizzled()) {
			// Special case: if we don't need to deal with packing, we don't need to copy.
			if ((g_Config.iTexScalingLevel == 1 && gl_extensions.EXT_unpack_subimage) || w == bufw) {
				if (UseBGRA8888()) {
					finalBuf = tmpTexBuf32.data();
					ConvertColors(finalBuf, texptr, dstFmt, bufw * h);
				} else {
					finalBuf = (void *)texptr;
				}
			} else {
				tmpTexBuf32.resize(std::max(bufw, w) * h);
				tmpTexBufRearrange.resize(std::max(bufw, w) * h);
				finalBuf = tmpTexBuf32.data();
				ConvertColors(finalBuf, texptr, dstFmt, bufw * h);
			}
		}
		else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			finalBuf = UnswizzleFromMem(texptr, bufw, 4, level);
			ConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
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
			ConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
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
			ConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
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
			ConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
		}
		break;

	default:
		ERROR_LOG_REPORT(G3D, "Unknown Texture Format %d!!!", format);
		return NULL;
	}

	if (!finalBuf) {
		ERROR_LOG_REPORT(G3D, "NO finalbuf! Will crash!");
	}

	if (!(g_Config.iTexScalingLevel == 1 && gl_extensions.EXT_unpack_subimage) && w != bufw) {
		int pixelSize;
		switch (dstFmt) {
		case GL_UNSIGNED_SHORT_4_4_4_4:
		case GL_UNSIGNED_SHORT_5_5_5_1:
		case GL_UNSIGNED_SHORT_5_6_5:
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

TextureCache::TexCacheEntry::Status TextureCache::CheckAlpha(u32 *pixelData, GLenum dstFmt, int stride, int w, int h) {
	// TODO: Could probably be optimized more.
	u32 hitZeroAlpha = 0;
	u32 hitSomeAlpha = 0;

	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		{
			const u32 *p = pixelData;
			for (int y = 0; y < h && hitSomeAlpha == 0; ++y) {
				for (int i = 0; i < (w + 1) / 2; ++i) {
					u32 a = p[i] & 0x000F000F;
					hitZeroAlpha |= a ^ 0x000F000F;
					if (a != 0x000F000F && a != 0x0000000F && a != 0x000F0000 && a != 0) {
						hitSomeAlpha = 1;
						break;
					}
				}
				p += stride/2;
			}
		}
		break;
	case GL_UNSIGNED_SHORT_5_5_5_1:
		{
			const u32 *p = pixelData;
			for (int y = 0; y < h; ++y) {
				for (int i = 0; i < (w + 1) / 2; ++i) {
					u32 a = p[i] & 0x00010001;
					hitZeroAlpha |= a ^ 0x00010001;
				}
				p += stride/2;
			}
		}
		break;
	case GL_UNSIGNED_SHORT_5_6_5:
		{
			// Never has any alpha.
		}
		break;
	default:
		{
			const u32 *p = pixelData;
			for (int y = 0; y < h && hitSomeAlpha == 0; ++y) {
				for (int i = 0; i < w; ++i) {
					u32 a = p[i] & 0xFF000000;
					hitZeroAlpha |= a ^ 0xFF000000;
					if (a != 0xFF000000 && a != 0) {
						hitSomeAlpha = 1;
						break;
					}
				}
				p += stride;
			}
		}
		break;
	}

	if (hitSomeAlpha != 0)
		return TexCacheEntry::STATUS_ALPHA_UNKNOWN;
	else if (hitZeroAlpha != 0)
		return TexCacheEntry::STATUS_ALPHA_SIMPLE;
	else
		return TexCacheEntry::STATUS_ALPHA_FULL;
}

void TextureCache::LoadTextureLevel(TexCacheEntry &entry, int level, bool replaceImages, int scaleFactor, GLenum dstFmt) {
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

	// Can restore these and remove the fixup at the end of DecodeTextureLevel on desktop GL and GLES 3.
	bool useUnpack = false;
	if ((g_Config.iTexScalingLevel == 1 && gl_extensions.EXT_unpack_subimage) && w != bufw) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, bufw);
		useUnpack = true;
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, texByteAlign);

	bool useBGRA = UseBGRA8888() && dstFmt == GL_UNSIGNED_BYTE;

	u32 *pixelData = (u32 *)finalBuf;
	if (scaleFactor > 1 && (entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0)
		scaler.Scale(pixelData, dstFmt, w, h, scaleFactor);

	if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
		TexCacheEntry::Status alphaStatus = CheckAlpha(pixelData, dstFmt, useUnpack ? bufw : w, w, h);
		entry.SetAlphaStatus(alphaStatus, level);
	} else {
		entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
	}

	GLuint components = dstFmt == GL_UNSIGNED_SHORT_5_6_5 ? GL_RGB : GL_RGBA;

	GLuint components2 = components;
	if (useBGRA) {
		components2 = GL_BGRA_EXT;
	}

	if (replaceImages) {
		glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, h, components2, dstFmt, pixelData);
	} else {
		glTexImage2D(GL_TEXTURE_2D, level, components, w, h, 0, components2, dstFmt, pixelData);
		GLenum err = glGetError();
		if (err == GL_OUT_OF_MEMORY) {
			lowMemoryMode_ = true;
			Decimate();
			// Try again.
			glTexImage2D(GL_TEXTURE_2D, level, components, w, h, 0, components2, dstFmt, pixelData);
		}
	}

	if (useUnpack) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
}

// Only used by Qt UI?
bool TextureCache::DecodeTexture(u8* output, GPUgstate state) {
	GPUgstate oldState = gstate;
	gstate = state;

	u32 texaddr = gstate.getTextureAddress(0);

	if (!Memory::IsValidAddress(texaddr)) {
		return false;
	}

	u32 texByteAlign = 1;
	GLenum dstFmt = 0;

	GETextureFormat format = gstate.getTextureFormat();
	GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
	u8 level = 0;

	int bufw = GetTextureBufw(level, texaddr, format);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	void *finalBuf = DecodeTextureLevel(format, clutformat, level, texByteAlign, dstFmt);
	if (finalBuf == NULL) {
		return false;
	}

	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		for (int y = 0; y < h; y++)
			for (int x = 0; x < bufw; x++) {
				u32 val = ((u16*)finalBuf)[y*bufw + x];
				u32 r = ((val>>12) & 0xF) * 17;
				u32 g = ((val>> 8) & 0xF) * 17;
				u32 b = ((val>> 4) & 0xF) * 17;
				u32 a = ((val>> 0) & 0xF) * 17;
				((u32*)output)[y*w + x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		break;

	case GL_UNSIGNED_SHORT_5_5_5_1:
		for (int y = 0; y < h; y++)
			for (int x = 0; x < bufw; x++) {
				u32 val = ((u16*)finalBuf)[y*bufw + x];
				u32 r = Convert5To8((val>>11) & 0x1F);
				u32 g = Convert5To8((val>> 6) & 0x1F);
				u32 b = Convert5To8((val>> 1) & 0x1F);
				u32 a = (val & 0x1) * 255;
				((u32*)output)[y*w + x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		break;

	case GL_UNSIGNED_SHORT_5_6_5:
		for (int y = 0; y < h; y++)
			for (int x = 0; x < bufw; x++) {
				u32 val = ((u16*)finalBuf)[y*bufw + x];
				u32 a = 0xFF;
				u32 r = Convert5To8((val>>11) & 0x1F);
				u32 g = Convert6To8((val>> 5) & 0x3F);
				u32 b = Convert5To8((val    ) & 0x1F);
				((u32*)output)[y*w + x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		break;

	default:
		for (int y = 0; y < h; y++)
			for (int x = 0; x < bufw; x++) {
				u32 val = ((u32*)finalBuf)[y*bufw + x];
				((u32*)output)[y*w + x] = ((val & 0xFF000000)) | ((val & 0x00FF0000)>>16) | ((val & 0x0000FF00)) | ((val & 0x000000FF)<<16);
			}
		break;
	}

	gstate = oldState;
	return true;
}
