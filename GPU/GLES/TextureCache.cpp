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

#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/Common/TextureDecoder.h"
#include "Core/Config.h"

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

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

// TODO: This helps when you have plenty of VRAM, sometimes quite a bit.
// But on Android, it sometimes causes out of memory that isn't recovered from.
#if !defined(USING_GLES2) && !defined(_XBOX)
#define USE_SECONDARY_CACHE 1
#else
#define USE_SECONDARY_CACHE 0
#endif

extern int g_iNumVideos;

TextureCache::TextureCache() : clearCacheNextFrame_(false), lowMemoryMode_(false), clutBuf_(NULL) {
	lastBoundTexture = -1;
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	// This is 5MB of temporary storage. Might be possible to shrink it.
	tmpTexBuf32.resize(1024 * 512);  // 2MB
	tmpTexBuf16.resize(1024 * 512);  // 1MB
	tmpTexBufRearrange.resize(1024 * 512);   // 2MB
	clutBufConverted_ = (u32 *)AllocateAlignedMemory(4096 * sizeof(u32), 16);  // 16KB
	clutBufRaw_ = (u32 *)AllocateAlignedMemory(4096 * sizeof(u32), 16);  // 16KB
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyLevel);
	SetupQuickTexHash();
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
	}
	if (cache.size() + secondCache.size()) {
		INFO_LOG(G3D, "Texture cached cleared from %i textures", (int)(cache.size() + secondCache.size()));
		cache.clear();
		secondCache.clear();
	}
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
		if (iter->second.lastFrame + TEXTURE_KILL_AGE < gpuStats.numFlips) {
			glDeleteTextures(1, &iter->second.texture);
			cache.erase(iter++);
		}
		else
			++iter;
	}
#if USE_SECONDARY_CACHE
	for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ) {
		if (lowMemoryMode_ || iter->second.lastFrame + TEXTURE_KILL_AGE < gpuStats.numFlips) {
			glDeleteTextures(1, &iter->second.texture);
			secondCache.erase(iter++);
		}
		else
			++iter;
	}
#endif
}

