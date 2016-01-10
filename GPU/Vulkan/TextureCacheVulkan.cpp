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
#include "thin3d/VulkanContext.h"
#include "Common/ColorConv.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/DepalettizeShaderVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
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

#define VULKAN_4444_FORMAT VK_FORMAT_R4G4B4A4_UNORM_PACK16
#define VULKAN_1555_FORMAT VK_FORMAT_R5G5B5A1_UNORM_PACK16   // TODO: Switch to the one that matches the PSP better.
#define VULKAN_565_FORMAT  VK_FORMAT_R5G6B5_UNORM_PACK16
#define VULKAN_8888_FORMAT VK_FORMAT_R8G8B8A8_UNORM

// Hack!
extern int g_iNumVideos;

SamplerCache::~SamplerCache() {
	for (auto iter : cache_) {
		vulkan_->QueueDelete(iter.second);
	}
}

VkSampler SamplerCache::GetOrCreateSampler(const SamplerCacheKey &key) {
	auto iter = cache_.find(key);
	if (iter != cache_.end()) {
		return iter->second;
	}

	VkSamplerCreateInfo samp = {};
	samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samp.pNext = nullptr;
	samp.addressModeU = key.sClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeV = key.tClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	samp.compareEnable = false;
	samp.compareOp = VK_COMPARE_OP_ALWAYS;
	samp.flags = 0;
	samp.magFilter = (key.magFilt & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samp.minFilter = key.minFilt ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;  // TODO: Aniso
	samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; // key.4) ? ((key.magFilt & 2) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST) : VK_SAMPLER_MIPMAP_MODE_BASE;
	samp.maxAnisotropy = 1.0f;
	samp.maxLod = 0.0f; // 1000.0f;
	samp.minLod = 0.0f;
	samp.unnormalizedCoordinates = false;
	samp.mipLodBias = 0.0f;

	VkSampler sampler;
	VkResult res = vkCreateSampler(vulkan_->GetDevice(), &samp, nullptr, &sampler);
	assert(res == VK_SUCCESS);
	cache_[key] = sampler;
	return sampler;
}

TextureCacheVulkan::TextureCacheVulkan(VulkanContext *vulkan) : vulkan_(vulkan), samplerCache_(vulkan), cacheSizeEstimate_(0), secondCacheSizeEstimate_(0), clearCacheNextFrame_(false), lowMemoryMode_(false), clutBuf_(NULL), texelsScaledThisFrame_(0) {
	timesInvalidatedAllThisFrame_ = 0;
	lastBoundTexture = nullptr;
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;

	SetupTextureDecoder();

	nextTexture_ = nullptr;
}

TextureCacheVulkan::~TextureCacheVulkan() {
	Clear(true);
}

void TextureCacheVulkan::DownloadFramebufferForClut(u32 clutAddr, u32 bytes) {

}

static u32 EstimateTexMemoryUsage(const TextureCacheVulkan::TexCacheEntry *entry) {
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

void TextureCacheVulkan::Clear(bool delete_them) {
	lastBoundTexture = nullptr;
	if (delete_them) {
		for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %p", iter->second.vkTex);
			delete iter->second.vkTex;
		}
		for (TexCache::iterator iter = secondCache.begin(); iter != secondCache.end(); ++iter) {
			DEBUG_LOG(G3D, "Deleting texture %p", iter->second.vkTex);
			delete iter->second.vkTex;
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

void TextureCacheVulkan::DeleteTexture(TexCache::iterator it) {
	delete it->second.vkTex;
	auto fbInfo = fbTexInfo_.find(it->second.addr);
	if (fbInfo != fbTexInfo_.end()) {
		fbTexInfo_.erase(fbInfo);
	}

	cacheSizeEstimate_ -= EstimateTexMemoryUsage(&it->second);
	cache.erase(it);
}

// Removes old textures.
void TextureCacheVulkan::Decimate() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	if (cacheSizeEstimate_ >= TEXCACHE_MIN_PRESSURE) {
		const u32 had = cacheSizeEstimate_;

		lastBoundTexture = nullptr;
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
				delete iter->second.vkTex;
				secondCacheSizeEstimate_ -= EstimateTexMemoryUsage(&iter->second);
				secondCache.erase(iter++);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(G3D, "Decimated second texture cache, saved %d estimated bytes - now %d bytes", had - secondCacheSizeEstimate_, secondCacheSizeEstimate_);
	}
}

void TextureCacheVulkan::Invalidate(u32 addr, int size, GPUInvalidationType type) {
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

void TextureCacheVulkan::InvalidateAll(GPUInvalidationType /*unused*/) {
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

void TextureCacheVulkan::ClearNextFrame() {
	clearCacheNextFrame_ = true;
}


void TextureCacheVulkan::AttachFramebufferValid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo) {
	const bool hasInvalidFramebuffer = entry->framebuffer == nullptr || entry->invalidHint == -1;
	const bool hasOlderFramebuffer = entry->framebuffer != nullptr && entry->framebuffer->last_frame_render < framebuffer->last_frame_render;
	bool hasFartherFramebuffer = false;
	if (!hasInvalidFramebuffer && !hasOlderFramebuffer) {
		// If it's valid, but the offset is greater, then we still win.
		if (fbTexInfo_[entry->addr].yOffset == fbInfo.yOffset)
			hasFartherFramebuffer = fbTexInfo_[entry->addr].xOffset > fbInfo.xOffset;
		else
			hasFartherFramebuffer = fbTexInfo_[entry->addr].yOffset > fbInfo.yOffset;
	}
	if (hasInvalidFramebuffer || hasOlderFramebuffer || hasFartherFramebuffer) {
		if (entry->framebuffer == nullptr) {
			cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
		}
		entry->framebuffer = framebuffer;
		entry->invalidHint = 0;
		entry->status &= ~TextureCacheVulkan::TexCacheEntry::STATUS_DEPALETTIZE;
		entry->maxLevel = 0;
		fbTexInfo_[entry->addr] = fbInfo;
		framebuffer->last_frame_attached = gpuStats.numFlips;
		host->GPUNotifyTextureAttachment(entry->addr);
	} else if (entry->framebuffer == framebuffer) {
		framebuffer->last_frame_attached = gpuStats.numFlips;
	}
}

void TextureCacheVulkan::AttachFramebufferInvalid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo) {
	if (entry->framebuffer == nullptr || entry->framebuffer == framebuffer) {
		if (entry->framebuffer == nullptr) {
			cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
		}
		entry->framebuffer = framebuffer;
		entry->invalidHint = -1;
		entry->status &= ~TextureCacheVulkan::TexCacheEntry::STATUS_DEPALETTIZE;
		entry->maxLevel = 0;
		fbTexInfo_[entry->addr] = fbInfo;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

bool TextureCacheVulkan::AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset) {
	static const u32 MAX_SUBAREA_Y_OFFSET_SAFE = 32;

	AttachedFramebufferInfo fbInfo = { 0 };

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

inline void TextureCacheVulkan::DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer) {
	if (entry->framebuffer == framebuffer) {
		cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);
		entry->framebuffer = 0;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

void *TextureCacheVulkan::ReadIndexedTex(int level, const u8 *texptr, int bytesPerIndex, VkFormat dstFmt, int bufw) {
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
			UnswizzleFromMem(texptr, bufw, h, bytesPerIndex);
			switch (bytesPerIndex) {
			case 1:
				DeIndexTexture(tmpTexBuf16.data(), (u8 *)tmpTexBuf32.data(), length, clut);
				break;

			case 2:
				DeIndexTexture(tmpTexBuf16.data(), (u16 *)tmpTexBuf32.data(), length, clut);
				break;

			case 4:
				DeIndexTexture(tmpTexBuf16.data(), (u32 *)tmpTexBuf32.data(), length, clut);
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
			UnswizzleFromMem(texptr, bufw, h, bytesPerIndex);
			// Since we had to unswizzle to tmpTexBuf32, let's output to tmpTexBuf16.
			tmpTexBuf16.resize(std::max(bufw, w) * h * 2);
			u32 *dest32 = (u32 *)tmpTexBuf16.data();
			switch (bytesPerIndex) {
			case 1:
				DeIndexTexture(dest32, (u8 *)tmpTexBuf32.data(), length, clut);
				buf = dest32;
				break;

			case 2:
				DeIndexTexture(dest32, (u16 *)tmpTexBuf32.data(), length, clut);
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

VkFormat getClutDestFormatVulkan(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return VULKAN_4444_FORMAT;
	case GE_CMODE_16BIT_ABGR5551:
		return VULKAN_1555_FORMAT;
	case GE_CMODE_16BIT_BGR5650:
		return VULKAN_565_FORMAT;
	case GE_CMODE_32BIT_ABGR8888:
		return VULKAN_8888_FORMAT;
	}
	return VK_FORMAT_UNDEFINED;
}

static const u8 texByteAlignMap[] = { 2, 2, 2, 4 };

static const VkFilter MagFiltVK[2] = {
	VK_FILTER_NEAREST,
	VK_FILTER_LINEAR
};

void TextureCacheVulkan::UpdateSamplingParams(TexCacheEntry &entry, SamplerCacheKey &key) {
	// TODO: Make GetSamplingParams write SamplerCacheKey directly
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, entry.maxLevel);
	key.minFilt = minFilt & 1;
	key.mipEnable = (minFilt >> 2) & 1;
	key.mipFilt = (minFilt >> 1) & 1;
	key.magFilt = magFilt & 1;
	key.sClamp = sClamp;
	key.tClamp = tClamp;
	/*
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
					break;
				case GE_TEXLEVEL_MODE_SLOPE:
					// TODO
					break;
				}
			}
			entry.lodBias = lodBias;
		}
	}
	*/

	if (entry.framebuffer) {
		WARN_LOG_REPORT_ONCE(wrongFramebufAttach, G3D, "Framebuffer still attached in UpdateSamplingParams()?");
	}
}

void TextureCacheVulkan::SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0);

	key.minFilt = minFilt & 1;
	key.mipFilt = 0;
	key.magFilt = magFilt & 1;
	key.sClamp = sClamp;
	key.tClamp = tClamp;

	// Often the framebuffer will not match the texture size.  We'll wrap/clamp in the shader in that case.
	// This happens whether we have OES_texture_npot or not.
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (w != bufferWidth || h != bufferHeight) {
		key.sClamp = true;
		key.tClamp = true;
	}
}

static void ConvertColors(void *dstBuf, const void *srcBuf, VkFormat dstFmt, int numPixels) {
	const u32 *src = (const u32 *)srcBuf;
	u32 *dst = (u32 *)dstBuf;
	switch (dstFmt) {
	case VULKAN_4444_FORMAT:
		ConvertRGBA4444ToABGR4444((u16 *)dst, (const u16 *)src, numPixels);
		break;
		// Final Fantasy 2 uses this heavily in animated textures.
	case VULKAN_1555_FORMAT:
		ConvertRGBA5551ToABGR1555((u16 *)dst, (const u16 *)src, numPixels);
		break;
	case VULKAN_565_FORMAT:
		ConvertRGB565ToBGR565((u16 *)dst, (const u16 *)src, numPixels);
		break;
	default:
		// No need to convert RGBA8888, right order already
		if (dst != src)
			memcpy(dst, src, numPixels * sizeof(u32));
		break;
	}
}

void TextureCacheVulkan::StartFrame() {
	lastBoundTexture = nullptr;
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

static inline u32 QuickTexHash(u32 addr, int bufw, int w, int h, GETextureFormat format, TextureCacheVulkan::TexCacheEntry *entry) {
	if (h == 512 && entry->maxSeenV < 512 && entry->maxSeenV != 0) {
		h = (int)entry->maxSeenV;
	}

	const u32 sizeInRAM = (textureBitsPerPixel[format] * bufw * h) / 8;
	const u32 *checkp = (const u32 *)Memory::GetPointer(addr);

	return DoQuickTexHash(checkp, sizeInRAM);
}

void TextureCacheVulkan::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
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
	if (clutFormat != GE_CMODE_32BIT_ABGR8888) {
		const int numColors = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutMaxBytes_ / sizeof(u32)) : (clutMaxBytes_ / sizeof(u16));
		ConvertColors(clutBufConverted_, clutBufRaw_, getClutDestFormatVulkan(clutFormat), numColors);
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
inline const T *TextureCacheVulkan::GetCurrentClut() {
	return (const T *)clutBuf_;
}

inline u32 TextureCacheVulkan::GetCurrentClutHash() {
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
		static const u32 solidTextureData[] = { 0x99AA99FF };

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

void TextureCacheVulkan::SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	_dbg_assert_msg_(G3D, framebuffer != nullptr, "Framebuffer must not be null.");

	framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		DepalShaderVulkan *depal = nullptr;
		if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
			// depal = depalShaderCache_->GetDepalettizeShader(clutFormat, framebuffer->drawnFormat);
		}
		if (depal) {
			const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
			const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

			TexCacheEntry::Status alphaStatus = CheckAlpha(clutBuf_, getClutDestFormatVulkan(clutFormat), clutTotalColors, clutTotalColors, 1);
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
		gstate_c.curTextureXOffset = fbTexInfo_[entry->addr].xOffset;
		gstate_c.curTextureYOffset = fbTexInfo_[entry->addr].yOffset;
		gstate_c.needShaderTexClamp = gstate_c.curTextureWidth != (u32)gstate.getTextureWidth(0) || gstate_c.curTextureHeight != (u32)gstate.getTextureHeight(0);
		if (gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0) {
			gstate_c.needShaderTexClamp = true;
		}

		nextTexture_ = entry;
	} else {
		if (framebuffer->fbo_vk) {
			delete framebuffer->fbo_vk;
			framebuffer->fbo_vk = 0;
		}
		gstate_c.needShaderTexClamp = false;
	}
}

void TextureCacheVulkan::ApplyTexture(VkImageView &imageView, VkSampler &sampler) {
	if (nextTexture_ == nullptr) {
		imageView = nullptr;
		sampler = nullptr;
		return;
	}
	VkCommandBuffer cmd = nullptr;
	if (nextTexture_->framebuffer) {
		ApplyTextureFramebuffer(cmd, nextTexture_, nextTexture_->framebuffer, imageView, sampler);
	} else {
		// If the texture is >= 512 pixels tall...
		if (nextTexture_->dim >= 0x900) {
			// Texture scale/offset and gen modes don't apply in through.
			// So we can optimize how much of the texture we look at.
			if (gstate.isModeThrough()) {
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
		
		imageView = nextTexture_->vkTex->texture_->GetImageView();

		SamplerCacheKey key;
		UpdateSamplingParams(*nextTexture_, key);
		sampler = samplerCache_.GetOrCreateSampler(key);

		lastBoundTexture = nextTexture_->vkTex;
	}

	nextTexture_ = nullptr;
}

void TextureCacheVulkan::ApplyTextureFramebuffer(VkCommandBuffer cmd, TexCacheEntry *entry, VirtualFramebuffer *framebuffer, VkImageView &imageView, VkSampler &sampler) {
	DepalShaderVulkan *depal = nullptr;
	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		// depal = depalShaderCache_->GetDepalettizeShader(clutFormat, framebuffer->drawnFormat);
	}
	if (depal) {
		// VulkanTexture *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);
		VulkanFramebuffer *depalFBO = framebufferManager_->GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, VK_FBO_8888);

		depalFBO->BeginPass(cmd);

		struct Pos {
			Pos(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {
			}
			float x;
			float y;
			float z;
		};
		struct UV {
			UV(float u_, float v_) : u(u_), v(v_) {
			}
			float u;
			float v;
		};

		Pos pos[4] = {
			{ -1, -1, -1 },
			{ 1, -1, -1 },
			{ 1,  1, -1 },
			{ -1,  1, -1 },
		};
		UV uv[4] = {
			{ 0, 0 },
			{ 1, 0 },
			{ 1, 1 },
			{ 0, 1 },
		};
		static const int indices[4] = { 0, 1, 3, 2 };

		// If min is not < max, then we don't have values (wasn't set during decode.)
		if (gstate_c.vertBounds.minV < gstate_c.vertBounds.maxV) {
			const float invWidth = 1.0f / (float)framebuffer->bufferWidth;
			const float invHeight = 1.0f / (float)framebuffer->bufferHeight;
			// Inverse of half = double.
			const float invHalfWidth = invWidth * 2.0f;
			const float invHalfHeight = invHeight * 2.0f;

			const int u1 = gstate_c.vertBounds.minU + gstate_c.curTextureXOffset;
			const int v1 = gstate_c.vertBounds.minV + gstate_c.curTextureYOffset;
			const int u2 = gstate_c.vertBounds.maxU + gstate_c.curTextureXOffset;
			const int v2 = gstate_c.vertBounds.maxV + gstate_c.curTextureYOffset;

			const float left = u1 * invHalfWidth - 1.0f;
			const float right = u2 * invHalfWidth - 1.0f;
			const float top = v1 * invHalfHeight - 1.0f;
			const float bottom = v2 * invHalfHeight - 1.0f;
			// Points are: BL, BR, TR, TL.
			pos[0] = Pos(left, bottom, -1.0f);
			pos[1] = Pos(right, bottom, -1.0f);
			pos[2] = Pos(right, top, -1.0f);
			pos[3] = Pos(left, top, -1.0f);

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			uv[0] = UV(uvleft, uvbottom);
			uv[1] = UV(uvright, uvbottom);
			uv[2] = UV(uvright, uvtop);
			uv[3] = UV(uvleft, uvtop);
		}

		shaderManager_->DirtyLastShader();

		/*
		glUseProgram(depal->program);

		// Restore will rebind all of the state below.
		if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
			transformDraw_->BindBuffer(pos, sizeof(pos), uv, sizeof(uv));
			transformDraw_->BindElementBuffer(indices, sizeof(indices));
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		glEnableVertexAttribArray(depal->a_position);
		glEnableVertexAttribArray(depal->a_texcoord0);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, clutTexture);
		glActiveTexture(GL_TEXTURE0);

		framebufferManager_->BindFramebufferColor(GL_TEXTURE0, gstate.getFrameBufRawAddress(), framebuffer, BINDFBCOLOR_SKIP_COPY);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		glstate.blend.force(false);
		glstate.colorMask.force(true, true, true, true);
		glstate.scissorTest.force(false);
		glstate.cullFace.force(false);
		glstate.depthTest.force(false);
		glstate.stencilTest.force(false);
#if !defined(USING_GLES2)
		glstate.colorLogicOp.force(false);
#endif
		glViewport(0, 0, framebuffer->renderWidth, framebuffer->renderHeight);

		if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
			glVertexAttribPointer(depal->a_position, 3, GL_FLOAT, GL_FALSE, 12, 0);
			glVertexAttribPointer(depal->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, (void *)sizeof(pos));
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, 0);
		} else {
			glVertexAttribPointer(depal->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
			glVertexAttribPointer(depal->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, uv);
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
		}
		glDisableVertexAttribArray(depal->a_position);
		glDisableVertexAttribArray(depal->a_texcoord0);
		*/
		depalFBO->EndPass(cmd);
		depalFBO->TransitionToTexture(cmd);
		imageView = depalFBO->GetColorImageView();
	}

	/*
	imageView = depalFBO->GetColorImageView();

	SamplerCacheKey samplerKey;
	framebufferManager_->RebindFramebuffer();
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight, samplerKey);
	sampler = GetOrCreateSampler(samplerKey);
	*/

	SamplerCacheKey key;
	UpdateSamplingParams(*nextTexture_, key);
	key.mipEnable = false;
	sampler = samplerCache_.GetOrCreateSampler(key);

	lastBoundTexture = nullptr;
}

bool TextureCacheVulkan::SetOffsetTexture(u32 offset) {
	if (g_Config.iRenderingMode != FB_BUFFERED_MODE) {
		return false;
	}
	u32 texaddr = gstate.getTextureAddress(0);
	if (!Memory::IsValidAddress(texaddr) || !Memory::IsValidAddress(texaddr + offset)) {
		return false;
	}

	const u16 dim = gstate.getTextureDimension(0);
	u64 cachekey = ((u64)(texaddr & 0x3FFFFFFF) << 32) | dim;
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

void TextureCacheVulkan::SetTexture() {
#ifdef DEBUG_TEXTURES
	if (SetDebugTexture()) {
		// A different texture was bound, let's rebind next time.
		lastBoundTexture = nullptr;
		return;
	}
#endif

	u32 texaddr = gstate.getTextureAddress(0);
	if (!Memory::IsValidAddress(texaddr)) {
		// Bind a null texture and return.
		lastBoundTexture = nullptr;
		return;
	}

	const u16 dim = gstate.getTextureDimension(0);
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (texaddr == 0x04000000 && w == 2 && h == 2) {
		// Nonsense bootup texture. Discard.
	}

	GETextureFormat format = gstate.getTextureFormat();
	if (format >= 11) {
		ERROR_LOG_REPORT(G3D, "Unknown texture format %i", format);
		// TODO: Better assumption?
		format = GE_TFMT_5650;
	}
	bool hasClut = gstate.isTextureFormatIndexed();

	// Ignore uncached/kernel when caching.
	u64 cachekey = ((u64)(texaddr & 0x3FFFFFFF) << 32) | dim;
	u32 cluthash;
	if (hasClut) {
		if (clutLastFormat_ != gstate.clutformat) {
			// We update here because the clut format can be specified after the load.
			UpdateCurrentClut(gstate.getClutPaletteFormat(), gstate.getClutIndexStartPos(), gstate.isClutIndexSimple());
		}
		cluthash = GetCurrentClutHash() ^ gstate.clutformat;
		cachekey ^= cluthash;
	} else {
		cluthash = 0;
	}

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

		if (match && (entry->status & TexCacheEntry::STATUS_TO_SCALE) && g_Config.iTexScalingLevel != 1 && texelsScaledThisFrame_ < TEXCACHE_MAX_TEXELS_SCALED) {
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
			if (entry->vkTex != lastBoundTexture) {
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
				if (entry->maxLevel == maxLevel && entry->dim == gstate.getTextureDimension(0) && entry->format == format && g_Config.iTexScalingLevel == 1) {
					// Actually, if size and number of levels match, let's try to avoid deleting and recreating.
					// Instead, let's use glTexSubImage to replace the images.
					replaceImages = true;
				} else {
					if (entry->vkTex == lastBoundTexture) {
						lastBoundTexture = nullptr;
					}
					delete entry->vkTex;
					entry->vkTex = nullptr;
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
		TexCacheEntry entryNew = { 0 };
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
		nextTexture_ = nullptr;
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
	VkFormat dstFmt = GetDestFormat(format, gstate.getClutPaletteFormat());

	int scaleFactor;
	// Auto-texture scale upto 5x rendering resolution
	if (g_Config.iTexScalingLevel == 0) {
		scaleFactor = g_Config.iInternalResolution;
		if (scaleFactor == 0) {
			scaleFactor = (PSP_CoreParameter().renderWidth + 479) / 480;
		}

		// Mobile devices don't get the higher scale factors, too expensive. Very rough way to decide though...
		if (!gstate_c.Supports(GPU_IS_MOBILE)) {
			bool supportNpot = gstate_c.Supports(GPU_SUPPORTS_OES_TEXTURE_NPOT);
			scaleFactor = std::min(supportNpot ? 5 : 4, scaleFactor);
			if (!supportNpot && scaleFactor == 3) {
				scaleFactor = 2;
			}
		} else {
			scaleFactor = std::min(gstate_c.Supports(GPU_SUPPORTS_OES_TEXTURE_NPOT) ? 3 : 2, scaleFactor);
		}
	} else {
		scaleFactor = g_Config.iTexScalingLevel;
	}

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

	// Ready or not, here I go...
	if (replaceImages) {
		if (!entry->vkTex) {
			DebugBreak();
		}
	} else {
		entry->vkTex = new CachedTextureVulkan();
		entry->vkTex->texture_ = new VulkanTexture(vulkan_);
		VulkanTexture *image = entry->vkTex->texture_;
		VkResult res = image->Create(w, h, dstFmt);
		assert(res == VK_SUCCESS);
	}
	lastBoundTexture = entry->vkTex;

	// GLES2 doesn't have support for a "Max lod" which is critical as PSP games often
	// don't specify mips all the way down. As a result, we either need to manually generate
	// the bottom few levels or rely on OpenGL's autogen mipmaps instead, which might not
	// be as good quality as the game's own (might even be better in some cases though).

	// Always load base level texture here 
	
	LoadTextureLevel(*entry, 0, replaceImages, scaleFactor, dstFmt);

	// Mipmapping only enable when texture scaling disable
	/*
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
			glGenerateMipmap(GL_TEXTURE_2D);
		}
	} else if (gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}
	*/
	gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
	gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;

	// TODO: Refactor away nextTexture_. Not needed on Vulkan.
	nextTexture_ = entry;
}

VkFormat TextureCacheVulkan::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormatVulkan(clutFormat);
	case GE_TFMT_4444:
		return VULKAN_4444_FORMAT;
	case GE_TFMT_5551:
		return VULKAN_1555_FORMAT;
	case GE_TFMT_5650:
		return VULKAN_565_FORMAT;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return VULKAN_8888_FORMAT;
	}
}

void *TextureCacheVulkan::DecodeTextureLevel(GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, VkFormat dstFmt, int scaleFactor, int *bufwout) {
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
				UnswizzleFromMem(texptr, bufw, h, 0);
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
				UnswizzleFromMem(texptr, bufw, h, 0);
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
			finalBuf = UnswizzleFromMem(texptr, bufw, h, 2);
			ConvertColors(finalBuf, finalBuf, dstFmt, bufw * h);
		}
		break;

	case GE_TFMT_8888:
		if (!swizzled) {
			// Special case: if we don't need to deal with packing, we don't need to copy.
			if (scaleFactor == 1 || w == bufw) {
				finalBuf = (void *)texptr;
			} else {
				tmpTexBuf32.resize(std::max(bufw, w) * h);
				tmpTexBufRearrange.resize(std::max(bufw, w) * h);
				finalBuf = tmpTexBuf32.data();
				ConvertColors(finalBuf, texptr, dstFmt, bufw * h);
			}
		} else {
			tmpTexBuf32.resize(std::max(bufw, w) * h);
			finalBuf = UnswizzleFromMem(texptr, bufw, h, 4);
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

	return finalBuf;
}

TextureCacheVulkan::TexCacheEntry::Status TextureCacheVulkan::CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int stride, int w, int h) {
	CheckAlphaResult res;
	switch (dstFmt) {
	case VULKAN_4444_FORMAT:
		res = CheckAlphaABGR4444Basic(pixelData, stride, w, h);
		break;
	case VULKAN_1555_FORMAT:
		res = CheckAlphaABGR1555Basic(pixelData, stride, w, h);
		break;
	case VULKAN_565_FORMAT:
		// Never has any alpha.
		res = CHECKALPHA_FULL;
		break;
	default:
		res = CheckAlphaRGBA8888Basic(pixelData, stride, w, h);
		break;
	}

	return (TexCacheEntry::Status)res;
}

void TextureCacheVulkan::LoadTextureLevel(TexCacheEntry &entry, int level, bool replaceImages, int scaleFactor, VkFormat dstFmt) {
	CachedTextureVulkan *tex = entry.vkTex;
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	u32 *pixelData;
	int decPitch;
	int rowBytes;
	{
		PROFILE_THIS_SCOPE("decodetex");

		u32 texByteAlign = 1;

		GETextureFormat tfmt = (GETextureFormat)entry.format;
		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		int bufw;
		void *finalBuf = DecodeTextureLevel(tfmt, clutformat, level, texByteAlign, dstFmt, scaleFactor, &bufw);
		if (finalBuf == NULL) {
			return;
		}
		decPitch = bufw * (dstFmt == VULKAN_8888_FORMAT ? 4 : 2);
		rowBytes = w * (dstFmt == VULKAN_8888_FORMAT ? 4 : 2);
		gpuStats.numTexturesDecoded++;

		pixelData = (u32 *)finalBuf;
		if (scaleFactor > 1) {
			u32 fmt = dstFmt;
			scaler.Scale(pixelData, fmt, w, h, scaleFactor);
			dstFmt = (VkFormat)fmt;
		}

		if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
			TexCacheEntry::Status alphaStatus = CheckAlpha(pixelData, dstFmt, bufw, w, h);
			entry.SetAlphaStatus(alphaStatus, level);
		} else {
			entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
		}
	}

	if (replaceImages) {
		// TODO: No support for texture shadows
		// DebugBreak();
	}

	PROFILE_THIS_SCOPE("loadtex");

	// Upload the texture data. TODO: Decode directly into this buffer.
	int rowPitch;
	uint8_t *writePtr = entry.vkTex->texture_->Lock(level, &rowPitch);
	for (int y = 0; y < h; y++) {
		memcpy(writePtr + rowPitch * y, (const uint8_t *)pixelData + decPitch * y, rowBytes);
		// uncomment to make all textures white for debugging
		//memset(writePtr + rowPitch * y, 0xff, rowBytes);
	}
	entry.vkTex->texture_->Unlock();

	/*
	if (!lowMemoryMode_) {
		GLenum err = glGetError();
		if (err == GL_OUT_OF_MEMORY) {
			WARN_LOG_REPORT(G3D, "Texture cache ran out of GPU memory; switching to low memory mode");
			lowMemoryMode_ = true;
			decimationCounter_ = 0;
			Decimate();
			// TODO: We need to stall the GPU here and wipe things out of memory.

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
	}*/
}
