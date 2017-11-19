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
#include "profiler/profiler.h"
#include "Common/ColorConv.h"
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
#if PPSSPP_ARCH(ARM_NEON)
#include <arm_neon.h>
#endif

// Videos should be updated every few frames, so we forget quickly.
#define VIDEO_DECIMATE_AGE 4

// If a texture hasn't been seen for this many frames, get rid of it.
#define TEXTURE_KILL_AGE 200
#define TEXTURE_KILL_AGE_LOWMEM 60
// Not used in lowmem mode.
#define TEXTURE_SECOND_KILL_AGE 100

// Try to be prime to other decimation intervals.
#define TEXCACHE_DECIMATION_INTERVAL 13

#define TEXCACHE_MIN_PRESSURE 16 * 1024 * 1024  // Total in VRAM
#define TEXCACHE_SECOND_MIN_PRESSURE 4 * 1024 * 1024

// Just for reference

// PSP Color formats:
// 565:  BBBBBGGGGGGRRRRR
// 5551: ABBBBBGGGGGRRRRR
// 4444: AAAABBBBGGGGRRRR
// 8888: AAAAAAAABBBBBBBBGGGGGGGGRRRRRRRR (Bytes in memory: RGBA)

// D3D11/9 Color formats:
// DXGI_FORMAT_B4G4R4A4/D3DFMT_A4R4G4B4:  AAAARRRRGGGGBBBB
// DXGI_FORMAT_B5G5R5A1/D3DFMT_A1R5G6B5:  ARRRRRGGGGGBBBBB
// DXGI_FORMAT_B5G6R6/D3DFMT_R5G6B5:      RRRRRGGGGGGBBBBB
// DXGI_FORMAT_B8G8R8A8:                  AAAAAAAARRRRRRRRGGGGGGGGBBBBBBBB  (Bytes in memory: BGRA)
// These are Data::Format::   A4R4G4B4_PACK16, A1R5G6B5_PACK16, R5G6B5_PACK16, B8G8R8A8.
// So these are good matches, just with R/B swapped.

// OpenGL ES color formats:
// GL_UNSIGNED_SHORT_4444: BBBBGGGGRRRRAAAA  (4-bit rotation)
// GL_UNSIGNED_SHORT_565:  BBBBBGGGGGGRRRRR   (match)
// GL_UNSIGNED_SHORT_1555: BBBBBGGGGGRRRRRA  (1-bit rotation)
// GL_UNSIGNED_BYTE/RGBA:  AAAAAAAABBBBBBBBGGGGGGGGRRRRRRRR  (match)
// These are Data::Format:: B4G4R4A4_PACK16, B5G6R6_PACK16, B5G5R5A1_PACK16, R8G8B8A8

// Vulkan color formats:
// TODO
TextureCacheCommon::TextureCacheCommon(Draw::DrawContext *draw)
	: draw_(draw),
		clearCacheNextFrame_(false),
		lowMemoryMode_(false),
		texelsScaledThisFrame_(0),
		cacheSizeEstimate_(0),
		secondCacheSizeEstimate_(0),
		nextTexture_(nullptr),
		clutLastFormat_(0xFFFFFFFF),
		clutTotalBytes_(0),
		clutMaxBytes_(0),
		clutRenderAddress_(0xFFFFFFFF),
		clutAlphaLinear_(false),
		isBgraBackend_(false) {
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;

	// TODO: Clamp down to 256/1KB?  Need to check mipmapShareClut and clamp loadclut.
	clutBufRaw_ = (u32 *)AllocateAlignedMemory(1024 * sizeof(u32), 16);  // 4KB
	clutBufConverted_ = (u32 *)AllocateAlignedMemory(1024 * sizeof(u32), 16);  // 4KB

	// Zap so we get consistent behavior if the game fails to load some of the CLUT.
	memset(clutBufRaw_, 0, 1024 * sizeof(u32));
	memset(clutBufConverted_, 0, 1024 * sizeof(u32));
	clutBuf_ = clutBufConverted_;

	// These buffers will grow if necessary, but most won't need more than this.
	tmpTexBuf32_.resize(512 * 512);  // 1MB
	tmpTexBuf16_.resize(512 * 512);  // 0.5MB
	tmpTexBufRearrange_.resize(512 * 512);   // 1MB

	replacer_.Init();
}

