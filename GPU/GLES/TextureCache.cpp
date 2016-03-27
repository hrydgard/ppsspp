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

#include "ext/xxhash.h"
#include "i18n/i18n.h"
#include "math/math_util.h"
#include "profiler/profiler.h"

#include "Common/ColorConv.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/GLStateCache.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/FragmentShaderGenerator.h"
#include "GPU/GLES/DepalettizeShader.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TransformPipeline.h"
#include "GPU/Common/TextureDecoder.h"
#include "UI/OnScreenDisplay.h"

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
// Note: only used when hash backoff is disabled.
#define TEXCACHE_FRAME_CHANGE_FREQUENT_REGAIN_TRUST 33

#define TEXCACHE_NAME_CACHE_SIZE 16

#define TEXCACHE_MAX_TEXELS_SCALED (256*256)  // Per frame

#define TEXCACHE_MIN_PRESSURE 16 * 1024 * 1024  // Total in GL
#define TEXCACHE_SECOND_MIN_PRESSURE 4 * 1024 * 1024

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

#define INVALID_TEX -1

// Hack!
extern int g_iNumVideos;

TextureCache::TextureCache() : secondCacheSizeEstimate_(0), clearCacheNextFrame_(false), lowMemoryMode_(false), clutBuf_(NULL), texelsScaledThisFrame_(0) {
	timesInvalidatedAllThisFrame_ = 0;
	lastBoundTexture = INVALID_TEX;
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;

	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyLevel);
	SetupTextureDecoder();

	nextTexture_ = nullptr;
}

TextureCache::~TextureCache() {
	Clear(true);
}

void TextureCache::Clear(bool delete_them) {
	glBindTexture(GL_TEXTURE_2D, 0);
	lastBoundTexture = INVALID_TEX;
	if (delete_them) {
		for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %i", iter->second.textureName);
			glDeleteTextures(1, &iter->second.textureName);
		}
		for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %i", iter->second.textureName);
			glDeleteTextures(1, &iter->second.textureName);
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
		cacheSizeEstimate_ = 0;
		secondCacheSizeEstimate_ = 0;
	}
	fbTexInfo_.clear();
}

void TextureCache::DeleteTexture(TexCache::iterator it) {
	glDeleteTextures(1, &it->second.textureName);
	auto fbInfo = fbTexInfo_.find(it->first);
	if (fbInfo != fbTexInfo_.end()) {
		fbTexInfo_.erase(fbInfo);
	}

	cacheSizeEstimate_ -= EstimateTexMemoryUsage(&it->second);
	cache.erase(it);
}

// Removes old textures.
void TextureCache::Decimate() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	if (cacheSizeEstimate_ >= TEXCACHE_MIN_PRESSURE) {
		const u32 had = cacheSizeEstimate_;

		glBindTexture(GL_TEXTURE_2D, 0);
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
			if (lowMemoryMode_ || iter->second.lastFrame + TEXTURE_SECOND_KILL_AGE < gpuStats.numFlips) {
				glDeleteTextures(1, &iter->second.textureName);
				secondCacheSizeEstimate_ -= EstimateTexMemoryUsage(&iter->second);
				secondCache.erase(iter++);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(G3D, "Decimated second texture cache, saved %d estimated bytes - now %d bytes", had - secondCacheSizeEstimate_, secondCacheSizeEstimate_);
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

bool TextureCache::AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset) {
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
		if (entry->format != (GETextureFormat)framebuffer->format) {
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
			if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
				GETexLevelMode mode = gstate.getTexLevelMode();
				switch (mode) {
				case GE_TEXLEVEL_MODE_AUTO:
					// TODO
					break;
				case GE_TEXLEVEL_MODE_CONST:
					// Sigh, LOD_BIAS is not even in ES 3.0..
#ifndef USING_GLES2
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, lodBias);
#endif
					break;
				case GE_TEXLEVEL_MODE_SLOPE:
					// TODO
					break;
				}
			}
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

	minFilt &= 1;  // framebuffers can't mipmap.

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
		ConvertRGBA4444ToABGR4444((u16 *)dst, (const u16 *)src, numPixels);
		break;
	// Final Fantasy 2 uses this heavily in animated textures.
	case GL_UNSIGNED_SHORT_5_5_5_1:
		ConvertRGBA5551ToABGR1555((u16 *)dst, (const u16 *)src, numPixels);
		break;
	case GL_UNSIGNED_SHORT_5_6_5:
		ConvertRGB565ToBGR565((u16 *)dst, (const u16 *)src, numPixels);
		break;
	default:
		if (UseBGRA8888()) {
			ConvertRGBA8888ToBGRA8888(dst, src, numPixels);
		} else {
			// No need to convert RGBA8888, right order already
			if (dst != src)
				memcpy(dst, src, numPixels * sizeof(u32));
		}
		break;
	}
}

