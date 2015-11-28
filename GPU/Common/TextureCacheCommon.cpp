// Copyright (c) 2013- PPSSPP Project.

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
#include "Common/MemoryUtil.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"

// Ugly.
extern int g_iNumVideos;

TextureCacheCommon::TextureCacheCommon()
	: nextTexture_(nullptr),
	clutLastFormat_(0xFFFFFFFF), clutTotalBytes_(0), clutMaxBytes_(0), clutRenderAddress_(0xFFFFFFFF) {
	// TODO: Clamp down to 256/1KB?  Need to check mipmapShareClut and clamp loadclut.
	clutBufRaw_ = (u32 *)AllocateAlignedMemory(1024 * sizeof(u32), 16);  // 4KB
	clutBufConverted_ = (u32 *)AllocateAlignedMemory(1024 * sizeof(u32), 16);  // 4KB

	// Zap so we get consistent behavior if the game fails to load some of the CLUT.
	memset(clutBufRaw_, 0, 1024 * sizeof(u32));
	memset(clutBufConverted_, 0, 1024 * sizeof(u32));
}

TextureCacheCommon::~TextureCacheCommon() {
	FreeAlignedMemory(clutBufConverted_);
	FreeAlignedMemory(clutBufRaw_);
}

bool TextureCacheCommon::SetOffsetTexture(u32 offset) {
	return false;
}

int TextureCacheCommon::AttachedDrawingHeight() {
	if (nextTexture_) {
		if (nextTexture_->framebuffer) {
			return nextTexture_->framebuffer->height;
		}
		u16 dim = nextTexture_->dim;
		const u8 dimY = dim >> 8;
		return 1 << dimY;
	}
	return 0;
}

void TextureCacheCommon::GetSamplingParams(int &minFilt, int &magFilt, bool &sClamp, bool &tClamp, float &lodBias, u8 maxLevel) {
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

	if (g_Config.iTexFiltering == TEX_FILTER_LINEAR_VIDEO && g_iNumVideos > 0 && (gstate.getTextureDimension(0) & 0xF) >= 9) {
		magFilt |= 1;
		minFilt |= 1;
	}
	if (g_Config.iTexFiltering == TEX_FILTER_LINEAR && (!gstate.isColorTestEnabled() || IsColorTestTriviallyTrue())) {
		if (!gstate.isAlphaTestEnabled() || IsAlphaTestTriviallyTrue()) {
			magFilt |= 1;
			minFilt |= 1;
		}
	}
	bool forceNearest = g_Config.iTexFiltering == TEX_FILTER_NEAREST;
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

void TextureCacheCommon::NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg) {
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
			// TODO: Wait, account for address?
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

void TextureCacheCommon::LoadClut(u32 clutAddr, u32 loadBytes) {
	// Clear the uncached bit, etc. to match framebuffers.
	clutAddr = clutAddr & 0x3FFFFFFF;
	bool foundFramebuffer = false;

	clutRenderAddress_ = 0xFFFFFFFF;
	for (size_t i = 0, n = fbCache_.size(); i < n; ++i) {
		auto framebuffer = fbCache_[i];
		if ((framebuffer->fb_address | 0x04000000) == clutAddr) {
			framebuffer->last_frame_clut = gpuStats.numFlips;
			framebuffer->usageFlags |= FB_USAGE_CLUT;
			foundFramebuffer = true;
			WARN_LOG_REPORT_ONCE(clutrenderdx9, G3D, "Using rendered CLUT for texture decode at %08x (%dx%dx%d)", clutAddr, framebuffer->width, framebuffer->height, framebuffer->colorDepth);
			clutRenderAddress_ = framebuffer->fb_address;
		}
	}

	clutTotalBytes_ = loadBytes;
	if (Memory::IsValidAddress(clutAddr)) {
		// It's possible for a game to (successfully) access outside valid memory.
		u32 bytes = Memory::ValidSize(clutAddr, loadBytes);
		if (foundFramebuffer && !g_Config.bDisableSlowFramebufEffects) {
			gpu->PerformMemoryDownload(clutAddr, bytes);
		}

#ifdef _M_SSE
		int numBlocks = bytes / 16;
		if (bytes == loadBytes) {
			const __m128i *source = (const __m128i *)Memory::GetPointerUnchecked(clutAddr);
			__m128i *dest = (__m128i *)clutBufRaw_;
			for (int i = 0; i < numBlocks; i++, source += 2, dest += 2) {
				__m128i data1 = _mm_loadu_si128(source);
				__m128i data2 = _mm_loadu_si128(source + 1);
				_mm_store_si128(dest, data1);
				_mm_store_si128(dest + 1, data2);
			}
		} else {
			Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, bytes);
			if (bytes < loadBytes) {
				memset((u8 *)clutBufRaw_ + bytes, 0x00, loadBytes - bytes);
			}
		}
#else
		Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, bytes);
		if (bytes < clutTotalBytes_) {
			memset((u8 *)clutBufRaw_ + bytes, 0x00, clutTotalBytes_ - bytes);
		}
#endif
	} else {
		memset(clutBufRaw_, 0x00, loadBytes);
	}
	// Reload the clut next time.
	clutLastFormat_ = 0xFFFFFFFF;
	clutMaxBytes_ = std::max(clutMaxBytes_, loadBytes);
}
