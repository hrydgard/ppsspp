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

#include "ppsspp_config.h"

#include <algorithm>

#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"
#include "Common/MemoryUtil.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "Core/Util/PPGeDraw.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

// Videos should be updated every few frames, so we forget quickly.
#define VIDEO_DECIMATE_AGE 4

// If a texture hasn't been seen for this many frames, get rid of it.
#define TEXTURE_KILL_AGE 200
#define TEXTURE_KILL_AGE_LOWMEM 60
// Not used in lowmem mode.
#define TEXTURE_SECOND_KILL_AGE 100
// Used when there are multiple CLUT variants of a texture.
#define TEXTURE_KILL_AGE_CLUT 6

#define TEXTURE_CLUT_VARIANTS_MIN 6

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

// Allow the extra bits from the remasters for the purposes of this.
inline int dimWidth(u16 dim) {
	return 1 << (dim & 0xFF);
}

inline int dimHeight(u16 dim) {
	return 1 << ((dim >> 8) & 0xFF);
}

// Vulkan color formats:
// TODO
TextureCacheCommon::TextureCacheCommon(Draw::DrawContext *draw)
	: draw_(draw),
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
	tmpTexBufRearrange_.resize(512 * 512);   // 1MB

	replacer_.Init();
}

TextureCacheCommon::~TextureCacheCommon() {
	FreeAlignedMemory(clutBufConverted_);
	FreeAlignedMemory(clutBufRaw_);
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

SamplerCacheKey TextureCacheCommon::GetSamplingParams(int maxLevel, const TexCacheEntry *entry) {
	SamplerCacheKey key;

	int minFilt = gstate.texfilter & 0x7;
	key.minFilt = minFilt & 1;
	key.mipEnable = (minFilt >> 2) & 1;
	key.mipFilt = (minFilt >> 1) & 1;
	key.magFilt = gstate.isMagnifyFilteringEnabled();
	key.sClamp = gstate.isTexCoordClampedS();
	key.tClamp = gstate.isTexCoordClampedT();
	key.aniso = false;

	GETexLevelMode mipMode = gstate.getTexLevelMode();
	bool autoMip = mipMode == GE_TEXLEVEL_MODE_AUTO;

	// TODO: Slope mipmap bias is still not well understood.
	float lodBias = (float)gstate.getTexLevelOffset16() * (1.0f / 16.0f);
	if (mipMode == GE_TEXLEVEL_MODE_SLOPE) {
		lodBias += 1.0f + TexLog2(gstate.getTextureLodSlope()) * (1.0f / 256.0f);
	}

	// If mip level is forced to zero, disable mipmapping.
	bool noMip = maxLevel == 0 || (!autoMip && lodBias <= 0.0f);
	if (IsFakeMipmapChange()) {
		noMip = noMip || !autoMip;
	}

	if (noMip) {
		// Enforce no mip filtering, for safety.
		key.mipEnable = false;
		key.mipFilt = 0;
		lodBias = 0.0f;
	}

	if (!key.mipEnable) {
		key.maxLevel = 0;
		key.minLevel = 0;
		key.lodBias = 0;
		key.mipFilt = 0;
	} else {
		switch (mipMode) {
		case GE_TEXLEVEL_MODE_AUTO:
			key.maxLevel = maxLevel * 256;
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
			key.maxLevel = maxLevel * 256;
			key.minLevel = 0;
			key.lodBias = 0;
			break;
		}
	}

	// Video bilinear override
	if (!key.magFilt && entry != nullptr && IsVideo(entry->addr)) {
		// Enforce bilinear filtering on magnification.
		key.magFilt = 1;
	}

	// Filtering overrides from replacements or settings.
	TextureFiltering forceFiltering = TEX_FILTER_AUTO;
	u64 cachekey = replacer_.Enabled() ? (entry ? entry->CacheKey() : 0) : 0;
	if (!replacer_.Enabled() || entry == nullptr || !replacer_.FindFiltering(cachekey, entry->fullhash, &forceFiltering)) {
		switch (g_Config.iTexFiltering) {
		case TEX_FILTER_AUTO:
			// Follow what the game wants. We just do a single heuristic change to avoid bleeding of wacky color test colors
			// in higher resolution (used by some games for sprites, and they accidentally have linear filter on).
			if (gstate.isModeThrough() && g_Config.iInternalResolution != 1) {
				bool uglyColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && gstate.getColorTestRef() != 0;
				if (uglyColorTest)
					forceFiltering = TEX_FILTER_FORCE_NEAREST;
			}
			break;
		case TEX_FILTER_FORCE_LINEAR:
			// Override to linear filtering if there's no alpha or color testing going on.
			if ((!gstate.isColorTestEnabled() || IsColorTestTriviallyTrue()) &&
				(!gstate.isAlphaTestEnabled() || IsAlphaTestTriviallyTrue())) {
				forceFiltering = TEX_FILTER_FORCE_LINEAR;
			}
			break;
		case TEX_FILTER_FORCE_NEAREST:
			// Just force to nearest without checks. Safe (but ugly).
			forceFiltering = TEX_FILTER_FORCE_NEAREST;
			break;
		case TEX_FILTER_AUTO_MAX_QUALITY:
		default:
			forceFiltering = TEX_FILTER_AUTO_MAX_QUALITY;
			if (gstate.isModeThrough() && g_Config.iInternalResolution != 1) {
				bool uglyColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && gstate.getColorTestRef() != 0;
				if (uglyColorTest)
					forceFiltering = TEX_FILTER_FORCE_NEAREST;
			}
			break;
		}
	}

	switch (forceFiltering) {
	case TEX_FILTER_AUTO:
		break;
	case TEX_FILTER_FORCE_LINEAR:
		key.magFilt = 1;
		key.minFilt = 1;
		key.mipFilt = 1;
		break;
	case TEX_FILTER_FORCE_NEAREST:
		key.magFilt = 0;
		key.minFilt = 0;
		break;
	case TEX_FILTER_AUTO_MAX_QUALITY:
		// NOTE: We do not override magfilt here. If a game should have pixellated filtering,
		// let it keep it. But we do enforce minification and mipmap filtering and max out the level.
		// Later we'll also auto-generate any missing mipmaps.
		key.minFilt = 1;
		key.mipFilt = 1;
		key.maxLevel = 9 * 256;
		key.lodBias = 0.0f;
		if (gstate_c.Supports(GPU_SUPPORTS_ANISOTROPY) && g_Config.iAnisotropyLevel > 0) {
			key.aniso = true;
		}
		break;
	}

	return key;
}

SamplerCacheKey TextureCacheCommon::GetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight) {
	SamplerCacheKey key = GetSamplingParams(0, nullptr);

	// Kill any mipmapping settings.
	key.mipEnable = false;
	key.mipFilt = false;
	key.aniso = 0.0;
	key.maxLevel = 0.0f;

	// Often the framebuffer will not match the texture size. We'll wrap/clamp in the shader in that case.
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (w != bufferWidth || h != bufferHeight) {
		key.sClamp = true;
		key.tClamp = true;
	}
	return key;
}