TextureCacheCommon::~TextureCacheCommon() {
	FreeAlignedMemory(clutBufConverted_);
	FreeAlignedMemory(clutBufRaw_);
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

// Produces a signed 1.23.8 value.
static int TexLog2(float delta) {
	union FloatBits {
		float f;
		u32 u;
	};
	FloatBits f;
	f.f = delta;
	// Use the exponent as the tex level, and the top mantissa bits for a frac.
	// We can't support more than 8 bits of frac, so truncate.
	int useful = (f.u >> 15) & 0xFFFF;
	// Now offset so the exponent aligns with log2f (exp=127 is 0.)
	return useful - 127 * 256;
}

void TextureCacheCommon::GetSamplingParams(int &minFilt, int &magFilt, bool &sClamp, bool &tClamp, float &lodBias, int maxLevel, u32 addr, GETexLevelMode &mode) {
	minFilt = gstate.texfilter & 0x7;
	magFilt = gstate.isMagnifyFilteringEnabled();
	sClamp = gstate.isTexCoordClampedS();
	tClamp = gstate.isTexCoordClampedT();

	GETexLevelMode mipMode = gstate.getTexLevelMode();
	mode = mipMode;
	bool autoMip = mipMode == GE_TEXLEVEL_MODE_AUTO;
	lodBias = (float)gstate.getTexLevelOffset16() * (1.0f / 16.0f);
	if (mipMode == GE_TEXLEVEL_MODE_SLOPE) {
		lodBias += 1.0f + TexLog2(gstate.getTextureLodSlope()) * (1.0f / 256.0f);
	}

	// If mip level is forced to zero, disable mipmapping.
	bool noMip = maxLevel == 0 || (!autoMip && lodBias <= 0.0f);
	if (IsFakeMipmapChange())
		noMip = noMip || !autoMip;

	if (noMip) {
		// Enforce no mip filtering, for safety.
		minFilt &= 1; // no mipmaps yet
		lodBias = 0.0f;
	}

	if (g_Config.iTexFiltering == TEX_FILTER_LINEAR_VIDEO) {
		bool isVideo = videos_.find(addr & 0x3FFFFFFF) != videos_.end();
		if (isVideo) {
			magFilt |= 1;
			minFilt |= 1;
		}
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
}

void TextureCacheCommon::UpdateSamplingParams(TexCacheEntry &entry, SamplerCacheKey &key) {
	// TODO: Make GetSamplingParams write SamplerCacheKey directly
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	int maxLevel = (entry.status & TexCacheEntry::STATUS_BAD_MIPS) ? 0 : entry.maxLevel;
	GETexLevelMode mode;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, maxLevel, entry.addr, mode);
	key.minFilt = minFilt & 1;
	key.mipEnable = (minFilt >> 2) & 1;
	key.mipFilt = (minFilt >> 1) & 1;
	key.magFilt = magFilt & 1;
	key.sClamp = sClamp;
	key.tClamp = tClamp;
	key.aniso = false;

	if (!key.mipEnable) {
		key.maxLevel = 0;
		key.minLevel = 0;
		key.lodBias = 0;
	} else {
		switch (mode) {
		case GE_TEXLEVEL_MODE_AUTO:
			key.maxLevel = entry.maxLevel * 256;
			key.minLevel = 0;
			key.lodBias = (int)(lodBias * 256.0f);
			if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY) && g_Config.iAnisotropyLevel > 0) {
				key.aniso = true;
			}
			break;
		case GE_TEXLEVEL_MODE_CONST:
		case GE_TEXLEVEL_MODE_UNKNOWN:
			key.maxLevel = (int)(lodBias * 256.0f);
			key.minLevel = (int)(lodBias * 256.0f);
			key.lodBias = 0;
			break;
		case GE_TEXLEVEL_MODE_SLOPE:
			// It's incorrect to use the slope as a bias. Instead it should be passed
			// into the shader directly as an explicit lod level, with the bias on top. For now, we just kill the
			// lodBias in this mode, working around #9772.
			key.maxLevel = entry.maxLevel * 256;
			key.minLevel = 0;
			key.lodBias = 0;
			break;
		}
	}

	if (entry.framebuffer) {
		WARN_LOG_REPORT_ONCE(wrongFramebufAttach, G3D, "Framebuffer still attached in UpdateSamplingParams()?");
	}
}

void TextureCacheCommon::UpdateMaxSeenV(TexCacheEntry *entry, bool throughMode) {
	// If the texture is >= 512 pixels tall...
	if (entry->dim >= 0x900) {
		// Texture scale/offset and gen modes don't apply in through.
		// So we can optimize how much of the texture we look at.
		if (throughMode) {
			if (entry->maxSeenV == 0 && gstate_c.vertBounds.maxV > 0) {
				// Let's not hash less than 272, we might use more later and have to rehash.  272 is very common.
				entry->maxSeenV = std::max((u16)272, gstate_c.vertBounds.maxV);
			} else if (gstate_c.vertBounds.maxV > entry->maxSeenV) {
				// The max height changed, so we're better off hashing the entire thing.
				entry->maxSeenV = 512;
				entry->status |= TexCacheEntry::STATUS_FREE_CHANGE;
			}
		} else {
			// Otherwise, we need to reset to ensure we use the whole thing.
			// Can't tell how much is used.
			// TODO: We could tell for texcoord UV gen, and apply scale to max?
			entry->maxSeenV = 512;
		}
	}
}


void TextureCacheCommon::SetTexture(bool force) {
#ifdef DEBUG_TEXTURES
	if (SetDebugTexture()) {
		// A different texture was bound, let's rebind next time.
		InvalidateLastTexture();
		return;
	}
#endif

	if (force) {
		InvalidateLastTexture();
	}

	u8 level = 0;
	if (IsFakeMipmapChange())
		level = std::max(0, gstate.getTexLevelOffset16() / 16);
	u32 texaddr = gstate.getTextureAddress(level);
	if (!Memory::IsValidAddress(texaddr)) {
		// Bind a null texture and return.
		Unbind();
		return;
	}

	const u16 dim = gstate.getTextureDimension(level);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

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
		cluthash = clutHash_ ^ gstate.clutformat;
	} else {
		cluthash = 0;
	}
	u64 cachekey = TexCacheEntry::CacheKey(texaddr, format, dim, cluthash);

	int bufw = GetTextureBufw(0, texaddr, format);
	u8 maxLevel = gstate.getTextureMaxLevel();

	u32 texhash = MiniHash((const u32 *)Memory::GetPointerUnchecked(texaddr));

	TexCache::iterator iter = cache_.find(cachekey);
	TexCacheEntry *entry = nullptr;

	// Note: It's necessary to reset needshadertexclamp, for otherwise DIRTY_TEXCLAMP won't get set later.
	// Should probably revisit how this works..
	gstate_c.SetNeedShaderTexclamp(false);
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;
	if (gstate_c.bgraTexture != isBgraBackend_) {
		gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
	}
	gstate_c.bgraTexture = isBgraBackend_;

	if (iter != cache_.end()) {
		entry = iter->second.get();
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
		} else if (!gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE)) {
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
						entry->framesUntilNextFullHash = std::min(512, entry->numFrames) + (((intptr_t)entry->textureName >> 8) & 15);
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
			gstate_c.curTextureWidth = w;
			gstate_c.curTextureHeight = h;
			if (rehash) {
				// Update in case any of these changed.
				entry->sizeInRAM = (textureBitsPerPixel[format] * bufw * h / 2) / 8;
				entry->bufw = bufw;
				entry->cluthash = cluthash;
			}

			nextTexture_ = entry;
			nextNeedsRehash_ = rehash;
			nextNeedsChange_ = false;
			// Might need a rebuild if the hash fails, but that will be set later.
			nextNeedsRebuild_ = false;
			VERBOSE_LOG(G3D, "Texture at %08x Found in Cache, applying", texaddr);
			return; //Done!
		} else {
			// Wasn't a match, we will rebuild.
			nextChangeReason_ = reason;
			nextNeedsChange_ = true;
		}
	} else {
		VERBOSE_LOG(G3D, "No texture in cache, decoding...");
		TexCacheEntry *entryNew = new TexCacheEntry{};
		cache_[cachekey].reset(entryNew);

		if (hasClut && clutRenderAddress_ != 0xFFFFFFFF) {
			WARN_LOG_REPORT_ONCE(clutUseRender, G3D, "Using texture with rendered CLUT: texfmt=%d, clutfmt=%d", gstate.getTextureFormat(), gstate.getClutPaletteFormat());
		}

		entry = entryNew;
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
	nextNeedsRebuild_ = true;
}