void TextureCache::Invalidate(u32 addr, int size, GPUInvalidationType type) {
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

void TextureCache::InvalidateAll(GPUInvalidationType /*unused*/) {
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

void TextureCache::ClearNextFrame() {
	clearCacheNextFrame_ = true;
}


template <typename T>
inline void AttachFramebufferValid(T &entry, VirtualFramebuffer *framebuffer) {
	const bool hasInvalidFramebuffer = entry->framebuffer == 0 || entry->invalidHint == -1;
	const bool hasOlderFramebuffer = entry->framebuffer != 0 && entry->framebuffer->last_frame_render < framebuffer->last_frame_render;
	if (hasInvalidFramebuffer || hasOlderFramebuffer) {
		entry->framebuffer = framebuffer;
		entry->invalidHint = 0;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

template <typename T>
inline void AttachFramebufferInvalid(T &entry, VirtualFramebuffer *framebuffer) {
	if (entry->framebuffer == 0 || entry->framebuffer == framebuffer) {
		entry->framebuffer = framebuffer;
		entry->invalidHint = -1;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

inline void TextureCache::AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, bool exactMatch) {
	// If they match exactly, it's non-CLUT and from the top left.
	if (exactMatch) {
		// Apply to non-buffered and buffered mode only.
		if (!(g_Config.iRenderingMode == FB_NON_BUFFERED_MODE || g_Config.iRenderingMode == FB_BUFFERED_MODE))
			return;

		DEBUG_LOG(G3D, "Render to texture detected at %08x!", address);
		if (!entry->framebuffer || entry->invalidHint == -1) {
			if (entry->format != framebuffer->format) {
				WARN_LOG_REPORT_ONCE(diffFormat1, G3D, "Render to texture with different formats %d != %d", entry->format, framebuffer->format);
				// If it already has one, let's hope that one is correct.
				AttachFramebufferInvalid(entry, framebuffer);
			} else {
				AttachFramebufferValid(entry, framebuffer);
			}
			// TODO: Delete the original non-fbo texture too.
		}
	} else {
		// Apply to buffered mode only.
		if (!(g_Config.iRenderingMode == FB_BUFFERED_MODE))
			return;

		// 3rd Birthday (and possibly other games) render to a 16 bit clut texture.
		const bool compatFormat = framebuffer->format == entry->format
			|| (framebuffer->format == GE_FORMAT_8888 && entry->format == GE_TFMT_CLUT32)
			|| (framebuffer->format != GE_FORMAT_8888 && entry->format == GE_TFMT_CLUT16);

		// Is it at least the right stride?
		if (framebuffer->fb_stride == entry->bufw && compatFormat) {
			if (framebuffer->format != entry->format) {
				WARN_LOG_REPORT_ONCE(diffFormat2, G3D, "Render to texture with different formats %d != %d at %08x", entry->format, framebuffer->format, address);
				// TODO: Use an FBO to translate the palette?
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

inline void TextureCache::DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer) {
	if (entry->framebuffer == framebuffer) {
		entry->framebuffer = 0;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

void TextureCache::NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg) {
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

void *TextureCache::UnswizzleFromMem(u32 texaddr, u32 bufw, u32 bytesPerPixel, u32 level) {
	const u32 rowWidth = (bytesPerPixel > 0) ? (bufw * bytesPerPixel) : (bufw / 2);
	const u32 pitch = rowWidth / 4;
	const int bxc = rowWidth / 16;
	int byc = (gstate.getTextureHeight(level) + 7) / 8;
	if (byc == 0)
		byc = 1;

	u32 ydest = 0;
	if (rowWidth >= 16) {
		const u32 *src = (u32 *) Memory::GetPointer(texaddr);
		u32 *ydestp = tmpTexBuf32.data();
		for (int by = 0; by < byc; by++) {
			u32 *xdest = ydestp;
			for (int bx = 0; bx < bxc; bx++) {
				u32 *dest = xdest;
				for (int n = 0; n < 8; n++) {
					memcpy(dest, src, 16);
					dest += pitch;
					src += 4;
				}
				xdest += 4;
			}
			ydestp += (rowWidth * 8) / 4;
		}
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

void *TextureCache::ReadIndexedTex(int level, u32 texaddr, int bytesPerIndex, GLuint dstFmt, int bufw) {
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
				DeIndexTexture<u16>(tmpTexBuf16.data(), texaddr, length, clut);
				break;

			case 4:
				DeIndexTexture<u32>(tmpTexBuf16.data(), texaddr, length, clut);
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
				DeIndexTexture<u16>(tmpTexBuf32.data(), texaddr, length, clut);
				break;

			case 4:
				DeIndexTexture<u32>(tmpTexBuf32.data(), texaddr, length, clut);
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
// TODO: Dirty-check this against the current texture.
void TextureCache::UpdateSamplingParams(TexCacheEntry &entry, bool force) {
	int minFilt = gstate.texfilter & 0x7;
	int magFilt = (gstate.texfilter>>8) & 1;
	bool sClamp = gstate.isTexCoordClampedS();
	bool tClamp = gstate.isTexCoordClampedT();

	bool noMip = (gstate.texlevel & 0xFFFFFF) == 0x000001 || (gstate.texlevel & 0xFFFFFF) == 0x100001 ;  // Fix texlevel at 0

	if (entry.maxLevel == 0) {
		// Enforce no mip filtering, for safety.
		minFilt &= 1; // no mipmaps yet
	} else {
		// TODO: Is this a signed value? Which direction?
		float lodBias = 0.0; // -(float)((gstate.texlevel >> 16) & 0xFF) / 16.0f;
		if (force || entry.lodBias != lodBias) {
#ifndef USING_GLES2
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, lodBias);
#endif
			entry.lodBias = lodBias;
		}
	}

	if ((g_Config.iTexFiltering == LINEAR && !gstate.isColorTestEnabled() && !gstate.isAlphaTestEnabled()) || (g_Config.iTexFiltering == LINEARFMV && g_iNumVideos)) {
		magFilt |= 1;
		minFilt |= 1;
	}
	// Force Nearest when color test enabled and rendering resolution greater than 480x272
	if (g_Config.iTexFiltering == NEAREST || (gstate.isColorTestEnabled() && g_Config.iInternalResolution != 1 && gstate.isModeThrough())) {
		magFilt &= ~1;
		minFilt &= ~1;
	}

	if (!g_Config.bMipMap || noMip) {
		magFilt &= 1;
		minFilt &= 1;
	}

	if (force || entry.minFilt != minFilt) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, MinFiltGL[minFilt]);
		entry.minFilt = minFilt;
	}
	if (force || entry.magFilt != magFilt) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, MagFiltGL[magFilt]);
		entry.magFilt = magFilt;
	}

	// Workaround for a clamping bug in pre-HD ATI/AMD drivers
	if (gl_extensions.ATIClampBug && entry.framebuffer)
		return;

	if (force || entry.sClamp != sClamp) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		entry.sClamp = sClamp;
	}
	if (force || entry.tClamp != tClamp) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		entry.tClamp = tClamp;
	}
}

static void ConvertColors(void *dstBuf, const void *srcBuf, GLuint dstFmt, int numPixels) {
	const u32 *src = (const u32 *)srcBuf;
	u32 *dst = (u32 *)dstBuf;
	// TODO: NEON.
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
		{
			// No need to convert RGBA8888, right order already
			if (dst != src)
				memcpy(dst, src, numPixels * sizeof(u32));
		}
		break;
	}
}

void TextureCache::StartFrame() {
	lastBoundTexture = -1;
	if(clearCacheNextFrame_) {
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

	clutHash_ = XXH32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);

	// Avoid a copy when we don't need to convert colors.
	if (clutFormat != GE_CMODE_32BIT_ABGR8888) {
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

void TextureCache::SetTextureFramebuffer(TexCacheEntry *entry)
{
	entry->framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		// For now, let's not bind FBOs that we know are off (invalidHint will be -1.)
		// But let's still not use random memory.
		if (entry->framebuffer->fbo) {
			fbo_bind_color_as_texture(entry->framebuffer->fbo, 0);
			// Keep the framebuffer alive.
			// TODO: Dangerous if it sets a new one?
			entry->framebuffer->last_frame_used = gpuStats.numFlips;
		} else {
			glBindTexture(GL_TEXTURE_2D, 0);
			gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		}
		UpdateSamplingParams(*entry, false);
		gstate_c.curTextureWidth = entry->framebuffer->width;
		gstate_c.curTextureHeight = entry->framebuffer->height;
		gstate_c.flipTexture = true;
		gstate_c.textureFullAlpha = entry->framebuffer->format == GE_FORMAT_565;
	} else {
		if (entry->framebuffer->fbo)
			entry->framebuffer->fbo = 0;
		glBindTexture(GL_TEXTURE_2D, 0);
	}
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
	int maxLevel = ((gstate.texmode >> 16) & 0x7);

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
#ifndef USING_GLES2
		match &= host->GPUAllowTextureCache(texaddr);
#endif

		// Check for FBO - slow!
		if (entry->framebuffer) {
			if (match) {
				SetTextureFramebuffer(entry);
				lastBoundTexture = -1;
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
					entry->status &= ~TexCacheEntry::STATUS_MASK;
				}
			}

			if (hashFail) {
				match = false;
				entry->status |= TexCacheEntry::STATUS_UNRELIABLE;
				entry->numFrames = 0;

				// Don't give up just yet.  Let's try the secondary cache if it's been invalidated before.
				// If it's failed a bunch of times, then the second cache is just wasting time and VRAM.
#if USE_SECONDARY_CACHE
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
#endif
			}
		}

		if (match) {
			// TODO: Mark the entry reliable if it's been safe for long enough?
			//got one!
			entry->lastFrame = gpuStats.numFlips;
			if (entry->texture != lastBoundTexture) {
				glBindTexture(GL_TEXTURE_2D, entry->texture);
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
						lastBoundTexture = -1;
					}
					glDeleteTextures(1, &entry->texture);
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
		entry->status = TexCacheEntry::STATUS_HASHING;
	}

	if ((bufw == 0 || (gstate.texbufwidth[0] & 0xf800) != 0) && texaddr >= PSP_GetUserMemoryBase()) {
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
		glGenTextures(1, &entry->texture);
	}

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
		lastBoundTexture = -1;
		entry->lastFrame = gpuStats.numFlips;
		return;
	}
	glBindTexture(GL_TEXTURE_2D, entry->texture);
	lastBoundTexture = entry->texture;

	// Adjust maxLevel to actually present levels..
	for (int i = 0; i <= maxLevel; i++) {
		// If encountering levels pointing to nothing, adjust max level.
		u32 levelTexaddr = gstate.getTextureAddress(i);
		if (!Memory::IsValidAddress(levelTexaddr)) {
			maxLevel = i - 1;
			break;
		}
	}

	// In addition, simply don't load more than level 0 if g_Config.bMipMap is false.
	if (!g_Config.bMipMap) {
		maxLevel = 0;
	}

	// If GLES3 is available, we can preallocate the storage, which makes texture loading more efficient.
	GLenum dstFmt = GetDestFormat(format, gstate.getClutPaletteFormat());

#if 0   // Needs more testing
#ifdef MAY_HAVE_GLES3
	if (gl_extensions.GLES3) {
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
		glTexStorage2D(GL_TEXTURE_2D, maxLevel + 1, storageFmt, w, h);
		// Make sure we don't use glTexImage2D after glTexStorage2D.
		replaceImages = true;
	}
#endif
#endif

	// GLES2 doesn't have support for a "Max lod" which is critical as PSP games often
	// don't specify mips all the way down. As a result, we either need to manually generate
	// the bottom few levels or rely on OpenGL's autogen mipmaps instead, which might not
	// be as good quality as the game's own (might even be better in some cases though).

	// For now, I choose to use autogen mips on GLES2 and the game's own on other platforms.
	// As is usual, GLES3 will solve this problem nicely but wide distribution of that is
	// years away.
	//
	// Actually, seems we reverted to autogen mipmaps on all platforms.
	LoadTextureLevel(*entry, 0, replaceImages, dstFmt);
	if (maxLevel > 0) {
		glGenerateMipmap(GL_TEXTURE_2D);
		/*
		for (int i = 0; i <= maxLevel; i++) {
			LoadTextureLevel(*entry, i, replaceImages);
		}
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, (float)maxLevel);
		*/
	} else {
		// TODO: This is supported on GLES3
#if !defined(USING_GLES2)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
#endif
	}

	int aniso = 1 << g_Config.iAnisotropyLevel;
	float anisotropyLevel = (float) aniso > maxAnisotropyLevel ? maxAnisotropyLevel : (float) aniso;
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropyLevel);

	UpdateSamplingParams(*entry, true);

	//glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	//glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	gstate_c.textureFullAlpha = (entry->status & TexCacheEntry::STATUS_ALPHA_MASK) == TexCacheEntry::STATUS_ALPHA_FULL;
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

	int bufw = GetTextureBufw(level, texaddr, format);
	if (bufwout)
		*bufwout = bufw;
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	const u8 *texptr = Memory::GetPointer(texaddr);

	switch (format) {
	case GE_TFMT_CLUT4:
		{
		const bool mipmapShareClut = (gstate.texmode & 0x100) == 0;
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
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texaddr, 1, dstFmt, bufw);
		break;

	case GE_TFMT_CLUT16:
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texaddr, 2, dstFmt, bufw);
		break;

	case GE_TFMT_CLUT32:
		texByteAlign = texByteAlignMap[gstate.getClutPaletteFormat()];
		finalBuf = ReadIndexedTex(level, texaddr, 4, dstFmt, bufw);
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
			ConvertColors(finalBuf, Memory::GetPointer(texaddr), dstFmt, bufw * h);
		} else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			finalBuf = UnswizzleFromMem(texaddr, bufw, 2, level);
			ConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
		}
		break;

	case GE_TFMT_8888:
		if (!gstate.isTextureSwizzled()) {
			// Special case: if we don't need to deal with packing, we don't need to copy.
			if ((g_Config.iTexScalingLevel == 1 && gl_extensions.EXT_unpack_subimage) || w == bufw) {
				finalBuf = Memory::GetPointer(texaddr);
			} else {
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

	if ((g_Config.iTexScalingLevel != 1 || !gl_extensions.EXT_unpack_subimage) && w != bufw) {
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

void TextureCache::CheckAlpha(TexCacheEntry &entry, u32 *pixelData, GLenum dstFmt, int w, int h) {
	// TODO: Could probably be optimized more.
	u32 hitZeroAlpha = 0;
	u32 hitSomeAlpha = 0;

	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		{
			const u32 *p = pixelData;
			for (int i = 0; i < (w * h + 1) / 2; ++i) {
				u32 a = p[i] & 0x000F000F;
				hitZeroAlpha |= a ^ 0x000F000F;
				if (a != 0x000F000F && a != 0x0000000F && a != 0x000F0000 && a != 0) {
					hitSomeAlpha = 1;
					break;
				}
			}
		}
		break;
	case GL_UNSIGNED_SHORT_5_5_5_1:
		{
			const u32 *p = pixelData;
			for (int i = 0; i < (w * h + 1) / 2; ++i) {
				u32 a = p[i] & 0x00010001;
				hitZeroAlpha |= a ^ 0x00010001;
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

void TextureCache::LoadTextureLevel(TexCacheEntry &entry, int level, bool replaceImages, GLenum dstFmt) {
	// TODO: only do this once
	u32 texByteAlign = 1;

	// TODO: Look into using BGRA for 32-bit textures when the GL_EXT_texture_format_BGRA8888 extension is available, as it's faster than RGBA on some chips.

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

	int scaleFactor;
	//Auto-texture scale upto 5x rendering resolution
	if (g_Config.iTexScalingLevel == 0)
#ifndef USING_GLES2
		scaleFactor = std::min(5, g_Config.iInternalResolution);
#else
		scaleFactor = std::min(3, g_Config.iInternalResolution);
#endif
	else
		scaleFactor = g_Config.iTexScalingLevel;

	// Don't scale the PPGe texture.
	if (entry.addr > 0x05000000 && entry.addr < 0x08800000)
		scaleFactor = 1;

	u32 *pixelData = (u32 *)finalBuf;
	if (scaleFactor > 1 && entry.numInvalidated == 0)
		scaler.Scale(pixelData, dstFmt, w, h, scaleFactor);
	// Or always?
	if (entry.numInvalidated == 0)
		CheckAlpha(entry, pixelData, dstFmt, w, h);
	else
		entry.status |= TexCacheEntry::STATUS_ALPHA_UNKNOWN;

	GLuint components = dstFmt == GL_UNSIGNED_SHORT_5_6_5 ? GL_RGB : GL_RGBA;

	if (replaceImages) {
		glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, h, components, dstFmt, pixelData);
	} else {
		glTexImage2D(GL_TEXTURE_2D, level, components, w, h, 0, components, dstFmt, pixelData);
		GLenum err = glGetError();
		if (err == GL_OUT_OF_MEMORY) {
			lowMemoryMode_ = true;
			Decimate();
			// Try again.
			glTexImage2D(GL_TEXTURE_2D, level, components, w, h, 0, components, dstFmt, pixelData);
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
