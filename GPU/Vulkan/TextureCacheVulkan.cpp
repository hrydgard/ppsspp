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
#include "Core/System.h"

#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanImage.h"
#include "Common/Vulkan/VulkanMemory.h"

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/DepalettizeShaderVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Common/TextureDecoder.h"

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

#define TEXCACHE_MIN_PRESSURE (16 * 1024 * 1024)  // Total in GL
#define TEXCACHE_SECOND_MIN_PRESSURE (4 * 1024 * 1024)

#define TEXCACHE_MIN_SLAB_SIZE (4 * 1024 * 1024)
#define TEXCACHE_MAX_SLAB_SIZE (32 * 1024 * 1024)

// Note: some drivers prefer B4G4R4A4_UNORM_PACK16 over R4G4B4A4_UNORM_PACK16.
#define VULKAN_4444_FORMAT VK_FORMAT_B4G4R4A4_UNORM_PACK16
#define VULKAN_1555_FORMAT VK_FORMAT_A1R5G5B5_UNORM_PACK16
#define VULKAN_565_FORMAT  VK_FORMAT_B5G6R5_UNORM_PACK16
#define VULKAN_8888_FORMAT VK_FORMAT_R8G8B8A8_UNORM

static const VkComponentMapping VULKAN_4444_SWIZZLE = { VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B };
static const VkComponentMapping VULKAN_1555_SWIZZLE = { VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A };
static const VkComponentMapping VULKAN_565_SWIZZLE = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
static const VkComponentMapping VULKAN_8888_SWIZZLE = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };

SamplerCache::~SamplerCache() {
	for (auto iter : cache_) {
		vulkan_->Delete().QueueDeleteSampler(iter.second);
	}
}

CachedTextureVulkan::~CachedTextureVulkan() {
	delete texture_;
}

VkSampler SamplerCache::GetOrCreateSampler(const SamplerCacheKey &key) {
	auto iter = cache_.find(key);
	if (iter != cache_.end()) {
		return iter->second;
	}

	VkSamplerCreateInfo samp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = key.sClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeV = key.tClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	samp.compareOp = VK_COMPARE_OP_ALWAYS;
	samp.flags = 0;
	samp.magFilter = key.magFilt ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samp.minFilter = key.minFilt ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samp.mipmapMode = key.mipFilt ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;

	if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY) && g_Config.iAnisotropyLevel > 0) {
		// Docs say the min of this value and the supported max are used.
		samp.maxAnisotropy = 1 << g_Config.iAnisotropyLevel;
		samp.anisotropyEnable = true;
	} else {
		samp.maxAnisotropy = 1.0f;
		samp.anisotropyEnable = false;
	}

	samp.maxLod = key.maxLevel;
	samp.minLod = 0.0f;
	samp.mipLodBias = 0.0f;

	VkSampler sampler;
	VkResult res = vkCreateSampler(vulkan_->GetDevice(), &samp, nullptr, &sampler);
	assert(res == VK_SUCCESS);
	cache_[key] = sampler;
	return sampler;
}

TextureCacheVulkan::TextureCacheVulkan(VulkanContext *vulkan)
	: vulkan_(vulkan), samplerCache_(vulkan), secondCacheSizeEstimate_(0),
	  clearCacheNextFrame_(false), lowMemoryMode_(false), texelsScaledThisFrame_(0) {
	timesInvalidatedAllThisFrame_ = 0;
	lastBoundTexture = nullptr;
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	allocator_ = new VulkanDeviceAllocator(vulkan_, TEXCACHE_MIN_SLAB_SIZE, TEXCACHE_MAX_SLAB_SIZE);

	SetupTextureDecoder();

	nextTexture_ = nullptr;
}

TextureCacheVulkan::~TextureCacheVulkan() {
	Clear(true);
	allocator_->Destroy();

	// We have to delete on queue, so this can free its queued deletions.
	vulkan_->Delete().QueueCallback([](void *ptr) {
		auto allocator = static_cast<VulkanDeviceAllocator *>(ptr);
		delete allocator;
	}, allocator_);
}

void TextureCacheVulkan::DownloadFramebufferForClut(u32 clutAddr, u32 bytes) {

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
	videos_.clear();
}

void TextureCacheVulkan::DeleteTexture(TexCache::iterator it) {
	delete it->second.vkTex;
	auto fbInfo = fbTexInfo_.find(it->first);
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

	DecimateVideos();
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
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, entry.maxLevel, entry.addr);
	key.minFilt = minFilt & 1;
	key.mipEnable = (minFilt >> 2) & 1;
	key.mipFilt = (minFilt >> 1) & 1;
	key.magFilt = magFilt & 1;
	key.sClamp = sClamp;
	key.tClamp = tClamp;
	key.maxLevel = entry.vkTex->texture_->GetNumMips() - 1;
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
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0, 0);

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

