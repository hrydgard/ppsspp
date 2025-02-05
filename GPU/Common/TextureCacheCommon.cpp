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
#include "Common/Data/Collections/TinySet.h"
#include "Common/Profiler/Profiler.h"
#include "Common/LogReporting.h"
#include "Common/MemoryUtil.h"
#include "Common/StringUtils.h"
#include "Common/Math/SIMDHeaders.h"
#include "Common/TimeUtil.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/thin3d.h"
#include "Core/HDRemaster.h"
#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/System.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/ge_constants.h"
#include "GPU/Debugger/Record.h"
#include "GPU/GPUState.h"
#include "Core/Util/PPGeDraw.h"

#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_internal.h"
#include "ext/imgui/imgui_impl_thin3d.h"

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

TextureCacheCommon::TextureCacheCommon(Draw::DrawContext *draw, Draw2D *draw2D)
	: draw_(draw), draw2D_(draw2D), replacer_(draw) {
	decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;

	// It's only possible to have 1KB of palette entries, although we allow 2KB in a hack.
	clutBufRaw_ = (u32 *)AllocateAlignedMemory(2048, 16);
	clutBufConverted_ = (u32 *)AllocateAlignedMemory(2048, 16);
	// Here we need 2KB to expand a 1KB CLUT.
	expandClut_ = (u32 *)AllocateAlignedMemory(2048, 16);

	_assert_(clutBufRaw_ && clutBufConverted_ && expandClut_);

	// Zap so we get consistent behavior if the game fails to load some of the CLUT.
	memset(clutBufRaw_, 0, 2048);
	memset(clutBufConverted_, 0, 2048);
	clutBuf_ = clutBufConverted_;

	// These buffers will grow if necessary, but most won't need more than this.
	tmpTexBuf32_.resize(512 * 512);  // 1MB
	tmpTexBufRearrange_.resize(512 * 512);   // 1MB

	textureShaderCache_ = new TextureShaderCache(draw, draw2D_);
}

TextureCacheCommon::~TextureCacheCommon() {
	delete textureShaderCache_;

	FreeAlignedMemory(clutBufConverted_);
	FreeAlignedMemory(clutBufRaw_);
	FreeAlignedMemory(expandClut_);
}

