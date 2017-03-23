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
#include "thin3d/thin3d.h"
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
#include <emmintrin.h>
#endif

#define TEXCACHE_NAME_CACHE_SIZE 16

#define TEXCACHE_MAX_TEXELS_SCALED (256*256)  // Per frame

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


CachedTextureVulkan::~CachedTextureVulkan() {
	delete texture_;
}

SamplerCache::~SamplerCache() {
	for (auto iter : cache_) {
		vulkan_->Delete().QueueDeleteSampler(iter.second);
	}
}

VkSampler SamplerCache::GetOrCreateSampler(const SamplerCacheKey &key) {
	auto iter = cache_.find(key);
	if (iter != cache_.end()) {
		return iter->second;
	}

	VkSamplerCreateInfo samp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = key.sClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeV = key.tClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeW = samp.addressModeU;  // irrelevant, but Mali recommends that all clamp modes are the same if possible.
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

void SamplerCache::DeviceLost() {
	for (auto iter : cache_) {
		vulkan_->Delete().QueueDeleteSampler(iter.second);
	}
	cache_.clear();
}

void SamplerCache::DeviceRestore(VulkanContext *vulkan) {
	vulkan_ = vulkan;
}

TextureCacheVulkan::TextureCacheVulkan(Draw::DrawContext *draw, VulkanContext *vulkan)
	: TextureCacheCommon(draw),
		vulkan_(vulkan),
		samplerCache_(vulkan),
		texelsScaledThisFrame_(0) {
	timesInvalidatedAllThisFrame_ = 0;
	lastBoundTexture = nullptr;
	allocator_ = new VulkanDeviceAllocator(vulkan_, TEXCACHE_MIN_SLAB_SIZE, TEXCACHE_MAX_SLAB_SIZE);

	SetupTextureDecoder();
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

void TextureCacheVulkan::SetFramebufferManager(FramebufferManagerVulkan *fbManager) {
	framebufferManagerVulkan_ = fbManager;
	framebufferManager_ = fbManager;
}

void TextureCacheVulkan::DeviceLost() {
	Clear(true);

	if (allocator_) {
		allocator_->Destroy();

		// We have to delete on queue, so this can free its queued deletions.
		vulkan_->Delete().QueueCallback([](void *ptr) {
			auto allocator = static_cast<VulkanDeviceAllocator *>(ptr);
			delete allocator;
		}, allocator_);
		allocator_ = nullptr;
	}

	samplerCache_.DeviceLost();

	nextTexture_ = nullptr;
}

void TextureCacheVulkan::DeviceRestore(VulkanContext *vulkan) {
	vulkan_ = vulkan;

	allocator_ = new VulkanDeviceAllocator(vulkan_, TEXCACHE_MIN_SLAB_SIZE, TEXCACHE_MAX_SLAB_SIZE);
	samplerCache_.DeviceRestore(vulkan);
}

void TextureCacheVulkan::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	DEBUG_LOG(G3D, "Deleting texture %p", entry->vkTex);
	delete entry->vkTex;
	entry->vkTex = nullptr;
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

void TextureCacheVulkan::BindTexture(TexCacheEntry *entry) {
	if (!entry || !entry->vkTex) {
		imageView_ = VK_NULL_HANDLE;
		sampler_ = VK_NULL_HANDLE;
		return;
	}

	imageView_ = entry->vkTex->texture_->GetImageView();
	SamplerCacheKey key;
	UpdateSamplingParams(*entry, key);
	sampler_ = samplerCache_.GetOrCreateSampler(key);
}

void TextureCacheVulkan::Unbind() {
	imageView_ = VK_NULL_HANDLE;
	sampler_ = VK_NULL_HANDLE;
}

void TextureCacheVulkan::ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
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

		shaderManagerVulkan_->DirtyLastShader();

		//depalFBO->EndPass(cmd);
		//depalFBO->TransitionToTexture(cmd);
		//imageView = depalFBO->GetColorImageView();

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TexCacheEntry::Status alphaStatus = CheckAlpha(clutBuf_, getClutDestFormatVulkan(clutFormat), clutTotalColors, clutTotalColors, 1);
		gstate_c.SetTextureFullAlpha(alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL);
		gstate_c.SetTextureSimpleAlpha(alphaStatus == TexCacheEntry::STATUS_ALPHA_SIMPLE);
	} else {
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;

		gstate_c.SetTextureFullAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
		gstate_c.SetTextureSimpleAlpha(gstate_c.textureFullAlpha);
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
		sampler_ = samplerCache_.GetOrCreateSampler(key);

		lastBoundTexture = nullptr;
	}
}