void TextureCache::StartFrame() {
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

static inline u32 QuickTexHash(u32 addr, int bufw, int w, int h, GETextureFormat format, TextureCache::TexCacheEntry *entry) {
	if (h == 512 && entry->maxSeenV < 512 && entry->maxSeenV != 0) {
		h = (int)entry->maxSeenV;
	}

	const u32 sizeInRAM = (textureBitsPerPixel[format] * bufw * h) / 8;
	const u32 *checkp = (const u32 *) Memory::GetPointer(addr);

	return DoQuickTexHash(checkp, sizeInRAM);
}

void TextureCache::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
	const u32 clutBaseBytes = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutBase * sizeof(u32)) : (clutBase * sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	//
	// TODO: Actually, this seems like a hack.  The game can upload part of a CLUT and reference other data.
	// clutTotalBytes_ is the last amount uploaded.  We should hash clutMaxBytes_, but this will often hash
	// unrelated old entries for small palettes.
	// Adding clutBaseBytes may just be mitigating this for some usage patterns.
	const u32 clutExtendedBytes = std::min(clutTotalBytes_ + clutBaseBytes, clutMaxBytes_);

	clutHash_ = DoReliableHash32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);

	// Avoid a copy when we don't need to convert colors.
	if (UseBGRA8888() || clutFormat != GE_CMODE_32BIT_ABGR8888) {
		const int numColors = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutMaxBytes_ / sizeof(u32)) : (clutMaxBytes_ / sizeof(u16));
		ConvertColors(clutBufConverted_, clutBufRaw_, getClutDestFormat(clutFormat), numColors);
		clutBuf_ = clutBufConverted_;
	} else {
		clutBuf_ = clutBufRaw_;
	}

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (clutFormat == GE_CMODE_16BIT_ABGR4444 && clutIndexIsSimple) {
		const u16_le *clut = GetCurrentClut<u16_le>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0xFFF0;
		for (int i = 0; i < 16; ++i) {
			u16 step = clutAlphaLinearColor_ | i;
			if (clut[i] != step) {
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
	_dbg_assert_msg_(G3D, framebuffer != nullptr, "Framebuffer must not be null.");

	framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		const u64 cachekey = entry->CacheKey();
		const auto &fbInfo = fbTexInfo_[cachekey];

		DepalShader *depal = nullptr;
		if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
			depal = depalShaderCache_->GetDepalettizeShader(clutFormat, framebuffer->drawnFormat);
		}
		if (depal) {
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
		gstate_c.curTextureXOffset = fbInfo.xOffset;
		gstate_c.curTextureYOffset = fbInfo.yOffset;
		gstate_c.needShaderTexClamp = gstate_c.curTextureWidth != (u32)gstate.getTextureWidth(0) || gstate_c.curTextureHeight != (u32)gstate.getTextureHeight(0);
		if (gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0) {
			gstate_c.needShaderTexClamp = true;
		}

		nextTexture_ = entry;
	} else {
		if (framebuffer->fbo) {
			fbo_destroy(framebuffer->fbo);
			framebuffer->fbo = 0;
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		gstate_c.needShaderTexClamp = false;
	}
}

void TextureCache::ApplyTexture() {
	if (nextTexture_ == nullptr) {
		return;
	}

	if (nextTexture_->framebuffer) {
		ApplyTextureFramebuffer(nextTexture_, nextTexture_->framebuffer);
	} else {
		UpdateMaxSeenV(gstate.isModeThrough());

		if (nextTexture_->textureName != lastBoundTexture) {
			glBindTexture(GL_TEXTURE_2D, nextTexture_->textureName);
			lastBoundTexture = nextTexture_->textureName;
		}
		UpdateSamplingParams(*nextTexture_, false);
	}

	nextTexture_ = nullptr;
}

void TextureCache::DownloadFramebufferForClut(u32 clutAddr, u32 bytes) {
	framebufferManager_->DownloadFramebufferForClut(clutAddr, bytes);
}

class TextureShaderApplier {
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

	TextureShaderApplier(DepalShader *shader, float bufferW, float bufferH, int renderW, int renderH)
		: shader_(shader), bufferW_(bufferW), bufferH_(bufferH), renderW_(renderW), renderH_(renderH) {
		static const Pos pos[4] = {
			{-1, -1, -1},
			{ 1, -1, -1},
			{ 1,  1, -1},
			{-1,  1, -1},
		};
		memcpy(pos_, pos, sizeof(pos_));

		static const UV uv[4] = {
			{0, 0},
			{1, 0},
			{1, 1},
			{0, 1},
		};
		memcpy(uv_, uv, sizeof(uv_));
	}

	void ApplyBounds(const KnownVertexBounds &bounds, u32 uoff, u32 voff) {
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

			const float left = u1 * invHalfWidth - 1.0f;
			const float right = u2 * invHalfWidth - 1.0f;
			const float top = v1 * invHalfHeight - 1.0f;
			const float bottom = v2 * invHalfHeight - 1.0f;
			// Points are: BL, BR, TR, TL.
			pos_[0] = Pos(left, bottom, -1.0f);
			pos_[1] = Pos(right, bottom, -1.0f);
			pos_[2] = Pos(right, top, -1.0f);
			pos_[3] = Pos(left, top, -1.0f);

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			uv_[0] = UV(uvleft, uvbottom);
			uv_[1] = UV(uvright, uvbottom);
			uv_[2] = UV(uvright, uvtop);
			uv_[3] = UV(uvleft, uvtop);
		}
	}

	void Use(TransformDrawEngine *transformDraw) {
		glUseProgram(shader_->program);

		// Restore will rebind all of the state below.
		if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
			static const GLubyte indices[4] = { 0, 1, 3, 2 };
			transformDraw->BindBuffer(pos_, sizeof(pos_), uv_, sizeof(uv_));
			transformDraw->BindElementBuffer(indices, sizeof(indices));
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		glEnableVertexAttribArray(shader_->a_position);
		glEnableVertexAttribArray(shader_->a_texcoord0);
	}

	void Shade() {
		static const GLubyte indices[4] = { 0, 1, 3, 2 };

		glstate.blend.force(false);
		glstate.colorMask.force(true, true, true, true);
		glstate.scissorTest.force(false);
		glstate.cullFace.force(false);
		glstate.depthTest.force(false);
		glstate.stencilTest.force(false);
#if !defined(USING_GLES2)
		glstate.colorLogicOp.force(false);
#endif
		glViewport(0, 0, renderW_, renderH_);

		if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
			glVertexAttribPointer(shader_->a_position, 3, GL_FLOAT, GL_FALSE, 12, 0);
			glVertexAttribPointer(shader_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, (void *)sizeof(pos_));
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, 0);
		} else {
			glVertexAttribPointer(shader_->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos_);
			glVertexAttribPointer(shader_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, uv_);
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
		}
		glDisableVertexAttribArray(shader_->a_position);
		glDisableVertexAttribArray(shader_->a_texcoord0);

		glstate.Restore();
	}