void TextureCacheCommon::UpdateMaxSeenV(TexCacheEntry *entry, bool throughMode) {
	// If the texture is >= 512 pixels tall...
	if (entry->dim >= 0x900) {
		if (entry->cluthash != 0 && entry->maxSeenV == 0) {
			const u64 cachekeyMin = (u64)(entry->addr & 0x3FFFFFFF) << 32;
			const u64 cachekeyMax = cachekeyMin + (1ULL << 32);
			for (auto it = cache_.lower_bound(cachekeyMin), end = cache_.upper_bound(cachekeyMax); it != end; ++it) {
				// They should all be the same, just make sure we take any that has already increased.
				// This is for a new texture.
				if (it->second->maxSeenV != 0) {
					entry->maxSeenV = it->second->maxSeenV;
					break;
				}
			}
		}

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

		// We need to keep all CLUT variants in sync so we detect changes properly.
		// See HandleTextureChange / STATUS_CLUT_RECHECK.
		if (entry->cluthash != 0) {
			const u64 cachekeyMin = (u64)(entry->addr & 0x3FFFFFFF) << 32;
			const u64 cachekeyMax = cachekeyMin + (1ULL << 32);
			for (auto it = cache_.lower_bound(cachekeyMin), end = cache_.upper_bound(cachekeyMax); it != end; ++it) {
				it->second->maxSeenV = entry->maxSeenV;
			}
		}
	}
}