// Removes old textures.
void TextureCacheCommon::Decimate() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	if (cacheSizeEstimate_ >= TEXCACHE_MIN_PRESSURE) {
		const u32 had = cacheSizeEstimate_;

		ForgetLastTexture();
		int killAge = lowMemoryMode_ ? TEXTURE_KILL_AGE_LOWMEM : TEXTURE_KILL_AGE;
		for (TexCache::iterator iter = cache_.begin(); iter != cache_.end(); ) {
			if (iter->second->lastFrame + killAge < gpuStats.numFlips) {
				DeleteTexture(iter++);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(G3D, "Decimated texture cache, saved %d estimated bytes - now %d bytes", had - cacheSizeEstimate_, cacheSizeEstimate_);
	}

	// If enabled, we also need to clear the secondary cache.
	if (g_Config.bTextureSecondaryCache && secondCacheSizeEstimate_ >= TEXCACHE_SECOND_MIN_PRESSURE) {
		const u32 had = secondCacheSizeEstimate_;

		for (TexCache::iterator iter = secondCache_.begin(); iter != secondCache_.end(); ) {
			// In low memory mode, we kill them all since secondary cache is disabled.
			if (lowMemoryMode_ || iter->second->lastFrame + TEXTURE_SECOND_KILL_AGE < gpuStats.numFlips) {
				ReleaseTexture(iter->second.get(), true);
				secondCacheSizeEstimate_ -= EstimateTexMemoryUsage(iter->second.get());
				secondCache_.erase(iter++);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(G3D, "Decimated second texture cache, saved %d estimated bytes - now %d bytes", had - secondCacheSizeEstimate_, secondCacheSizeEstimate_);
	}

	DecimateVideos();
}

void TextureCacheCommon::DecimateVideos() {
	if (!videos_.empty()) {
		for (auto iter = videos_.begin(); iter != videos_.end(); ) {
			if (iter->second + VIDEO_DECIMATE_AGE < gpuStats.numFlips) {
				videos_.erase(iter++);
			} else {
				++iter;
			}
		}
	}
}

bool TextureCacheCommon::HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete) {
	bool replaceImages = false;

	cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
	entry->numInvalidated++;
	gpuStats.numTextureInvalidations++;
	DEBUG_LOG(G3D, "Texture different or overwritten, reloading at %08x: %s", entry->addr, reason);
	if (doDelete) {
		if (initialMatch && standardScaleFactor_ == 1 && (entry->status & TexCacheEntry::STATUS_IS_SCALED) == 0) {
			// Actually, if size and number of levels match, let's try to avoid deleting and recreating.
			// Instead, let's use glTexSubImage to replace the images.
			replaceImages = true;
		} else {
			InvalidateLastTexture();
			ReleaseTexture(entry, true);
			entry->status &= ~TexCacheEntry::STATUS_IS_SCALED;
		}
	}
	// Clear the reliable bit if set.
	if (entry->GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
		entry->SetHashStatus(TexCacheEntry::STATUS_HASHING);
	}

	// Also, mark any textures with the same address but different clut.  They need rechecking.
	if (entry->cluthash != 0) {
		const u64 cachekeyMin = (u64)(entry->addr & 0x3FFFFFFF) << 32;
		const u64 cachekeyMax = cachekeyMin + (1ULL << 32);
		for (auto it = cache_.lower_bound(cachekeyMin), end = cache_.upper_bound(cachekeyMax); it != end; ++it) {
			if (it->second->cluthash != entry->cluthash) {
				it->second->status |= TexCacheEntry::STATUS_CLUT_RECHECK;
			}
		}
	}

	entry->status |= TexCacheEntry::STATUS_UNRELIABLE;
	if (entry->numFrames < TEXCACHE_FRAME_CHANGE_FREQUENT) {
		if (entry->status & TexCacheEntry::STATUS_FREE_CHANGE) {
			entry->status &= ~TexCacheEntry::STATUS_FREE_CHANGE;
		} else {
			entry->status |= TexCacheEntry::STATUS_CHANGE_FREQUENT;
		}
	}
	entry->numFrames = 0;

	return replaceImages;
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
		for (auto it = cache_.lower_bound(cacheKey), end = cache_.upper_bound(cacheKeyEnd); it != end; ++it) {
			AttachFramebuffer(it->second.get(), addr, framebuffer);
		}
		// Let's assume anything in mirrors is fair game to check.
		for (auto it = cache_.lower_bound(mirrorCacheKey), end = cache_.upper_bound(mirrorCacheKeyEnd); it != end; ++it) {
			const u64 mirrorlessKey = it->first & ~0x0060000000000000ULL;
			// Let's still make sure it's in the cache range.
			if (mirrorlessKey >= cacheKey && mirrorlessKey <= cacheKeyEnd) {
				AttachFramebuffer(it->second.get(), addr, framebuffer);
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

			DetachFramebuffer(cache_[cachekey].get(), addr, framebuffer);
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
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;
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
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;
		entry->maxLevel = 0;
		fbTexInfo_[cachekey] = fbInfo;
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

void TextureCacheCommon::DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer) {
	if (entry->framebuffer == framebuffer) {
		const u64 cachekey = entry->CacheKey();
		cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);
		entry->framebuffer = nullptr;
		fbTexInfo_.erase(cachekey);
		host->GPUNotifyTextureAttachment(entry->addr);
	}
}

bool TextureCacheCommon::AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset) {
	static const u32 MAX_SUBAREA_Y_OFFSET_SAFE = 32;

	AttachedFramebufferInfo fbInfo = { 0 };

	const u64 mirrorMask = 0x00600000;
	// Must be in VRAM so | 0x04000000 it is.  Also, ignore memory mirrors.
	const u32 addr = (address | 0x04000000) & 0x3FFFFFFF & ~mirrorMask;
	const u32 texaddr = ((entry->addr + texaddrOffset) & ~mirrorMask);
	const bool noOffset = texaddr == addr;
	const bool exactMatch = noOffset && entry->format < 4;
	const u32 w = 1 << ((entry->dim >> 0) & 0xf);
	const u32 h = 1 << ((entry->dim >> 8) & 0xf);
	// 512 on a 272 framebuffer is sane, so let's be lenient.
	const u32 minSubareaHeight = h / 4;

	// If they match exactly, it's non-CLUT and from the top left.
	if (exactMatch) {
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

		// Check if it's in bufferWidth (which might be higher than width and may indicate the framebuffer includes the data.)
		if (fbInfo.xOffset >= framebuffer->bufferWidth && fbInfo.xOffset + w <= (u32)framebuffer->fb_stride) {
			// This happens in Brave Story, see #10045 - the texture is in the space between strides, with matching stride.
			DetachFramebuffer(entry, address, framebuffer);
			return false;
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

void TextureCacheCommon::SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
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
		if (gstate_c.bgraTexture) {
			gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
		} else if ((gstate_c.curTextureXOffset == 0) != (fbInfo.xOffset == 0) || (gstate_c.curTextureYOffset == 0) != (fbInfo.yOffset == 0)) {
			gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}
		gstate_c.bgraTexture = false;
		gstate_c.curTextureXOffset = fbInfo.xOffset;
		gstate_c.curTextureYOffset = fbInfo.yOffset;
		u32 texW = (u32)gstate.getTextureWidth(0);
		u32 texH = (u32)gstate.getTextureHeight(0);
		gstate_c.SetNeedShaderTexclamp(gstate_c.curTextureWidth != texW || gstate_c.curTextureHeight != texH);
		if (gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0) {
			gstate_c.SetNeedShaderTexclamp(true);
		}

		nextTexture_ = entry;
	} else {
		if (framebuffer->fbo) {
			framebuffer->fbo->Release();
			framebuffer->fbo = nullptr;
		}
		Unbind();
		gstate_c.SetNeedShaderTexclamp(false);
	}

	nextNeedsRehash_ = false;
	nextNeedsChange_ = false;
	nextNeedsRebuild_ = false;
}

bool TextureCacheCommon::SetOffsetTexture(u32 offset) {
	if (g_Config.iRenderingMode != FB_BUFFERED_MODE) {
		return false;
	}
	u32 texaddr = gstate.getTextureAddress(0);
	if (!Memory::IsValidAddress(texaddr) || !Memory::IsValidAddress(texaddr + offset)) {
		return false;
	}

	const u16 dim = gstate.getTextureDimension(0);
	u64 cachekey = TexCacheEntry::CacheKey(texaddr, gstate.getTextureFormat(), dim, 0);
	TexCache::iterator iter = cache_.find(cachekey);
	if (iter == cache_.end()) {
		return false;
	}
	TexCacheEntry *entry = iter->second.get();

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

		scaleFactor = std::min(5, scaleFactor);
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

	replacer_.NotifyConfigChanged();
}

void TextureCacheCommon::NotifyVideoUpload(u32 addr, int size, int width, GEBufferFormat fmt) {
	addr &= 0x3FFFFFFF;
	videos_[addr] = gpuStats.numFlips;
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
			framebufferManager_->DownloadFramebufferForClut(clutRenderAddress_, clutRenderOffset_ + bytes);
			Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, bytes);
			if (bytes < loadBytes) {
				memset((u8 *)clutBufRaw_ + bytes, 0x00, loadBytes - bytes);
			}
		} else {
#ifdef _M_SSE
			if (bytes == loadBytes) {
				const __m128i *source = (const __m128i *)Memory::GetPointerUnchecked(clutAddr);
				__m128i *dest = (__m128i *)clutBufRaw_;
				int numBlocks = bytes / 32;
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
#elif PPSSPP_ARCH(ARM_NEON)
			if (bytes == loadBytes) {
				const uint32_t *source = (const uint32_t *)Memory::GetPointerUnchecked(clutAddr);
				uint32_t *dest = (uint32_t *)clutBufRaw_;
				int numBlocks = bytes / 32;
				for (int i = 0; i < numBlocks; i++, source += 8, dest += 8) {
					uint32x4_t data1 = vld1q_u32(source);
					uint32x4_t data2 = vld1q_u32(source + 4);
					vst1q_u32(dest, data1);
					vst1q_u32(dest + 4, data2);
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

bool TextureCacheCommon::GetCurrentClutBuffer(GPUDebugBuffer &buffer) {
	const u32 bpp = gstate.getClutPaletteFormat() == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const u32 pixels = 1024 / bpp;

	buffer.Allocate(pixels, 1, (GEBufferFormat)gstate.getClutPaletteFormat());
	memcpy(buffer.GetData(), clutBufRaw_, 1024);
	return true;
}

// Host memory usage, not PSP memory usage.
u32 TextureCacheCommon::EstimateTexMemoryUsage(const TexCacheEntry *entry) {
	const u16 dim = entry->dim;
	// TODO: This does not take into account the HD remaster's larger textures.
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

static void ReverseColors(void *dstBuf, const void *srcBuf, GETextureFormat fmt, int numPixels, bool useBGRA) {
	switch (fmt) {
	case GE_TFMT_4444:
		ConvertRGBA4444ToABGR4444((u16 *)dstBuf, (const u16 *)srcBuf, numPixels);
		break;
	// Final Fantasy 2 uses this heavily in animated textures.
	case GE_TFMT_5551:
		ConvertRGBA5551ToABGR1555((u16 *)dstBuf, (const u16 *)srcBuf, numPixels);
		break;
	case GE_TFMT_5650:
		ConvertRGB565ToBGR565((u16 *)dstBuf, (const u16 *)srcBuf, numPixels);
		break;
	default:
		if (useBGRA) {
			ConvertRGBA8888ToBGRA8888((u32 *)dstBuf, (const u32 *)srcBuf, numPixels);
		} else {
			// No need to convert RGBA8888, right order already
			if (dstBuf != srcBuf)
				memcpy(dstBuf, srcBuf, numPixels * sizeof(u32));
		}
		break;
	}
}

static inline void ConvertFormatToRGBA8888(GETextureFormat format, u32 *dst, const u16 *src, u32 numPixels) {
	switch (format) {
	case GE_TFMT_4444:
		ConvertRGBA4444ToRGBA8888(dst, src, numPixels);
		break;
	case GE_TFMT_5551:
		ConvertRGBA5551ToRGBA8888(dst, src, numPixels);
		break;
	case GE_TFMT_5650:
		ConvertRGBA565ToRGBA8888(dst, src, numPixels);
		break;
	default:
		_dbg_assert_msg_(G3D, false, "Incorrect texture format.");
		break;
	}
}

static inline void ConvertFormatToRGBA8888(GEPaletteFormat format, u32 *dst, const u16 *src, u32 numPixels) {
	// The supported values are 1:1 identical.
	ConvertFormatToRGBA8888(GETextureFormat(format), dst, src, numPixels);
}

void TextureCacheCommon::DecodeTextureLevel(u8 *out, int outPitch, GETextureFormat format, GEPaletteFormat clutformat, uint32_t texaddr, int level, int bufw, bool reverseColors, bool useBGRA, bool expandTo32bit) {
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

	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	const u8 *texptr = Memory::GetPointer(texaddr);

	switch (format) {
	case GE_TFMT_CLUT4:
	{
		const bool mipmapShareClut = gstate.isClutSharedForMipmaps();
		const int clutSharingOffset = mipmapShareClut ? 0 : level * 16;

		if (swizzled) {
			tmpTexBuf32_.resize(bufw * ((h + 7) & ~7));
			UnswizzleFromMem(tmpTexBuf32_.data(), bufw / 2, texptr, bufw, h, 0);
			texptr = (u8 *)tmpTexBuf32_.data();
		}

		switch (clutformat) {
		case GE_CMODE_16BIT_BGR5650:
		case GE_CMODE_16BIT_ABGR5551:
		case GE_CMODE_16BIT_ABGR4444:
		{
			const u16 *clut = GetCurrentClut<u16>() + clutSharingOffset;
			if (clutAlphaLinear_ && mipmapShareClut && !expandTo32bit) {
				// Here, reverseColors means the CLUT is already reversed.
				if (reverseColors) {
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4Optimal((u16 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clutAlphaLinearColor_);
					}
				} else {
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4OptimalRev((u16 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clutAlphaLinearColor_);
					}
				}
			} else {
				if (expandTo32bit && !reverseColors) {
					// We simply expand the CLUT to 32-bit, then we deindex as usual. Probably the fastest way.
					ConvertFormatToRGBA8888(clutformat, expandClut_, clut, 16);
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4((u32 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, expandClut_);
					}
				} else {
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4((u16 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clut);
					}
				}
			}
		}
		break;

		case GE_CMODE_32BIT_ABGR8888:
		{
			const u32 *clut = GetCurrentClut<u32>() + clutSharingOffset;
			for (int y = 0; y < h; ++y) {
				DeIndexTexture4((u32 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clut);
			}
		}
		break;

		default:
			ERROR_LOG_REPORT(G3D, "Unknown CLUT4 texture mode %d", gstate.getClutPaletteFormat());
			return;
		}
	}
	break;

	case GE_TFMT_CLUT8:
		ReadIndexedTex(out, outPitch, level, texptr, 1, bufw, expandTo32bit);
		break;

	case GE_TFMT_CLUT16:
		ReadIndexedTex(out, outPitch, level, texptr, 2, bufw, expandTo32bit);
		break;

	case GE_TFMT_CLUT32:
		ReadIndexedTex(out, outPitch, level, texptr, 4, bufw, expandTo32bit);
		break;

	case GE_TFMT_4444:
	case GE_TFMT_5551:
	case GE_TFMT_5650:
		if (!swizzled) {
			// Just a simple copy, we swizzle the color format.
			if (reverseColors) {
				for (int y = 0; y < h; ++y) {
					ReverseColors(out + outPitch * y, texptr + bufw * sizeof(u16) * y, format, w, useBGRA);
				}
			} else if (expandTo32bit) {
				for (int y = 0; y < h; ++y) {
					ConvertFormatToRGBA8888(format, (u32 *)(out + outPitch * y), (const u16 *)texptr + bufw * y, w);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					memcpy(out + outPitch * y, texptr + bufw * sizeof(u16) * y, w * sizeof(u16));
				}
			}
		} else if (h >= 8 && !expandTo32bit) {
			// Note: this is always safe since h must be a power of 2, so a multiple of 8.
			UnswizzleFromMem((u32 *)out, outPitch, texptr, bufw, h, 2);
			if (reverseColors) {
				ReverseColors(out, out, format, h * outPitch / 2, useBGRA);
			}
		} else {
			// We don't have enough space for all rows in out, so use a temp buffer.
			tmpTexBuf32_.resize(bufw * ((h + 7) & ~7));
			UnswizzleFromMem(tmpTexBuf32_.data(), bufw * 2, texptr, bufw, h, 2);
			const u8 *unswizzled = (u8 *)tmpTexBuf32_.data();

			if (reverseColors) {
				for (int y = 0; y < h; ++y) {
					ReverseColors(out + outPitch * y, unswizzled + bufw * sizeof(u16) * y, format, w, useBGRA);
				}
			} else if (expandTo32bit) {
				for (int y = 0; y < h; ++y) {
					ConvertFormatToRGBA8888(format, (u32 *)(out + outPitch * y), (const u16 *)unswizzled + bufw * y, w);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					memcpy(out + outPitch * y, unswizzled + bufw * sizeof(u16) * y, w * sizeof(u16));
				}
			}
		}
		break;

	case GE_TFMT_8888:
		if (!swizzled) {
			if (reverseColors) {
				for (int y = 0; y < h; ++y) {
					ReverseColors(out + outPitch * y, texptr + bufw * sizeof(u32) * y, format, w, useBGRA);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					memcpy(out + outPitch * y, texptr + bufw * sizeof(u32) * y, w * sizeof(u32));
				}
			}
		} else if (h >= 8) {
			UnswizzleFromMem((u32 *)out, outPitch, texptr, bufw, h, 4);
			if (reverseColors) {
				ReverseColors(out, out, format, h * outPitch / 4, useBGRA);
			}
		} else {
			// We don't have enough space for all rows in out, so use a temp buffer.
			tmpTexBuf32_.resize(bufw * ((h + 7) & ~7));
			UnswizzleFromMem(tmpTexBuf32_.data(), bufw * 4, texptr, bufw, h, 4);
			const u8 *unswizzled = (u8 *)tmpTexBuf32_.data();

			if (reverseColors) {
				for (int y = 0; y < h; ++y) {
					ReverseColors(out + outPitch * y, unswizzled + bufw * sizeof(u32) * y, format, w, useBGRA);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					memcpy(out + outPitch * y, unswizzled + bufw * sizeof(u32) * y, w * sizeof(u32));
				}
			}
		}
		break;

	case GE_TFMT_DXT1:
	{
		int minw = std::min(bufw, w);
		u32 *dst = (u32 *)out;
		int outPitch32 = outPitch / sizeof(u32);
		DXT1Block *src = (DXT1Block*)texptr;

		for (int y = 0; y < h; y += 4) {
			u32 blockIndex = (y / 4) * (bufw / 4);
			int blockHeight = std::min(h - y, 4);
			for (int x = 0; x < minw; x += 4) {
				DecodeDXT1Block(dst + outPitch32 * y + x, src + blockIndex, outPitch32, blockHeight, false);
				blockIndex++;
			}
		}
		w = (w + 3) & ~3;
		if (reverseColors) {
			ReverseColors(out, out, GE_TFMT_8888, outPitch32 * h, useBGRA);
		}
		break;
	}

	case GE_TFMT_DXT3:
	{
		int minw = std::min(bufw, w);
		u32 *dst = (u32 *)out;
		int outPitch32 = outPitch / sizeof(u32);
		DXT3Block *src = (DXT3Block*)texptr;

		for (int y = 0; y < h; y += 4) {
			u32 blockIndex = (y / 4) * (bufw / 4);
			int blockHeight = std::min(h - y, 4);
			for (int x = 0; x < minw; x += 4) {
				DecodeDXT3Block(dst + outPitch32 * y + x, src + blockIndex, outPitch32, blockHeight);
				blockIndex++;
			}
		}
		w = (w + 3) & ~3;
		if (reverseColors) {
			ReverseColors(out, out, GE_TFMT_8888, outPitch32 * h, useBGRA);
		}
		break;
	}

	case GE_TFMT_DXT5:
	{
		int minw = std::min(bufw, w);
		u32 *dst = (u32 *)out;
		int outPitch32 = outPitch / sizeof(u32);
		DXT5Block *src = (DXT5Block*)texptr;

		for (int y = 0; y < h; y += 4) {
			u32 blockIndex = (y / 4) * (bufw / 4);
			int blockHeight = std::min(h - y, 4);
			for (int x = 0; x < minw; x += 4) {
				DecodeDXT5Block(dst + outPitch32 * y + x, src + blockIndex, outPitch32, blockHeight);
				blockIndex++;
			}
		}
		w = (w + 3) & ~3;
		if (reverseColors) {
			ReverseColors(out, out, GE_TFMT_8888, outPitch32 * h, useBGRA);
		}
		break;
	}

	default:
		ERROR_LOG_REPORT(G3D, "Unknown Texture Format %d!!!", format);
		break;
	}
}

void TextureCacheCommon::ReadIndexedTex(u8 *out, int outPitch, int level, const u8 *texptr, int bytesPerIndex, int bufw, bool expandTo32Bit) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	if (gstate.isTextureSwizzled()) {
		tmpTexBuf32_.resize(bufw * ((h + 7) & ~7));
		UnswizzleFromMem(tmpTexBuf32_.data(), bufw * bytesPerIndex, texptr, bufw, h, bytesPerIndex);
		texptr = (u8 *)tmpTexBuf32_.data();
	}

	int palFormat = gstate.getClutPaletteFormat();

	const u16 *clut16 = (const u16 *)clutBuf_;
	const u32 *clut32 = (const u32 *)clutBuf_;

	if (expandTo32Bit && palFormat != GE_CMODE_32BIT_ABGR8888) {
		ConvertFormatToRGBA8888(GEPaletteFormat(palFormat), expandClut_, clut16, 256);
		clut32 = expandClut_;
		palFormat = GE_CMODE_32BIT_ABGR8888;
	}

	switch (palFormat) {
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
	{
		switch (bytesPerIndex) {
		case 1:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u16 *)(out + outPitch * y), (const u8 *)texptr + bufw * y, w, clut16);
			}
			break;

		case 2:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u16 *)(out + outPitch * y), (const u16_le *)texptr + bufw * y, w, clut16);
			}
			break;

		case 4:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u16 *)(out + outPitch * y), (const u32_le *)texptr + bufw * y, w, clut16);
			}
			break;
		}
	}
	break;

	case GE_CMODE_32BIT_ABGR8888:
	{
		switch (bytesPerIndex) {
		case 1:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u32 *)(out + outPitch * y), (const u8 *)texptr + bufw * y, w, clut32);
			}
			break;

		case 2:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u32 *)(out + outPitch * y), (const u16_le *)texptr + bufw * y, w, clut32);
			}
			break;

		case 4:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u32 *)(out + outPitch * y), (const u32_le *)texptr + bufw * y, w, clut32);
			}
			break;
		}
	}
	break;

	default:
		ERROR_LOG_REPORT(G3D, "Unhandled clut texture mode %d!!!", gstate.getClutPaletteFormat());
		break;
	}
}