void TextureCacheCommon::StartFrame() {
	ForgetLastTexture();
	textureShaderCache_->Decimate();
	timesInvalidatedAllThisFrame_ = 0;
	replacementTimeThisFrame_ = 0.0;

	if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS) {
		gpuStats.numReplacerTrackedTex = replacer_.GetNumTrackedTextures();
		gpuStats.numCachedReplacedTextures = replacer_.GetNumCachedReplacedTextures();
	}

	if (texelsScaledThisFrame_) {
		VERBOSE_LOG(Log::G3D, "Scaled %d texels", texelsScaledThisFrame_);
	}
	texelsScaledThisFrame_ = 0;

	if (clearCacheNextFrame_) {
		Clear(true);
		clearCacheNextFrame_ = false;
	} else {
		Decimate(nullptr, false);
	}
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
	SamplerCacheKey key{};

	int minFilt = gstate.texfilter & 0x7;
	key.minFilt = minFilt & 1;
	key.mipEnable = (minFilt >> 2) & 1;
	key.mipFilt = (minFilt >> 1) & 1;
	key.magFilt = gstate.isMagnifyFilteringEnabled();
	key.sClamp = gstate.isTexCoordClampedS();
	key.tClamp = gstate.isTexCoordClampedT();
	key.aniso = false;
	key.texture3d = gstate_c.curTextureIs3D;

	GETexLevelMode mipMode = gstate.getTexLevelMode();
	bool autoMip = mipMode == GE_TEXLEVEL_MODE_AUTO;

	// TODO: Slope mipmap bias is still not well understood.
	float lodBias = (float)gstate.getTexLevelOffset16() * (1.0f / 16.0f);
	if (mipMode == GE_TEXLEVEL_MODE_SLOPE) {
		lodBias += 1.0f + TexLog2(gstate.getTextureLodSlope()) * (1.0f / 256.0f);
	}

	// If mip level is forced to zero, disable mipmapping.
	bool noMip = maxLevel == 0 || (!autoMip && lodBias <= 0.0f);
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
			if (gstate_c.Use(GPU_USE_ANISOTROPY) && g_Config.iAnisotropyLevel > 0) {
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
	bool useReplacerFiltering = false;
	if (entry && replacer_.Enabled() && entry->replacedTexture) {
		// If replacement textures have multiple mip levels, enforce mip filtering.
		if (entry->replacedTexture->State() == ReplacementState::ACTIVE && entry->replacedTexture->NumLevels() > 1) {
			key.mipEnable = true;
			key.mipFilt = 1;
			key.maxLevel = 9 * 256;
			if (gstate_c.Use(GPU_USE_ANISOTROPY) && g_Config.iAnisotropyLevel > 0) {
				key.aniso = true;
			}
		}
		useReplacerFiltering = entry->replacedTexture->ForceFiltering(&forceFiltering);
	}
	if (!useReplacerFiltering) {
		switch (g_Config.iTexFiltering) {
		case TEX_FILTER_AUTO:
			// Follow what the game wants. We just do a single heuristic change to avoid bleeding of wacky color test colors
			// in higher resolution (used by some games for sprites, and they accidentally have linear filter on).
			if (gstate.isModeThrough() && g_Config.iInternalResolution != 1) {
				bool uglyColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && gstate.getColorTestRef() != 0;
				if (uglyColorTest)
					forceFiltering = TEX_FILTER_FORCE_NEAREST;
			}
			if (gstate_c.pixelMapped) {
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
			if (gstate_c.pixelMapped) {
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
		if (gstate_c.Use(GPU_USE_ANISOTROPY) && g_Config.iAnisotropyLevel > 0) {
			key.aniso = true;
		}
		break;
	}

	return key;
}

SamplerCacheKey TextureCacheCommon::GetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight) {
	SamplerCacheKey key = GetSamplingParams(0, nullptr);

	// In case auto max quality was on, restore min filt. Another fix for water in Outrun.
	if (g_Config.iTexFiltering == TEX_FILTER_AUTO_MAX_QUALITY) {
		int minFilt = gstate.texfilter & 0x7;
		key.minFilt = minFilt & 1;
	}

	// Kill any mipmapping settings.
	key.mipEnable = false;
	key.mipFilt = false;
	key.aniso = 0.0f;
	key.maxLevel = 0.0f;
	key.lodBias = 0.0f;

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
		gstate_c.SetTextureIsVideo(false);
		gstate_c.SetTextureIs3D(false);
		gstate_c.SetTextureIsArray(false);
		gstate_c.SetTextureIsFramebuffer(false);
		return nullptr;
	}

	const u16 dim = gstate.getTextureDimension(level);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	GETextureFormat texFormat = gstate.getTextureFormat();
	if (texFormat >= 11) {
		// TODO: Better assumption? Doesn't really matter, these are invalid.
		texFormat = GE_TFMT_5650;
	}

	bool hasClut = gstate.isTextureFormatIndexed();
	bool hasClutGPU = false;
	u32 cluthash;
	if (hasClut) {
		if (clutRenderAddress_ != 0xFFFFFFFF) {
			gstate_c.curTextureXOffset = 0.0f;
			gstate_c.curTextureYOffset = 0.0f;
			hasClutGPU = true;
			cluthash = 0;  // Or should we use some other marker value?
		} else {
			if (clutLastFormat_ != gstate.clutformat) {
				// We update here because the clut format can be specified after the load.
				// TODO: Unify this as far as possible (I think only GLES backend really needs its own implementation due to different component order).
				UpdateCurrentClut(gstate.getClutPaletteFormat(), gstate.getClutIndexStartPos(), gstate.isClutIndexSimple());
			}
			cluthash = clutHash_ ^ gstate.clutformat;
		}
	} else {
		cluthash = 0;
	}
	u64 cachekey = TexCacheEntry::CacheKey(texaddr, texFormat, dim, cluthash);

	int bufw = GetTextureBufw(0, texaddr, texFormat);
	u8 maxLevel = gstate.getTextureMaxLevel();

	u32 minihash = MiniHash((const u32 *)Memory::GetPointerUnchecked(texaddr));

	TexCache::iterator entryIter = cache_.find(cachekey);
	TexCacheEntry *entry = nullptr;

	// Note: It's necessary to reset needshadertexclamp, for otherwise DIRTY_TEXCLAMP won't get set later.
	// Should probably revisit how this works..
	gstate_c.SetNeedShaderTexclamp(false);
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;

	if (entryIter != cache_.end()) {
		entry = entryIter->second.get();

		// Validate the texture still matches the cache entry.
		bool match = entry->Matches(dim, texFormat, maxLevel);
		const char *reason = "different params";

		// Check for dynamic CLUT status
		if (((entry->status & TexCacheEntry::STATUS_CLUT_GPU) != 0) != hasClutGPU) {
			// Need to recreate, suddenly a CLUT GPU texture was used without it, or vice versa.
			// I think this can only happen on a clut hash collision with the marker value, so highly unlikely.
			match = false;
		}

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
						// textureName is unioned with texturePtr and vkTex so will work for the other backends.
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
				// INFO_LOG(Log::G3D, "Reloading texture to do the scaling we skipped..");
				match = false;
				reason = "scaling";
			}
		}

		if (match && (entry->status & TexCacheEntry::STATUS_TO_REPLACE) && replacementTimeThisFrame_ < replacementFrameBudget_) {
			int w0 = gstate.getTextureWidth(0);
			int h0 = gstate.getTextureHeight(0);
			int d0 = 1;
			if (entry->replacedTexture) {
				PollReplacement(entry, &w0, &h0, &d0);
				// This texture is pending a replacement load.
				// So check the replacer if it's reached a conclusion.
				switch (entry->replacedTexture->State()) {
				case ReplacementState::NOT_FOUND:
					// Didn't find a replacement, so stop looking.
					// DEBUG_LOG(Log::G3D, "No replacement for texture %dx%d", w0, h0);
					entry->status &= ~TexCacheEntry::STATUS_TO_REPLACE;
					if (g_Config.bSaveNewTextures) {
						// Load it once more to actually save it. Since we don't set STATUS_TO_REPLACE, we won't end up looping.
						match = false;
						reason = "replacing";
					}
					break;
				case ReplacementState::ACTIVE:
					// There is now replacement data available!
					// Just reload the texture to process the replacement.
					match = false;
					reason = "replacing";
					break;
				default:
					// We'll just wait for a result.
					break;
				}
			}
		}

		if (match) {
			// got one!
			gstate_c.curTextureWidth = w;
			gstate_c.curTextureHeight = h;
			gstate_c.SetTextureIsVideo(false);
			gstate_c.SetTextureIs3D((entry->status & TexCacheEntry::STATUS_3D) != 0);
			gstate_c.SetTextureIsArray(false);
			gstate_c.SetTextureIsBGRA((entry->status & TexCacheEntry::STATUS_BGRA) != 0);
			gstate_c.SetTextureIsFramebuffer(false);

			if (rehash) {
				// Update in case any of these changed.
				entry->bufw = bufw;
				entry->cluthash = cluthash;
			}

			nextTexture_ = entry;
			nextNeedsRehash_ = rehash;
			nextNeedsChange_ = false;
			// Might need a rebuild if the hash fails, but that will be set later.
			nextNeedsRebuild_ = false;
			failedTexture_ = false;
			VERBOSE_LOG(Log::G3D, "Texture at %08x found in cache, applying", texaddr);
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
	def.format = texFormat;
	def.bufw = bufw;

	AttachCandidate bestCandidate;
	if (GetBestFramebufferCandidate(def, 0, &bestCandidate)) {
		// If we had a texture entry here, let's get rid of it.
		if (entryIter != cache_.end()) {
			DeleteTexture(entryIter);
		}

		nextTexture_ = nullptr;
		nextNeedsRebuild_ = false;

		SetTextureFramebuffer(bestCandidate);  // sets curTexture3D
		return nullptr;
	}

	// Didn't match a framebuffer, keep going.

	if (!entry) {
		VERBOSE_LOG(Log::G3D, "No texture in cache for %08x, decoding...", texaddr);
		entry = new TexCacheEntry{};
		cache_[cachekey].reset(entry);

		if (PPGeIsFontTextureAddress(texaddr)) {
			// It's the builtin font texture.
			entry->status = TexCacheEntry::STATUS_RELIABLE;
		} else if (g_Config.bTextureBackoffCache && !IsVideo(texaddr)) {
			entry->status = TexCacheEntry::STATUS_HASHING;
		} else {
			entry->status = TexCacheEntry::STATUS_UNRELIABLE;
		}

		if (hasClutGPU) {
			WARN_LOG_N_TIMES(clutUseRender, 5, Log::G3D, "Using texture with dynamic CLUT: texfmt=%d, clutfmt=%d", gstate.getTextureFormat(), gstate.getClutPaletteFormat());
			entry->status |= TexCacheEntry::STATUS_CLUT_GPU;
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
	entry->format = texFormat;
	entry->maxLevel = maxLevel;
	entry->status &= ~TexCacheEntry::STATUS_BGRA;

	entry->bufw = bufw;

	entry->cluthash = cluthash;

	gstate_c.curTextureWidth = w;
	gstate_c.curTextureHeight = h;
	gstate_c.SetTextureIsVideo(false);
	gstate_c.SetTextureIs3D((entry->status & TexCacheEntry::STATUS_3D) != 0);
	gstate_c.SetTextureIsArray(false);  // Ordinary 2D textures still aren't used by array view in VK. We probably might as well, though, at this point..
	gstate_c.SetTextureIsFramebuffer(false);

	failedTexture_ = false;
	nextTexture_ = entry;
	nextFramebufferTexture_ = nullptr;
	nextNeedsRehash_ = true;
	// We still need to rebuild, to allocate a texture.  But we'll bail early.
	nextNeedsRebuild_ = true;
	return entry;
}

bool TextureCacheCommon::GetBestFramebufferCandidate(const TextureDefinition &entry, u32 texAddrOffset, AttachCandidate *bestCandidate) const {
	gpuStats.numFramebufferEvaluations++;

	TinySet<AttachCandidate, 6> candidates;

	const std::vector<VirtualFramebuffer *> &framebuffers = framebufferManager_->Framebuffers();

	for (VirtualFramebuffer *framebuffer : framebuffers) {
		FramebufferMatchInfo match{};
		if (MatchFramebuffer(entry, framebuffer, texAddrOffset, RASTER_COLOR, &match)) {
			candidates.push_back(AttachCandidate{ framebuffer, match, RASTER_COLOR });
		}
		match = {};
		if (MatchFramebuffer(entry, framebuffer, texAddrOffset, RASTER_DEPTH, &match)) {
			candidates.push_back(AttachCandidate{ framebuffer, match, RASTER_DEPTH });
		}
	}

	if (candidates.size() == 0) {
		return false;
	} else if (candidates.size() == 1) {
		*bestCandidate = candidates[0];
		return true;
	}

	bool logging = Reporting::ShouldLogNTimes("multifbcandidate", 5);

	// OK, multiple possible candidates. Will need to figure out which one is the most relevant.
	int bestRelevancy = -1;
	size_t bestIndex = -1;

	bool kzCompat = PSP_CoreParameter().compat.flags().SplitFramebufferMargin;

	// We simply use the sequence counter as relevancy nowadays.
	for (size_t i = 0; i < candidates.size(); i++) {
		AttachCandidate &candidate = candidates[i];
		int relevancy = candidate.channel == RASTER_COLOR ? candidate.fb->colorBindSeq : candidate.fb->depthBindSeq;

		// Add a small negative penalty if the texture is currently bound as a framebuffer, and offset is not zero.
		// Should avoid problems when pingponging two nearby buffers, like in Wipeout Pure in #15927.
		if (candidate.channel == RASTER_COLOR &&
			(candidate.match.yOffset != 0 || candidate.match.xOffset != 0) &&
			candidate.fb->fb_address == (gstate.getFrameBufRawAddress() | 0x04000000)) {
			relevancy -= 2;
		}

		if (candidate.match.xOffset != 0 && PSP_CoreParameter().compat.flags().DisallowFramebufferAtOffset) {
			continue;
		}

		// Avoid binding as texture the framebuffer we're rendering to.
		// In Killzone, we split the framebuffer but the matching algorithm can still pick the wrong one,
		// which this avoids completely.
		if (kzCompat && candidate.fb == framebufferManager_->GetCurrentRenderVFB()) {
			continue;
		}

		if (logging) {
			candidate.relevancy = relevancy;
		}

		if (relevancy > bestRelevancy) {
			bestRelevancy = relevancy;
			bestIndex = i;
		}
	}

	if (logging) {
		std::string cands;
		for (size_t i = 0; i < candidates.size(); i++) {
			cands += candidates[i].ToString();
			if (i != candidates.size() - 1)
				cands += "\n";
		}
		cands += "\n";

		WARN_LOG(Log::G3D, "GetFramebufferCandidates(tex): Multiple (%d) candidate framebuffers. texaddr: %08x offset: %d (%dx%d stride %d, %s):\n%s",
			(int)candidates.size(),
			entry.addr, texAddrOffset, dimWidth(entry.dim), dimHeight(entry.dim), entry.bufw, GeTextureFormatToString(entry.format),
			cands.c_str()
		);
		logging = true;
	}

	if (bestIndex != -1) {
		if (logging) {
			WARN_LOG(Log::G3D, "Chose candidate %d:\n%s\n", (int)bestIndex, candidates[bestIndex].ToString().c_str());
		}
		*bestCandidate = candidates[bestIndex];
		return true;
	} else {
		return false;
	}
}

// Removes old textures.
void TextureCacheCommon::Decimate(TexCacheEntry *exceptThisOne, bool forcePressure) {
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
			if (iter->second.get() == exceptThisOne) {
				++iter;
				continue;
			}
			bool hasClut = (iter->second->status & TexCacheEntry::STATUS_CLUT_VARIANTS) != 0;
			int killAge = hasClut ? TEXTURE_KILL_AGE_CLUT : killAgeBase;
			if (iter->second->lastFrame + killAge < gpuStats.numFlips) {
				DeleteTexture(iter++);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(Log::G3D, "Decimated texture cache, saved %d estimated bytes - now %d bytes", had - cacheSizeEstimate_, cacheSizeEstimate_);
	}

	// If enabled, we also need to clear the secondary cache.
	if (PSP_CoreParameter().compat.flags().SecondaryTextureCache && (forcePressure || secondCacheSizeEstimate_ >= TEXCACHE_SECOND_MIN_PRESSURE)) {
		const u32 had = secondCacheSizeEstimate_;

		for (TexCache::iterator iter = secondCache_.begin(); iter != secondCache_.end(); ) {
			if (iter->second.get() == exceptThisOne) {
				++iter;
				continue;
			}
			// In low memory mode, we kill them all since secondary cache is disabled.
			if (lowMemoryMode_ || iter->second->lastFrame + TEXTURE_SECOND_KILL_AGE < gpuStats.numFlips) {
				ReleaseTexture(iter->second.get(), true);
				secondCacheSizeEstimate_ -= EstimateTexMemoryUsage(iter->second.get());
				iter = secondCache_.erase(iter);
			} else {
				++iter;
			}
		}

		VERBOSE_LOG(Log::G3D, "Decimated second texture cache, saved %d estimated bytes - now %d bytes", had - secondCacheSizeEstimate_, secondCacheSizeEstimate_);
	}

	DecimateVideos();
	replacer_.Decimate(forcePressure ? ReplacerDecimateMode::FORCE_PRESSURE : ReplacerDecimateMode::NEW_FRAME);
}

void TextureCacheCommon::DecimateVideos() {
	for (auto iter = videos_.begin(); iter != videos_.end(); ) {
		if (iter->flips + VIDEO_DECIMATE_AGE < gpuStats.numFlips) {
			iter = videos_.erase(iter);
		} else {
			++iter;
		}
	}
}

bool TextureCacheCommon::IsVideo(u32 texaddr) const {
	texaddr &= 0x3FFFFFFF;
	for (auto &info : videos_) {
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
	DEBUG_LOG(Log::G3D, "Texture different or overwritten, reloading at %08x: %s", entry->addr, reason);
	if (doDelete) {
		ForgetLastTexture();
		ReleaseTexture(entry, true);
		entry->status &= ~(TexCacheEntry::STATUS_IS_SCALED_OR_REPLACED | TexCacheEntry::STATUS_TO_REPLACE);
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
	const u32 fb_addr = framebuffer->fb_address;
	const u32 z_addr = framebuffer->z_address;

	const u32 fb_bpp = BufferFormatBytesPerPixel(framebuffer->fb_format);
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

bool TextureCacheCommon::MatchFramebuffer(
	const TextureDefinition &entry,
	VirtualFramebuffer *framebuffer, u32 texaddrOffset, RasterChannel channel, FramebufferMatchInfo *matchInfo) const {
	static const u32 MAX_SUBAREA_Y_OFFSET_SAFE = 32;

	uint32_t fb_address = channel == RASTER_DEPTH ? framebuffer->z_address : framebuffer->fb_address;
	uint32_t fb_stride = channel == RASTER_DEPTH ? framebuffer->z_stride : framebuffer->fb_stride;
	GEBufferFormat fb_format = channel == RASTER_DEPTH ? GE_FORMAT_DEPTH16 : framebuffer->fb_format;

	if (channel == RASTER_DEPTH && (framebuffer->z_address == framebuffer->fb_address || framebuffer->z_address == 0)) {
		// Try to avoid silly matches to somewhat malformed buffers.
		return false;
	}

	if (!fb_stride) {
		// Hard to make decisions.
		return false;
	}

	switch (entry.format) {
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
		return false;
	default: break;
	}

	uint32_t fb_stride_in_bytes = fb_stride * BufferFormatBytesPerPixel(fb_format);
	uint32_t tex_stride_in_bytes = entry.bufw * textureBitsPerPixel[entry.format] / 8;  // Note, we're looking up bits here so need to divide by 8.

	u32 addr = fb_address;
	u32 texaddr = entry.addr + texaddrOffset;

	bool texInVRAM = Memory::IsVRAMAddress(texaddr);
	bool fbInVRAM = Memory::IsVRAMAddress(fb_address);

	if (texInVRAM != fbInVRAM) {
		// Shortcut. Cannot possibly be a match.
		return false;
	}

	if (texInVRAM) {
		const u32 mirrorMask = 0x041FFFFF;

		addr &= mirrorMask;
		texaddr &= mirrorMask;
	}

	const bool noOffset = texaddr == addr;
	const bool exactMatch = noOffset && entry.format < 4 && channel == RASTER_COLOR && fb_stride_in_bytes == tex_stride_in_bytes;

	const u32 texWidth = 1 << ((entry.dim >> 0) & 0xf);
	const u32 texHeight = 1 << ((entry.dim >> 8) & 0xf);

	// 512 on a 272 framebuffer is sane, so let's be lenient.
	const u32 minSubareaHeight = texHeight / 4;

	// If they match "exactly", it's non-CLUT and from the top left.
	if (exactMatch) {
		// NOTE: This check is okay because the first texture formats are the same as the buffer formats.
		if (IsTextureFormatBufferCompatible(entry.format)) {
			if (TextureFormatMatchesBufferFormat(entry.format, fb_format) || (framebuffer->usageFlags & FB_USAGE_BLUE_TO_ALPHA)) {
				return true;
			} else {
				WARN_LOG_ONCE(diffFormat1, Log::G3D, "Found matching framebuffer with reinterpretable fb_format: %s != %s at %08x", GeTextureFormatToString(entry.format), GeBufferFormatToString(fb_format), fb_address);
				*matchInfo = FramebufferMatchInfo{ 0, 0, true, TextureFormatToBufferFormat(entry.format) };
				return true;
			}
		} else {
			// Format incompatible, ignoring without comment. (maybe some really gnarly hacks will end up here...)
			return false;
		}
	} else {
		// Apply to buffered mode only.
		if (!framebufferManager_->UseBufferedRendering()) {
			return false;
		}

		// Check works for D16 too.
		// These are combinations that we have special-cased handling for. There are more
		// ones possible, but rare - we'll add them as we find them used.
		const bool matchingClutFormat =
			(fb_format == GE_FORMAT_DEPTH16 && entry.format == GE_TFMT_CLUT16) ||
			(fb_format == GE_FORMAT_DEPTH16 && entry.format == GE_TFMT_5650) ||
			(fb_format == GE_FORMAT_8888 && entry.format == GE_TFMT_CLUT32) ||
			(fb_format != GE_FORMAT_8888 && entry.format == GE_TFMT_CLUT16) ||
			(fb_format == GE_FORMAT_8888 && entry.format == GE_TFMT_CLUT8) ||
			(fb_format == GE_FORMAT_5551 && entry.format == GE_TFMT_CLUT8 && PSP_CoreParameter().compat.flags().SOCOMClut8Replacement);

		const int texBitsPerPixel = TextureFormatBitsPerPixel(entry.format);
		const int byteOffset = texaddr - addr;
		if (byteOffset > 0) {
			int texbpp = texBitsPerPixel;
			if (fb_format == GE_FORMAT_5551 && entry.format == GE_TFMT_CLUT8) {
				// In this case we treat CLUT8 as if it were CLUT16, see issue #16210. So we need
				// to compute the x offset appropriately.
				texbpp = 16;
			}

			matchInfo->yOffset = byteOffset / fb_stride_in_bytes;
			matchInfo->xOffset = 8 * (byteOffset % fb_stride_in_bytes) / texbpp;
		} else if (byteOffset < 0) {
			int texelOffset = 8 * byteOffset / texBitsPerPixel;
			// We don't support negative Y offsets, and negative X offsets are only for the Killzone workaround.
			if (texelOffset < -(int)entry.bufw || !PSP_CoreParameter().compat.flags().SplitFramebufferMargin) {
				return false;
			}
			matchInfo->xOffset = entry.bufw == 0 ? 0 : -(-texelOffset % (int)entry.bufw);
		}

		if (matchInfo->yOffset > 0 && matchInfo->yOffset + minSubareaHeight >= framebuffer->height) {
			// Can't be inside the framebuffer.
			return false;
		}

		// Check if it's in bufferWidth (which might be higher than width and may indicate the framebuffer includes the data.)
		// Do the computation in bytes so that it's valid even in case of weird reinterpret scenarios.
		const int xOffsetInBytes = matchInfo->xOffset * 8 / texBitsPerPixel;
		const int texWidthInBytes = texWidth * 8 / texBitsPerPixel;
		if (xOffsetInBytes >= framebuffer->BufferWidthInBytes() && xOffsetInBytes + texWidthInBytes <= (int)fb_stride_in_bytes) {
			// This happens in Brave Story, see #10045 - the texture is in the space between strides, with matching stride.
			return false;
		}

		// Trying to play it safe.  Below 0x04110000 is almost always framebuffers.
		// TODO: Maybe we can reduce this check and find a better way above 0x04110000?
		if (matchInfo->yOffset > MAX_SUBAREA_Y_OFFSET_SAFE && addr > 0x04110000 && !PSP_CoreParameter().compat.flags().AllowLargeFBTextureOffsets) {
			WARN_LOG_ONCE(subareaIgnored, Log::G3D, "Ignoring possible texturing from framebuffer at %08x +%dx%d / %dx%d", fb_address, matchInfo->xOffset, matchInfo->yOffset, framebuffer->width, framebuffer->height);
			return false;
		}

		// Note the check for texHeight - we really don't care about a stride mismatch if texHeight == 1.
		// This also takes care of the 4x1 texture check we used to have here for Burnout Dominator.
		if (fb_stride_in_bytes != tex_stride_in_bytes && texHeight > 1) {
			// Probably irrelevant.
			return false;
		}

		// Check for CLUT. The framebuffer is always RGB, but it can be interpreted as a CLUT texture.
		// 3rd Birthday (and a bunch of other games) render to a 16 bit clut texture.
		if (matchingClutFormat) {
			if (!noOffset) {
				WARN_LOG_ONCE(subareaClut, Log::G3D, "Matching framebuffer (%s) using %s with offset at %08x +%dx%d", RasterChannelToString(channel), GeTextureFormatToString(entry.format), fb_address, matchInfo->xOffset, matchInfo->yOffset);
			}
			return true;
		} else if (IsClutFormat((GETextureFormat)(entry.format)) || IsDXTFormat((GETextureFormat)(entry.format))) {
			WARN_LOG_ONCE(fourEightBit, Log::G3D, "%s texture format not matching framebuffer of format %s at %08x/%d", GeTextureFormatToString(entry.format), GeBufferFormatToString(fb_format), fb_address, fb_stride);
			return false;
		}

		// This is either normal or we failed to generate a shader to depalettize
		if ((int)fb_format == (int)entry.format || matchingClutFormat) {
			if ((int)fb_format  != (int)entry.format) {
				WARN_LOG_ONCE(diffFormat2, Log::G3D, "Matching framebuffer with different formats %s != %s at %08x",
					GeTextureFormatToString(entry.format), GeBufferFormatToString(fb_format), fb_address);
				return true;
			} else {
				WARN_LOG_ONCE(subarea, Log::G3D, "Matching from framebuffer at %08x +%dx%d", fb_address, matchInfo->xOffset, matchInfo->yOffset);
				return true;
			}
		} else {
			WARN_LOG_ONCE(diffFormat2, Log::G3D, "Ignoring possible texturing from framebuffer at %08x with incompatible format %s != %s (+%dx%d)",
				fb_address, GeTextureFormatToString(entry.format), GeBufferFormatToString(fb_format), matchInfo->xOffset, matchInfo->yOffset);
			return false;
		}
	}
}

void TextureCacheCommon::SetTextureFramebuffer(const AttachCandidate &candidate) {
	VirtualFramebuffer *framebuffer = candidate.fb;
	RasterChannel channel = candidate.channel;

	if (candidate.match.reinterpret) {
		framebuffer = framebufferManager_->ResolveFramebufferColorToFormat(candidate.fb, candidate.match.reinterpretTo);
	}

	_dbg_assert_msg_(framebuffer != nullptr, "Framebuffer must not be null.");

	framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	// Keep the framebuffer alive.
	framebuffer->last_frame_used = gpuStats.numFlips;

	nextFramebufferTextureChannel_ = RASTER_COLOR;

	if (framebufferManager_->UseBufferedRendering()) {
		FramebufferMatchInfo fbInfo = candidate.match;
		// Detect when we need to apply the horizontal texture swizzle.
		u64 depthUpperBits = (channel == RASTER_DEPTH && framebuffer->fb_format == GE_FORMAT_8888) ? ((gstate.getTextureAddress(0) & 0x600000) >> 20) : 0;
		bool needsDepthXSwizzle = depthUpperBits == 2;

		// We need to force it, since we may have set it on a texture before attaching.
		int texWidth = framebuffer->bufferWidth;
		int texHeight = framebuffer->bufferHeight;
		if (candidate.channel == RASTER_COLOR && gstate.getTextureFormat() == GE_TFMT_CLUT8 && framebuffer->fb_format == GE_FORMAT_5551 && PSP_CoreParameter().compat.flags().SOCOMClut8Replacement) {
			// See #16210. UV must be adjusted as if the texture was twice the width.
			texWidth *= 2.0f;
		}

		if (needsDepthXSwizzle) {
			texWidth = RoundUpToPowerOf2(texWidth);
		}

		gstate_c.curTextureWidth = texWidth;
		gstate_c.curTextureHeight = texHeight;
		gstate_c.SetTextureIsFramebuffer(true);
		gstate_c.SetTextureIsBGRA(false);

		if ((gstate_c.curTextureXOffset == 0) != (fbInfo.xOffset == 0) || (gstate_c.curTextureYOffset == 0) != (fbInfo.yOffset == 0)) {
			gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}

		gstate_c.curTextureXOffset = fbInfo.xOffset;
		gstate_c.curTextureYOffset = fbInfo.yOffset;
		u32 texW = (u32)gstate.getTextureWidth(0);
		u32 texH = (u32)gstate.getTextureHeight(0);
		gstate_c.SetNeedShaderTexclamp(gstate_c.curTextureWidth != texW || gstate_c.curTextureHeight != texH);
		if (gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0) {
			gstate_c.SetNeedShaderTexclamp(true);
		}
		if (channel == RASTER_DEPTH) {
			framebuffer->usageFlags |= FB_USAGE_COLOR_MIXED_DEPTH;
		}

		if (channel == RASTER_DEPTH && !gstate_c.Use(GPU_USE_DEPTH_TEXTURE)) {
			WARN_LOG_ONCE(ndepthtex, Log::G3D, "Depth textures not supported, not binding");
			// Flag to bind a null texture if we can't support depth textures.
			// Should only happen on old OpenGL.
			nextFramebufferTexture_ = nullptr;
			failedTexture_ = true;
		} else {
			nextFramebufferTexture_ = framebuffer;
			nextFramebufferTextureChannel_ = channel;
		}
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

	gstate_c.SetTextureIsVideo(false);
	gstate_c.SetTextureIs3D(false);
	gstate_c.SetTextureIsArray(true);

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

	AttachCandidate bestCandidate;
	if (GetBestFramebufferCandidate(def, texaddrOffset, &bestCandidate)) {
		SetTextureFramebuffer(bestCandidate);
		return true;
	} else {
		return false;
	}
}

bool TextureCacheCommon::GetCurrentFramebufferTextureDebug(GPUDebugBuffer &buffer, bool *isFramebuffer) {
	if (!nextFramebufferTexture_)
		return false;
	*isFramebuffer = true;

	VirtualFramebuffer *vfb = nextFramebufferTexture_;
	u8 sf = vfb->renderScaleFactor;
	int x = gstate_c.curTextureXOffset * sf;
	int y = gstate_c.curTextureYOffset * sf;
	int desiredW = gstate.getTextureWidth(0) * sf;
	int desiredH = gstate.getTextureHeight(0) * sf;
	int w = std::min(desiredW, vfb->bufferWidth * sf - x);
	int h = std::min(desiredH, vfb->bufferHeight * sf - y);

	bool retval;
	if (nextFramebufferTextureChannel_ == RASTER_DEPTH) {
		buffer.Allocate(desiredW, desiredH, GPU_DBG_FORMAT_FLOAT, false);
		if (w < desiredW || h < desiredH)
			buffer.ZeroBytes();
		retval = draw_->CopyFramebufferToMemory(vfb->fbo, Draw::Aspect::DEPTH_BIT, x, y, w, h, Draw::DataFormat::D32F, buffer.GetData(), desiredW, Draw::ReadbackMode::BLOCK, "GetCurrentTextureDebug");
	} else {
		buffer.Allocate(desiredW, desiredH, GPU_DBG_FORMAT_8888, false);
		if (w < desiredW || h < desiredH)
			buffer.ZeroBytes();
		retval = draw_->CopyFramebufferToMemory(vfb->fbo, Draw::Aspect::COLOR_BIT, x, y, w, h, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), desiredW, Draw::ReadbackMode::BLOCK, "GetCurrentTextureDebug");
	}

	// Vulkan requires us to re-apply all dynamic state for each command buffer, and the above will cause us to start a new cmdbuf.
	// So let's dirty the things that are involved in Vulkan dynamic state. Readbacks are not frequent so this won't hurt other backends.
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
	// We may have blitted to a temp FBO.
	framebufferManager_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");
	if (!retval)
		ERROR_LOG(Log::G3D, "Failed to get debug texture: copy to memory failed");
	return retval;
}

void TextureCacheCommon::NotifyConfigChanged() {
	int scaleFactor = g_Config.iTexScalingLevel;

	if (!gstate_c.Use(GPU_USE_TEXTURE_NPOT)) {
		// Reduce the scale factor to a power of two (e.g. 2 or 4) if textures must be a power of two.
		// TODO: In addition we should probably remove these options from the UI in this case.
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

void TextureCacheCommon::NotifyWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat fmt) {
	addr &= 0x3FFFFFFF;
	videos_.push_back({ addr, (u32)size, gpuStats.numFlips });
}

void TextureCacheCommon::LoadClut(u32 clutAddr, u32 loadBytes, GPURecord::Recorder *recorder) {
	if (loadBytes == 0) {
		// Don't accidentally overwrite clutTotalBytes_ with a zero.
		return;
	}

	_assert_(loadBytes <= 2048);
	clutTotalBytes_ = loadBytes;
	clutRenderAddress_ = 0xFFFFFFFF;

	if (!Memory::IsValidAddress(clutAddr)) {
		memset(clutBufRaw_, 0x00, loadBytes);
		// Reload the clut next time (should we really do it in this case?)
		clutLastFormat_ = 0xFFFFFFFF;
		clutMaxBytes_ = std::max(clutMaxBytes_, loadBytes);
		return;
	}

	if (Memory::IsVRAMAddress(clutAddr)) {
		// Clear the uncached and mirror bits, etc. to match framebuffers.
		const u32 clutLoadAddr = clutAddr & 0x041FFFFF;
		const u32 clutLoadEnd = clutLoadAddr + loadBytes;
		static const u32 MAX_CLUT_OFFSET = 4096;

		clutRenderOffset_ = MAX_CLUT_OFFSET;
		const std::vector<VirtualFramebuffer *> &framebuffers = framebufferManager_->Framebuffers();

		u32 bestClutAddress = 0xFFFFFFFF;

		VirtualFramebuffer *chosenFramebuffer = nullptr;
		for (VirtualFramebuffer *framebuffer : framebuffers) {
			// Let's not deal with divide by zero.
			if (framebuffer->fb_stride == 0)
				continue;

			const u32 fb_address = framebuffer->fb_address;
			const u32 fb_bpp = BufferFormatBytesPerPixel(framebuffer->fb_format);
			int offset = clutLoadAddr - fb_address;

			// Is this inside the framebuffer at all? Note that we only check the first line here, this should
			// be changed.
			bool matchRange = offset >= 0 && offset < (int)(framebuffer->fb_stride * fb_bpp);
			if (matchRange) {
				// And is it inside the rendered area?  Sometimes games pack data in the margin between width and stride.
				// If the framebuffer width was detected as 512, we're gonna assume it's really 480.
				int fbMatchWidth = framebuffer->width;
				if (fbMatchWidth == 512) {
					fbMatchWidth = 480;
				}
				int pixelOffsetX = ((offset / fb_bpp) % framebuffer->fb_stride);
				bool inMargin = pixelOffsetX >= fbMatchWidth && (pixelOffsetX + (loadBytes / fb_bpp) <= framebuffer->fb_stride);

				// The offset check here means, in the context of the loop, that we'll pick
				// the framebuffer with the smallest offset. This is yet another framebuffer matching
				// loop with its own rules, eventually we'll probably want to do something
				// more systematic.
				bool okAge = !PSP_CoreParameter().compat.flags().LoadCLUTFromCurrentFrameOnly || framebuffer->last_frame_render == gpuStats.numFlips;  // Here we can try heuristics.
				if (matchRange && !inMargin && offset < (int)clutRenderOffset_) {
					if (okAge) {
						WARN_LOG_N_TIMES(clutfb, 5, Log::G3D, "Detected LoadCLUT(%d bytes) from framebuffer %08x (%s), last render %d frames ago, byte offset %d, pixel offset %d",
							loadBytes, fb_address, GeBufferFormatToString(framebuffer->fb_format), gpuStats.numFlips - framebuffer->last_frame_render, offset, offset / fb_bpp);
						framebuffer->last_frame_clut = gpuStats.numFlips;
						// Also mark used so it's not decimated.
						framebuffer->last_frame_used = gpuStats.numFlips;
						framebuffer->usageFlags |= FB_USAGE_CLUT;
						bestClutAddress = framebuffer->fb_address;
						clutRenderOffset_ = (u32)offset;
						chosenFramebuffer = framebuffer;
						if (offset == 0) {
							// Not gonna find a better match according to the smallest-offset rule, so we'll go with this one.
							break;
						}
					} else {
						WARN_LOG(Log::G3D, "Ignoring CLUT load from %d frames old buffer at %08x", gpuStats.numFlips - framebuffer->last_frame_render, fb_address);
					}
				}
			}
		}

		// To turn off dynamic CLUT (for demonstration or testing purposes), add "false &&" to this check.
		if (chosenFramebuffer && chosenFramebuffer->fbo) {
			clutRenderAddress_ = bestClutAddress;

			if (!dynamicClutTemp_) {
				Draw::FramebufferDesc desc{};
				desc.width = 512;
				desc.height = 1;
				desc.depth = 1;
				desc.z_stencil = false;
				desc.numLayers = 1;
				desc.multiSampleLevel = 0;
				desc.tag = "dynamic_clut";
				dynamicClutFbo_ = draw_->CreateFramebuffer(desc);
				desc.tag = "dynamic_clut_temp";
				dynamicClutTemp_ = draw_->CreateFramebuffer(desc);
			}

			// We'll need to copy from the offset.
			const u32 fb_bpp = BufferFormatBytesPerPixel(chosenFramebuffer->fb_format);
			const int totalPixelsOffset = clutRenderOffset_ / fb_bpp;
			const int clutYOffset = totalPixelsOffset / chosenFramebuffer->fb_stride;
			const int clutXOffset = totalPixelsOffset % chosenFramebuffer->fb_stride;
			const int scale = chosenFramebuffer->renderScaleFactor;

			// Copy the pixels to our temp clut, scaling down if needed and wrapping.
			framebufferManager_->BlitUsingRaster(
				chosenFramebuffer->fbo, clutXOffset * scale, clutYOffset * scale, (clutXOffset + 512.0f) * scale, (clutYOffset + 1.0f) * scale,
				dynamicClutTemp_, 0.0f, 0.0f, 512.0f, 1.0f, 
				false, scale, framebufferManager_->Get2DPipeline(DRAW2D_COPY_COLOR_RECT2LIN), "copy_clut_to_temp");

			framebufferManager_->RebindFramebuffer("after_copy_clut_to_temp");
			clutRenderFormat_ = chosenFramebuffer->fb_format;
		}
		NotifyMemInfo(MemBlockFlags::ALLOC, clutAddr, loadBytes, "CLUT");
	}

	// It's possible for a game to load CLUT outside valid memory without crashing, should result in zeroes.
	u32 bytes = Memory::ValidSize(clutAddr, loadBytes);
	_assert_(bytes <= 2048);
	bool performDownload = PSP_CoreParameter().compat.flags().AllowDownloadCLUT;
	if (recorder->IsActive())
		performDownload = true;
	if (clutRenderAddress_ != 0xFFFFFFFF && performDownload) {
		framebufferManager_->DownloadFramebufferForClut(clutRenderAddress_, clutRenderOffset_ + bytes);
		Memory::MemcpyUnchecked(clutBufRaw_, clutAddr, bytes);
		if (bytes < loadBytes) {
			memset((u8 *)clutBufRaw_ + bytes, 0x00, loadBytes - bytes);
		}
	} else {
		// Here we could check for clutRenderAddress_ != 0xFFFFFFFF and zero the CLUT or something,
		// but choosing not to for now. Though the results of loading the CLUT from RAM here is
		// almost certainly going to be bogus.
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

ReplacedTexture *TextureCacheCommon::FindReplacement(TexCacheEntry *entry, int *w, int *h, int *d) {
	if (*d != 1) {
		// We don't yet support replacing 3D textures.
		return nullptr;
	}

	// Short circuit the non-enabled case.
	// Otherwise, due to bReplaceTexturesAllowLate, we'll still spawn tasks looking for replacements
	// that then won't be used.
	if (!replacer_.ReplaceEnabled()) {
		return nullptr;
	}

	if ((entry->status & TexCacheEntry::STATUS_VIDEO) && !replacer_.AllowVideo()) {
		return nullptr;
	}

	double replaceStart = time_now_d();
	u64 cachekey = entry->CacheKey();
	ReplacedTexture *replaced = replacer_.FindReplacement(cachekey, entry->fullhash, *w, *h);
	replacementTimeThisFrame_ += time_now_d() - replaceStart;
	if (!replaced) {
		// TODO: Remove the flag here?
		// entry->status &= ~TexCacheEntry::STATUS_TO_REPLACE;
		return nullptr;
	}
	entry->replacedTexture = replaced;  // we know it's non-null here.
	PollReplacement(entry, w, h, d);
	return replaced;
}

void TextureCacheCommon::PollReplacement(TexCacheEntry *entry, int *w, int *h, int *d) {
	// Allow some delay to reduce pop-in.
	constexpr double MAX_BUDGET_PER_TEX = 0.25 / 60.0;

	double budget = std::min(MAX_BUDGET_PER_TEX, replacementFrameBudget_ - replacementTimeThisFrame_);

	double replaceStart = time_now_d();
	if (entry->replacedTexture->Poll(budget)) {
		if (entry->replacedTexture->State() == ReplacementState::ACTIVE) {
			entry->replacedTexture->GetSize(0, w, h);
			// Consider it already "scaled.".
			entry->status |= TexCacheEntry::STATUS_IS_SCALED_OR_REPLACED;
		}

		// Remove the flag, even if it was invalid.
		entry->status &= ~TexCacheEntry::STATUS_TO_REPLACE;
	}
	replacementTimeThisFrame_ += time_now_d() - replaceStart;

	switch (entry->replacedTexture->State()) {
	case ReplacementState::UNLOADED:
	case ReplacementState::PENDING:
		// Make sure we keep polling.
		entry->status |= TexCacheEntry::STATUS_TO_REPLACE;
		break;
	default:
		break;
	}
}

// This is only used in the GLES backend, where we don't point these to video memory.
// So we shouldn't add a check for dstBuf != srcBuf, as long as the functions we call can handle that.
static void ReverseColors(void *dstBuf, const void *srcBuf, GETextureFormat fmt, int numPixels) {
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
		// No need to convert RGBA8888, right order already
		if (dstBuf != srcBuf) {
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
	int w, int h, int bufw, bool reverseColors) {

	int minw = std::min(bufw, w);
	uint32_t *dst = (uint32_t *)out;
	int outPitch32 = outPitch / sizeof(uint32_t);
	const DXTBlock *src = (const DXTBlock *)texptr;

	if (!Memory::IsValidRange(texaddr, (h / 4) * (bufw / 4) * sizeof(DXTBlock))) {
		ERROR_LOG_REPORT(Log::G3D, "DXT%d texture extends beyond valid RAM: %08x + %d x %d", n, texaddr, bufw, h);
		uint32_t limited = Memory::ValidSize(texaddr, (h / 4) * (bufw / 4) * sizeof(DXTBlock));
		// This might possibly be 0, but try to decode what we can (might even be how the PSP behaves.)
		h = (((int)limited / sizeof(DXTBlock)) / (bufw / 4)) * 4;
	}

	u32 alphaSum = 1;
	for (int y = 0; y < h; y += 4) {
		u32 blockIndex = (y / 4) * (bufw / 4);
		int blockHeight = std::min(h - y, 4);
		for (int x = 0; x < minw; x += 4) {
			int blockWidth = std::min(minw - x, 4);
			if constexpr (n == 1)
				DecodeDXT1Block(dst + outPitch32 * y + x, (const DXT1Block *)src + blockIndex, outPitch32, blockWidth, blockHeight, &alphaSum);
			else if constexpr (n == 3)
				DecodeDXT3Block(dst + outPitch32 * y + x, (const DXT3Block *)src + blockIndex, outPitch32, blockWidth, blockHeight);
			else if constexpr (n == 5)
				DecodeDXT5Block(dst + outPitch32 * y + x, (const DXT5Block *)src + blockIndex, outPitch32, blockWidth, blockHeight);
			blockIndex++;
		}
	}

	if (reverseColors) {
		ReverseColors(out, out, GE_TFMT_8888, outPitch32 * h);
	}

	if constexpr (n == 1) {
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

// Used for converting CLUT4 to CLUT8.
// Could SIMD or whatever, though will hardly be a bottleneck.
static void Expand4To8Bits(u8 *dest, const u8 *src, int srcWidth) {
	for (int i = 0; i < (srcWidth + 1) / 2; i++) {
		u8 lower = src[i] & 0xF;
		u8 upper = src[i] >> 4;
		dest[i * 2] = lower;
		dest[i * 2 + 1] = upper;
	}
}

CheckAlphaResult TextureCacheCommon::DecodeTextureLevel(u8 *out, int outPitch, GETextureFormat format, GEPaletteFormat clutformat, uint32_t texaddr, int level, int bufw, TexDecodeFlags flags) {
	u32 alphaSum = 0xFFFFFFFF;
	u32 fullAlphaMask = 0x0;

	bool expandTo32bit = (flags & TexDecodeFlags::EXPAND32) != 0;
	bool reverseColors = (flags & TexDecodeFlags::REVERSE_COLORS) != 0;
	bool toClut8 = (flags & TexDecodeFlags::TO_CLUT8) != 0;

	if (toClut8 && format != GE_TFMT_CLUT8 && format != GE_TFMT_CLUT4) {
		_dbg_assert_(false);
	}

	bool swizzled = gstate.isTextureSwizzled();
	if ((texaddr & 0x00600000) != 0 && Memory::IsVRAMAddress(texaddr)) {
		// This means it's in a mirror, possibly a swizzled mirror.  Let's report.
		WARN_LOG_REPORT_ONCE(texmirror, Log::G3D, "Decoding texture from VRAM mirror at %08x swizzle=%d", texaddr, swizzled ? 1 : 0);
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

		if (toClut8) {
			// We just need to expand from 4 to 8 bits.
			for (int y = 0; y < h; ++y) {
				Expand4To8Bits((u8 *)out + outPitch * y, texptr + (bufw * y) / 2, w);
			}
			// We can't know anything about alpha.
			return CHECKALPHA_ANY;
		}

		switch (clutformat) {
		case GE_CMODE_16BIT_BGR5650:
		case GE_CMODE_16BIT_ABGR5551:
		case GE_CMODE_16BIT_ABGR4444:
		{
			// The w > 1 check is to not need a case that handles a single pixel
			// in DeIndexTexture4Optimal<u16>.
			if (clutAlphaLinear_ && mipmapShareClut && !expandTo32bit && w >= 4) {
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
				// Need to have the "un-reversed" (raw) CLUT here since we are using a generic conversion function.
				if (expandTo32bit) {
					// We simply expand the CLUT to 32-bit, then we deindex as usual. Probably the fastest way.
					const u16 *clut = GetCurrentRawClut<u16>() + clutSharingOffset;
					const int clutStart = gstate.getClutIndexStartPos();
					if (gstate.getClutIndexShift() == 0 || gstate.getClutIndexMask() <= 16) {
						ConvertFormatToRGBA8888(clutformat, expandClut_ + clutStart, clut + clutStart, 16);
					} else {
						// To be safe for shifts and wrap around, convert the entire CLUT.
						ConvertFormatToRGBA8888(clutformat, expandClut_, clut, 512);
					}
					fullAlphaMask = 0xFF000000;
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4<u32>((u32 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, expandClut_, &alphaSum);
					}
				} else {
					// If we're reversing colors, the CLUT was already reversed, no special handling needed.
					const u16 *clut = GetCurrentClut<u16>() + clutSharingOffset;
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
			ERROR_LOG_REPORT(Log::G3D, "Unknown CLUT4 texture mode %d", gstate.getClutPaletteFormat());
			return CHECKALPHA_ANY;
		}
	}
	break;

	case GE_TFMT_CLUT8:
		if (toClut8) {
			if (gstate.isTextureSwizzled()) {
				tmpTexBuf32_.resize(bufw * ((h + 7) & ~7));
				UnswizzleFromMem(tmpTexBuf32_.data(), bufw, texptr, bufw, h, 1);
				texptr = (u8 *)tmpTexBuf32_.data();
			}
			// After deswizzling, we are in the correct format and can just copy.
			for (int y = 0; y < h; ++y) {
				memcpy((u8 *)out + outPitch * y, texptr + (bufw * y), w);
			}
			// We can't know anything about alpha.
			return CHECKALPHA_ANY;
		}
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
			if (expandTo32bit) {
				// This is OK even if reverseColors is on, because it expands to the 8888 format which is the same in reverse mode.
				for (int y = 0; y < h; ++y) {
					CheckMask16((const u16 *)(texptr + bufw * sizeof(u16) * y), w, &alphaSum);
					ConvertFormatToRGBA8888(format, (u32 *)(out + outPitch * y), (const u16 *)texptr + bufw * y, w);
				}
			} else if (reverseColors) {
				// Just check the input's alpha to reuse code. TODO: make a specialized ReverseColors that checks as we go.
				for (int y = 0; y < h; ++y) {
					CheckMask16((const u16 *)(texptr + bufw * sizeof(u16) * y), w, &alphaSum);
					ReverseColors(out + outPitch * y, texptr + bufw * sizeof(u16) * y, format, w);
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
			if (expandTo32bit) {
				// This is OK even if reverseColors is on, because it expands to the 8888 format which is the same in reverse mode.
				// Just check the swizzled input's alpha to reuse code. TODO: make a specialized ConvertFormatToRGBA8888 that checks as we go.
				for (int y = 0; y < h; ++y) {
					CheckMask16((const u16 *)(unswizzled + bufw * sizeof(u16) * y), w, &alphaSum);
					ConvertFormatToRGBA8888(format, (u32 *)(out + outPitch * y), (const u16 *)unswizzled + bufw * y, w);
				}
			} else if (reverseColors) {
				// Just check the swizzled input's alpha to reuse code. TODO: make a specialized ReverseColors that checks as we go.
				for (int y = 0; y < h; ++y) {
					CheckMask16((const u16 *)(unswizzled + bufw * sizeof(u16) * y), w, &alphaSum);
					ReverseColors(out + outPitch * y, unswizzled + bufw * sizeof(u16) * y, format, w);
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
					ReverseColors(out + outPitch * y, texptr + bufw * sizeof(u32) * y, format, w);
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
					ReverseColors(out + outPitch * y, unswizzled + bufw * sizeof(u32) * y, format, w);
				}
			} else {
				for (int y = 0; y < h; ++y) {
					CopyAndSumMask32((u32 *)(out + outPitch * y), (const u32 *)(unswizzled + bufw * sizeof(u32) * y), w, &alphaSum);
				}
			}
		}
		break;

	case GE_TFMT_DXT1:
		return DecodeDXTBlocks<DXT1Block, 1>(out, outPitch, texaddr, texptr, w, h, bufw, reverseColors);

	case GE_TFMT_DXT3:
		return DecodeDXTBlocks<DXT3Block, 3>(out, outPitch, texaddr, texptr, w, h, bufw, reverseColors);

	case GE_TFMT_DXT5:
		return DecodeDXTBlocks<DXT5Block, 5>(out, outPitch, texaddr, texptr, w, h, bufw, reverseColors);

	default:
		ERROR_LOG_REPORT(Log::G3D, "Unknown Texture Format %d!!!", format);
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

	// Misshitsu no Sacrifice has separate CLUT data, this is a hack to allow it.
	// Normally separate CLUTs are not allowed for 8-bit or higher indices.
	const bool mipmapShareClut = gstate.isClutSharedForMipmaps() || gstate.getClutLoadBlocks() != 0x40;
	const int clutSharingOffset = mipmapShareClut ? 0 : (level & 1) * 256;

	GEPaletteFormat palFormat = (GEPaletteFormat)gstate.getClutPaletteFormat();

	const u16 *clut16 = (const u16 *)clutBuf_ + clutSharingOffset;
	const u32 *clut32 = (const u32 *)clutBuf_ + clutSharingOffset;

	if (expandTo32Bit && palFormat != GE_CMODE_32BIT_ABGR8888) {
		const u16 *clut16raw = (const u16 *)clutBufRaw_ + clutSharingOffset;
		// It's possible to access the latter half of the CLUT using the start pos.
		const int clutStart = gstate.getClutIndexStartPos();
		if (clutStart > 256) {
			// Access wraps around when start + index goes over.
			ConvertFormatToRGBA8888(GEPaletteFormat(palFormat), expandClut_, clut16raw, 512);
		} else {
			ConvertFormatToRGBA8888(GEPaletteFormat(palFormat), expandClut_ + clutStart, clut16raw + clutStart, 256);
		}
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
		ERROR_LOG_REPORT(Log::G3D, "Unhandled clut texture mode %d!!!", gstate.getClutPaletteFormat());
		break;
	}

	if (palFormat == GE_CMODE_16BIT_BGR5650) {
		return CHECKALPHA_FULL;
	} else {
		return AlphaSumIsFull(alphaSum, fullAlphaMask) ? CHECKALPHA_FULL : CHECKALPHA_ANY;
	}
}

void TextureCacheCommon::ApplyTexture(bool doBind) {
	TexCacheEntry *entry = nextTexture_;
	if (!entry) {
		// Maybe we bound a framebuffer?
		ForgetLastTexture();
		if (failedTexture_) {
			// Backends should handle this by binding a black texture with 0 alpha.
			BindTexture(nullptr);
		} else if (nextFramebufferTexture_) {
			// ApplyTextureFrameBuffer is responsible for setting SetTextureFullAlpha.
			ApplyTextureFramebuffer(nextFramebufferTexture_, gstate.getTextureFormat(), nextFramebufferTextureChannel_);
			nextFramebufferTexture_ = nullptr;
		}

		// We don't set the 3D texture state here or anything else, on some backends (?)
		// a nextTexture_ of nullptr means keep the current texture.
		return;
	}

	nextTexture_ = nullptr;

	UpdateMaxSeenV(entry, gstate.isModeThrough());

	if (nextNeedsRebuild_) {
		// Regardless of hash fails or otherwise, if this is a video, mark it frequently changing.
		// This prevents temporary scaling perf hits on the first second of video.
		if (IsVideo(entry->addr)) {
			entry->status |= TexCacheEntry::STATUS_CHANGE_FREQUENT | TexCacheEntry::STATUS_VIDEO;
		} else {
			entry->status &= ~TexCacheEntry::STATUS_VIDEO;
		}

		if (nextNeedsRehash_) {
			PROFILE_THIS_SCOPE("texhash");
			// Update the hash on the texture.
			int w = gstate.getTextureWidth(0);
			int h = gstate.getTextureHeight(0);
			bool swizzled = gstate.isTextureSwizzled();
			entry->fullhash = QuickTexHash(replacer_, entry->addr, entry->bufw, w, h, swizzled, GETextureFormat(entry->format), entry);

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
		ForgetLastTexture();
	}

	gstate_c.SetTextureIsVideo((entry->status & TexCacheEntry::STATUS_VIDEO) != 0);
	if (entry->status & TexCacheEntry::STATUS_CLUT_GPU) {
		// Special process.
		ApplyTextureDepal(entry);
		entry->lastFrame = gpuStats.numFlips;
		gstate_c.SetTextureFullAlpha(false);
		gstate_c.SetTextureIs3D(false);
		gstate_c.SetTextureIsArray(false);
		gstate_c.SetTextureIsBGRA(false);
	} else {
		entry->lastFrame = gpuStats.numFlips;
		if (doBind) {
			BindTexture(entry);
		}
		gstate_c.SetTextureFullAlpha(entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL);
		gstate_c.SetTextureIs3D((entry->status & TexCacheEntry::STATUS_3D) != 0);
		gstate_c.SetTextureIsArray(false);
		gstate_c.SetTextureIsBGRA((entry->status & TexCacheEntry::STATUS_BGRA) != 0);
		gstate_c.SetUseShaderDepal(ShaderDepalMode::OFF);
	}
}

// Can we depalettize at all? This refers to both in-fragment-shader depal and "traditional" depal through a separate pass.
static bool CanDepalettize(GETextureFormat texFormat, GEBufferFormat bufferFormat) {
	if (IsClutFormat(texFormat)) {
		switch (bufferFormat) {
		case GE_FORMAT_4444:
		case GE_FORMAT_565:
		case GE_FORMAT_5551:
		case GE_FORMAT_DEPTH16:
			if (texFormat == GE_TFMT_CLUT16) {
				return true;
			}
			if (texFormat == GE_TFMT_CLUT8 && bufferFormat == GE_FORMAT_5551 && PSP_CoreParameter().compat.flags().SOCOMClut8Replacement) {
				// Wacky case from issue #16210 (SOCOM etc).
				return true;
			}
			break;
		case GE_FORMAT_8888:
			if (texFormat == GE_TFMT_CLUT32 || texFormat == GE_TFMT_CLUT8) {  // clut8 takes a special depal mode.
				return true;
			}
			break;
		case GE_FORMAT_CLUT8:
		case GE_FORMAT_INVALID:
			// Shouldn't happen here.
			return false;
		}
		WARN_LOG(Log::G3D, "Invalid CLUT/framebuffer combination: %s vs %s", GeTextureFormatToString(texFormat), GeBufferFormatToString(bufferFormat));
		return false;
	} else if (texFormat == GE_TFMT_5650 && bufferFormat == GE_FORMAT_DEPTH16) {
		// We can also "depal" 565 format, this is used to read depth buffers as 565 on occasion (#15491).
		return true;
	}
	return false;
}

// If the palette is detected as a smooth ramp, we can interpolate for higher color precision.
// But we only do it if the mask/shift exactly matches a color channel, else something different might be going
// on and we definitely don't want to interpolate.
// Great enhancement for Test Drive and Manhunt 2.
static bool CanUseSmoothDepal(const GPUgstate &gstate, GEBufferFormat framebufferFormat, const ClutTexture &clutTexture) {
	for (int i = 0; i < ClutTexture::MAX_RAMPS; i++) {
		if (gstate.getClutIndexStartPos() == clutTexture.rampStarts[i] &&
			gstate.getClutIndexMask() < clutTexture.rampLengths[i]) {
			switch (framebufferFormat) {
			case GE_FORMAT_565:
				if (gstate.getClutIndexShift() == 0 || gstate.getClutIndexShift() == 11) {
					return gstate.getClutIndexMask() == 0x1F;
				} else if (gstate.getClutIndexShift() == 5) {
					return gstate.getClutIndexMask() == 0x3F;
				}
				break;
			case GE_FORMAT_5551:
				if (gstate.getClutIndexShift() == 0 || gstate.getClutIndexShift() == 5 || gstate.getClutIndexShift() == 10) {
					return gstate.getClutIndexMask() == 0x1F;
				}
				break;
			default:
				// No uses for the other formats yet, add if needed.
				break;
			}
		}
	}
	return false;
}

void TextureCacheCommon::ApplyTextureFramebuffer(VirtualFramebuffer *framebuffer, GETextureFormat texFormat, RasterChannel channel) {
	Draw2DPipeline *textureShader = nullptr;
	uint32_t clutMode = gstate.clutformat & 0xFFFFFF;

	bool depth = channel == RASTER_DEPTH;
	bool need_depalettize = CanDepalettize(texFormat, depth ? GE_FORMAT_DEPTH16 : framebuffer->fb_format);

	// Shader depal is not supported during 3D texturing or depth texturing, and requires 32-bit integer instructions in the shader.
	bool useShaderDepal = framebufferManager_->GetCurrentRenderVFB() != framebuffer &&
		!depth && clutRenderAddress_ == 0xFFFFFFFF &&
		!gstate_c.curTextureIs3D &&
		draw_->GetShaderLanguageDesc().bitwiseOps &&
		!(texFormat == GE_TFMT_CLUT8 && framebuffer->fb_format == GE_FORMAT_5551);  // socom

	switch (draw_->GetShaderLanguageDesc().shaderLanguage) {
	case ShaderLanguage::GLSL_1xx:
		// Force off for now, in case <= GLSL 1.20 or GLES 2, which don't support switch-case.
		useShaderDepal = false;
		break;
	default:
		break;
	}

	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	ClutTexture clutTexture{};
	bool smoothedDepal = false;
	u32 depthUpperBits = 0;

	if (need_depalettize) {
		if (clutRenderAddress_ == 0xFFFFFFFF) {
			clutTexture = textureShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBufRaw_);
			smoothedDepal = CanUseSmoothDepal(gstate, framebuffer->fb_format, clutTexture);
		} else {
			// The CLUT texture is dynamic, it's the framebuffer pointed to by clutRenderAddress.
			// Instead of texturing directly from that, we copy to a temporary CLUT texture.
			GEBufferFormat expectedCLUTBufferFormat = (GEBufferFormat)clutFormat;

			// OK, figure out what format we want our framebuffer in, so it can be reinterpreted if needed.
			// If no reinterpretation is needed, we'll automatically just get a copy shader.
			float scaleFactorX = 1.0f;
			Draw2DPipeline *reinterpret = framebufferManager_->GetReinterpretPipeline(clutRenderFormat_, expectedCLUTBufferFormat, &scaleFactorX);
			framebufferManager_->BlitUsingRaster(dynamicClutTemp_, 0.0f, 0.0f, 512.0f, 1.0f, dynamicClutFbo_, 0.0f, 0.0f, scaleFactorX * 512.0f, 1.0f, false, 1.0f, reinterpret, "reinterpret_clut");
		}

		if (useShaderDepal) {
			// Very icky conflation here of native and thin3d rendering. This will need careful work per backend in BindAsClutTexture.
			BindAsClutTexture(clutTexture.texture, smoothedDepal);

			framebufferManager_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET, Draw::ALL_LAYERS);
			// Vulkan needs to do some extra work here to pick out the native handle from Draw.
			BoundFramebufferTexture();

			SamplerCacheKey samplerKey = GetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);
			samplerKey.magFilt = false;
			samplerKey.minFilt = false;
			samplerKey.mipEnable = false;
			ApplySamplingParams(samplerKey);

			ShaderDepalMode mode = ShaderDepalMode::NORMAL;
			if (texFormat == GE_TFMT_CLUT8 && framebuffer->fb_format == GE_FORMAT_8888) {
				mode = ShaderDepalMode::CLUT8_8888;
				smoothedDepal = false;  // just in case
			} else if (smoothedDepal) {
				mode = ShaderDepalMode::SMOOTHED;
			}

			gstate_c.Dirty(DIRTY_DEPAL);
			gstate_c.SetUseShaderDepal(mode);
			gstate_c.depalFramebufferFormat = framebuffer->fb_format;

			const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
			const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;
			CheckAlphaResult alphaStatus = CheckCLUTAlpha((const uint8_t *)clutBufRaw_, clutFormat, clutTotalColors);
			gstate_c.SetTextureFullAlpha(alphaStatus == CHECKALPHA_FULL);

			draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
			return;
		}

		depthUpperBits = (depth && framebuffer->fb_format == GE_FORMAT_8888) ? ((gstate.getTextureAddress(0) & 0x600000) >> 20) : 0;

		textureShader = textureShaderCache_->GetDepalettizeShader(clutMode, texFormat, depth ? GE_FORMAT_DEPTH16 : framebuffer->fb_format, smoothedDepal, depthUpperBits);
		gstate_c.SetUseShaderDepal(ShaderDepalMode::OFF);
	}

	if (textureShader) {
		bool needsDepthXSwizzle = depthUpperBits == 2;

		int depalWidth = framebuffer->renderWidth;
		int texWidth = framebuffer->bufferWidth;
		if (needsDepthXSwizzle) {
			texWidth = RoundUpToPowerOf2(framebuffer->bufferWidth);
			depalWidth = texWidth * framebuffer->renderScaleFactor;
			gstate_c.Dirty(DIRTY_UVSCALEOFFSET);
		}

		// If min is not < max, then we don't have values (wasn't set during decode.)
		const KnownVertexBounds &bounds = gstate_c.vertBounds;
		float u1 = 0.0f;
		float v1 = 0.0f;
		float u2 = depalWidth;
		float v2 = framebuffer->renderHeight;
		if (bounds.minV < bounds.maxV) {
			u1 = (bounds.minU + gstate_c.curTextureXOffset) * framebuffer->renderScaleFactor;
			v1 = (bounds.minV + gstate_c.curTextureYOffset) * framebuffer->renderScaleFactor;
			u2 = (bounds.maxU + gstate_c.curTextureXOffset) * framebuffer->renderScaleFactor;
			v2 = (bounds.maxV + gstate_c.curTextureYOffset) * framebuffer->renderScaleFactor;
			// We need to reapply the texture next time since we cropped UV.
			gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		}

		Draw::Framebuffer *depalFBO = framebufferManager_->GetTempFBO(TempFBO::DEPAL, depalWidth, framebuffer->renderHeight);
		draw_->BindTexture(0, nullptr);
		draw_->BindTexture(1, nullptr);
		draw_->BindFramebufferAsRenderTarget(depalFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "Depal");
		draw_->InvalidateFramebuffer(Draw::FB_INVALIDATION_STORE, Draw::Aspect::DEPTH_BIT | Draw::Aspect::STENCIL_BIT);
		draw_->SetScissorRect(u1, v1, u2 - u1, v2 - v1);
		Draw::Viewport viewport{ 0.0f, 0.0f, (float)depalWidth, (float)framebuffer->renderHeight, 0.0f, 1.0f };
		draw_->SetViewport(viewport);

		draw_->BindFramebufferAsTexture(framebuffer->fbo, 0, depth ? Draw::Aspect::DEPTH_BIT : Draw::Aspect::COLOR_BIT, Draw::ALL_LAYERS);
		if (clutRenderAddress_ == 0xFFFFFFFF) {
			draw_->BindTexture(1, clutTexture.texture);
		} else {
			draw_->BindFramebufferAsTexture(dynamicClutFbo_, 1, Draw::Aspect::COLOR_BIT, 0);
		}
		Draw::SamplerState *nearest = textureShaderCache_->GetSampler(false);
		Draw::SamplerState *clutSampler = textureShaderCache_->GetSampler(smoothedDepal);
		draw_->BindSamplerStates(0, 1, &nearest);
		draw_->BindSamplerStates(1, 1, &clutSampler);

		draw2D_->Blit(textureShader, u1, v1, u2, v2, u1, v1, u2, v2, framebuffer->renderWidth, framebuffer->renderHeight, depalWidth, framebuffer->renderHeight, false, framebuffer->renderScaleFactor);

		gpuStats.numDepal++;

		gstate_c.curTextureWidth = texWidth;
		gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

		draw_->BindTexture(0, nullptr);
		framebufferManager_->RebindFramebuffer("ApplyTextureFramebuffer");

		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::Aspect::COLOR_BIT, Draw::ALL_LAYERS);
		BoundFramebufferTexture();

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		CheckAlphaResult alphaStatus = CheckCLUTAlpha((const uint8_t *)clutBufRaw_, clutFormat, clutTotalColors);
		gstate_c.SetTextureFullAlpha(alphaStatus == CHECKALPHA_FULL);

		draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
		shaderManager_->DirtyLastShader();
	} else {
		framebufferManager_->RebindFramebuffer("ApplyTextureFramebuffer");
		framebufferManager_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET, Draw::ALL_LAYERS);
		BoundFramebufferTexture();

		gstate_c.SetUseShaderDepal(ShaderDepalMode::OFF);
		gstate_c.SetTextureFullAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
	}

	SamplerCacheKey samplerKey = GetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight);
	ApplySamplingParams(samplerKey);

	// Since we've drawn using thin3d, might need these.
	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
}

// Applies depal to a normal (non-framebuffer) texture, pre-decoded to CLUT8 format.
void TextureCacheCommon::ApplyTextureDepal(TexCacheEntry *entry) {
	uint32_t clutMode = gstate.clutformat & 0xFFFFFF;

	switch (entry->format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
		break; // These are OK
	default:
		_dbg_assert_(false);
		return;
	}

	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	u32 depthUpperBits = 0;

	// The CLUT texture is dynamic, it's the framebuffer pointed to by clutRenderAddress.
	// Instead of texturing directly from that, we copy to a temporary CLUT texture.
	GEBufferFormat expectedCLUTBufferFormat = (GEBufferFormat)clutFormat;  // All entries from clutFormat correspond directly to buffer formats.

	// OK, figure out what format we want our framebuffer in, so it can be reinterpreted if needed.
	// If no reinterpretation is needed, we'll automatically just get a copy shader.
	float scaleFactorX = 1.0f;
	Draw2DPipeline *reinterpret = framebufferManager_->GetReinterpretPipeline(clutRenderFormat_, expectedCLUTBufferFormat, &scaleFactorX);
	framebufferManager_->BlitUsingRaster(
		dynamicClutTemp_, 0.0f, 0.0f, 512.0f, 1.0f, dynamicClutFbo_, 0.0f, 0.0f, scaleFactorX * 512.0f, 1.0f, false, 1.0f, reinterpret, "reinterpret_clut");

	Draw2DPipeline *textureShader = textureShaderCache_->GetDepalettizeShader(clutMode, GE_TFMT_CLUT8, GE_FORMAT_CLUT8, false, 0);
	gstate_c.SetUseShaderDepal(ShaderDepalMode::OFF);

	int texWidth = gstate.getTextureWidth(0);
	int texHeight = gstate.getTextureHeight(0);

	// If min is not < max, then we don't have values (wasn't set during decode.)
	const KnownVertexBounds &bounds = gstate_c.vertBounds;
	float u1 = 0.0f;
	float v1 = 0.0f;
	float u2 = texWidth;
	float v2 = texHeight;
	if (bounds.minV < bounds.maxV) {
		// These are already in pixel coords! Doesn't seem like we should multiply by texwidth/height.
		u1 = bounds.minU + gstate_c.curTextureXOffset;
		v1 = bounds.minV + gstate_c.curTextureYOffset;
		u2 = bounds.maxU + gstate_c.curTextureXOffset + 1.0f;
		v2 = bounds.maxV + gstate_c.curTextureYOffset + 1.0f;
		// We need to reapply the texture next time since we cropped UV.
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	}

	Draw::Framebuffer *depalFBO = framebufferManager_->GetTempFBO(TempFBO::DEPAL, texWidth, texHeight);
	draw_->BindTexture(0, nullptr);
	draw_->BindTexture(1, nullptr);
	draw_->BindFramebufferAsRenderTarget(depalFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "Depal");
	draw_->InvalidateFramebuffer(Draw::FB_INVALIDATION_STORE, Draw::Aspect::DEPTH_BIT | Draw::Aspect::STENCIL_BIT);
	draw_->SetScissorRect(u1, v1, u2 - u1, v2 - v1);
	Draw::Viewport viewport{ 0.0f, 0.0f, (float)texWidth, (float)texHeight, 0.0f, 1.0f };
	draw_->SetViewport(viewport);

	draw_->BindNativeTexture(0, GetNativeTextureView(entry, false));
	draw_->BindFramebufferAsTexture(dynamicClutFbo_, 1, Draw::Aspect::COLOR_BIT, 0);
	Draw::SamplerState *nearest = textureShaderCache_->GetSampler(false);
	Draw::SamplerState *clutSampler = textureShaderCache_->GetSampler(false);
	draw_->BindSamplerStates(0, 1, &nearest);
	draw_->BindSamplerStates(1, 1, &clutSampler);

	draw2D_->Blit(textureShader, u1, v1, u2, v2, u1, v1, u2, v2, texWidth, texHeight, texWidth, texHeight, false, 1);

	gpuStats.numDepal++;

	gstate_c.curTextureWidth = texWidth;
	gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

	draw_->BindTexture(0, nullptr);
	framebufferManager_->RebindFramebuffer("ApplyTextureFramebuffer");

	draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::Aspect::COLOR_BIT, 0);
	BoundFramebufferTexture();

	const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
	const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

	// We don't know about alpha at all.
	gstate_c.SetTextureFullAlpha(false);

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
	shaderManager_->DirtyLastShader();

	SamplerCacheKey samplerKey = GetFramebufferSamplingParams(texWidth, texHeight);
	ApplySamplingParams(samplerKey);

	// Since we've drawn using thin3d, might need these.
	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
}

void TextureCacheCommon::Clear(bool delete_them) {
	textureShaderCache_->Clear();

	ForgetLastTexture();
	for (TexCache::iterator iter = cache_.begin(); iter != cache_.end(); ++iter) {
		ReleaseTexture(iter->second.get(), delete_them);
	}
	// In case the setting was changed, we ALWAYS clear the secondary cache (enabled or not.)
	for (TexCache::iterator iter = secondCache_.begin(); iter != secondCache_.end(); ++iter) {
		ReleaseTexture(iter->second.get(), delete_them);
	}
	if (cache_.size() + secondCache_.size()) {
		INFO_LOG(Log::G3D, "Texture cached cleared from %i textures", (int)(cache_.size() + secondCache_.size()));
		cache_.clear();
		secondCache_.clear();
		cacheSizeEstimate_ = 0;
		secondCacheSizeEstimate_ = 0;
	}
	videos_.clear();

	if (dynamicClutFbo_) {
		dynamicClutFbo_->Release();
		dynamicClutFbo_ = nullptr;
	}
	if (dynamicClutTemp_) {
		dynamicClutTemp_->Release();
		dynamicClutTemp_ = nullptr;
	}
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
	bool swizzled = gstate.isTextureSwizzled();

	// Don't even check the texture, just assume it has changed.
	if (isVideo && g_Config.bTextureBackoffCache) {
		// Attempt to ensure the hash doesn't incorrectly match in if the video stops.
		entry->fullhash = (entry->fullhash + 0xA535A535) * 11 + (entry->fullhash & 4);
		return false;
	}

	u32 fullhash;
	{
		PROFILE_THIS_SCOPE("texhash");
		fullhash = QuickTexHash(replacer_, entry->addr, entry->bufw, w, h, swizzled, GETextureFormat(entry->format), entry);
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
	if (PSP_CoreParameter().compat.flags().SecondaryTextureCache) {
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
		// Intentional underestimate here.
		u32 texEnd = entry->addr + entry->SizeInRAM() / 2;

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

std::string AttachCandidate::ToString() const {
	return StringFromFormat("[%s seq:%d rel:%d C:%08x/%d(%s) Z:%08x/%d X:%d Y:%d reint: %s]",
		RasterChannelToString(this->channel),
		this->channel == RASTER_COLOR ? this->fb->colorBindSeq : this->fb->depthBindSeq,
		this->relevancy,
		this->fb->fb_address, this->fb->fb_stride, GeBufferFormatToString(this->fb->fb_format),
		this->fb->z_address, this->fb->z_stride,
		this->match.xOffset, this->match.yOffset, this->match.reinterpret ? "true" : "false");
}

bool TextureCacheCommon::PrepareBuildTexture(BuildTexturePlan &plan, TexCacheEntry *entry) {
	gpuStats.numTexturesDecoded++;

	// For the estimate, we assume cluts always point to 8888 for simplicity.
	cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

	plan.badMipSizes = false;
	// maxLevel here is the max level to upload. Not the count.
	plan.levelsToLoad = entry->maxLevel + 1;
	for (int i = 0; i < plan.levelsToLoad; i++) {
		// If encountering levels pointing to nothing, adjust max level.
		u32 levelTexaddr = gstate.getTextureAddress(i);
		if (!Memory::IsValidAddress(levelTexaddr)) {
			plan.levelsToLoad = i;
			break;
		}

		// If size reaches 1, stop, and override maxlevel.
		int tw = gstate.getTextureWidth(i);
		int th = gstate.getTextureHeight(i);
		if (tw == 1 || th == 1) {
			plan.levelsToLoad = i + 1;  // next level is assumed to be invalid
			break;
		}

		if (i > 0) {
			int lastW = gstate.getTextureWidth(i - 1);
			int lastH = gstate.getTextureHeight(i - 1);

			if (gstate_c.Use(GPU_USE_TEXTURE_LOD_CONTROL)) {
				if (tw != 1 && tw != (lastW >> 1))
					plan.badMipSizes = true;
				else if (th != 1 && th != (lastH >> 1))
					plan.badMipSizes = true;
			}
		}
	}

	plan.scaleFactor = standardScaleFactor_;
	plan.depth = 1;

	// Rachet down scale factor in low-memory mode.
	// TODO: I think really we should just turn it off?
	if (lowMemoryMode_ && !plan.hardwareScaling) {
		// Keep it even, though, just in case of npot troubles.
		plan.scaleFactor = plan.scaleFactor > 4 ? 4 : (plan.scaleFactor > 2 ? 2 : 1);
	}

	bool isFakeMipmapChange = false;
	if (plan.badMipSizes) {
		isFakeMipmapChange = IsFakeMipmapChange();

		// Check for pure 3D texture.
		int tw = gstate.getTextureWidth(0);
		int th = gstate.getTextureHeight(0);
		bool pure3D = true;
		for (int i = 0; i < plan.levelsToLoad; i++) {
			if (gstate.getTextureWidth(i) != gstate.getTextureWidth(0) || gstate.getTextureHeight(i) != gstate.getTextureHeight(0)) {
				pure3D = false;
				break;
			}
		}

		// Check early for the degenerate case from Tactics Ogre.
		if (pure3D && plan.levelsToLoad == 2 && gstate.getTextureAddress(0) == gstate.getTextureAddress(1)) {
			// Simply treat it as a regular 2D texture, no fake mipmaps or anything.
			// levelsToLoad/Create gets set to 1 on the way out from the surrounding if.
			isFakeMipmapChange = false;
			pure3D = false;
		} else if (isFakeMipmapChange) {
			// We don't want to create a volume texture, if this is a "fake mipmap change".
			// In practice due to the compat flag, the only time we end up here is in JP Tactics Ogre.
			pure3D = false;
		}

		if (pure3D && draw_->GetDeviceCaps().texture3DSupported) {
			plan.depth = plan.levelsToLoad;
			plan.scaleFactor = 1;
		}

		plan.levelsToLoad = 1;
		plan.levelsToCreate = 1;
	}

	if (plan.hardwareScaling) {
		plan.scaleFactor = shaderScaleFactor_;
	}

	// We generate missing mipmaps from maxLevel+1 up to this level. maxLevel can get overwritten below
	// such as when using replacement textures - but let's keep the same amount of levels for generation.
	// Not all backends will generate mipmaps, and in GL we can't really control the number of levels.
	plan.levelsToCreate = plan.levelsToLoad;

	plan.w = gstate.getTextureWidth(0);
	plan.h = gstate.getTextureHeight(0);

	bool isPPGETexture = entry->addr >= PSP_GetKernelMemoryBase() && entry->addr < PSP_GetKernelMemoryEnd();

	// Don't scale the PPGe texture.
	if (isPPGETexture) {
		plan.scaleFactor = 1;
	} else if (!g_DoubleTextureCoordinates) {
		// Refuse to load invalid-ly sized textures, which can happen through display list corruption.
		// However, turns out some games uses huge textures for font rendering for no apparent reason.
		// These will only work correctly in the top 512x512 part. So, I've increased the threshold quite a bit.
		// We probably should handle these differently, by clamping the texture size and texture coordinates, but meh.
		if (plan.w > 2048 || plan.h > 2048) {
			ERROR_LOG(Log::G3D, "Bad texture dimensions: %dx%d", plan.w, plan.h);
			return false;
		}
	}

	if (PSP_CoreParameter().compat.flags().ForceLowerResolutionForEffectsOn && gstate.FrameBufStride() < 0x1E0) {
		// A bit of an esoteric workaround - force off upscaling for static textures that participate directly in small-resolution framebuffer effects.
		// This fixes the water in Outrun/DiRT 2 with upscaling enabled.
		plan.scaleFactor = 1;
	}

	if ((entry->status & TexCacheEntry::STATUS_CHANGE_FREQUENT) != 0 && plan.scaleFactor != 1 && plan.slowScaler) {
		// Remember for later that we /wanted/ to scale this texture.
		entry->status |= TexCacheEntry::STATUS_TO_SCALE;
		plan.scaleFactor = 1;
	}

	if (plan.scaleFactor != 1) {
		if (texelsScaledThisFrame_ >= TEXCACHE_MAX_TEXELS_SCALED && plan.slowScaler) {
			entry->status |= TexCacheEntry::STATUS_TO_SCALE;
			plan.scaleFactor = 1;
		} else {
			entry->status &= ~TexCacheEntry::STATUS_TO_SCALE;
			entry->status |= TexCacheEntry::STATUS_IS_SCALED_OR_REPLACED;
			texelsScaledThisFrame_ += plan.w * plan.h;
		}
	}

	plan.isVideo = IsVideo(entry->addr);

	// TODO: Support reading actual mip levels for upscaled images, instead of just generating them.
	// Maybe can just remove this check?
	if (plan.scaleFactor > 1) {
		plan.levelsToLoad = 1;

		bool enableVideoUpscaling = false;

		if (!enableVideoUpscaling && plan.isVideo) {
			plan.scaleFactor = 1;
			plan.levelsToCreate = 1;
		}
	}

	bool canReplace = !isPPGETexture;
	if (entry->status & TexCacheEntry::TexStatus::STATUS_CLUT_GPU) {
		_dbg_assert_(entry->format == GE_TFMT_CLUT4 || entry->format == GE_TFMT_CLUT8);
		plan.decodeToClut8 = true;
		// We only support 1 mip level when doing CLUT on GPU for now.
		// Supporting more would be possible, just not very interesting until we need it.
		plan.levelsToCreate = 1;
		plan.levelsToLoad = 1;
		plan.maxPossibleLevels = 1;
		plan.scaleFactor = 1;
		plan.saveTexture = false;  // Can't yet save these properly.
		canReplace = false;
	} else {
		plan.decodeToClut8 = false;
	}

	if (canReplace) {
		// This is the "trigger point" for replacement.
		plan.replaced = FindReplacement(entry, &plan.w, &plan.h, &plan.depth);
		plan.doReplace = plan.replaced ? plan.replaced->State() == ReplacementState::ACTIVE : false;
	} else {
		plan.replaced = nullptr;
		plan.doReplace = false;
	}

	// NOTE! Last chance to change scale factor here!

	plan.saveTexture = false;
	if (plan.doReplace) {
		// We're replacing, so we won't scale.
		plan.scaleFactor = 1;
		// We're ignoring how many levels were specified - instead we just load all available from the replacer.
		plan.levelsToLoad = plan.replaced->NumLevels();
		plan.levelsToCreate = plan.levelsToLoad;  // Or more, if we wanted to generate.
		plan.badMipSizes = false;
		// But, we still need to create the texture at a larger size.
		plan.replaced->GetSize(0, &plan.createW, &plan.createH);
	} else {
		if (replacer_.SaveEnabled() && !plan.doReplace && plan.depth == 1 && canReplace) {
			ReplacedTextureDecodeInfo replacedInfo;
			// TODO: Do we handle the race where a replacement becomes valid AFTER this but before we save?
			replacedInfo.cachekey = entry->CacheKey();
			replacedInfo.hash = entry->fullhash;
			replacedInfo.addr = entry->addr;
			replacedInfo.isFinal = (entry->status & TexCacheEntry::STATUS_TO_SCALE) == 0;
			replacedInfo.isVideo = plan.isVideo;
			replacedInfo.fmt = Draw::DataFormat::R8G8B8A8_UNORM;
			plan.saveTexture = replacer_.WillSave(replacedInfo);
		}
		plan.createW = plan.w * plan.scaleFactor;
		plan.createH = plan.h * plan.scaleFactor;
	}

	// Always load base level texture here 
	plan.baseLevelSrc = 0;
	if (isFakeMipmapChange) {
		// NOTE: Since the level is not part of the cache key, we assume it never changes.
		plan.baseLevelSrc = std::max(0, gstate.getTexLevelOffset16() / 16);
		// Tactics Ogre: If this is an odd level and it has the same texture address the below even level,
		// let's just say it's the even level for the purposes of replacement.
		// I assume this is done to avoid blending between levels accidentally?
		// The Japanese version of Tactics Ogre uses multiple of these "double" levels to fit more characters.
		if ((plan.baseLevelSrc & 1) && gstate.getTextureAddress(plan.baseLevelSrc) == gstate.getTextureAddress(plan.baseLevelSrc & ~1)) {
			plan.baseLevelSrc &= ~1;
		}
		plan.levelsToCreate = 1;
		plan.levelsToLoad = 1;
		// Make sure we already decided not to do a 3D texture above.
		_dbg_assert_(plan.depth == 1);
	}

	if (plan.isVideo || plan.depth != 1 || plan.decodeToClut8) {
		plan.levelsToLoad = 1;
		plan.maxPossibleLevels = 1;
	} else {
		plan.maxPossibleLevels = log2i(std::max(plan.createW, plan.createH)) + 1;
	}

	if (plan.levelsToCreate == 1) {
		entry->status |= TexCacheEntry::STATUS_NO_MIPS;
	} else {
		entry->status &= ~TexCacheEntry::STATUS_NO_MIPS;
	}

	// Will be filled in again during decode.
	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;
	return true;
}

// Passing 0 into dataSize will disable checking.
void TextureCacheCommon::LoadTextureLevel(TexCacheEntry &entry, uint8_t *data, size_t dataSize, int stride, BuildTexturePlan &plan, int srcLevel, Draw::DataFormat dstFmt, TexDecodeFlags texDecFlags) {
	int w = gstate.getTextureWidth(srcLevel);
	int h = gstate.getTextureHeight(srcLevel);

	PROFILE_THIS_SCOPE("decodetex");

	if (plan.doReplace) {
		plan.replaced->GetSize(srcLevel, &w, &h);
		double replaceStart = time_now_d();
		plan.replaced->CopyLevelTo(srcLevel, data, dataSize, stride);
		replacementTimeThisFrame_ += time_now_d() - replaceStart;
	} else {
		GETextureFormat tfmt = (GETextureFormat)entry.format;
		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		u32 texaddr = gstate.getTextureAddress(srcLevel);
		const int bufw = GetTextureBufw(srcLevel, texaddr, tfmt);
		u32 *pixelData;
		int decPitch;
		if (plan.scaleFactor > 1) {
			tmpTexBufRearrange_.resize(std::max(bufw, w) * h);
			pixelData = tmpTexBufRearrange_.data();
			// We want to end up with a neatly packed texture for scaling.
			decPitch = w * 4;
		} else {
			pixelData = (u32 *)data;
			decPitch = stride;
		}

		if (!gstate_c.Use(GPU_USE_16BIT_FORMATS) || dstFmt == Draw::DataFormat::R8G8B8A8_UNORM) {
			texDecFlags |= TexDecodeFlags::EXPAND32;
		}
		if (entry.status & TexCacheEntry::STATUS_CLUT_GPU) {
			texDecFlags |= TexDecodeFlags::TO_CLUT8;
		}

		CheckAlphaResult alphaResult = DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, srcLevel, bufw, texDecFlags);
		entry.SetAlphaStatus(alphaResult, srcLevel);

		int scaledW = w, scaledH = h;
		if (plan.scaleFactor > 1) {
			// Note that this updates w and h!
			scaler_.ScaleAlways((u32 *)data, pixelData, w, h, &scaledW, &scaledH, plan.scaleFactor);
			pixelData = (u32 *)data;

			decPitch = scaledW * sizeof(u32);

			if (decPitch != stride) {
				// Rearrange in place to match the requested pitch.
				// (it can only be larger than w * bpp, and a match is likely.)
				// Note! This is bad because it reads the mapped memory! TODO: Look into if DX9 does this right.
				for (int y = scaledH - 1; y >= 0; --y) {
					memcpy((u8 *)data + stride * y, (u8 *)data + decPitch * y, scaledW *4);
				}
				decPitch = stride;
			}
		}

		if (plan.saveTexture && !lowMemoryMode_) {
			ReplacedTextureDecodeInfo replacedInfo;
			replacedInfo.cachekey = entry.CacheKey();
			replacedInfo.hash = entry.fullhash;
			replacedInfo.addr = entry.addr;
			replacedInfo.isVideo = IsVideo(entry.addr);
			replacedInfo.isFinal = (entry.status & TexCacheEntry::STATUS_TO_SCALE) == 0;
			replacedInfo.fmt = dstFmt;

			// NOTE: Reading the decoded texture here may be very slow, if we just wrote it to write-combined memory.
			replacer_.NotifyTextureDecoded(plan.replaced, replacedInfo, pixelData, decPitch, srcLevel, w, h, scaledW, scaledH);
		}
	}
}

CheckAlphaResult TextureCacheCommon::CheckCLUTAlpha(const uint8_t *pixelData, GEPaletteFormat clutFormat, int w) {
	switch (clutFormat) {
	case GE_CMODE_16BIT_ABGR4444:
		return CheckAlpha16((const u16 *)pixelData, w, 0xF000);
	case GE_CMODE_16BIT_ABGR5551:
		return CheckAlpha16((const u16 *)pixelData, w, 0x8000);
	case GE_CMODE_16BIT_BGR5650:
		// Never has any alpha.
		return CHECKALPHA_FULL;
	default:
		return CheckAlpha32((const u32 *)pixelData, w, 0xFF000000);
	}
}

void TextureCacheCommon::DrawImGuiDebug(uint64_t &selectedTextureId) const {
	ImVec2 avail = ImGui::GetContentRegionAvail();
	auto &style = ImGui::GetStyle();
	ImGui::BeginChild("left", ImVec2(140.0f, 0.0f), ImGuiChildFlags_ResizeX);
	float window_visible_x2 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

	// Global texture stats
	int replacementStateCounts[(int)ReplacementState::COUNT]{};
	if (!secondCache_.empty()) {
		ImGui::Text("Primary Cache");
	}

	for (auto &iter : cache_) {
		u64 id = iter.first;
		const TexCacheEntry *entry = iter.second.get();
		void *nativeView = GetNativeTextureView(iter.second.get(), true);
		int w = 128;
		int h = 128;

		if (entry->replacedTexture) {
			replacementStateCounts[(int)entry->replacedTexture->State()]++;
		}

		ImTextureID texId = ImGui_ImplThin3d_AddNativeTextureTemp(nativeView);
		float last_button_x2 = ImGui::GetItemRectMax().x;
		float next_button_x2 = last_button_x2 + style.ItemSpacing.x + w; // Expected position if next button was on same line
		if (next_button_x2 < window_visible_x2)
			ImGui::SameLine();

		float x = ImGui::GetCursorPosX();
		if (ImGui::Selectable(("##Image" + std::to_string(id)).c_str(), selectedTextureId == id, 0, ImVec2(w, h))) {
			selectedTextureId = id; // Update the selected index if clicked
		}

		ImGui::SameLine();
		ImGui::SetCursorPosX(x + 2.0f);
		ImGui::Image(texId, ImVec2(128, 128));
	}

	if (!secondCache_.empty()) {
		ImGui::Text("Secondary Cache (%d): TODO", (int)secondCache_.size());
		// TODO
	}

	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("right", ImVec2(0.f, 0.0f));
	if (ImGui::CollapsingHeader("Texture", nullptr, ImGuiTreeNodeFlags_DefaultOpen)) {
		if (selectedTextureId) {
			auto iter = cache_.find(selectedTextureId);
			if (iter != cache_.end()) {
				void *nativeView = GetNativeTextureView(iter->second.get(), true);
				ImTextureID texId = ImGui_ImplThin3d_AddNativeTextureTemp(nativeView);
				const TexCacheEntry *entry = iter->second.get();
				int dim = entry->dim;
				int w = dimWidth(dim);
				int h = dimHeight(dim);
				ImGui::Image(texId, ImVec2(w, h));
				ImGui::Text("%08x: %dx%d, %d mips, %s", (uint32_t)(selectedTextureId & 0xFFFFFFFF), w, h, entry->maxLevel + 1, GeTextureFormatToString((GETextureFormat)entry->format));
				ImGui::Text("Stride: %d", entry->bufw);
				ImGui::Text("Status: %08x", entry->status);  // TODO: Show the flags
				ImGui::Text("Hash: %08x", entry->fullhash);
				ImGui::Text("CLUT Hash: %08x", entry->cluthash);
				ImGui::Text("Minihash: %08x", entry->minihash);
				ImGui::Text("MaxSeenV: %08x", entry->maxSeenV);
				if (entry->replacedTexture) {
					if (ImGui::CollapsingHeader("Replacement", ImGuiTreeNodeFlags_DefaultOpen)) {
						const auto &desc = entry->replacedTexture->Desc();
						ImGui::Text("State: %s", StateString(entry->replacedTexture->State()));
						// ImGui::Text("Original: %dx%d (%dx%d)", desc.w, desc.h, desc.newW, desc.newH);
						if (entry->replacedTexture->State() == ReplacementState::ACTIVE) {
							int w, h;
							entry->replacedTexture->GetSize(0, &w, &h);
							int numLevels = entry->replacedTexture->NumLevels();
							ImGui::Text("Replaced: %dx%d, %d mip levels", w, h, numLevels);
							ImGui::Text("Level 0 size: %d bytes, format: %s", entry->replacedTexture->GetLevelDataSizeAfterCopy(0), Draw::DataFormatToString(entry->replacedTexture->Format()));
						}
						ImGui::Text("Key: %08x_%08x", (u32)(desc.cachekey >> 32), (u32)desc.cachekey);
						ImGui::Text("Hashfiles: %s", desc.hashfiles.c_str());
						ImGui::Text("Base: %s", desc.basePath.c_str());
						ImGui::Text("Alpha status: %02x", entry->replacedTexture->AlphaStatus());
					}
				} else {
					ImGui::Text("Not replaced");
				}
				ImGui::Text("Frames until next full hash: %08x", entry->framesUntilNextFullHash);  // TODO: Show the flags
			} else {
				selectedTextureId = 0;
			}
		} else {
			ImGui::Text("(no texture selected)");
		}
	}

	if (ImGui::CollapsingHeader("Texture Cache State", nullptr, ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Cache: %d textures, size est %d", (int)cache_.size(), cacheSizeEstimate_);
		if (!secondCache_.empty()) {
			ImGui::Text("Second: %d textures, size est %d", (int)secondCache_.size(), secondCacheSizeEstimate_);
		}
		ImGui::Text("Standard/shader scale factor: %d/%d", standardScaleFactor_, shaderScaleFactor_);
		ImGui::Text("Texels scaled this frame: %d", texelsScaledThisFrame_);
		ImGui::Text("Low memory mode: %d", (int)lowMemoryMode_);
		if (ImGui::CollapsingHeader("Texture Replacement", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Frame time/budget: %0.3f/%0.3f ms", replacementTimeThisFrame_ * 1000.0f, replacementFrameBudget_ * 1000.0f);
			ImGui::Text("UNLOADED: %d PENDING: %d NOT_FOUND: %d ACTIVE: %d CANCEL_INIT: %d",
				replacementStateCounts[(int)ReplacementState::UNLOADED],
				replacementStateCounts[(int)ReplacementState::PENDING],
				replacementStateCounts[(int)ReplacementState::NOT_FOUND],
				replacementStateCounts[(int)ReplacementState::ACTIVE],
				replacementStateCounts[(int)ReplacementState::CANCEL_INIT]);
		}
		if (videos_.size()) {
			if (ImGui::CollapsingHeader("Tracked video playback memory")) {
				for (auto &video : videos_) {
					ImGui::Text("%08x: %d flips, size = %d", video.addr, video.flips, video.size);
				}
			}
		}
	}

	ImGui::EndChild();
}
