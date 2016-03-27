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
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif

// Ugly.
extern int g_iNumVideos;

TextureCacheCommon::TextureCacheCommon()
	: cacheSizeEstimate_(0), nextTexture_(nullptr),
	clutLastFormat_(0xFFFFFFFF), clutTotalBytes_(0), clutMaxBytes_(0), clutRenderAddress_(0xFFFFFFFF) {
	// TODO: Clamp down to 256/1KB?  Need to check mipmapShareClut and clamp loadclut.
	clutBufRaw_ = (u32 *)AllocateAlignedMemory(1024 * sizeof(u32), 16);  // 4KB
	clutBufConverted_ = (u32 *)AllocateAlignedMemory(1024 * sizeof(u32), 16);  // 4KB

	// Zap so we get consistent behavior if the game fails to load some of the CLUT.
	memset(clutBufRaw_, 0, 1024 * sizeof(u32));
	memset(clutBufConverted_, 0, 1024 * sizeof(u32));

	// These buffers will grow if necessary, but most won't need more than this.
	tmpTexBuf32.resize(512 * 512);  // 1MB
	tmpTexBuf16.resize(512 * 512);  // 0.5MB
	tmpTexBufRearrange.resize(512 * 512);   // 1MB
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
	magFilt = (gstate.texfilter >> 8) & 1;
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

void TextureCacheCommon::UpdateMaxSeenV(bool throughMode) {
	// If the texture is >= 512 pixels tall...
	if (nextTexture_->dim >= 0x900) {
		// Texture scale/offset and gen modes don't apply in through.
		// So we can optimize how much of the texture we look at.
		if (throughMode) {
			if (nextTexture_->maxSeenV == 0 && gstate_c.vertBounds.maxV > 0) {
				// Let's not hash less than 272, we might use more later and have to rehash.  272 is very common.
				nextTexture_->maxSeenV = std::max((u16)272, gstate_c.vertBounds.maxV);
			} else if (gstate_c.vertBounds.maxV > nextTexture_->maxSeenV) {
				// The max height changed, so we're better off hashing the entire thing.
				nextTexture_->maxSeenV = 512;
				nextTexture_->status |= TexCacheEntry::STATUS_FREE_CHANGE;
			}
		} else {
			// Otherwise, we need to reset to ensure we use the whole thing.
			// Can't tell how much is used.
			// TODO: We could tell for texcoord UV gen, and apply scale to max?
			nextTexture_->maxSeenV = 512;
		}
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
			const u64 mirrorlessKey = it->first & ~0x0060000000000000ULL;
			// Let's still make sure it's in the cache range.
			if (mirrorlessKey >= cacheKey && mirrorlessKey <= cacheKeyEnd) {
				AttachFramebuffer(&it->second, addr, framebuffer);
			}
		}
		break;

	case NOTIFY_FB_DESTROYED:
		fbCache_.erase(std::remove(fbCache_.begin(), fbCache_.end(), framebuffer), fbCache_.end());

		// We may have an offset texture attached.  So we use fbTexInfo as a guide.
		// We're not likely to have many attached framebuffers.
		for (auto it = fbTexInfo_.begin(); it != fbTexInfo_.end(); ) {
			u64 cachekey = it->first;
			// We might erase, so move to the next one already (which won't become invalid.)
			++it;

			DetachFramebuffer(&cache[cachekey], addr, framebuffer);
		}
		break;
	}
}
void TextureCacheCommon::AttachFramebufferValid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo) {
	const u64 cachekey = entry->CacheKey();
	const bool hasInvalidFramebuffer = entry->framebuffer == nullptr || entry->invalidHint == -1;
	const bool hasOlderFramebuffer = entry->framebuffer != nullptr && entry->framebuffer->last_frame_render < framebuffer->last_frame_render;
	bool hasFartherFramebuffer = false;

	if (!hasInvalidFramebuffer && !hasOlderFramebuffer) {
		// If it's valid, but the offset is greater, then we still win.
		if (fbTexInfo_[cachekey].yOffset == fbInfo.yOffset)
			hasFartherFramebuffer = fbTexInfo_[cachekey].xOffset > fbInfo.xOffset;
		else
			hasFartherFramebuffer = fbTexInfo_[cachekey].yOffset > fbInfo.yOffset;
	}

	if (hasInvalidFramebuffer || hasOlderFramebuffer || hasFartherFramebuffer) {
		if (entry->framebuffer == nullptr) {
			cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
		}
		entry->framebuffer = framebuffer;
		entry->invalidHint = 0;
		entry->status &= ~TextureCacheCommon::TexCacheEntry::STATUS_DEPALETTIZE;
		entry->maxLevel = 0;
		fbTexInfo_[cachekey] = fbInfo;
		framebuffer->last_frame_attached = gpuStats.numFlips;
		host->GPUNotifyTextureAttachment(entry->addr);
	} else if (entry->framebuffer == framebuffer) {
		framebuffer->last_frame_attached = gpuStats.numFlips;
	}
}