void TextureCacheCommon::ApplyTexture() {
	TexCacheEntry *entry = nextTexture_;
	if (entry == nullptr) {
		return;
	}
	nextTexture_ = nullptr;

	UpdateMaxSeenV(entry, gstate.isModeThrough());

	bool replaceImages = false;
	if (nextNeedsRebuild_) {
		// Regardless of hash fails or otherwise, if this is a video, mark it frequently changing.
		// This prevents temporary scaling perf hits on the first second of video.
		bool isVideo = videos_.find(entry->addr & 0x3FFFFFFF) != videos_.end();
		if (isVideo) {
			entry->status |= TexCacheEntry::STATUS_CHANGE_FREQUENT;
		}

		if (nextNeedsRehash_) {
			PROFILE_THIS_SCOPE("texhash");
			// Update the hash on the texture.
			int w = gstate.getTextureWidth(0);
			int h = gstate.getTextureHeight(0);
			entry->fullhash = QuickTexHash(replacer_, entry->addr, entry->bufw, w, h, GETextureFormat(entry->format), entry);

			// TODO: Here we could check the secondary cache; maybe the texture is in there?
			// We would need to abort the build if so.
		}
		if (nextNeedsChange_) {
			// This texture existed previously, let's handle the change.
			replaceImages = HandleTextureChange(entry, nextChangeReason_, false, true);
		}
		// We actually build afterward (shared with rehash rebuild.)
	} else if (nextNeedsRehash_) {
		// Okay, this matched and didn't change - but let's check the hash.  Maybe it will change.
		bool doDelete = true;
		if (!CheckFullHash(entry, doDelete)) {
			replaceImages = HandleTextureChange(entry, "hash fail", true, doDelete);
			nextNeedsRebuild_ = true;
		} else if (nextTexture_ != nullptr) {
			// The secondary cache may choose an entry from its storage by setting nextTexture_.
			// This means we should set that, instead of our previous entry.
			entry = nextTexture_;
			nextTexture_ = nullptr;
			UpdateMaxSeenV(entry, gstate.isModeThrough());
		}
	}

	// Okay, now actually rebuild the texture if needed.
	if (nextNeedsRebuild_) {
		BuildTexture(entry, replaceImages);
	}

	entry->lastFrame = gpuStats.numFlips;
	if (entry->framebuffer) {
		ApplyTextureFramebuffer(entry, entry->framebuffer);
	} else {
		BindTexture(entry);
		gstate_c.SetTextureFullAlpha(entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL);
	}
}