ReplacedTextureFormat FromVulkanFormat(VkFormat fmt) {
	switch (fmt) {
	case VULKAN_565_FORMAT: return ReplacedTextureFormat::F_5650;
	case VULKAN_1555_FORMAT: return ReplacedTextureFormat::F_5551;
	case VULKAN_4444_FORMAT: return ReplacedTextureFormat::F_4444;
	case VULKAN_8888_FORMAT: default: return ReplacedTextureFormat::F_8888;
	}
}

VkFormat ToVulkanFormat(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650: return VULKAN_565_FORMAT;
	case ReplacedTextureFormat::F_5551: return VULKAN_1555_FORMAT;
	case ReplacedTextureFormat::F_4444: return VULKAN_4444_FORMAT;
	case ReplacedTextureFormat::F_8888: default: return VULKAN_8888_FORMAT;
	}
}

void TextureCacheVulkan::BuildTexture(TexCacheEntry *const entry, bool replaceImages) {
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

	u64 cachekey = replacer_.Enabled() ? entry->CacheKey() : 0;
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	ReplacedTexture &replaced = replacer_.FindReplacement(cachekey, entry->fullhash, w, h);
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
	if (entry->addr > 0x05000000 && entry->addr < PSP_GetKernelMemoryEnd())
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
	if (replacer_.Enabled() && !replaced.Valid()) {
		replacedInfo.cachekey = cachekey;
		replacedInfo.hash = entry->fullhash;
		replacedInfo.addr = entry->addr;
		replacedInfo.isVideo = videos_.find(entry->addr & 0x3FFFFFFF) != videos_.end();
		replacedInfo.isFinal = (entry->status & TexCacheEntry::STATUS_TO_SCALE) == 0;
		replacedInfo.scaleFactor = scaleFactor;
		replacedInfo.fmt = FromVulkanFormat(actualFmt);
	}

	if (entry->vkTex) {
		u8 level = (gstate.texlevel >> 20) & 0xF;
		bool fakeMipmap = IsFakeMipmapChange() && level > 0;
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
			void *data = drawEngine_->GetPushBufferForTextureData()->Push(size, &bufferOffset, &texBuf);
			if (replaced.Valid()) {
				replaced.Load(i, data, stride);
			} else {
				if (fakeMipmap) {
					LoadTextureLevel(*entry, (uint8_t *)data, stride, level, scaleFactor, dstFmt);
					entry->vkTex->texture_->UploadMip(0, mipWidth, mipHeight, texBuf, bufferOffset, stride / bpp);
					break;
				} else
					LoadTextureLevel(*entry, (uint8_t *)data, stride, i, scaleFactor, dstFmt);
				if (replacer_.Enabled()) {
					replacer_.NotifyTextureDecoded(replacedInfo, data, stride, i, mipWidth, mipHeight);
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

TexCacheEntry::Status TextureCacheVulkan::CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int stride, int w, int h) {
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
			tmpTexBufRearrange_.resize(std::max(bufw, w) * h);
			pixelData = tmpTexBufRearrange_.data();
			// We want to end up with a neatly packed texture for scaling.
			decPitch = w * bpp;
		}

		DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, level, bufw, false, false, false);
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