TexCacheEntry *TextureCacheCommon::SetTexture() {
	u8 level = 0;
	if (IsFakeMipmapChange()) {
		level = std::max(0, gstate.getTexLevelOffset16() / 16);
	}
	u32 texaddr = gstate.getTextureAddress(level);
	if (!Memory::IsValidAddress(texaddr)) {
		// Bind a null texture and return.
		Unbind();
		return nullptr;
	}

	const u16 dim = gstate.getTextureDimension(level);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	GETextureFormat format = gstate.getTextureFormat();
	if (format >= 11) {
		// TODO: Better assumption? Doesn't really matter, these are invalid.
		format = GE_TFMT_5650;
	}

	bool hasClut = gstate.isTextureFormatIndexed();
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

	u32 minihash = MiniHash((const u32 *)Memory::GetPointerUnchecked(texaddr));

	TexCache::iterator entryIter = cache_.find(cachekey);
	TexCacheEntry *entry = nullptr;

	// Note: It's necessary to reset needshadertexclamp, for otherwise DIRTY_TEXCLAMP won't get set later.
	// Should probably revisit how this works..
	gstate_c.SetNeedShaderTexclamp(false);
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;
	if (gstate_c.bgraTexture != isBgraBackend_) {
		gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
	}
	gstate_c.bgraTexture = isBgraBackend_;

	if (entryIter != cache_.end()) {
		entry = entryIter->second.get();
		// Validate the texture still matches the cache entry.
		bool match = entry->Matches(dim, format, maxLevel);
		const char *reason = "different params";

		// Check for FBO changes.
		if (entry->status & TexCacheEntry::STATUS_FRAMEBUFFER_OVERLAP) {
			// Fall through to the end where we'll delete the entry if there's a framebuffer.
			entry->status &= ~TexCacheEntry::STATUS_FRAMEBUFFER_OVERLAP;
			match = false;
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

		// Do we need to recreate?
		if (entry->status & TexCacheEntry::STATUS_FORCE_REBUILD) {
			match = false;
			entry->status &= ~TexCacheEntry::STATUS_FORCE_REBUILD;
		}

		if (match) {
			if (entry->lastFrame != gpuStats.numFlips) {
				u32 diff = gpuStats.numFlips - entry->lastFrame;
				entry->numFrames++;

				if (entry->framesUntilNextFullHash < diff) {
					// Exponential backoff up to 512 frames.  Textures are often reused.
					if (entry->numFrames > 32) {
						// Also, try to add some "randomness" to avoid rehashing several textures the same frame.
						entry->framesUntilNextFullHash = std::min(512, entry->numFrames) + (((intptr_t)(entry->textureName) >> 12) & 15);
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

			if (minihash != entry->minihash) {
				match = false;
				reason = "minihash";
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

		if (match && (entry->status & TexCacheEntry::STATUS_TO_REPLACE) && replacementTimeThisFrame_ < replacementFrameBudget_) {
			int w0 = gstate.getTextureWidth(0);
			int h0 = gstate.getTextureHeight(0);
			ReplacedTexture &replaced = FindReplacement(entry, w0, h0);
			if (replaced.Valid()) {
				match = false;
				reason = "replacing";
			}
		}

		if (match) {
			// got one!
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
			VERBOSE_LOG(G3D, "Texture at %08x found in cache, applying", texaddr);
			return entry; //Done!
		} else {
			// Wasn't a match, we will rebuild.
			nextChangeReason_ = reason;
			nextNeedsChange_ = true;
			// Fall through to the rebuild case.
		}
	}

	// No texture found, or changed (depending on entry).
	// Check for framebuffers.

	TextureDefinition def{};
	def.addr = texaddr;
	def.dim = dim;
	def.format = format;
	def.bufw = bufw;

	std::vector<AttachCandidate> candidates = GetFramebufferCandidates(def, 0);
	if (candidates.size() > 0) {
		int index = GetBestCandidateIndex(candidates);
		if (index != -1) {
			// If we had a texture entry here, let's get rid of it.
			if (entryIter != cache_.end()) {
				DeleteTexture(entryIter);
			}

			const AttachCandidate &candidate = candidates[index];
			nextTexture_ = nullptr;
			nextNeedsRebuild_ = false;
			SetTextureFramebuffer(candidate);
			return nullptr;
		}
	}

	// Didn't match a framebuffer, keep going.

	if (!entry) {
		VERBOSE_LOG(G3D, "No texture in cache for %08x, decoding...", texaddr);
		entry = new TexCacheEntry{};
		cache_[cachekey].reset(entry);

		if (hasClut && clutRenderAddress_ != 0xFFFFFFFF) {
			WARN_LOG_REPORT_ONCE(clutUseRender, G3D, "Using texture with rendered CLUT: texfmt=%d, clutfmt=%d", gstate.getTextureFormat(), gstate.getClutPaletteFormat());
		}

		if (PPGeIsFontTextureAddress(texaddr)) {
			// It's the builtin font texture.
			entry->status = TexCacheEntry::STATUS_RELIABLE;
		} else if (g_Config.bTextureBackoffCache && !IsVideo(texaddr)) {
			entry->status = TexCacheEntry::STATUS_HASHING;
		} else {
			entry->status = TexCacheEntry::STATUS_UNRELIABLE;
		}

		if (hasClut && clutRenderAddress_ == 0xFFFFFFFF) {
			const u64 cachekeyMin = (u64)(texaddr & 0x3FFFFFFF) << 32;
			const u64 cachekeyMax = cachekeyMin + (1ULL << 32);

			int found = 0;
			for (auto it = cache_.lower_bound(cachekeyMin), end = cache_.upper_bound(cachekeyMax); it != end; ++it) {
				found++;
			}

			if (found >= TEXTURE_CLUT_VARIANTS_MIN) {
				for (auto it = cache_.lower_bound(cachekeyMin), end = cache_.upper_bound(cachekeyMax); it != end; ++it) {
					it->second->status |= TexCacheEntry::STATUS_CLUT_VARIANTS;
				}

				entry->status |= TexCacheEntry::STATUS_CLUT_VARIANTS;
			}
		}

		nextNeedsChange_ = false;
	}

	// We have to decode it, let's setup the cache entry first.
	entry->addr = texaddr;
	entry->minihash = minihash;
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

	nextTexture_ = entry;
	if (nextFramebufferTexture_) {
		nextFramebufferTexture_ = nullptr;  // in case it was accidentally set somehow?
	}
	nextNeedsRehash_ = true;
	// We still need to rebuild, to allocate a texture.  But we'll bail early.
	nextNeedsRebuild_ = true;
	return entry;
}

std::vector<AttachCandidate> TextureCacheCommon::GetFramebufferCandidates(const TextureDefinition &entry, u32 texAddrOffset) {
	gpuStats.numFramebufferEvaluations++;

	std::vector<AttachCandidate> candidates;

	FramebufferNotificationChannel channel = Memory::IsDepthTexVRAMAddress(entry.addr) ? FramebufferNotificationChannel::NOTIFY_FB_DEPTH : FramebufferNotificationChannel::NOTIFY_FB_COLOR;
	if (channel == FramebufferNotificationChannel::NOTIFY_FB_DEPTH && !gstate_c.Supports(GPU_SUPPORTS_DEPTH_TEXTURE)) {
		// Depth texture not supported. Don't try to match it, fall back to the memory behind..
		return std::vector<AttachCandidate>();
	}

	const std::vector<VirtualFramebuffer *> &framebuffers = framebufferManager_->Framebuffers();

	for (VirtualFramebuffer *framebuffer : framebuffers) {
		FramebufferMatchInfo match = MatchFramebuffer(entry, framebuffer, texAddrOffset, channel);
		switch (match.match) {
		case FramebufferMatch::VALID:
			candidates.push_back(AttachCandidate{ match, entry, framebuffer, channel });
			break;
		default:
			break;
		}
	}

	if (candidates.size() > 1) {
		bool depth = channel == FramebufferNotificationChannel::NOTIFY_FB_DEPTH;

		std::string cands;
		for (auto &candidate : candidates) {
			cands += candidate.ToString() + " ";
		}

		WARN_LOG_REPORT_ONCE(multifbcandidate, G3D, "GetFramebufferCandidates(%s): Multiple (%d) candidate framebuffers. First will be chosen. texaddr: %08x offset: %d (%dx%d stride %d, %s):\n%s",
			depth ? "DEPTH" : "COLOR", (int)candidates.size(),
			entry.addr, texAddrOffset, dimWidth(entry.dim), dimHeight(entry.dim), entry.bufw, GeTextureFormatToString(entry.format),
			cands.c_str()
		);
	}

	return candidates;
}

int TextureCacheCommon::GetBestCandidateIndex(const std::vector<AttachCandidate> &candidates) {
	_dbg_assert_(!candidates.empty());

	if (candidates.size() == 1) {
		return 0;
	}

	// OK, multiple possible candidates. Will need to figure out which one is the most relevant.
	int bestRelevancy = -1;
	int bestIndex = -1;

	// TODO: Instead of scores, we probably want to use std::min_element to pick the top element, using 
	// a comparison function.
	for (int i = 0; i < (int)candidates.size(); i++) {
		const AttachCandidate &candidate = candidates[i];
		int relevancy = 0;
		switch (candidate.match.match) {
		case FramebufferMatch::VALID:
			relevancy += 1000;
			break;
		default:
			break;
		}

		// Bonus point for matching stride.
		if (candidate.channel == NOTIFY_FB_COLOR && candidate.fb->fb_stride == candidate.entry.bufw) {
			relevancy += 100;
		}

		// Bonus points for no offset.
		if (candidate.match.xOffset == 0 && candidate.match.yOffset == 0) {
			relevancy += 10;
		}

		if (candidate.channel == NOTIFY_FB_COLOR && candidate.fb->last_frame_render == gpuStats.numFlips) {
			relevancy += 5;
		} else if (candidate.channel == NOTIFY_FB_DEPTH && candidate.fb->last_frame_depth_render == gpuStats.numFlips) {
			relevancy += 5;
		}

		if (relevancy > bestRelevancy) {
			bestRelevancy = relevancy;
			bestIndex = i;
		}
	}

	return bestIndex;
}

// Removes old textures.
void TextureCacheCommon::Decimate(bool forcePressure) {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	if (forcePressure || cacheSizeEstimate_ >= TEXCACHE_MIN_PRESSURE) {
		const u32 had = cacheSizeEstimate_;

		ForgetLastTexture();
		int killAgeBase = lowMemoryMode_ ? TEXTURE_KILL_AGE_LOWMEM : TEXTURE_KILL_AGE;
		for (TexCache::iterator iter = cache_.begin(); iter != cache_.end(); ) {
			bool hasClut = (iter->second->status & TexCacheEntry::STATUS_CLUT_VARIANTS) != 0;
			int killAge = hasClut ? TEXTURE_KILL_AGE_CLUT : killAgeBase;
			if (iter->second->lastFrame + killAge < gpuStats.numFlips) {
				DeleteTexture(iter++);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(G3D, "Decimated texture cache, saved %d estimated bytes - now %d bytes", had - cacheSizeEstimate_, cacheSizeEstimate_);
	}

	// If enabled, we also need to clear the secondary cache.
	if (g_Config.bTextureSecondaryCache && (forcePressure || secondCacheSizeEstimate_ >= TEXCACHE_SECOND_MIN_PRESSURE)) {
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
	replacer_.Decimate(forcePressure ? ReplacerDecimateMode::FORCE_PRESSURE : ReplacerDecimateMode::NEW_FRAME);
}

void TextureCacheCommon::DecimateVideos() {
	for (auto iter = videos_.begin(); iter != videos_.end(); ) {
		if (iter->flips + VIDEO_DECIMATE_AGE < gpuStats.numFlips) {
			iter = videos_.erase(iter++);
		} else {
			++iter;
		}
	}
}

bool TextureCacheCommon::IsVideo(u32 texaddr) {
	texaddr &= 0x3FFFFFFF;
	for (auto info : videos_) {
		if (texaddr < info.addr) {
			continue;
		}
		if (texaddr < info.addr + info.size) {
			return true;
		}
	}
	return false;
}

void TextureCacheCommon::HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete) {
	cacheSizeEstimate_ -= EstimateTexMemoryUsage(entry);
	entry->numInvalidated++;
	gpuStats.numTextureInvalidations++;
	DEBUG_LOG(G3D, "Texture different or overwritten, reloading at %08x: %s", entry->addr, reason);
	if (doDelete) {
		InvalidateLastTexture();
		ReleaseTexture(entry, true);
		entry->status &= ~TexCacheEntry::STATUS_IS_SCALED;
	}

	// Mark as hashing, if marked as reliable.
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

	if (entry->numFrames < TEXCACHE_FRAME_CHANGE_FREQUENT) {
		if (entry->status & TexCacheEntry::STATUS_FREE_CHANGE) {
			entry->status &= ~TexCacheEntry::STATUS_FREE_CHANGE;
		} else {
			entry->status |= TexCacheEntry::STATUS_CHANGE_FREQUENT;
		}
	}
	entry->numFrames = 0;
}

void TextureCacheCommon::NotifyFramebuffer(VirtualFramebuffer *framebuffer, FramebufferNotification msg) {
	const u32 mirrorMask = 0x00600000;
	const u32 fb_addr = framebuffer->fb_address;

	const u32 z_addr = framebuffer->z_address & ~mirrorMask;  // Probably unnecessary.

	const u32 fb_bpp = framebuffer->format == GE_FORMAT_8888 ? 4 : 2;
	const u32 z_bpp = 2;  // No other format exists.
	const u32 fb_stride = framebuffer->fb_stride;
	const u32 z_stride = framebuffer->z_stride;

	// NOTE: Some games like Burnout massively misdetects the height of some framebuffers, leading to a lot of unnecessary invalidations.
	// Let's only actually get rid of textures that cover the very start of the framebuffer.
	const u32 fb_endAddr = fb_addr + fb_stride * std::min((int)framebuffer->height, 16) * fb_bpp;
	const u32 z_endAddr = z_addr + z_stride * std::min((int)framebuffer->height, 16) * z_bpp;

	switch (msg) {
	case NOTIFY_FB_CREATED:
	case NOTIFY_FB_UPDATED:
	{
		// Try to match the new framebuffer to existing textures.
		// Backwards from the "usual" texturing case so can't share a utility function.

		std::vector<AttachCandidate> candidates;

		u64 cacheKey = (u64)fb_addr << 32;
		// If it has a clut, those are the low 32 bits, so it'll be inside this range.
		// Also, if it's a subsample of the buffer, it'll also be within the FBO.
		u64 cacheKeyEnd = (u64)fb_endAddr << 32;

		// Color - no need to look in the mirrors.
		for (auto it = cache_.lower_bound(cacheKey), end = cache_.upper_bound(cacheKeyEnd); it != end; ++it) {
			it->second->status |= TexCacheEntry::STATUS_FRAMEBUFFER_OVERLAP;
			gpuStats.numTextureInvalidationsByFramebuffer++;
		}

		if (z_stride != 0) {
			// Depth. Just look at the range, but in each mirror (0x04200000 and 0x04600000).
			// Games don't use 0x04400000 as far as I know - it has no swizzle effect so kinda useless.
			cacheKey = (u64)z_addr << 32;
			cacheKeyEnd = (u64)z_endAddr << 32;
			for (auto it = cache_.lower_bound(cacheKey | 0x200000), end = cache_.upper_bound(cacheKeyEnd | 0x200000); it != end; ++it) {
				it->second->status |= TexCacheEntry::STATUS_FRAMEBUFFER_OVERLAP;
				gpuStats.numTextureInvalidationsByFramebuffer++;
			}
			for (auto it = cache_.lower_bound(cacheKey | 0x600000), end = cache_.upper_bound(cacheKeyEnd | 0x600000); it != end; ++it) {
				it->second->status |= TexCacheEntry::STATUS_FRAMEBUFFER_OVERLAP;
				gpuStats.numTextureInvalidationsByFramebuffer++;
			}
		}
		break;
	}
	default:
		break;
	}
}

FramebufferMatchInfo TextureCacheCommon::MatchFramebuffer(
	const TextureDefinition &entry,
	VirtualFramebuffer *framebuffer, u32 texaddrOffset, FramebufferNotificationChannel channel) const {
	static const u32 MAX_SUBAREA_Y_OFFSET_SAFE = 32;

	uint32_t fb_address = channel == NOTIFY_FB_DEPTH ? framebuffer->z_address : framebuffer->fb_address;

	u32 addr = fb_address & 0x3FFFFFFF;
	u32 texaddr = entry.addr + texaddrOffset;

	bool texInVRAM = Memory::IsVRAMAddress(texaddr);
	bool fbInVRAM = Memory::IsVRAMAddress(fb_address);

	if (texInVRAM != fbInVRAM) {
		// Shortcut. Cannot possibly be a match.
		return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
	}

	if (texInVRAM) {
		const u32 mirrorMask = 0x00600000;

		// This bit controls swizzle. The swizzles at 0x00200000 and 0x00600000 are designed
		// to perfectly match reading depth as color (which one to use I think might be related
		// to the bpp of the color format used when rendering to it).
		// It's fairly unlikely that games would screw this up since the result will be garbage so
		// we use it to filter out unlikely matches.
		switch (entry.addr & mirrorMask) {
		case 0x00000000:
		case 0x00400000:
			// Don't match the depth channel with these addresses when texturing.
			if (channel == FramebufferNotificationChannel::NOTIFY_FB_DEPTH) {
				return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
			}
			break;
		case 0x00200000:
		case 0x00600000:
			// Don't match the color channel with these addresses when texturing.
			if (channel == FramebufferNotificationChannel::NOTIFY_FB_COLOR) {
				return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
			}
			break;
		}

		addr &= ~mirrorMask;
		texaddr &= ~mirrorMask;
	}

	const bool noOffset = texaddr == addr;
	const bool exactMatch = noOffset && entry.format < 4 && channel == NOTIFY_FB_COLOR;
	const u32 w = 1 << ((entry.dim >> 0) & 0xf);
	const u32 h = 1 << ((entry.dim >> 8) & 0xf);
	// 512 on a 272 framebuffer is sane, so let's be lenient.
	const u32 minSubareaHeight = h / 4;

	// If they match "exactly", it's non-CLUT and from the top left.
	if (exactMatch) {
		if (framebuffer->fb_stride != entry.bufw) {
			WARN_LOG_ONCE(diffStrides1, G3D, "Texturing from framebuffer with different strides %d != %d", entry.bufw, framebuffer->fb_stride);
		}
		// NOTE: This check is okay because the first texture formats are the same as the buffer formats.
		if (IsTextureFormatBufferCompatible(entry.format)) {
			if (TextureFormatMatchesBufferFormat(entry.format, framebuffer->format) || (framebuffer->usageFlags & FB_USAGE_BLUE_TO_ALPHA)) {
				return FramebufferMatchInfo{ FramebufferMatch::VALID };
			} else if (IsTextureFormat16Bit(entry.format) && IsBufferFormat16Bit(framebuffer->format)) {
				WARN_LOG_ONCE(diffFormat1, G3D, "Texturing from framebuffer with reinterpretable format: %s != %s", GeTextureFormatToString(entry.format), GeBufferFormatToString(framebuffer->format));
				return FramebufferMatchInfo{ FramebufferMatch::VALID, 0, 0, true, TextureFormatToBufferFormat(entry.format) };
			} else {
				WARN_LOG_ONCE(diffFormat2, G3D, "Texturing from framebuffer with incompatible formats %s != %s", GeTextureFormatToString(entry.format), GeBufferFormatToString(framebuffer->format));
				return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
			}
		} else {
			// Format incompatible, ignoring without comment. (maybe some really gnarly hacks will end up here...)
			return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
		}
	} else {
		// Apply to buffered mode only.
		if (!framebufferManager_->UseBufferedRendering()) {
			return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
		}

		// Check works for D16 too (???)
		const bool matchingClutFormat =
			(channel != NOTIFY_FB_COLOR && entry.format == GE_TFMT_CLUT16) ||
			(channel == NOTIFY_FB_COLOR && framebuffer->format == GE_FORMAT_8888 && entry.format == GE_TFMT_CLUT32) ||
			(channel == NOTIFY_FB_COLOR && framebuffer->format != GE_FORMAT_8888 && entry.format == GE_TFMT_CLUT16);

		// To avoid ruining git blame, kept the same name as the old struct.
		FramebufferMatchInfo fbInfo{ FramebufferMatch::VALID };

		const u32 bitOffset = (texaddr - addr) * 8;
		if (bitOffset != 0) {
			const u32 pixelOffset = bitOffset / std::max(1U, (u32)textureBitsPerPixel[entry.format]);

			fbInfo.yOffset = entry.bufw == 0 ? 0 : pixelOffset / entry.bufw;
			fbInfo.xOffset = entry.bufw == 0 ? 0 : pixelOffset % entry.bufw;
		}

		if (fbInfo.yOffset + minSubareaHeight >= framebuffer->height) {
			// Can't be inside the framebuffer.
			return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
		}

		if (framebuffer->fb_stride != entry.bufw) {
			if (noOffset) {
				WARN_LOG_ONCE(diffStrides2, G3D, "Texturing from framebuffer (matching_clut=%s) different strides %d != %d", matchingClutFormat ? "yes" : "no", entry.bufw, framebuffer->fb_stride);
				// Continue on with other checks.
				// Not actually sure why we even try here. There's no way it'll go well if the strides are different.
			} else {
				// Assume any render-to-tex with different bufw + offset is a render from ram.
				return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
			}
		}

		// Check if it's in bufferWidth (which might be higher than width and may indicate the framebuffer includes the data.)
		if (fbInfo.xOffset >= framebuffer->bufferWidth && fbInfo.xOffset + w <= (u32)framebuffer->fb_stride) {
			// This happens in Brave Story, see #10045 - the texture is in the space between strides, with matching stride.
			return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
		}

		// Trying to play it safe.  Below 0x04110000 is almost always framebuffers.
		// TODO: Maybe we can reduce this check and find a better way above 0x04110000?
		if (fbInfo.yOffset > MAX_SUBAREA_Y_OFFSET_SAFE && addr > 0x04110000 && !PSP_CoreParameter().compat.flags().AllowLargeFBTextureOffsets) {
			WARN_LOG_REPORT_ONCE(subareaIgnored, G3D, "Ignoring possible texturing from framebuffer at %08x +%dx%d / %dx%d", fb_address, fbInfo.xOffset, fbInfo.yOffset, framebuffer->width, framebuffer->height);
			return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
		}

		// Check for CLUT. The framebuffer is always RGB, but it can be interpreted as a CLUT texture.
		// 3rd Birthday (and a bunch of other games) render to a 16 bit clut texture.
		if (matchingClutFormat) {
			if (!noOffset) {
				WARN_LOG_ONCE(subareaClut, G3D, "Texturing from framebuffer using CLUT with offset at %08x +%dx%d", fb_address, fbInfo.xOffset, fbInfo.yOffset);
			}
			fbInfo.match = FramebufferMatch::VALID;  // We check the format again later, no need to return a special value here.
			return fbInfo;
		} else if (IsClutFormat((GETextureFormat)(entry.format)) || IsDXTFormat((GETextureFormat)(entry.format))) {
			WARN_LOG_ONCE(fourEightBit, G3D, "%s format not supported when texturing from framebuffer of format %s", GeTextureFormatToString(entry.format), GeBufferFormatToString(framebuffer->format));
			return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
		}

		// This is either normal or we failed to generate a shader to depalettize
		if ((int)framebuffer->format == (int)entry.format || matchingClutFormat) {
			if ((int)framebuffer->format != (int)entry.format) {
				WARN_LOG_ONCE(diffFormat2, G3D, "Texturing from framebuffer with different formats %s != %s at %08x",
					GeTextureFormatToString(entry.format), GeBufferFormatToString(framebuffer->format), fb_address);
				return fbInfo;
			} else {
				WARN_LOG_ONCE(subarea, G3D, "Texturing from framebuffer at %08x +%dx%d", fb_address, fbInfo.xOffset, fbInfo.yOffset);
				return fbInfo;
			}
		} else {
			WARN_LOG_ONCE(diffFormat2, G3D, "Texturing from framebuffer with incompatible format %s != %s at %08x",
				GeTextureFormatToString(entry.format), GeBufferFormatToString(framebuffer->format), fb_address);
			return FramebufferMatchInfo{ FramebufferMatch::NO_MATCH };
		}
	}
}

void TextureCacheCommon::SetTextureFramebuffer(const AttachCandidate &candidate) {
	VirtualFramebuffer *framebuffer = candidate.fb;
	FramebufferMatchInfo fbInfo = candidate.match;

	if (candidate.match.reinterpret) {
		GEBufferFormat oldFormat = candidate.fb->format;
		candidate.fb->format = candidate.match.reinterpretTo;
		framebufferManager_->ReinterpretFramebuffer(candidate.fb, oldFormat, candidate.match.reinterpretTo);
	}

	_dbg_assert_msg_(framebuffer != nullptr, "Framebuffer must not be null.");

	framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	if (framebufferManager_->UseBufferedRendering()) {
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

		nextFramebufferTexture_ = framebuffer;
		nextTexture_ = nullptr;
	} else {
		if (framebuffer->fbo) {
			framebuffer->fbo->Release();
			framebuffer->fbo = nullptr;
		}
		Unbind();
		gstate_c.SetNeedShaderTexclamp(false);
		nextFramebufferTexture_ = nullptr;
		nextTexture_ = nullptr;
	}

	nextNeedsRehash_ = false;
	nextNeedsChange_ = false;
	nextNeedsRebuild_ = false;
}

// Only looks for framebuffers.
bool TextureCacheCommon::SetOffsetTexture(u32 yOffset) {
	if (!framebufferManager_->UseBufferedRendering()) {
		return false;
	}

	u32 texaddr = gstate.getTextureAddress(0);
	GETextureFormat fmt = gstate.getTextureFormat();
	const u32 bpp = fmt == GE_TFMT_8888 ? 4 : 2;
	const u32 texaddrOffset = yOffset * gstate.getTextureWidth(0) * bpp;

	if (!Memory::IsValidAddress(texaddr) || !Memory::IsValidAddress(texaddr + texaddrOffset)) {
		return false;
	}

	TextureDefinition def;
	def.addr = texaddr;
	def.format = fmt;
	def.bufw = GetTextureBufw(0, texaddr, fmt);
	def.dim = gstate.getTextureDimension(0);

	std::vector<AttachCandidate> candidates = GetFramebufferCandidates(def, texaddrOffset);
	if (candidates.size() > 0) {
		int index = GetBestCandidateIndex(candidates);
		if (index != -1) {
			SetTextureFramebuffer(candidates[index]);
			return true;
		}
	}
	return false;
}

void TextureCacheCommon::NotifyConfigChanged() {
	int scaleFactor = g_Config.iTexScalingLevel;

	if (!gstate_c.Supports(GPU_SUPPORTS_TEXTURE_NPOT)) {
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
	videos_.push_back({ addr, (u32)size, gpuStats.numFlips });
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
			const std::vector<VirtualFramebuffer *> &framebuffers = framebufferManager_->Framebuffers();
			for (VirtualFramebuffer *framebuffer : framebuffers) {
				const u32 fb_address = framebuffer->fb_address & 0x3FFFFFFF;
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

			NotifyMemInfo(MemBlockFlags::ALLOC, clutAddr, loadBytes, "CLUT");
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

ReplacedTexture &TextureCacheCommon::FindReplacement(TexCacheEntry *entry, int &w, int &h) {
	// Short circuit the non-enabled case.
	// Otherwise, due to bReplaceTexturesAllowLate, we'll still spawn tasks looking for replacements
	// that then won't be used.
	if (!replacer_.Enabled()) {
		return replacer_.FindNone();
	}

	// Allow some delay to reduce pop-in.
	constexpr double MAX_BUDGET_PER_TEX = 0.25 / 60.0;

	double replaceStart = time_now_d();
	u64 cachekey = replacer_.Enabled() ? entry->CacheKey() : 0;
	ReplacedTexture &replaced = replacer_.FindReplacement(cachekey, entry->fullhash, w, h);
	if (replaced.IsReady(std::min(MAX_BUDGET_PER_TEX, replacementFrameBudget_ - replacementTimeThisFrame_))) {
		if (replaced.GetSize(0, w, h)) {
			replacementTimeThisFrame_ += time_now_d() - replaceStart;

			// Consider it already "scaled" and remove any delayed replace flag.
			entry->status |= TexCacheEntry::STATUS_IS_SCALED;
			entry->status &= ~TexCacheEntry::STATUS_TO_REPLACE;
			return replaced;
		}
	} else if (replaced.Valid()) {
		entry->status |= TexCacheEntry::STATUS_TO_REPLACE;
	}
	replacementTimeThisFrame_ += time_now_d() - replaceStart;
	return replacer_.FindNone();
}

// This is only used in the GLES backend, where we don't point these to video memory.
// So we shouldn't add a check for dstBuf != srcBuf, as long as the functions we call can handle that.
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
		ConvertRGB565ToRGBA8888(dst, src, numPixels);
		break;
	default:
		_dbg_assert_msg_(false, "Incorrect texture format.");
		break;
	}
}

static inline void ConvertFormatToRGBA8888(GEPaletteFormat format, u32 *dst, const u16 *src, u32 numPixels) {
	// The supported values are 1:1 identical.
	ConvertFormatToRGBA8888(GETextureFormat(format), dst, src, numPixels);
}

template <typename DXTBlock, int n>
static CheckAlphaResult DecodeDXTBlocks(uint8_t *out, int outPitch, uint32_t texaddr, const uint8_t *texptr,
	int w, int h, int bufw, bool reverseColors, bool useBGRA) {

	int minw = std::min(bufw, w);
	uint32_t *dst = (uint32_t *)out;
	int outPitch32 = outPitch / sizeof(uint32_t);
	const DXTBlock *src = (const DXTBlock *)texptr;


	if (!Memory::IsValidRange(texaddr, (h / 4) * (bufw / 4) * sizeof(DXTBlock))) {
		ERROR_LOG_REPORT(G3D, "DXT%d texture extends beyond valid RAM: %08x + %d x %d", n, texaddr, bufw, h);
		uint32_t limited = Memory::ValidSize(texaddr, (h / 4) * (bufw / 4) * sizeof(DXTBlock));
		// This might possibly be 0, but try to decode what we can (might even be how the PSP behaves.)
		h = (((int)limited / sizeof(DXTBlock)) / (bufw / 4)) * 4;
	}

	u32 alphaSum = 1;
	for (int y = 0; y < h; y += 4) {
		u32 blockIndex = (y / 4) * (bufw / 4);
		int blockHeight = std::min(h - y, 4);
		for (int x = 0; x < minw; x += 4) {
			switch (n) {
			case 1:
				DecodeDXT1Block(dst + outPitch32 * y + x, (const DXT1Block *)src + blockIndex, outPitch32, blockHeight, &alphaSum);
				break;
			case 3:
				DecodeDXT3Block(dst + outPitch32 * y + x, (const DXT3Block *)src + blockIndex, outPitch32, blockHeight);
				break;
			case 5:
				DecodeDXT5Block(dst + outPitch32 * y + x, (const DXT5Block *)src + blockIndex, outPitch32, blockHeight);
				break;
			}
			blockIndex++;
		}
	}

	if (reverseColors) {
		ReverseColors(out, out, GE_TFMT_8888, outPitch32 * h, useBGRA);
	}

	if (n == 1) {
		return alphaSum == 1 ? CHECKALPHA_FULL : CHECKALPHA_ANY;
	} else {
		// Just report that we don't have full alpha, since these formats are made for that.
		return CHECKALPHA_ANY;
	}
}

inline u32 ClutFormatToFullAlpha(GEPaletteFormat fmt, bool reverseColors) {
	switch (fmt) {
	case GE_CMODE_16BIT_ABGR4444: return reverseColors ? 0x000F : 0xF000;
	case GE_CMODE_16BIT_ABGR5551: return reverseColors ? 0x0001 : 0x8000;
	case GE_CMODE_32BIT_ABGR8888: return 0xFF000000;
	case GE_CMODE_16BIT_BGR5650: return 0;
	default: return 0;
	}
}

inline u32 TfmtRawToFullAlpha(GETextureFormat fmt) {
	switch (fmt) {
	case GE_TFMT_4444: return 0xF000;
	case GE_TFMT_5551: return 0x8000;
	case GE_TFMT_8888: return 0xFF000000;
	case GE_TFMT_5650: return 0;
	default: return 0;
	}
}

CheckAlphaResult TextureCacheCommon::DecodeTextureLevel(u8 *out, int outPitch, GETextureFormat format, GEPaletteFormat clutformat, uint32_t texaddr, int level, int bufw, bool reverseColors, bool useBGRA, bool expandTo32bit) {
	u32 alphaSum = 0xFFFFFFFF;
	u32 fullAlphaMask = 0x0;

	bool swizzled = gstate.isTextureSwizzled();
	if ((texaddr & 0x00600000) != 0 && Memory::IsVRAMAddress(texaddr)) {
		// This means it's in a mirror, possibly a swizzled mirror.  Let's report.
		WARN_LOG_REPORT_ONCE(texmirror, G3D, "Decoding texture from VRAM mirror at %08x swizzle=%d", texaddr, swizzled ? 1 : 0);
		if ((texaddr & 0x00200000) == 0x00200000) {
			// Technically 2 and 6 are slightly different, but this is better than nothing probably.
			// We should only see this with depth textures anyway which we don't support uploading (yet).
			swizzled = !swizzled;
		}
		// Note that (texaddr & 0x00600000) == 0x00600000 is very likely to be depth texturing.
	}

	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	const u8 *texptr = Memory::GetPointer(texaddr);
	const uint32_t byteSize = (textureBitsPerPixel[format] * bufw * h) / 8;

	char buf[128];
	size_t len = snprintf(buf, sizeof(buf), "Tex_%08x_%dx%d_%s", texaddr, w, h, GeTextureFormatToString(format, clutformat));
	NotifyMemInfo(MemBlockFlags::TEXTURE, texaddr, byteSize, buf, len);

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
			if (clutAlphaLinear_ && mipmapShareClut && !expandTo32bit) {
				// We don't bother with fullalpha here (clutAlphaLinear_)
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
				const u16 *clut = GetCurrentClut<u16>() + clutSharingOffset;
				if (expandTo32bit && !reverseColors) {
					// We simply expand the CLUT to 32-bit, then we deindex as usual. Probably the fastest way.
					ConvertFormatToRGBA8888(clutformat, expandClut_, clut, 16);
					fullAlphaMask = 0xFF000000;
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4<u32>((u32 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, expandClut_, &alphaSum);
					}
				} else {
					// If we're reversing colors, the CLUT was already reversed.
					fullAlphaMask = ClutFormatToFullAlpha(clutformat, reverseColors);
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4<u16>((u16 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clut, &alphaSum);
					}
				}
			}

			if (clutformat == GE_CMODE_16BIT_BGR5650) {
				// Our formula at the end of the function can't handle this cast so we return early.
				return CHECKALPHA_FULL;
			}
		}
		break;

		case GE_CMODE_32BIT_ABGR8888:
		{
			const u32 *clut = GetCurrentClut<u32>() + clutSharingOffset;
			fullAlphaMask = 0xFF000000;
			for (int y = 0; y < h; ++y) {
				DeIndexTexture4<u32>((u32 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clut, &alphaSum);
			}
		}
		break;

		default:
			ERROR_LOG_REPORT(G3D, "Unknown CLUT4 texture mode %d", gstate.getClutPaletteFormat());
			return CHECKALPHA_ANY;
		}
	}
	break;

	case GE_TFMT_CLUT8:
		return ReadIndexedTex(out, outPitch, level, texptr, 1, bufw, reverseColors, expandTo32bit);

	case GE_TFMT_CLUT16:
		return ReadIndexedTex(out, outPitch, level, texptr, 2, bufw, reverseColors, expandTo32bit);

	case GE_TFMT_CLUT32:
		return ReadIndexedTex(out, outPitch, level, texptr, 4, bufw, reverseColors, expandTo32bit);

	case GE_TFMT_4444:
	case GE_TFMT_5551:
	case GE_TFMT_5650:
		if (!swizzled) {
			// Just a simple copy, we swizzle the color format.
			fullAlphaMask = TfmtRawToFullAlpha(format);
			if (reverseColors) {
				// Just check the input's alpha to reuse code. TODO: make a specialized ReverseColors that checks as we go.
				for (int y = 0; y < h; ++y) {
					CheckMask16((const u16 *)(texptr + bufw * sizeof(u16) * y), w, &alphaSum);
					ReverseColors(out + outPitch * y, texptr + bufw * sizeof(u16) * y, format, w, useBGRA);
				}
			} else if (expandTo32bit) {
				for (int y = 0; y < h; ++y) {
					CheckMask16((const u16 *)(texptr + bufw * sizeof(u16) * y), w, &alphaSum);
					ConvertFormatToRGBA8888(format, (u32 *)(out + outPitch * y), (const u16 *)texptr + bufw * y, w);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					CopyAndSumMask16((u16 *)(out + outPitch * y), (u16 *)(texptr + bufw * sizeof(u16) * y), w, &alphaSum);
				}
			}
		} /* else if (h >= 8 && bufw <= w && !expandTo32bit) {
			// TODO: Handle alpha mask. This will require special versions of UnswizzleFromMem to keep the optimization.
			// Note: this is always safe since h must be a power of 2, so a multiple of 8.
			UnswizzleFromMem((u32 *)out, outPitch, texptr, bufw, h, 2);
			if (reverseColors) {
				ReverseColors(out, out, format, h * outPitch / 2, useBGRA);
			}
		}*/ else {
			// We don't have enough space for all rows in out, so use a temp buffer.
			tmpTexBuf32_.resize(bufw * ((h + 7) & ~7));
			UnswizzleFromMem(tmpTexBuf32_.data(), bufw * 2, texptr, bufw, h, 2);
			const u8 *unswizzled = (u8 *)tmpTexBuf32_.data();

			fullAlphaMask = TfmtRawToFullAlpha(format);
			if (reverseColors) {
				// Just check the swizzled input's alpha to reuse code. TODO: make a specialized ReverseColors that checks as we go.
				for (int y = 0; y < h; ++y) {
					CheckMask16((const u16 *)(unswizzled + bufw * sizeof(u16) * y), w, &alphaSum);
					ReverseColors(out + outPitch * y, unswizzled + bufw * sizeof(u16) * y, format, w, useBGRA);
				}
			} else if (expandTo32bit) {
				// Just check the swizzled input's alpha to reuse code. TODO: make a specialized ConvertFormatToRGBA8888 that checks as we go.
				for (int y = 0; y < h; ++y) {
					CheckMask16((const u16 *)(unswizzled + bufw * sizeof(u16) * y), w, &alphaSum);
					ConvertFormatToRGBA8888(format, (u32 *)(out + outPitch * y), (const u16 *)unswizzled + bufw * y, w);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					CopyAndSumMask16((u16 *)(out + outPitch * y), (const u16 *)(unswizzled + bufw * sizeof(u16) * y), w, &alphaSum);
				}
			}
		}
		if (format == GE_TFMT_5650) {
			return CHECKALPHA_FULL;
		}
		break;

	case GE_TFMT_8888:
		if (!swizzled) {
			fullAlphaMask = TfmtRawToFullAlpha(format);
			if (reverseColors) {
				for (int y = 0; y < h; ++y) {
					CheckMask32((const u32 *)(texptr + bufw * sizeof(u32) * y), w, &alphaSum);
					ReverseColors(out + outPitch * y, texptr + bufw * sizeof(u32) * y, format, w, useBGRA);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					CopyAndSumMask32((u32 *)(out + outPitch * y), (const u32 *)(texptr + bufw * sizeof(u32) * y), w, &alphaSum);
				}
			}
		} /* else if (h >= 8 && bufw <= w) {
			// TODO: Handle alpha mask
			UnswizzleFromMem((u32 *)out, outPitch, texptr, bufw, h, 4);
			if (reverseColors) {
				ReverseColors(out, out, format, h * outPitch / 4, useBGRA);
			}
		}*/ else {
			tmpTexBuf32_.resize(bufw * ((h + 7) & ~7));
			UnswizzleFromMem(tmpTexBuf32_.data(), bufw * 4, texptr, bufw, h, 4);
			const u8 *unswizzled = (u8 *)tmpTexBuf32_.data();

			fullAlphaMask = TfmtRawToFullAlpha(format);
			if (reverseColors) {
				for (int y = 0; y < h; ++y) {
					CheckMask32((const u32 *)(unswizzled + bufw * sizeof(u32) * y), w, &alphaSum);
					ReverseColors(out + outPitch * y, unswizzled + bufw * sizeof(u32) * y, format, w, useBGRA);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					CopyAndSumMask32((u32 *)(out + outPitch * y), (const u32 *)(unswizzled + bufw * sizeof(u32) * y), w, &alphaSum);
				}
			}
		}
		break;

	case GE_TFMT_DXT1:
		return DecodeDXTBlocks<DXT1Block, 1>(out, outPitch, texaddr, texptr, w, h, bufw, reverseColors, useBGRA);

	case GE_TFMT_DXT3:
		return DecodeDXTBlocks<DXT3Block, 3>(out, outPitch, texaddr, texptr, w, h, bufw, reverseColors, useBGRA);

	case GE_TFMT_DXT5:
		return DecodeDXTBlocks<DXT5Block, 5>(out, outPitch, texaddr, texptr, w, h, bufw, reverseColors, useBGRA);

	default:
		ERROR_LOG_REPORT(G3D, "Unknown Texture Format %d!!!", format);
		break;
	}

	return AlphaSumIsFull(alphaSum, fullAlphaMask) ? CHECKALPHA_FULL : CHECKALPHA_ANY;
}

CheckAlphaResult TextureCacheCommon::ReadIndexedTex(u8 *out, int outPitch, int level, const u8 *texptr, int bytesPerIndex, int bufw, bool reverseColors, bool expandTo32Bit) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	if (gstate.isTextureSwizzled()) {
		tmpTexBuf32_.resize(bufw * ((h + 7) & ~7));
		UnswizzleFromMem(tmpTexBuf32_.data(), bufw * bytesPerIndex, texptr, bufw, h, bytesPerIndex);
		texptr = (u8 *)tmpTexBuf32_.data();
	}

	GEPaletteFormat palFormat = (GEPaletteFormat)gstate.getClutPaletteFormat();

	const u16 *clut16 = (const u16 *)clutBuf_;
	const u32 *clut32 = (const u32 *)clutBuf_;

	if (expandTo32Bit && palFormat != GE_CMODE_32BIT_ABGR8888) {
		ConvertFormatToRGBA8888(GEPaletteFormat(palFormat), expandClut_, clut16, 256);
		clut32 = expandClut_;
		palFormat = GE_CMODE_32BIT_ABGR8888;
	}

	u32 alphaSum = 0xFFFFFFFF;
	u32 fullAlphaMask = ClutFormatToFullAlpha(palFormat, reverseColors);

	switch (palFormat) {
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
	{
		switch (bytesPerIndex) {
		case 1:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u16 *)(out + outPitch * y), (const u8 *)texptr + bufw * y, w, clut16, &alphaSum);
			}
			break;

		case 2:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u16 *)(out + outPitch * y), (const u16_le *)texptr + bufw * y, w, clut16, &alphaSum);
			}
			break;

		case 4:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u16 *)(out + outPitch * y), (const u32_le *)texptr + bufw * y, w, clut16, &alphaSum);
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
				DeIndexTexture((u32 *)(out + outPitch * y), (const u8 *)texptr + bufw * y, w, clut32, &alphaSum);
			}
			break;

		case 2:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u32 *)(out + outPitch * y), (const u16_le *)texptr + bufw * y, w, clut32, &alphaSum);
			}
			break;

		case 4:
			for (int y = 0; y < h; ++y) {
				DeIndexTexture((u32 *)(out + outPitch * y), (const u32_le *)texptr + bufw * y, w, clut32, &alphaSum);
			}
			break;
		}
	}
	break;

	default:
		ERROR_LOG_REPORT(G3D, "Unhandled clut texture mode %d!!!", gstate.getClutPaletteFormat());
		break;
	}

	if (palFormat == GE_CMODE_16BIT_BGR5650) {
		return CHECKALPHA_FULL;
	} else {
		return AlphaSumIsFull(alphaSum, fullAlphaMask) ? CHECKALPHA_FULL : CHECKALPHA_ANY;
	}
}