void TextureCacheCommon::Clear(bool delete_them) {
	ForgetLastTexture();
	for (TexCache::iterator iter = cache_.begin(); iter != cache_.end(); ++iter) {
		ReleaseTexture(iter->second.get(), delete_them);
	}
	// In case the setting was changed, we ALWAYS clear the secondary cache (enabled or not.)
	for (TexCache::iterator iter = secondCache_.begin(); iter != secondCache_.end(); ++iter) {
		ReleaseTexture(iter->second.get(), delete_them);
	}
	if (cache_.size() + secondCache_.size()) {
		INFO_LOG(G3D, "Texture cached cleared from %i textures", (int)(cache_.size() + secondCache_.size()));
		cache_.clear();
		secondCache_.clear();
		cacheSizeEstimate_ = 0;
		secondCacheSizeEstimate_ = 0;
	}
	fbTexInfo_.clear();
	videos_.clear();
}

void TextureCacheCommon::DeleteTexture(TexCache::iterator it) {
	ReleaseTexture(it->second.get(), true);
	auto fbInfo = fbTexInfo_.find(it->first);
	if (fbInfo != fbTexInfo_.end()) {
		fbTexInfo_.erase(fbInfo);
	}
	cacheSizeEstimate_ -= EstimateTexMemoryUsage(it->second.get());
	cache_.erase(it);
}