protected:
	DepalShader *shader_;
	Pos pos_[4];
	UV uv_[4];
	float bufferW_;
	float bufferH_;
	int renderW_;
	int renderH_;
};

void TextureCache::ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	DepalShader *depal = nullptr;
	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		depal = depalShaderCache_->GetDepalettizeShader(clutFormat, framebuffer->drawnFormat);
	}
	if (depal) {
		GLuint clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);
		FBO *depalFBO = framebufferManager_->GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, FBO_8888);
		fbo_bind_as_render_target(depalFBO);
		shaderManager_->DirtyLastShader();

		TextureShaderApplier shaderApply(depal, framebuffer->bufferWidth, framebuffer->bufferHeight, framebuffer->renderWidth, framebuffer->renderHeight);
		shaderApply.ApplyBounds(gstate_c.vertBounds, gstate_c.curTextureXOffset, gstate_c.curTextureYOffset);
		shaderApply.Use(transformDraw_);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, clutTexture);
		glActiveTexture(GL_TEXTURE0);

		framebufferManager_->BindFramebufferColor(GL_TEXTURE0, gstate.getFrameBufRawAddress(), framebuffer, BINDFBCOLOR_SKIP_COPY);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		shaderApply.Shade();

		fbo_bind_color_as_texture(depalFBO, 0);
	} else {
		framebufferManager_->BindFramebufferColor(GL_TEXTURE0, gstate.getFrameBufRawAddress(), framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);
	}

	framebufferManager_->RebindFramebuffer();
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);

	lastBoundTexture = INVALID_TEX;
}

bool TextureCache::SetOffsetTexture(u32 offset) {
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
		// This will not apply the texture immediately.
		SetTextureFramebuffer(entry, entry->framebuffer);
		entry->lastFrame = gpuStats.numFlips;
		return true;
	}

	return false;
}