void TextureCacheCommon::ApplyTexture() {
	TexCacheEntry *entry = nextTexture_;
	if (!entry) {
		// Maybe we bound a framebuffer?
		InvalidateLastTexture();
		if (nextFramebufferTexture_) {
			bool depth = Memory::IsDepthTexVRAMAddress(gstate.getTextureAddress(0));
			// ApplyTextureFrameBuffer is responsible for setting SetTextureFullAlpha.
			ApplyTextureFramebuffer(nextFramebufferTexture_, gstate.getTextureFormat(), depth ? NOTIFY_FB_DEPTH : NOTIFY_FB_COLOR);
			nextFramebufferTexture_ = nullptr;
		}
		return;
	}

	nextTexture_ = nullptr;

	UpdateMaxSeenV(entry, gstate.isModeThrough());

	if (nextNeedsRebuild_) {
		// Regardless of hash fails or otherwise, if this is a video, mark it frequently changing.
		// This prevents temporary scaling perf hits on the first second of video.
		if (IsVideo(entry->addr)) {
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
			// The secondary cache may choose an entry from its storage by setting nextTexture_.
			// This means we should set that, instead of our previous entry.
			entry = nextTexture_;
			nextTexture_ = nullptr;
			UpdateMaxSeenV(entry, gstate.isModeThrough());
		}
	}

	// Okay, now actually rebuild the texture if needed.
	if (nextNeedsRebuild_) {
		_assert_(!entry->texturePtr);
		BuildTexture(entry);
		InvalidateLastTexture();
	}

	entry->lastFrame = gpuStats.numFlips;
	BindTexture(entry);
	gstate_c.SetTextureFullAlpha(entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL);
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
	videos_.clear();
}