void TextureCacheVulkan::StartFrame() {
	lastBoundTexture = nullptr;
	timesInvalidatedAllThisFrame_ = 0;
	texelsScaledThisFrame_ = 0;

	if (clearCacheNextFrame_) {
		Clear(true);
		clearCacheNextFrame_ = false;
	} else {
		Decimate();
	}

	allocator_->Begin();
}

void TextureCacheVulkan::EndFrame() {
	allocator_->End();

	if (texelsScaledThisFrame_) {
		// INFO_LOG(G3D, "Scaled %i texels", texelsScaledThisFrame_);
	}
}

static inline u32 MiniHash(const u32 *ptr) {
	return ptr[0];
}

static inline u32 QuickTexHash(TextureReplacer &replacer, u32 addr, int bufw, int w, int h, GETextureFormat format, TextureCacheVulkan::TexCacheEntry *entry) {
	if (replacer.Enabled()) {
		return replacer.ComputeHash(addr, bufw, w, h, format, entry->maxSeenV);
	}

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

inline u32 TextureCacheVulkan::GetCurrentClutHash() {
	return clutHash_;
}

void TextureCacheVulkan::SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
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
		gstate_c.curTextureXOffset = fbInfo.xOffset;
		gstate_c.curTextureYOffset = fbInfo.yOffset;
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

	nextNeedsRehash_ = false;
	nextNeedsChange_ = false;
	nextNeedsRebuild_ = false;
}

void TextureCacheVulkan::ApplyTexture(VulkanPushBuffer *uploadBuffer, VkImageView &imageView, VkSampler &sampler) {
	TexCacheEntry *entry = nextTexture_;
	if (entry == nullptr) {
		imageView = VK_NULL_HANDLE;
		sampler = VK_NULL_HANDLE;
		return;
	}
	nextTexture_ = nullptr;

	UpdateMaxSeenV(entry, gstate.isModeThrough());

	if (nextNeedsRebuild_) {
		if (nextNeedsRehash_) {
			// Update the hash on the texture.
			int w = gstate.getTextureWidth(0);
			int h = gstate.getTextureHeight(0);
			entry->fullhash = QuickTexHash(replacer, entry->addr, entry->bufw, w, h, GETextureFormat(entry->format), entry);
		}
		if (nextNeedsChange_) {
			// This texture existed previously, let's handle the change.
			HandleTextureChange(entry, nextChangeReason_, false, true);
		}

		// We actually build afterward (shared with rehash rebuild.)
	} else if (nextNeedsRehash_) {
		// Okay, this matched and didn't change - but let's check the hash.  Maybe it will change.
		bool doDelete = true;
		if (!CheckFullHash(entry, doDelete)) {
			HandleTextureChange(entry, "hash fail", true, doDelete);
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
		BuildTexture(entry, uploadBuffer);
	}

	entry->lastFrame = gpuStats.numFlips;
	VkCommandBuffer cmd = nullptr;
	if (entry->framebuffer) {
		ApplyTextureFramebuffer(cmd, entry, entry->framebuffer, imageView, sampler);
	} else if (entry->vkTex) {
		imageView = entry->vkTex->texture_->GetImageView();

		SamplerCacheKey key;
		UpdateSamplingParams(*entry, key);
		sampler = samplerCache_.GetOrCreateSampler(key);

		gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
		gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;

		lastBoundTexture = entry->vkTex;
	} else {
		imageView = VK_NULL_HANDLE;
		sampler = VK_NULL_HANDLE;
	}
}

void TextureCacheVulkan::ApplyTextureFramebuffer(VkCommandBuffer cmd, TexCacheEntry *entry, VirtualFramebuffer *framebuffer, VkImageView &imageView, VkSampler &sampler) {
	DepalShaderVulkan *depal = nullptr;
	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		// depal = depalShaderCache_->GetDepalettizeShader(clutFormat, framebuffer->drawnFormat);
	}
	if (depal) {
		// VulkanTexture *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_);
		// VulkanFBO *depalFBO = framebufferManager_->GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, VK_FBO_8888);

		//depalFBO->BeginPass(cmd);

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

		//depalFBO->EndPass(cmd);
		//depalFBO->TransitionToTexture(cmd);
		//imageView = depalFBO->GetColorImageView();

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

	/*
	imageView = depalFBO->GetColorImageView();

	SamplerCacheKey samplerKey;
	framebufferManager_->RebindFramebuffer();
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight, samplerKey);
	sampler = GetOrCreateSampler(samplerKey);
	*/

	if (entry->vkTex) {
		SamplerCacheKey key;
		UpdateSamplingParams(*entry, key);
		key.mipEnable = false;
		sampler = samplerCache_.GetOrCreateSampler(key);

		lastBoundTexture = nullptr;
	}
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
		return true;
	}

	return false;
}