bool TextureCacheCommon::CheckFullHash(TexCacheEntry *entry, bool &doDelete) {
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	u32 fullhash;
	{
		PROFILE_THIS_SCOPE("texhash");
		fullhash = QuickTexHash(replacer_, entry->addr, entry->bufw, w, h, GETextureFormat(entry->format), entry);
	}

	if (fullhash == entry->fullhash) {
		if (g_Config.bTextureBackoffCache) {
			if (entry->GetHashStatus() != TexCacheEntry::STATUS_HASHING && entry->numFrames > TexCacheEntry::FRAMES_REGAIN_TRUST) {
				// Reset to STATUS_HASHING.
				entry->SetHashStatus(TexCacheEntry::STATUS_HASHING);
				entry->status &= ~TexCacheEntry::STATUS_CHANGE_FREQUENT;
			}
		} else if (entry->numFrames > TEXCACHE_FRAME_CHANGE_FREQUENT_REGAIN_TRUST) {
			entry->status &= ~TexCacheEntry::STATUS_CHANGE_FREQUENT;
		}

		return true;
	}

	// Don't give up just yet.  Let's try the secondary cache if it's been invalidated before.
	if (g_Config.bTextureSecondaryCache) {
		// Don't forget this one was unreliable (in case we match a secondary entry.)
		entry->status |= TexCacheEntry::STATUS_UNRELIABLE;

		// If it's failed a bunch of times, then the second cache is just wasting time and VRAM.
		// In that case, skip.
		if (entry->numInvalidated > 2 && entry->numInvalidated < 128 && !lowMemoryMode_) {
			// We have a new hash: look for that hash in the secondary cache.
			u64 secondKey = fullhash | (u64)entry->cluthash << 32;
			TexCache::iterator secondIter = secondCache_.find(secondKey);
			if (secondIter != secondCache_.end()) {
				// Found it, but does it match our current params?  If not, abort.
				TexCacheEntry *secondEntry = secondIter->second.get();
				if (secondEntry->Matches(entry->dim, entry->format, entry->maxLevel)) {
					// Reset the numInvalidated value lower, we got a match.
					if (entry->numInvalidated > 8) {
						--entry->numInvalidated;
					}

					// Now just use our archived texture, instead of entry.
					nextTexture_ = secondEntry;
					return true;
				}
			} else {
				// It wasn't found, so we're about to throw away entry and rebuild a texture.
				// Let's save this in the secondary cache in case it gets used again.
				secondKey = entry->fullhash | ((u64)entry->cluthash << 32);
				secondCacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

				// If the entry already exists in the secondary texture cache, drop it nicely.
				auto oldIter = secondCache_.find(secondKey);
				if (oldIter != secondCache_.end()) {
					ReleaseTexture(oldIter->second.get(), true);
				}

				// Archive the entire texture entry as is, since we'll use its params if it is seen again.
				// We keep parameters on the current entry, since we are STILL building a new texture here.
				secondCache_[secondKey].reset(new TexCacheEntry(*entry));

				// Make sure we don't delete the texture we just archived.
				entry->texturePtr = nullptr;
				doDelete = false;
			}
		}
	}

	// We know it failed, so update the full hash right away.
	entry->fullhash = fullhash;
	return false;
}