void TextureCacheCommon::DeleteTexture(TexCache::iterator it) {
	ReleaseTexture(it->second.get(), true);
	cacheSizeEstimate_ -= EstimateTexMemoryUsage(it->second.get());
	cache_.erase(it);
}

bool TextureCacheCommon::CheckFullHash(TexCacheEntry *entry, bool &doDelete) {
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	bool isVideo = IsVideo(entry->addr);

	// Don't even check the texture, just assume it has changed.
	if (isVideo && g_Config.bTextureBackoffCache) {
		// Attempt to ensure the hash doesn't incorrectly match in if the video stops.
		entry->fullhash = (entry->fullhash + 0xA535A535) * 11 + (entry->fullhash & 4);
		return false;
	}

	u32 fullhash;
	{
		PROFILE_THIS_SCOPE("texhash");
		fullhash = QuickTexHash(replacer_, entry->addr, entry->bufw, w, h, GETextureFormat(entry->format), entry);
	}

	if (fullhash == entry->fullhash) {
		if (g_Config.bTextureBackoffCache && !isVideo) {
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
				// It wasn't found, so we're about to throw away the entry and rebuild a texture.
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
	// TODO: Keep track of the largest texture size in bytes, and use that instead of this
	// humongous unrealistic value.

	const int LARGEST_TEXTURE_SIZE = 512 * 512 * 4;

	addr &= 0x3FFFFFFF;
	const u32 addr_end = addr + size;

	if (type == GPU_INVALIDATE_ALL) {
		// This is an active signal from the game that something in the texture cache may have changed.
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	} else {
		// Do a quick check to see if the current texture could potentially be in range.
		const u32 currentAddr = gstate.getTextureAddress(0);
		// TODO: This can be made tighter.
		if (addr_end >= currentAddr && addr < currentAddr + LARGEST_TEXTURE_SIZE) {
			gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		}
	}

	// If we're hashing every use, without backoff, then this isn't needed.
	if (!g_Config.bTextureBackoffCache && type != GPU_INVALIDATE_FORCE) {
		return;
	}

	const u64 startKey = (u64)(addr - LARGEST_TEXTURE_SIZE) << 32;
	u64 endKey = (u64)(addr + size + LARGEST_TEXTURE_SIZE) << 32;
	if (endKey < startKey) {
		endKey = (u64)-1;
	}

	for (TexCache::iterator iter = cache_.lower_bound(startKey), end = cache_.upper_bound(endKey); iter != end; ++iter) {
		auto &entry = iter->second;
		u32 texAddr = entry->addr;
		u32 texEnd = entry->addr + entry->sizeInRAM;

		// Quick check for overlap. Yes the check is right.
		if (addr < texEnd && addr_end > texAddr) {
			if (entry->GetHashStatus() == TexCacheEntry::STATUS_RELIABLE) {
				entry->SetHashStatus(TexCacheEntry::STATUS_HASHING);
			}
			if (type == GPU_INVALIDATE_FORCE) {
				// Just random values to force the hash not to match.
				entry->fullhash = (entry->fullhash ^ 0x12345678) + 13;
				entry->minihash = (entry->minihash ^ 0x89ABCDEF) + 89;
			}
			if (type != GPU_INVALIDATE_ALL) {
				gpuStats.numTextureInvalidations++;
				// Start it over from 0 (unless it's safe.)
				entry->numFrames = type == GPU_INVALIDATE_SAFE ? 256 : 0;
				if (type == GPU_INVALIDATE_SAFE) {
					u32 diff = gpuStats.numFlips - entry->lastFrame;
					// We still need to mark if the texture is frequently changing, even if it's safely changing.
					if (diff < TEXCACHE_FRAME_CHANGE_FREQUENT) {
						entry->status |= TexCacheEntry::STATUS_CHANGE_FREQUENT;
					}
				}
				entry->framesUntilNextFullHash = 0;
			} else {
				entry->invalidHint++;
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
		iter->second->invalidHint++;
	}
}

void TextureCacheCommon::ClearNextFrame() {
	clearCacheNextFrame_ = true;
}

std::string AttachCandidate::ToString() {
	return StringFromFormat("[C:%08x/%d Z:%08x/%d X:%d Y:%d reint: %s]", this->fb->fb_address, this->fb->fb_stride, this->fb->z_address, this->fb->z_stride, this->match.xOffset, this->match.yOffset, this->match.reinterpret ? "true" : "false");
}