void TextureCacheCommon::AttachFramebufferInvalid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo) {
	const u64 cachekey = entry->CacheKey();

	if (entry->framebuffer == nullptr || entry->framebuffer == framebuffer) {
		if (entry->framebuffer == nullptr) {
			cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
		}
		entry->framebuffer = framebuffer;
		entry->invalidHint = -1;
		entry->status &= ~TextureCacheCommon::TexCacheEntry::STATUS_DEPALETTIZE;
		entry->maxLevel = 0;
		fbTexInfo_[cachekey] = fbInfo;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

void TextureCacheCommon::DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer) {
	const u64 cachekey = entry->CacheKey();

	if (entry->framebuffer == framebuffer) {
		cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);
		entry->framebuffer = 0;
		fbTexInfo_.erase(cachekey);
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

void TextureCacheCommon::NotifyConfigChanged() {
	int scaleFactor;

	// 0 means automatic texture scaling, up to 5x, based on resolution.
	if (g_Config.iTexScalingLevel == 0) {
		scaleFactor = g_Config.iInternalResolution;
		// Automatic resolution too?  Okay.
		if (scaleFactor == 0) {
			if (!g_Config.IsPortrait()) {
				scaleFactor = (PSP_CoreParameter().pixelWidth + 479) / 480;
			} else {
				scaleFactor = (PSP_CoreParameter().pixelHeight + 479) / 480;
			}
		}

		// Mobile devices don't get the higher scale factors, too expensive. Very rough way to decide though...
		if (!gstate_c.Supports(GPU_IS_MOBILE)) {
			scaleFactor = std::min(5, scaleFactor);
		} else {
			scaleFactor = std::min(3, scaleFactor);
		}
	} else {
		scaleFactor = g_Config.iTexScalingLevel;
	}

	if (!gstate_c.Supports(GPU_SUPPORTS_OES_TEXTURE_NPOT)) {
		// Reduce the scale factor to a power of two (e.g. 2 or 4) if textures must be a power of two.
		while ((scaleFactor & (scaleFactor - 1)) != 0) {
			--scaleFactor;
		}
	}

	// Just in case, small display with auto resolution or something.
	if (scaleFactor <= 0) {
		scaleFactor = 1;
	}

	standardScaleFactor_ = scaleFactor;
}

void TextureCacheCommon::LoadClut(u32 clutAddr, u32 loadBytes) {
	clutTotalBytes_ = loadBytes;
	clutRenderAddress_ = 0xFFFFFFFF;

	if (Memory::IsValidAddress(clutAddr)) {
		if (Memory::IsVRAMAddress(clutAddr)) {
			// Clear the uncached bit, etc. to match framebuffers.
			const u32 clutFramebufAddr = clutAddr & 0x3FFFFFFF;
			const u32 clutFramebufEnd = clutFramebufAddr + loadBytes;
			static const u32 MAX_CLUT_OFFSET = 4096;

			clutRenderOffset_ = MAX_CLUT_OFFSET;
			for (size_t i = 0, n = fbCache_.size(); i < n; ++i) {
				auto framebuffer = fbCache_[i];
				const u32 fb_address = framebuffer->fb_address | 0x04000000;
				const u32 bpp = framebuffer->drawnFormat == GE_FORMAT_8888 ? 4 : 2;
				u32 offset = clutFramebufAddr - fb_address;

				// Is this inside the framebuffer at all?
				bool matchRange = fb_address + framebuffer->fb_stride * bpp > clutFramebufAddr && fb_address < clutFramebufEnd;
				// And is it inside the rendered area?  Sometimes games pack data outside.
				bool matchRegion = ((offset / bpp) % framebuffer->fb_stride) < framebuffer->width;
				if (matchRange && matchRegion && offset < clutRenderOffset_) {
					framebuffer->last_frame_clut = gpuStats.numFlips;
					framebuffer->usageFlags |= FB_USAGE_CLUT;
					clutRenderAddress_ = framebuffer->fb_address;
					clutRenderOffset_ = offset;
					if (offset == 0) {
						break;
					}
				}
			}
		}

		// It's possible for a game to (successfully) access outside valid memory.
		u32 bytes = Memory::ValidSize(clutAddr, loadBytes);
		if (clutRenderAddress_ != 0xFFFFFFFF && !g_Config.bDisableSlowFramebufEffects) {
			DownloadFramebufferForClut(clutRenderAddress_, clutRenderOffset_ + bytes);
			Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, bytes);
			if (bytes < loadBytes) {
				memset((u8 *)clutBufRaw_ + bytes, 0x00, loadBytes - bytes);
			}
		} else {
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
			if (bytes < loadBytes) {
				memset((u8 *)clutBufRaw_ + bytes, 0x00, loadBytes - bytes);
			}
#endif
		}
	} else {
		memset(clutBufRaw_, 0x00, loadBytes);
	}
	// Reload the clut next time.
	clutLastFormat_ = 0xFFFFFFFF;
	clutMaxBytes_ = std::max(clutMaxBytes_, loadBytes);
}

void TextureCacheCommon::UnswizzleFromMem(u32 *dest, u32 destPitch, const u8 *texptr, u32 bufw, u32 height, u32 bytesPerPixel) {
	// Note: bufw is always aligned to 16 bytes, so rowWidth is always >= 16.
	const u32 rowWidth = (bytesPerPixel > 0) ? (bufw * bytesPerPixel) : (bufw / 2);
	// A visual mapping of unswizzling, where each letter is 16-byte and 8 letters is a block:
	//
	// ABCDEFGH IJKLMNOP
	//      ->
	// AI
	// BJ
	// CK
	// ...
	//
	// bxc is the number of blocks in the x direction, and byc the number in the y direction.
	const int bxc = rowWidth / 16;
	// The height is not always aligned to 8, but rounds up.
	int byc = (height + 7) / 8;

	DoUnswizzleTex16(texptr, dest, bxc, byc, destPitch);
}

void *TextureCacheCommon::RearrangeBuf(void *inBuf, u32 inRowBytes, u32 outRowBytes, int h, bool allowInPlace) {
	const u8 *read = (const u8 *)inBuf;
	void *outBuf = inBuf;
	u8 *write = (u8 *)inBuf;
	if (outRowBytes > inRowBytes || !allowInPlace) {
		write = (u8 *)tmpTexBufRearrange.data();
		outBuf = tmpTexBufRearrange.data();
	}
	for (int y = 0; y < h; y++) {
		memmove(write, read, outRowBytes);
		read += inRowBytes;
		write += outRowBytes;
	}

	return outBuf;
}

bool TextureCacheCommon::GetCurrentClutBuffer(GPUDebugBuffer &buffer) {
	const u32 bpp = gstate.getClutPaletteFormat() == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const u32 pixels = 1024 / bpp;

	buffer.Allocate(pixels, 1, (GEBufferFormat)gstate.getClutPaletteFormat());
	memcpy(buffer.GetData(), clutBufRaw_, 1024);
	return true;
}

u32 TextureCacheCommon::EstimateTexMemoryUsage(const TexCacheEntry *entry) {
	const u16 dim = entry->dim;
	const u8 dimW = ((dim >> 0) & 0xf);
	const u8 dimH = ((dim >> 8) & 0xf);

	u32 pixelSize = 2;
	switch (entry->format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		// We assume cluts always point to 8888 for simplicity.
		pixelSize = 4;
		break;
	case GE_TFMT_4444:
	case GE_TFMT_5551:
	case GE_TFMT_5650:
		break;

	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		pixelSize = 4;
		break;
	}

	// This in other words multiplies by w and h.
	return pixelSize << (dimW + dimH);
}