void TextureCacheCommon::Invalidate(u32 addr, int size, GPUInvalidationType type) {
	// They could invalidate inside the texture, let's just give a bit of leeway.
	const int LARGEST_TEXTURE_SIZE = 512 * 512 * 4;

	addr &= 0x3FFFFFFF;
	const u32 addr_end = addr + size;

	if (type == GPU_INVALIDATE_ALL) {
		// This is an active signal from the game that something in the texture cache may have changed.
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	} else {
		// Do a quick check to see if the current texture is in range.
		const u32 currentAddr = gstate.getTextureAddress(0);
		if (addr_end >= currentAddr && addr < currentAddr + LARGEST_TEXTURE_SIZE) {
			gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		}
	}

	// If we're hashing every use, without backoff, then this isn't needed.
	if (!g_Config.bTextureBackoffCache) {
		return;
	}

	const u64 startKey = (u64)(addr - LARGEST_TEXTURE_SIZE) << 32;
	u64 endKey = (u64)(addr + size + LARGEST_TEXTURE_SIZE) << 32;
	if (endKey < startKey) {
		endKey = (u64)-1;
	}

	for (TexCache::iterator iter = cache_.lower_bound(startKey), end = cache_.upper_bound(endKey); iter != end; ++iter) {
		u32 texAddr = iter->second->addr;
		u32 texEnd = iter->second->addr + iter->second->sizeInRAM;

		if (texAddr < addr_end && addr < texEnd) {
			if (iter->second->GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
				iter->second->SetHashStatus(TexCacheEntry::STATUS_HASHING);
			}
			if (type != GPU_INVALIDATE_ALL) {
				gpuStats.numTextureInvalidations++;
				// Start it over from 0 (unless it's safe.)
				iter->second->numFrames = type == GPU_INVALIDATE_SAFE ? 256 : 0;
				if (type == GPU_INVALIDATE_SAFE) {
					u32 diff = gpuStats.numFlips - iter->second->lastFrame;
					// We still need to mark if the texture is frequently changing, even if it's safely changing.
					if (diff < TEXCACHE_FRAME_CHANGE_FREQUENT) {
						iter->second->status |= TexCacheEntry::STATUS_CHANGE_FREQUENT;
					}
				}
				iter->second->framesUntilNextFullHash = 0;
			} else if (!iter->second->framebuffer) {
				iter->second->invalidHint++;
			}
		}
	}
}

void TextureCacheCommon::InvalidateAll(GPUInvalidationType /*unused*/) {
	// If we're hashing every use, without backoff, then this isn't needed.
	if (!g_Config.bTextureBackoffCache) {
		return;
	}

	if (timesInvalidatedAllThisFrame_ > 5) {
		return;
	}
	timesInvalidatedAllThisFrame_++;

	for (TexCache::iterator iter = cache_.begin(), end = cache_.end(); iter != end; ++iter) {
		if (iter->second->GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
			iter->second->SetHashStatus(TexCacheEntry::STATUS_HASHING);
		}
		if (!iter->second->framebuffer) {
			iter->second->invalidHint++;
		}
	}
}

void TextureCacheCommon::ClearNextFrame() {
	clearCacheNextFrame_ = true;
}