void TextureCache::SetTexture(bool force) {
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
		glBindTexture(GL_TEXTURE_2D, 0);
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

	u32 texhash = MiniHash((const u32 *)Memory::GetPointerUnchecked(texaddr));
	u32 fullhash = 0;

	TexCache::iterator iter = cache.find(cachekey);
	TexCacheEntry *entry = NULL;
	gstate_c.needShaderTexClamp = false;
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;
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
						entry->framesUntilNextFullHash = std::min(512, entry->numFrames) + (entry->textureName & 15);
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
			if (entry->textureName != lastBoundTexture) {
				gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
				gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;
			}
			nextTexture_ = entry;
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
					if (entry->textureName == lastBoundTexture) {
						lastBoundTexture = INVALID_TEX;
					}
					glDeleteTextures(1, &entry->textureName);
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
		// Proceeding here can cause a crash.
		return;
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

	entry->fullhash = fullhash == 0 ? QuickTexHash(texaddr, bufw, w, h, format, entry) : fullhash;
	entry->cluthash = cluthash;

	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;

	gstate_c.curTextureWidth = w;
	gstate_c.curTextureHeight = h;

	// For the estimate, we assume cluts always point to 8888 for simplicity.
	cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

	// Always generate a texture name, we might need it if the texture is replaced later.
	if (!replaceImages) {
		entry->textureName = AllocTextureName();
	}

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
	glBindTexture(GL_TEXTURE_2D, entry->textureName);
	lastBoundTexture = entry->textureName;

	// Adjust maxLevel to actually present levels..
	bool badMipSizes = false;
	for (u32 i = 0; i <= maxLevel; i++) {
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
	GLenum dstFmt = GetDestFormat(format, gstate.getClutPaletteFormat());

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
	if (maxLevel > 0 && scaleFactor == 1) {
		if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
			if (badMipSizes) {
				// WARN_LOG(G3D, "Bad mipmap for texture sized %dx%dx%d - autogenerating", w, h, (int)format);
				glGenerateMipmap(GL_TEXTURE_2D);
			} else {
				for (int i = 1; i <= maxLevel; i++) {
					LoadTextureLevel(*entry, i, replaceImages, scaleFactor, dstFmt);
				}
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, (float)maxLevel);
			}
		} else {
			// Avoid PowerVR driver bug
			if (w > 1 && h > 1 && !(h > w && (gl_extensions.bugs & BUG_PVR_GENMIPMAP_HEIGHT_GREATER))) {  // Really! only seems to fail if height > width
				// NOTICE_LOG(G3D, "Generating mipmap for texture sized %dx%d%d", w, h, (int)format);
				glGenerateMipmap(GL_TEXTURE_2D);
			} else {
				entry->maxLevel = 0;
			}
		}
	} else if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}

	if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY)) {
		int aniso = 1 << g_Config.iAnisotropyLevel;
		float anisotropyLevel = (float) aniso > maxAnisotropyLevel ? maxAnisotropyLevel : (float) aniso;
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropyLevel);
	}

	gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
	gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;

	// This will rebind it, but that's okay.
	nextTexture_ = entry;
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

void *TextureCache::DecodeTextureLevel(GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, GLenum dstFmt, int scaleFactor, int *bufwout) {
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
					DeIndexTexture4Optimal(tmpTexBuf16.data(), texptr, bufw * h, clutAlphaLinearColor_);
				} else {
					DeIndexTexture4(tmpTexBuf16.data(), texptr, bufw * h, clut);
				}
			} else {
				tmpTexBuf32.resize(std::max(bufw, w) * h);
				UnswizzleFromMem(tmpTexBuf32.data(), bufw / 2, texptr, bufw, h, 0);
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
			finalBuf = tmpTexBuf16.data();
			ConvertColors(finalBuf, texptr, dstFmt, bufw * h);
		} else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			UnswizzleFromMem(tmpTexBuf32.data(), bufw * 2, texptr, bufw, h, 2);
			finalBuf = tmpTexBuf32.data();
			ConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
		}
		break;

	case GE_TFMT_8888:
		if (!swizzled) {
			// Special case: if we don't need to deal with packing, we don't need to copy.
			if ((scaleFactor == 1 && gstate_c.Supports(GPU_SUPPORTS_UNPACK_SUBIMAGE)) || w == bufw) {
				if (UseBGRA8888()) {
					tmpTexBuf32.resize(std::max(bufw, w) * h);
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
		} else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			UnswizzleFromMem(tmpTexBuf32.data(), bufw * 4, texptr, bufw, h, 4);
			finalBuf = tmpTexBuf32.data();
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

	if (!(scaleFactor == 1 && gstate_c.Supports(GPU_SUPPORTS_UNPACK_SUBIMAGE)) && w != bufw) {
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
		finalBuf = RearrangeBuf(finalBuf, bufw * pixelSize, w * pixelSize, h);
	}

	return finalBuf;
}

TextureCache::TexCacheEntry::Status TextureCache::CheckAlpha(const u32 *pixelData, GLenum dstFmt, int stride, int w, int h) {
	CheckAlphaResult res;
	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		res = CheckAlphaABGR4444Basic(pixelData, stride, w, h);
		break;
	case GL_UNSIGNED_SHORT_5_5_5_1:
		res = CheckAlphaABGR1555Basic(pixelData, stride, w, h);
		break;
	case GL_UNSIGNED_SHORT_5_6_5:
		// Never has any alpha.
		res = CHECKALPHA_FULL;
		break;
	default:
		res = CheckAlphaRGBA8888Basic(pixelData, stride, w, h);
		break;
	}

	return (TexCacheEntry::Status)res;
}