ReplacedTextureFormat FromVulkanFormat(VkFormat fmt) {
	switch (fmt) {
	case VULKAN_565_FORMAT:
		return ReplacedTextureFormat::F_5650;
	case VULKAN_1555_FORMAT:
		return ReplacedTextureFormat::F_5551;
	case VULKAN_4444_FORMAT:
		return ReplacedTextureFormat::F_4444;
	case VULKAN_8888_FORMAT:
	default:
		return ReplacedTextureFormat::F_8888;
	}
}

VkFormat ToVulkanFormat(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650:
		return VULKAN_565_FORMAT;
	case ReplacedTextureFormat::F_5551:
		return VULKAN_1555_FORMAT;
	case ReplacedTextureFormat::F_4444:
		return VULKAN_4444_FORMAT;
	case ReplacedTextureFormat::F_8888:
	default:
		return VULKAN_8888_FORMAT;
	}
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

	TexCache::iterator iter = cache.find(cachekey);
	TexCacheEntry *entry = NULL;
	gstate_c.needShaderTexClamp = false;
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;

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
			if (entry->vkTex != lastBoundTexture) {
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

bool TextureCacheVulkan::CheckFullHash(TexCacheEntry *const entry, bool &doDelete) {
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

bool TextureCacheVulkan::HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete) {
	cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
	entry->numInvalidated++;
	gpuStats.numTextureInvalidations++;
	DEBUG_LOG(G3D, "Texture different or overwritten, reloading at %08x: %s", entry->addr, reason);
	if (doDelete) {
		if (entry->vkTex == lastBoundTexture) {
			lastBoundTexture = nullptr;
		}
		delete entry->vkTex;
		entry->vkTex = nullptr;
		entry->status &= ~TexCacheEntry::STATUS_IS_SCALED;
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

	return false;
}

void TextureCacheVulkan::BuildTexture(TexCacheEntry *const entry,VulkanPushBuffer *uploadBuffer) {
	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;

	// For the estimate, we assume cluts always point to 8888 for simplicity.
	cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

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
	if (!g_Config.bMipMap || badMipSizes) {
		maxLevel = 0;
	}

	// If GLES3 is available, we can preallocate the storage, which makes texture loading more efficient.
	VkFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

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

	// TODO
	if (scaleFactor > 1) {
		maxLevel = 0;
	}

	VkFormat actualFmt = scaleFactor > 1 ? VULKAN_8888_FORMAT : dstFmt;
	if (replaced.Valid()) {
		actualFmt = ToVulkanFormat(replaced.Format(0));
	}
	if (!entry->vkTex) {
		entry->vkTex = new CachedTextureVulkan();
		entry->vkTex->texture_ = new VulkanTexture(vulkan_, allocator_);
		VulkanTexture *image = entry->vkTex->texture_;

		const VkComponentMapping *mapping;
		switch (actualFmt) {
		case VULKAN_4444_FORMAT:
			mapping = &VULKAN_4444_SWIZZLE;
			break;

		case VULKAN_1555_FORMAT:
			mapping = &VULKAN_1555_SWIZZLE;
			break;

		case VULKAN_565_FORMAT:
			mapping = &VULKAN_565_SWIZZLE;
			break;

		default:
			mapping = &VULKAN_8888_SWIZZLE;
			break;
		}

		bool allocSuccess = image->CreateDirect(w * scaleFactor, h * scaleFactor, maxLevel + 1, actualFmt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mapping);
		if (!allocSuccess && !lowMemoryMode_) {
			WARN_LOG_REPORT(G3D, "Texture cache ran out of GPU memory; switching to low memory mode");
			lowMemoryMode_ = true;
			decimationCounter_ = 0;
			Decimate();
			// TODO: We should stall the GPU here and wipe things out of memory.
			// As is, it will almost definitely fail the second time, but next frame it may recover.

			I18NCategory *err = GetI18NCategory("Error");
			if (scaleFactor > 1) {
				host->NotifyUserMessage(err->T("Warning: Video memory FULL, reducing upscaling and switching to slow caching mode"), 2.0f);
			} else {
				host->NotifyUserMessage(err->T("Warning: Video memory FULL, switching to slow caching mode"), 2.0f);
			}

			scaleFactor = 1;
			actualFmt = dstFmt;

			allocSuccess = image->CreateDirect(w * scaleFactor, h * scaleFactor, maxLevel + 1, actualFmt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mapping);
		}

		if (!allocSuccess) {
			delete entry->vkTex;
			entry->vkTex = nullptr;
		}
	} else {
		// TODO: If reusing an existing texture object, we must transition it into the correct layout.
	}
	lastBoundTexture = entry->vkTex;

	ReplacedTextureDecodeInfo replacedInfo;
	if (replacer.Enabled() && !replaced.Valid()) {
		replacedInfo.cachekey = cachekey;
		replacedInfo.hash = entry->fullhash;
		replacedInfo.addr = entry->addr;
		replacedInfo.isVideo = videos_.find(entry->addr & 0x3FFFFFFF) != videos_.end();
		replacedInfo.isFinal = (entry->status & TexCacheEntry::STATUS_TO_SCALE) == 0;
		replacedInfo.scaleFactor = scaleFactor;
		replacedInfo.fmt = FromVulkanFormat(actualFmt);
	}

	if (entry->vkTex) {
		// Upload the texture data.
		for (int i = 0; i <= maxLevel; i++) {
			int mipWidth = gstate.getTextureWidth(i) * scaleFactor;
			int mipHeight = gstate.getTextureHeight(i) * scaleFactor;
			if (replaced.Valid()) {
				replaced.GetSize(i, mipWidth, mipHeight);
			}
			int bpp = actualFmt == VULKAN_8888_FORMAT ? 4 : 2;
			int stride = (mipWidth * bpp + 15) & ~15;
			int size = stride * mipHeight;
			uint32_t bufferOffset;
			VkBuffer texBuf;
			void *data = uploadBuffer->Push(size, &bufferOffset, &texBuf);
			if (replaced.Valid()) {
				replaced.Load(i, data, stride);
			} else {
				LoadTextureLevel(*entry, (uint8_t *)data, stride, i, scaleFactor, dstFmt);
				if (replacer.Enabled()) {
					replacer.NotifyTextureDecoded(replacedInfo, data, stride, i, mipWidth, mipHeight);
				}
			}
			entry->vkTex->texture_->UploadMip(i, mipWidth, mipHeight, texBuf, bufferOffset, stride / bpp);
		}

		if (replaced.Valid()) {
			entry->SetAlphaStatus(TexCacheEntry::Status(replaced.AlphaStatus()));
		}
	}

	entry->vkTex->texture_->EndCreate();

	gstate_c.textureFullAlpha = entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL;
	gstate_c.textureSimpleAlpha = entry->GetAlphaStatus() != TexCacheEntry::STATUS_ALPHA_UNKNOWN;
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

TextureCacheVulkan::TexCacheEntry::Status TextureCacheVulkan::CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int stride, int w, int h) {
	CheckAlphaResult res;
	switch (dstFmt) {
	case VULKAN_4444_FORMAT:
		res = CheckAlphaRGBA4444Basic(pixelData, stride, w, h);
		break;
	case VULKAN_1555_FORMAT:
		res = CheckAlphaRGBA5551Basic(pixelData, stride, w, h);
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

void TextureCacheVulkan::LoadTextureLevel(TexCacheEntry &entry, uint8_t *writePtr, int rowPitch, int level, int scaleFactor, VkFormat dstFmt) {
	CachedTextureVulkan *tex = entry.vkTex;
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	{
		PROFILE_THIS_SCOPE("decodetex");

		GETextureFormat tfmt = (GETextureFormat)entry.format;
		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		u32 texaddr = gstate.getTextureAddress(level);
		int bufw = GetTextureBufw(level, texaddr, tfmt);
		int bpp = dstFmt == VULKAN_8888_FORMAT ? 4 : 2;

		u32 *pixelData = (u32 *)writePtr;
		int decPitch = rowPitch;
		if (scaleFactor > 1) {
			tmpTexBufRearrange.resize(std::max(bufw, w) * h);
			pixelData = tmpTexBufRearrange.data();
			// We want to end up with a neatly packed texture for scaling.
			decPitch = w * bpp;
		}

		bool decSuccess = DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, level, bufw, false);
		if (!decSuccess) {
			memset(writePtr, 0, rowPitch * h);
			return;
		}
		gpuStats.numTexturesDecoded++;

		if (scaleFactor > 1) {
			u32 fmt = dstFmt;
			scaler.ScaleAlways((u32 *)writePtr, pixelData, fmt, w, h, scaleFactor);
			pixelData = (u32 *)writePtr;
			dstFmt = (VkFormat)fmt;

			// We always end up at 8888.  Other parts assume this.
			assert(dstFmt == VULKAN_8888_FORMAT);
			bpp = sizeof(u32);
			decPitch = w * bpp;

			if (decPitch != rowPitch) {
				// Rearrange in place to match the requested pitch.
				// (it can only be larger than w * bpp, and a match is likely.)
				for (int y = h - 1; y >= 0; --y) {
					memcpy(writePtr + rowPitch * y, writePtr + decPitch * y, w * bpp);
				}
				decPitch = rowPitch;
			}
		}

		if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
			TexCacheEntry::Status alphaStatus = CheckAlpha(pixelData, dstFmt, decPitch / bpp, w, h);
			entry.SetAlphaStatus(alphaStatus, level);
		} else {
			entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
		}
	}
}