void TextureCache::LoadTextureLevel(TexCacheEntry &entry, int level, bool replaceImages, int scaleFactor, GLenum dstFmt) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	bool useUnpack = false;
	bool useBGRA;
	u32 *pixelData;
	{

	PROFILE_THIS_SCOPE("decodetex");

	// TODO: only do this once
	u32 texByteAlign = 1;

	GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
	int bufw;
	void *finalBuf = DecodeTextureLevel(GETextureFormat(entry.format), clutformat, level, texByteAlign, dstFmt, scaleFactor, &bufw);
	if (finalBuf == NULL) {
		return;
	}

	gpuStats.numTexturesDecoded++;

	// Can restore these and remove the fixup at the end of DecodeTextureLevel on desktop GL and GLES 3.
	if (scaleFactor == 1 && gstate_c.Supports(GPU_SUPPORTS_UNPACK_SUBIMAGE) && w != bufw) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, bufw);
		useUnpack = true;
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, texByteAlign);

	useBGRA = UseBGRA8888() && dstFmt == GL_UNSIGNED_BYTE;

	pixelData = (u32 *)finalBuf;
	if (scaleFactor > 1)
		scaler.Scale(pixelData, dstFmt, w, h, scaleFactor);

	if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
		TexCacheEntry::Status alphaStatus = CheckAlpha(pixelData, dstFmt, useUnpack ? bufw : w, w, h);
		entry.SetAlphaStatus(alphaStatus, level);
	} else {
		entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
	}
	}

	GLuint components = dstFmt == GL_UNSIGNED_SHORT_5_6_5 ? GL_RGB : GL_RGBA;

	GLuint components2 = components;
	if (useBGRA) {
		components2 = GL_BGRA_EXT;
	}

	if (replaceImages) {
		PROFILE_THIS_SCOPE("repltex");
		glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, h, components2, dstFmt, pixelData);
	} else {
		PROFILE_THIS_SCOPE("loadtex");
		glTexImage2D(GL_TEXTURE_2D, level, components, w, h, 0, components2, dstFmt, pixelData);
		if (!lowMemoryMode_) {
			GLenum err = glGetError();
			if (err == GL_OUT_OF_MEMORY) {
				WARN_LOG_REPORT(G3D, "Texture cache ran out of GPU memory; switching to low memory mode");
				lowMemoryMode_ = true;
				decimationCounter_ = 0;
				Decimate();
				// Try again, now that we've cleared out textures in lowMemoryMode_.
				glTexImage2D(GL_TEXTURE_2D, level, components, w, h, 0, components2, dstFmt, pixelData);

				I18NCategory *err = GetI18NCategory("Error");
				if (scaleFactor > 1) {
					osm.Show(err->T("Warning: Video memory FULL, reducing upscaling and switching to slow caching mode"), 2.0f);
				} else {
					osm.Show(err->T("Warning: Video memory FULL, switching to slow caching mode"), 2.0f);
				}
			} else if (err != GL_NO_ERROR) {
				// We checked the err anyway, might as well log if there is one.
				WARN_LOG(G3D, "Got an error in texture upload: %08x", err);
			}
		}
	}

	if (useUnpack) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
}

// Only used by Qt UI?
bool TextureCache::DecodeTexture(u8* output, const GPUgstate &state) {
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

	void *finalBuf = DecodeTextureLevel(format, clutformat, level, texByteAlign, dstFmt, 1);
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
