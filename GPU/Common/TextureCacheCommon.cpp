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

#include "ext/xxhash.h"
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
#include "Core/HW/Display.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/ge_constants.h"
#include "GPU/Debugger/Record.h"
#include "GPU/GPUState.h"
#include "Core/Util/PPGeDraw.h"

// Videos should be updated every few frames, so we forget quickly.
#define VIDEO_DECIMATE_AGE 4

// If a texture hasn't been seen for this many frames, get rid of it.
#define TEXTURE_KILL_AGE 200
#define TEXTURE_KILL_AGE_LOWMEM 60
// Not used in lowmem mode.
#define TEXTURE_SECOND_KILL_AGE 100
// Used when there are multiple CLUT variants of a texture, to avoid blowing up memory. See #10418
// We should change this to do shader-based palette expansion.
#define TEXTURE_KILL_AGE_CLUT 6

#define TEXTURE_CLUT_VARIANTS_MIN 6

// Try to be prime to other decimation intervals.
#define TEXCACHE_DECIMATION_INTERVAL 13

#define TEXCACHE_MIN_PRESSURE 16 * 1024 * 1024  // Total in VRAM
#define TEXCACHE_SECOND_MIN_PRESSURE 4 * 1024 * 1024

static bool MatchFramebuffer(const TextureDefinition &entry, VirtualFramebuffer *framebuffer, u32 texaddrOffset, RasterChannel channel, bool useBufferedRendering, FramebufferMatchInfo *matchInfo);
static bool GetBestFramebufferCandidate(FramebufferManagerCommon *fbManager, const TextureDefinition &entry, u32 texAddrOffset, AttachCandidate *bestCandidate, const char *context);

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
	: draw_(draw), draw2D_(draw2D), replacer_(draw), textureShaderCache_(draw, draw2D_), clutTextureCache_(draw) {
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
}

TextureCacheCommon::~TextureCacheCommon() {
	FreeAlignedMemory(clutBufConverted_);
	FreeAlignedMemory(clutBufRaw_);
	FreeAlignedMemory(expandClut_);
}

void TextureCacheCommon::StartFrame() {
	ForgetLastTexture();
	clutTextureCache_.Decimate();
	replacementTimeThisFrame_ = 0.0;

	float fps;
	__DisplayGetFPS(nullptr, &fps, nullptr);
	if (fps <= 5.0f) {
		fps = 60.0f;
	}

	float baseValue = 0.5f;
	switch (g_Config.iReplacementTextureLoadSpeed) {
	case (int)ReplacementTextureLoadSpeed::SLOW:
		baseValue = 0.5f;
		break;
	case (int)ReplacementTextureLoadSpeed::MEDIUM:
		baseValue = 0.75f;
		break;
	case (int)ReplacementTextureLoadSpeed::FAST:
		baseValue = 1.0f;
		break;
	case (int)ReplacementTextureLoadSpeed::INSTANT:
		baseValue = 100000.0f;  // no budget limit, effectively.
		break;
	}

	// Allow spending half a frame on uploading textures.
	replacementFrameBudgetSeconds_ = baseValue / fps;

	if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS) {
		gpuStats.perFrame.numReplacerTrackedTex = replacer_.GetNumTrackedTextures();
		gpuStats.perFrame.numCachedReplacedTextures = replacer_.GetNumCachedReplacedTextures();
	}

	if (texelsScaledThisFrame_) {
		VERBOSE_LOG(Log::TexCache, "Scaled %d texels", texelsScaledThisFrame_);
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

SamplerCacheKey TextureCacheCommon::GetSamplingParams(int maxLevel, const TexCacheEntry *entry, bool flatZ, bool pixelMapped) {
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
			if (gstate_c.Use(GPU_USE_ANISOTROPY) && !flatZ) {
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
			if (gstate_c.Use(GPU_USE_ANISOTROPY) && !flatZ) {
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
			if (pixelMapped) {
				forceFiltering = TEX_FILTER_FORCE_NEAREST;
			}
			break;
		case TEX_FILTER_FORCE_LINEAR:
			// Override to linear filtering if there's no alpha or color testing going on.
			if (CanForceBilinear(gstate)) {
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
			if (gstate_c.Use(GPU_USE_ANISOTROPY) && !flatZ) {
				key.aniso = true;
			}
			if (gstate.isModeThrough() && g_Config.iInternalResolution != 1) {
				bool uglyColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && gstate.getColorTestRef() != 0;
				if (uglyColorTest) {
					forceFiltering = TEX_FILTER_FORCE_NEAREST;
					key.aniso = false;
				}
			}
			if (pixelMapped) {
				forceFiltering = TEX_FILTER_FORCE_NEAREST;
				key.aniso = false;
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
		break;
	}

	return key;
}

SamplerCacheKey GetFramebufferSamplingParams(const GEState &gstate, u16 bufferWidth, u16 bufferHeight, bool pixelMapped) {
	SamplerCacheKey key{};

	key.magFilt = gstate.isMagnifyFilteringEnabled();
	int minFilt = gstate.texfilter & 0x7;
	key.minFilt = minFilt & 1;
	key.mipEnable = false;
	key.mipFilt = false;
	key.mipFilt = false;
	key.sClamp = gstate.isTexCoordClampedS();
	key.tClamp = gstate.isTexCoordClampedT();
	key.aniso = false;
	key.texture3d = false;

	// Filtering overrides from replacements or settings.
	switch ((TextureFiltering)g_Config.iTexFiltering) {
	case TEX_FILTER_AUTO:
	case TEX_FILTER_AUTO_MAX_QUALITY:
		if (pixelMapped) {
			key.magFilt = false;
			key.minFilt = false;
		}
		break;
	case TEX_FILTER_FORCE_LINEAR:
		key.magFilt = true;
		key.minFilt = true;
		break;
	case TEX_FILTER_FORCE_NEAREST:
		key.magFilt = false;
		key.minFilt = false;
		break;
	}

	// Kill any mipmapping settings.
	key.aniso = 0.0f;
	key.maxLevel = 0.0f;
	key.lodBias = 0.0f;

	// Often the framebuffer will not match the texture size. We'll wrap/clamp in the shader in that case.
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (w != bufferWidth || h != bufferHeight) {
		// Actually, maybe we should always do this?
		key.sClamp = true;
		key.tClamp = true;
	}
	return key;
}

static u32 ComputeTextureHash(TextureReplacer &replacer, u32 addr, int bufw, int w, int h, bool swizzled, const TexCacheEntry *entry) {
	const GETextureFormat format = entry->format;
	if (replacer.Enabled()) {
		return replacer.ComputeHash(addr, bufw, w, h, swizzled, format, entry->maxSeenV);
	}

	if (h == 512 && entry->maxSeenV < 512 && entry->maxSeenV != 0) {
		h = (int)entry->maxSeenV;
	}

	u32 sizeInRAM;
	if (swizzled) {
		// In swizzle mode, textures are stored in rectangular blocks with the height 8.
		// That means that for a 64x4 texture, like in issue #9308, we would only hash half of the texture!
		// In theory, we should make sure to only hash half of each block, but in reality it's not likely that
		// games are using that memory for anything else. So we'll just make sure to compute the full size to hash.
		// To do that, we just use the same calculation but round the height upwards to the nearest multiple of 8.
		sizeInRAM = (textureBitsPerPixel[format] * bufw * ((h + 7) & ~7)) >> 3;
	} else {
		sizeInRAM = (textureBitsPerPixel[format] * bufw * h) >> 3;
	}
	const u32 *checkp = (const u32 *)Memory::GetPointer(addr);

	if (Memory::IsValidAddress(addr + sizeInRAM)) {
		gpuStats.perFrame.numTextureDataBytesHashed += sizeInRAM;

		// return XXH64(checkp, sizeInRAM, 0xBACD7814);
		return StableQuickTexHash(checkp, sizeInRAM);
	} else {
		return 0;
	}
}

// NOTE: this is overridden in TextureCacheGLES, with some extra color conversion.
void TextureCacheCommon::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
	const u32 clutBaseBytes = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutBase * sizeof(u32)) : (clutBase * sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	//
	// TODO: Actually, this seems like a hack.  The game can upload part of a CLUT and reference other data.
	// clutTotalBytes_ is the last amount uploaded.  We should hash clutMaxBytes_, but this will often hash
	// unrelated old entries for small palettes.
	// Adding clutBaseBytes may just be mitigating this for some usage patterns.
	const u32 clutExtendedBytes = std::min(clutTotalBytes_ + clutBaseBytes, clutMaxBytes_);

	if (replacer_.Enabled()) {
		// Replacer is still hardcoded to use XXH32 for the CLUT hash, so we need to keep that. It's not a bad one
		// but XXH3_64bits should be faster (?).
		clutHash_ = XXH32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);
	} else {
		clutHash_ = XXH3_64bits((const char *)clutBufRaw_, clutExtendedBytes) & 0xFFFFFFFF;
	}
	clutBuf_ = clutBufRaw_;

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	bool alphaLinear = false;
	u16 alphaLinearColor = 0;
	if (clutFormat == GE_CMODE_16BIT_ABGR4444 && clutIndexIsSimple) {
		const u16 *clut = (const u16 *)(clutBuf_);
		alphaLinear = true;
		alphaLinearColor = clut[15] & 0x0FFF;
		for (int i = 0; i < 16; ++i) {
			const u16 step = alphaLinearColor | (i << 12);
			if (clut[i] != step) {
				alphaLinear = false;
				break;
			}
		}
	}

	clutProperties_.clutAlphaLinear = alphaLinear;
	clutProperties_.clutAlphaLinearColor = alphaLinearColor;

	clutLastFormat_ = gstate.clutformat;
}

// TODO: This should use information from the through mode bbox check.
// TODO: Also we should feed this information in so we have it during creation, if at all possible.
void TextureCacheCommon::UpdateMaxSeenV(TexCacheEntry *entry, bool throughMode) {
	// If the texture is >= 512 pixels tall... otherwise we don't bother.
	if (entry->dim < 0x900) {
		return;
	}

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

TextureApplyResult TextureCacheCommon::ApplyTexture(bool doBind) {
	u8 level = 0;
	if (IsFakeMipmapChange()) {
		level = std::max(0, gstate.getTexLevelOffset16() / 16);
	}
	const u32 texaddr = gstate.getTextureAddress(level);
	if (!Memory::IsValidAddress(texaddr)) {
		// Bind a null texture and return.
		Unbind();
		gstate_c.SetTextureIsVideo(false);
		gstate_c.SetTextureIs3D(false);
		gstate_c.SetTextureIsArray(false);
		gstate_c.SetTextureIsFramebuffer(false);
		gstate_c.SetShaderDepal(ShaderDepalMode::OFF);
		return TextureApplyResult();
	}

	const u16 dim = gstate.getTextureDimension(level);
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	GETextureFormat texFormat = gstate.getTextureFormat();
	if (texFormat >= 11) {
		// TODO: Better assumption? Doesn't really matter, these are invalid.
		texFormat = GE_TFMT_5650;
	}

	bool hasClutGPU = false;
	bool clutInShader = false;
	u32 cluthash;
	if (gstate.isTextureFormatIndexed()) {
		// Check if we should use dynamic CLUT in shader or some other tricks.
		if (PSP_CoreParameter().compat.flags().TextureCLUTInShader && !replacer_.Enabled() && (texFormat == GE_TFMT_CLUT8 || texFormat == GE_TFMT_CLUT4)) {
			if (clutLastFormat_ != gstate.clutformat) {
				// We update here because the clut format can be specified after the load.
				// TODO: Unify this as far as possible (I think only GLES backend really needs its own implementation due to different component order).
				UpdateCurrentClut(gstate.getClutPaletteFormat(), gstate.getClutIndexStartPos(), gstate.isClutIndexSimple());
			}
			// We computed clutHash_ (with underscore) previously. But cluthash we set to 0, so the cache will collapse all the textures with various palettes.
			cluthash = 0;
			clutInShader = true;
		} else if (clutRenderAddress_ != 0xFFFFFFFF) {
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
			// Ugh, this xors in the control bits of clutformat... silly.
			cluthash = clutHash_ ^ gstate.clutformat;
		}
	} else {
		cluthash = 0;
	}
	const u64 cachekey = TexCacheEntry::CacheKey(texaddr, texFormat, dim, cluthash);

	int bufw = GetTextureBufw(0, texaddr, texFormat);
	u8 maxLevel = gstate.getTextureMaxLevel();
	const bool swizzled = gstate.isTextureSwizzled();

	TexCache::iterator entryIter = cache_.find(cachekey);

	// Note: It's necessary to reset needshadertexclamp, for otherwise DIRTY_TEXCLAMP won't get set later.
	// Should probably revisit how this works..
	gstate_c.SetNeedShaderTexclamp(false);
	gstate_c.skipDrawReason &= ~SKIPDRAW_BAD_FB_TEXTURE;

	if (entryIter != cache_.end()) {
		TexCacheEntry *entry = entryIter->second.get();

		// Validate the texture still matches the cache entry.
		bool match = entry->MatchesProperties(dim, texFormat, maxLevel);
		const char *reason = "(matches)";
		if (match == false) {
			reason = "different params";
		}

		// Check for dynamic CLUT status
		if (((entry->status & TexStatus::CLUT_GPU) != 0) != hasClutGPU) {
			// Need to recreate, suddenly a CLUT GPU texture was used without it, or vice versa.
			// I think this can only happen on a clut hash collision with the marker value, so highly unlikely.
			match = false;
			reason = "clutgpu";
		}

		// Check for FBO changes.
		if (entry->status & TexStatus::FRAMEBUFFER_OVERLAP) {
			// Fall through to the end where we'll delete the entry if there's a framebuffer.
			entry->status &= ~TexStatus::FRAMEBUFFER_OVERLAP;
			match = false;
			reason = "fboverlap";
		}

		if (match && (entry->status & TexStatus::TO_SCALE) && (standardScaleFactor_ > 1 || shaderScaleFactor_ > 1) && texelsScaledThisFrame_ < TEXCACHE_MAX_TEXELS_SCALED) {
			DEBUG_LOG(Log::TexReplacement, "%08x: Reloading texture to do the scaling we skipped before", texaddr);
			match = false;
			reason = "scaling";
		}

		if (match && (entry->status & TexStatus::TO_REPLACE) && replacementTimeThisFrame_ < replacementFrameBudgetSeconds_) {
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
					DEBUG_LOG(Log::TexReplacement, "No replacement for texture %dx%d", w0, h0);
					entry->status &= ~TexStatus::TO_REPLACE;
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
			// !!! Here we will do the check for "sync domain"
			// NOTE: Reliable is just for the font texture now.
			bool rehash = (entry->status & TexStatus::RELIABLE) == 0;

			if (entry->lastSyncDomain == gstate_c.textureSyncTimeDomain) {
				// The texture was last used in the same sync domain.
				rehash = false;
			} else {
				entry->lastSyncDomain = gstate_c.textureSyncTimeDomain;
				// We don't set rehash = true here, the declaration above did it properly.
			}

			// CLUT recheck was triggered by a "CLUT twin" (same address, different CLUT) needing a rehash.
			if (entry->status & TexStatus::HASH_RECHECK) {
				// If it's reliable, we can skip the sync domain check, since it won't change under us.
				rehash = true;
				entry->status &= ~(TexStatus::HASH_RECHECK);
			}

			if (entry->status & TexStatus::VIDEO) {
				// Video textures are always rehashed, since they change every frame.
				// Actually we don't really need to bother re-hashing, we just have to force-recreate it... Well,
				// let's fix that later.
				rehash = true;
			}

			// got one!
			VERBOSE_LOG(Log::TexCache, "%08x: Texture found in cache. Rehash flag = %d", texaddr, (int)rehash);

			gstate_c.curTextureWidth = w;
			gstate_c.curTextureHeight = h;

			if (rehash) {
				// Update in case any of these changed.
				entry->bufw = bufw;
				if (entry->cluthash != cluthash) {
					WARN_LOG(Log::TexCache, "Mysterious cluthash miss: %08x vs %08x", entry->cluthash, cluthash);
				}
				const bool isVideo = IsVideo(entry->addr);
				if (isVideo) {
					entry->status |= TexStatus::VIDEO;
				} else {
					entry->status &= ~TexStatus::VIDEO;
				}

				// OK, time to check the contentshash if needed. This might lead to us throwing it in the backoff cache.
				PROFILE_THIS_SCOPE("texhash");
				// Update the hash on the texture.
				// int w = gstate.getTextureWidth(0);
				// int h = gstate.getTextureHeight(0);
				_dbg_assert_(w == gstate.getTextureWidth(0));
				_dbg_assert_(h == gstate.getTextureHeight(0));
				_dbg_assert_(entry->addr == texaddr);
				UpdateMaxSeenV(entry, gstate.isModeThrough());
				const u32 newFullHash = ComputeTextureHash(replacer_, entry->addr, entry->bufw, w, h, swizzled, entry);
				if (newFullHash != entry->fullhash) {
					// The texture changed. Throw it in the secondary cache. Then we'll create a new entry later.
					gpuStats.perFrame.numTexturesChanged++;
					if (!isVideo) {
						DEBUG_LOG(Log::TexCache, "%08x: Texture hash not matching: old %08x vs new %08x, reloading (%s) (w=%d h=%d maxSeenV=%d)", entry->addr, entry->fullhash, newFullHash, reason, w, h, entry->maxSeenV);
					}
					// Mark any textures with the same address but different clut.  They need rechecking. Though, I think this is actually
					// not needed anymore with the sync domains... Anyway.
					if (entry->cluthash != 0) {
						const u64 cachekeyMin = (u64)(entry->addr & 0x3FFFFFFF) << 32;
						const u64 cachekeyMax = cachekeyMin + (1ULL << 32);
						for (auto it = cache_.lower_bound(cachekeyMin), end = cache_.upper_bound(cachekeyMax); it != end; ++it) {
							if (it->second->cluthash != entry->cluthash) {
								it->second->status |= TexStatus::HASH_RECHECK;
							}
						}
					}

					// Release the texture from the cache entry, since we're about to throw it away.
					entryIter->second.release();

					const u64 secondKeyOld = (u64)entry->fullhash | ((u64)cluthash << 32);

					// Start by throwing the old entry into the secondary cache - but only if not video.
					// If it's video, just drop it.
					if (!isVideo) {
						TexCache::iterator secondIterOld = secondCache_.find(secondKeyOld);
						if (secondIterOld == secondCache_.end()) {
							DEBUG_LOG(Log::TexCache, "%08x: Hash changed from old %08x to new %08x, moving old entry to secondary cache.", texaddr, entry->fullhash, newFullHash);
							// Not yet in the secondary, put it there.
							secondCache_[secondKeyOld].reset(entry);
							// Forget the entry.
							entry = nullptr;
						} else {
							// This is expected with video, multiple black frames for example. However, shouldn't really happen much with
							// other stuff like Gran Turismo or Gods Eater font rendering unless there are hash collisions..
							DEBUG_LOG(Log::TexCache, "%08x: Second cache already had one with hash %08x (tex has addr %08x)!", texaddr, entry->fullhash, entry->addr);
							// Just release the old entry, drop it on the ground.
							ReleaseTexture(entry, true);
							entry = nullptr;
						}
					} else {
						// Just release the old video entry, drop it on the ground.
						VERBOSE_LOG(Log::TexCache, "%08x: Dropping old invalidated video image", texaddr);
						ReleaseTexture(entry, true);
						entry = nullptr;
					}

					// We have a new hash: look for that hash in the secondary cache.
					const u64 secondKeyNew = (u64)newFullHash | ((u64)cluthash << 32);

					// Now, look for the new hash in the secondary cache.
					// If we find it, we can use that instead of building a new texture. We then pull it out from
					// the secondary cache and move it to the main cache.
					TexCache::iterator secondIterNew = secondCache_.find(secondKeyNew);
					if (secondIterNew != secondCache_.end()) {
						// Found it, but does it match our current params?  If not, abort.
						if (secondIterNew->second->MatchesProperties(dim, texFormat, maxLevel)) {
							// We got a match in the secondary cache that we can use.
							// So we take it out of the secondary cache and move it back into the primary cache,
							// updating the address as appropriate.
							TexCacheEntry *secondEntry = secondIterNew->second.release();
							secondCache_.erase(secondIterNew);
							entryIter->second.reset(secondEntry);  // Here we move it into the main cache.
							entry = secondEntry;
							// Make sure the address is correct.
							if (entry->addr != texaddr) {
								DEBUG_LOG(Log::TexCache, "%08x: Texture matched with secondary cache entry with hash %08x at other address %08x. Updating address.", texaddr, newFullHash, entry->addr);
								entry->addr = texaddr;
							}
							return ApplyTextureFinish(entry, doBind);
						} else {
							DEBUG_LOG(Log::TexCache, "%08x: Entry in secondary cache not suitable, ignoring and creating new: %08x", texaddr, newFullHash);
							// The entry in the secondary cache doesn't match our current parameters, so we can't use it.
							// Let's leave it in there for now (revisit later).
						}
					} else {
						if (!isVideo) {
							// This is expected with video, multiple black frames for example. However, shouldn't really happen much with
							// other stuff like Gran Turismo or Gods Eater font rendering unless there are hash collisions..
							DEBUG_LOG(Log::TexCache, "%08x: No entry for hash %08x in secondary cache, creating new in main cache.", texaddr, newFullHash);
						}
						// Well, not found, so we need to create a new entry.
						cache_.erase(entryIter);
					}
					entry = nullptr;
					entryIter = cache_.end();
				} else {
					// The texture didn't change after checking the hash, so we can just use it as-is.
					return ApplyTextureFinish(entry, doBind);
				}
			} else {
				// rehash was false, so let's just use the texture.
				return ApplyTextureFinish(entry, doBind);
			}
		} else {
			DEBUG_LOG(Log::TexCache, "%08x: Texture was not a match (%s), recreating.", texaddr, reason);
			// Wasn't a match even in format. Let's just delete it right away, since we know we need to rebuild it,
			// and it's unlikely that putting it in the secondary cache will do us any good. We do that for things we rehash, though.
			ReleaseTexture(entryIter->second.get(), true);
			cache_.erase(entryIter);
			entryIter = cache_.end();  // make sure we don't look at it again.
			entry = nullptr;
		}
		// If we fall out here, the iterator must be gone and entry must be nullptr.
		_dbg_assert_(entry == nullptr);
		_dbg_assert_(entryIter == cache_.end());
	}

	// No texture found when looking up the key, or changed (depending on entry).
	// Check for framebuffers.

	TextureDefinition def{};
	def.addr = texaddr;
	def.dim = dim;
	def.format = texFormat;
	def.bufw = bufw;

	AttachCandidate bestCandidate;
	if (GetBestFramebufferCandidate(framebufferManager_, def, 0, &bestCandidate, "texture")) {
		RasterChannel channel;
		VirtualFramebuffer *framebuffer = SetTextureFramebuffer(bestCandidate, &channel);  // sets curTexture3D
		TextureApplyResult result;
		// Maybe we bound a framebuffer?
		ForgetLastTexture();
		if (framebuffer) {
			// ApplyTextureFramebuffer is responsible for setting SetTextureFullAlpha.
			ApplyTextureFramebuffer(framebuffer, gstate.getTextureFormat(), channel);
			result.framebuffer = framebuffer;
			result.framebufferTextureChannel = channel;
			framebuffer = nullptr;
			gstate_c.SetTextureIsArray(true);
		} else {
			// Backends should handle this by binding a black texture with 0 alpha.
			BindTexture(nullptr);
			gstate_c.SetTextureIsArray(false);
		}
		gstate_c.SetTextureIsVideo(false);
		gstate_c.SetTextureIs3D(false);
		gstate_c.SetShaderDepal(ShaderDepalMode::OFF);
		return result;
	}

	// Didn't match a framebuffer, keep going and create a brand new texture.

	TexCacheEntry *entry = new TexCacheEntry{};
	cache_[cachekey].reset(entry);
	entry->status = {};
	if (PPGeIsFontTextureAddress(texaddr)) {
		// It's the builtin font texture.
		entry->status |= TexStatus::RELIABLE;
	}

	if (hasClutGPU) {
		WARN_LOG_N_TIMES(clutUseRender, 5, Log::TexCache, "Using texture with dynamic CLUT: texfmt=%d, clutfmt=%d", gstate.getTextureFormat(), gstate.getClutPaletteFormat());
		entry->status |= TexStatus::CLUT_GPU | TexStatus::CLUT8_INDEXED;
	}

	if (gstate.isTextureFormatIndexed() && clutRenderAddress_ == 0xFFFFFFFF) {
		const u64 cachekeyMin = (u64)(texaddr & 0x3FFFFFFF) << 32;
		const u64 cachekeyMax = cachekeyMin + (1ULL << 32);

		int found = 0;
		for (auto it = cache_.lower_bound(cachekeyMin), end = cache_.upper_bound(cachekeyMax); it != end; ++it) {
			found++;
		}

		if (found >= TEXTURE_CLUT_VARIANTS_MIN) {
			for (auto it = cache_.lower_bound(cachekeyMin), end = cache_.upper_bound(cachekeyMax); it != end; ++it) {
				it->second->status |= TexStatus::MANY_CLUT_VARIANTS;
			}

			entry->status |= TexStatus::MANY_CLUT_VARIANTS;
		}
	}

	// We have to decode it, let's setup the cache entry first.
	entry->addr = texaddr;
	entry->dim = dim;
	entry->format = texFormat;
	entry->maxLevel = maxLevel;
	entry->bufw = bufw;
	entry->cluthash = cluthash;
	if (clutInShader) {
		entry->status |= TexStatus::CLUT8_INDEXED;
	}
	if (IsVideo(entry->addr)) {
		entry->status |= TexStatus::VIDEO;
	}

	gstate_c.curTextureWidth = w;
	gstate_c.curTextureHeight = h;
	UpdateMaxSeenV(entry, gstate.isModeThrough());  // Critical to update this before hashing! As it's used to decide the hash range.
	entry->fullhash = ComputeTextureHash(replacer_, entry->addr, entry->bufw, w, h, swizzled, entry);

	DEBUG_LOG(Log::TexCache, "%08x: Creating new texture, hash %08x (maxSeenV=%d), w: %d h: %d, creating", texaddr, entry->fullhash, entry->maxSeenV, w, h);

	BuildTexture(entry);
	ForgetLastTexture();  // is this needed?
	return ApplyTextureFinish(entry, doBind);
}

TextureApplyResult TextureCacheCommon::ApplyTextureFinish(TexCacheEntry *entry, bool doBind) {
	_dbg_assert_(entry);

	gstate_c.SetTextureIsVideo((entry->status & TexStatus::VIDEO) != 0);
	gstate_c.SetTextureIsArray(false);  // Ordinary 2D textures still aren't used by array view in VK. We probably might as well, though, at this point..
	gstate_c.SetTextureIsFramebuffer(false);
	entry->lastFrame = gpuStats.totals.numFlips;
	if (entry->status & TexStatus::CLUT_GPU) {
		_dbg_assert_(entry->status & TexStatus::CLUT8_INDEXED);
		// Special process.
		if (doBind) {
			ApplyTextureDepalFramebufferCLUT(entry);
		}
		gstate_c.SetTextureSolidAlpha(false);
		gstate_c.SetTextureIs3D(false);
		return TextureApplyResult{};
	} else {
		if (doBind) {
			BindTexture(entry);
		}
		gstate_c.SetTextureSolidAlpha((entry->status & TexStatus::ALPHA_SOLID) != 0);
		gstate_c.SetTextureIs3D((entry->status & TexStatus::IS_3D) != 0);
		if (entry->status & TexStatus::CLUT8_INDEXED) {
			bool smoothedDepal = false;
			u32 depthUpperBits = 0;
			ClutTexture clutTexture = clutTextureCache_.GetClutTexture(gstate.getClutPaletteFormat(), clutHash_, clutBuf_);
			BindAsClutTexture(clutTexture.texture, false);
			gstate_c.SetShaderDepal(ShaderDepalMode::NORMAL, GE_FORMAT_CLUT8);
		} else {
			gstate_c.SetShaderDepal(ShaderDepalMode::OFF, GE_FORMAT_INVALID);
		}
	}
	return TextureApplyResult{entry, nullptr};
}

static bool GetBestFramebufferCandidate(FramebufferManagerCommon *fbManager, const TextureDefinition &entry, u32 texAddrOffset, AttachCandidate *bestCandidate, const char *context) {
	gpuStats.perFrame.numFramebufferEvaluations++;

	TinySet<AttachCandidate, 6> candidates;

	const std::vector<VirtualFramebuffer *> &framebuffers = fbManager->Framebuffers();

	const bool useBufferedRendering = fbManager->UseBufferedRendering();

	for (VirtualFramebuffer *framebuffer : framebuffers) {
		FramebufferMatchInfo match{};
		if (MatchFramebuffer(entry, framebuffer, texAddrOffset, RASTER_COLOR, useBufferedRendering, &match)) {
			candidates.push_back(AttachCandidate{ framebuffer, match, RASTER_COLOR });
		}
		match = {};
		if (MatchFramebuffer(entry, framebuffer, texAddrOffset, RASTER_DEPTH, useBufferedRendering, &match)) {
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

		if (candidate.fb->fb_address == entry.addr && PSP_CoreParameter().compat.flags().BoostExactFramebufferMatch) {
			// Perfect match, prefer this one heavily. Works around an overlapping framebuffer problem in Tales of Phantasia X: #21162
			relevancy += 3;
		}

		if (candidate.match.xOffset != 0 && PSP_CoreParameter().compat.flags().DisallowFramebufferAtOffset) {
			continue;
		}

		// Avoid binding as texture the framebuffer we're rendering to.
		// In Killzone, we split the framebuffer but the matching algorithm can still pick the wrong one,
		// which this avoids completely.
		if (kzCompat && candidate.fb == fbManager->GetCurrentRenderVFB()) {
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

		WARN_LOG(Log::TexCache, "GetBestFramebufferCandidate(%s): Multiple (%d) candidate framebuffers. texaddr: %08x offset: %d (%dx%d stride %d, %s):\n%s",
			context,
			(int)candidates.size(),
			entry.addr, texAddrOffset, dimWidth(entry.dim), dimHeight(entry.dim), entry.bufw, GeTextureFormatToString(entry.format),
			cands.c_str()
		);
		logging = true;
	}

	if (bestIndex != -1) {
		if (logging) {
			WARN_LOG(Log::TexCache, "Chose candidate %d:\n%s", (int)bestIndex, candidates[bestIndex].ToString().c_str());
		}
		*bestCandidate = candidates[bestIndex];
		return true;
	} else {
		return false;
	}
}

// Removes old textures.
void TextureCacheCommon::Decimate(const TexCacheEntry *const exceptThisOne, bool forcePressure) {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = TEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	// Compute the cache size.
	s64 cacheSizeEstimate = CacheSizeEstimate();
	if (forcePressure || cacheSizeEstimate >= TEXCACHE_MIN_PRESSURE) {
		const u32 had = cacheSizeEstimate;

		ForgetLastTexture();
		for (TexCache::iterator iter = cache_.begin(); iter != cache_.end(); ) {
			if (iter->second.get() == exceptThisOne) {
				++iter;
				continue;
			}
			bool hasClutVariants = (iter->second->status & TexStatus::MANY_CLUT_VARIANTS) != 0;
			int killAge = hasClutVariants ? TEXTURE_KILL_AGE_CLUT : TEXTURE_KILL_AGE;
			if (iter->second->lastFrame + killAge < gpuStats.totals.numFlips) {
				DEBUG_LOG(Log::TexCache, "Decimating cached texture at %08x (hash: %08x)", iter->second->addr, iter->second->fullhash);
				cacheSizeEstimate -= iter->second->EstimateTexMemoryUsage();
				ReleaseTexture(iter->second.get(), true);
				iter = cache_.erase(iter);
			} else {
				++iter;
			}
		}

		if (had != cacheSizeEstimate) {
			DEBUG_LOG(Log::TexCache, "Decimated texture cache, saved %d estimated bytes - now %d bytes", (int)(had - cacheSizeEstimate), (int)cacheSizeEstimate);
		}
	}

	s64 secondCacheSizeEstimate = SecondCacheSizeEstimate();
	// If enabled, we also need to clear the secondary cache.
	if (forcePressure || secondCacheSizeEstimate >= TEXCACHE_SECOND_MIN_PRESSURE) {
		const u32 had = secondCacheSizeEstimate;

		for (TexCache::iterator iter = secondCache_.begin(); iter != secondCache_.end(); ) {
			if (iter->second.get() == exceptThisOne) {
				++iter;
				continue;
			}
			if (iter->second->lastFrame + TEXTURE_SECOND_KILL_AGE < gpuStats.totals.numFlips) {
				DEBUG_LOG(Log::TexCache, "Decimating second-cache texture at %08x (hash: %08x)", iter->second->addr, iter->second->fullhash);
				ReleaseTexture(iter->second.get(), true);
				secondCacheSizeEstimate -= iter->second->EstimateTexMemoryUsage();
				iter = secondCache_.erase(iter);
			} else {
				++iter;
			}
		}

		if (had != secondCacheSizeEstimate) {
			DEBUG_LOG(Log::TexCache, "Decimated second texture cache, saved %d estimated bytes - now %d bytes", (int)(had - secondCacheSizeEstimate), (int)secondCacheSizeEstimate);
		}
	}

	// Decimate known videos (so the list doesn't grow unboundedly or we start to misidentify textures as video).
	for (auto iter = videos_.begin(); iter != videos_.end(); ) {
		if (iter->flips + VIDEO_DECIMATE_AGE < gpuStats.totals.numFlips) {
			iter = videos_.erase(iter);
		} else {
			++iter;
		}
	}

	replacer_.Decimate(forcePressure ? ReplacerDecimateMode::FORCE_PRESSURE : ReplacerDecimateMode::NEW_FRAME);
}

bool TextureCacheCommon::IsVideo(u32 texaddr) const {
	texaddr &= 0x3FFFFFFF;
	for (const VideoInfo &info : videos_) {
		if (texaddr >= info.addr && texaddr < info.addr + info.size) {
			return true;
		}
	}
	return false;
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
			it->second->status |= TexStatus::FRAMEBUFFER_OVERLAP;
			gpuStats.perFrame.numTextureInvalidationsByFramebuffer++;
		}

		if (z_stride != 0) {
			// Depth. Just look at the range, but in each mirror (0x04200000 and 0x04600000).
			// Games don't use 0x04400000 as far as I know - it has no swizzle effect so kinda useless.
			cacheKey = (u64)z_addr << 32;
			cacheKeyEnd = (u64)z_endAddr << 32;
			for (auto it = cache_.lower_bound(cacheKey | 0x200000), end = cache_.upper_bound(cacheKeyEnd | 0x200000); it != end; ++it) {
				it->second->status |= TexStatus::FRAMEBUFFER_OVERLAP;
				gpuStats.perFrame.numTextureInvalidationsByFramebuffer++;
			}
			for (auto it = cache_.lower_bound(cacheKey | 0x600000), end = cache_.upper_bound(cacheKeyEnd | 0x600000); it != end; ++it) {
				it->second->status |= TexStatus::FRAMEBUFFER_OVERLAP;
				gpuStats.perFrame.numTextureInvalidationsByFramebuffer++;
			}
		}
		break;
	}
	default:
		break;
	}
}

static bool MatchFramebuffer(const TextureDefinition &entry,
	VirtualFramebuffer *framebuffer, u32 texaddrOffset, RasterChannel channel, bool useBufferedRendering, FramebufferMatchInfo *matchInfo) {
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
	} else if (!useBufferedRendering) {
		return false;
	} else {
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

		if (matchInfo->yOffset >= 512 && !PSP_CoreParameter().compat.flags().AllowLargeFBTextureOffsets) {
			// Reject unreasonably large y offsets. 512 is the largest texture size.
			return false;
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
			if (channel == RasterChannel::RASTER_DEPTH && framebuffer->last_frame_depth_updated < 0) {
				// Reject depth textures that have not been rendered to. See #15828
				// We're right before the final check here, so we can bail.
				return false;
			} else {
				if (!noOffset) {
					WARN_LOG_ONCE(subareaClut, Log::G3D, "Matching framebuffer (%s) using %s with offset at %08x +%dx%d", RasterChannelToString(channel), GeTextureFormatToString(entry.format), fb_address, matchInfo->xOffset, matchInfo->yOffset);
				}
				return true;
			}
		} else if (IsClutFormat((GETextureFormat)(entry.format))) {
			WARN_LOG_ONCE(nomatch_clut, Log::G3D, "%s texture format not matching framebuffer of format %s at %08x/%d", GeTextureFormatToString(entry.format), GeBufferFormatToString(fb_format), fb_address, fb_stride);
			// Seen in Silent Hill: Shattered Memories (#6265).
			if (entry.format == GE_TFMT_CLUT32 && fb_format != GE_FORMAT_8888) {
				matchInfo->reinterpret = true;
				matchInfo->reinterpretTo = GE_FORMAT_8888;
				return true;
			}
			return false;
		} else if (IsDXTFormat((GETextureFormat)(entry.format))) {
			WARN_LOG_ONCE(nomatch_dxt, Log::G3D, "%s texture format (DXT!) not matching framebuffer of format %s at %08x/%d", GeTextureFormatToString(entry.format), GeBufferFormatToString(fb_format), fb_address, fb_stride);
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

VirtualFramebuffer *TextureCacheCommon::SetTextureFramebuffer(const AttachCandidate &candidate, RasterChannel *framebufferTextureChannel) {
	VirtualFramebuffer *framebuffer = candidate.fb;
	RasterChannel channel = candidate.channel;

	if (candidate.match.reinterpret) {
		framebuffer = framebufferManager_->ResolveFramebufferColorToFormat(candidate.fb, candidate.match.reinterpretTo);
	}

	_dbg_assert_msg_(framebuffer != nullptr, "Framebuffer must not be null.");

	framebuffer->usageFlags |= FB_USAGE_TEXTURE;
	// Keep the framebuffer alive.
	framebuffer->last_frame_used = gpuStats.totals.numFlips;
	*framebufferTextureChannel = RASTER_COLOR;

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
			texWidth = RoundToNextPowerOf2(texWidth);
		}

		gstate_c.curTextureWidth = texWidth;
		gstate_c.curTextureHeight = texHeight;
		gstate_c.SetTextureIsFramebuffer(true);

		if ((gstate_c.curTextureXOffset == 0) != (fbInfo.xOffset == 0) || (gstate_c.curTextureYOffset == 0) != (fbInfo.yOffset == 0)) {
			// Hm, this seems a bit iffy.
			gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}

		gstate_c.curTextureXOffset = fbInfo.xOffset;
		gstate_c.curTextureYOffset = fbInfo.yOffset;
		u32 texW = (u32)gstate.getTextureWidth(0);
		u32 texH = (u32)gstate.getTextureHeight(0);
		const bool needShaderTexClamp = gstate_c.curTextureWidth != texW || gstate_c.curTextureHeight != texH || gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0;
		gstate_c.SetNeedShaderTexclamp(needShaderTexClamp);
		if (channel == RASTER_DEPTH) {
			framebuffer->usageFlags |= FB_USAGE_COLOR_MIXED_DEPTH;
		}

		if (channel == RASTER_DEPTH && !gstate_c.Use(GPU_USE_DEPTH_TEXTURE)) {
			WARN_LOG_ONCE(ndepthtex, Log::G3D, "Depth textures not supported, not binding");
			// Flag to bind a null texture if we can't support depth textures.
			// Should only happen on old OpenGL.
			framebuffer = nullptr;
		} else {
			*framebufferTextureChannel = channel;
		}
	} else {
		if (framebuffer->fbo) {
			framebuffer->fbo->Release();
			framebuffer->fbo = nullptr;
		}
		Unbind();
		gstate_c.SetNeedShaderTexclamp(false);
		framebuffer = nullptr;
	}
	return framebuffer;
}

bool TextureCacheCommon::GetFramebufferTextureDebug(const VirtualFramebuffer *vfb, RasterChannel channel, GPUDebugBuffer &buffer) {
	u8 sf = vfb->renderScaleFactor;
	int x = gstate_c.curTextureXOffset * sf;
	int y = gstate_c.curTextureYOffset * sf;
	int desiredW = gstate.getTextureWidth(0) * sf;
	int desiredH = gstate.getTextureHeight(0) * sf;
	int w = std::min(desiredW, vfb->bufferWidth * sf - x);
	int h = std::min(desiredH, vfb->bufferHeight * sf - y);

	bool retval;
	if (channel == RASTER_DEPTH) {
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

	// Just in case, small display with auto resolution or something.
	if (scaleFactor <= 0) {
		scaleFactor = 1;
	}

	standardScaleFactor_ = scaleFactor;

	replacer_.NotifyConfigChanged();
}

void TextureCacheCommon::NotifyWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat fmt) {
	addr &= 0x3FFFFFFF;
	videos_.push_back({ addr, (u32)size, gpuStats.totals.numFlips });
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
				bool okAge = !PSP_CoreParameter().compat.flags().LoadCLUTFromCurrentFrameOnly || framebuffer->last_frame_render == gpuStats.totals.numFlips;  // Here we can try heuristics.
				if (matchRange && !inMargin && offset < (int)clutRenderOffset_) {
					if (okAge) {
						WARN_LOG_N_TIMES(clutfb, 5, Log::G3D, "Detected LoadCLUT(%d bytes) from framebuffer %08x (%s), last render %d frames ago, byte offset %d, pixel offset %d",
							loadBytes, fb_address, GeBufferFormatToString(framebuffer->fb_format), gpuStats.totals.numFlips - framebuffer->last_frame_render, offset, offset / fb_bpp);
						framebuffer->last_frame_clut = gpuStats.totals.numFlips;
						// Also mark used so it's not decimated.
						framebuffer->last_frame_used = gpuStats.totals.numFlips;
						framebuffer->usageFlags |= FB_USAGE_CLUT;
						bestClutAddress = framebuffer->fb_address;
						clutRenderOffset_ = (u32)offset;
						chosenFramebuffer = framebuffer;
						if (offset == 0) {
							// Not gonna find a better match according to the smallest-offset rule, so we'll go with this one.
							break;
						}
					} else {
						WARN_LOG(Log::TexCache, "Ignoring CLUT load from %d frames old buffer at %08x", gpuStats.totals.numFlips - framebuffer->last_frame_render, fb_address);
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
	u32 bytes = Memory::ClampValidSizeAt(clutAddr, loadBytes);
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
u32 TexCacheEntry::EstimateTexMemoryUsage() const {
	// TODO: This does not take into account the HD remaster's larger textures.
	const u8 dimW = ((dim >> 0) & 0xf);
	const u8 dimH = ((dim >> 8) & 0xf);

	u32 pixelSize = 2;
	switch (format) {
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

const size_t TextureCacheCommon::CacheSizeEstimate() const {
	s64 cacheSizeEstimate = 0;
	for (auto &[_ , value] : cache_) {
		cacheSizeEstimate += value->EstimateTexMemoryUsage();
	}
	return cacheSizeEstimate;
}

const size_t TextureCacheCommon::SecondCacheSizeEstimate() const {
	s64 cacheSizeEstimate = 0;
	for (auto &[_, value] : secondCache_) {
		cacheSizeEstimate += value->EstimateTexMemoryUsage();
	}
	return cacheSizeEstimate;
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

	if ((entry->status & TexStatus::VIDEO) && !replacer_.AllowVideo()) {
		return nullptr;
	}

	double replaceStart = time_now_d();
	ReplacementCacheKey cacheKey(entry->CacheKey(), entry->fullhash);
	ReplacedTexture *replaced = replacer_.FindReplacement(cacheKey, *w, *h);
	replacementTimeThisFrame_ += time_now_d() - replaceStart;
	if (!replaced) {
		// TODO: Remove the flag here?
		// entry->status &= ~TexStatus::TO_REPLACE;
		return nullptr;
	}
	entry->replacedTexture = replaced;  // we know it's non-null here.
	PollReplacement(entry, w, h, d);
	return replaced;
}

void TextureCacheCommon::PollReplacement(TexCacheEntry *entry, int *w, int *h, int *d) {
	double waitBudget = replacementFrameBudgetSeconds_ - replacementTimeThisFrame_;
	// Note: Don't avoid the Poll call if budget is 0, we do meaningful things there.
	// Poll also handles negative budgets.

	double replaceStart = time_now_d();

	// Unless the mode is set to Instant (where the user explicitly wants to wait for each texture),
	// it's just a waste of time to wait here really. OK, we might get a texture one frame early but
	// we wasted a lot of time waiting, likely slowing down our framerate.
	if (g_Config.iReplacementTextureLoadSpeed != ReplacementTextureLoadSpeed::INSTANT) {
		waitBudget = 0.0;
	}
	if (entry->replacedTexture->Poll(waitBudget)) {
		if (entry->replacedTexture->State() == ReplacementState::ACTIVE) {
			entry->replacedTexture->GetSize(0, w, h);
			// Consider it already "scaled.".
			entry->status |= TexStatus::IS_SCALED_OR_REPLACED;
		}

		// Remove the flag, even if it was invalid.
		entry->status &= ~TexStatus::TO_REPLACE;
	}
	replacementTimeThisFrame_ += time_now_d() - replaceStart;

	switch (entry->replacedTexture->State()) {
	case ReplacementState::UNLOADED:
	case ReplacementState::PENDING:
		// Make sure we keep polling.
		entry->status |= TexStatus::TO_REPLACE;
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
static TextureAlpha DecodeDXTBlocks(uint8_t *out, int outPitch, uint32_t texaddr, const uint8_t *texptr,
	int w, int h, int bufw, bool reverseColors) {

	int minw = std::min(bufw, w);
	uint32_t *dst = (uint32_t *)out;
	int outPitch32 = outPitch / sizeof(uint32_t);
	const DXTBlock *src = (const DXTBlock *)texptr;

	if (!Memory::IsValidRange(texaddr, ((h + 3) / 4) * (bufw / 4) * sizeof(DXTBlock))) {
		ERROR_LOG_REPORT(Log::G3D, "DXT%d texture extends beyond valid RAM: %08x + %d x %d", n, texaddr, bufw, h);
		uint32_t limited = Memory::ClampValidSizeAt(texaddr, (h / 4) * (bufw / 4) * sizeof(DXTBlock));
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
		return alphaSum == 1 ? TextureAlpha::Solid : TextureAlpha::Any;
	} else {
		// Just report that we don't have full alpha, since these formats are made for that.
		return TextureAlpha::Any;
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

TextureAlpha TextureCacheCommon::DecodeTextureLevel(u8 *out, int outPitch, GETextureFormat format, GEPaletteFormat clutformat, uint32_t texaddr, int level, int bufw, TexDecodeFlags flags) {
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
			return TextureAlpha::Any;
		}

		switch (clutformat) {
		case GE_CMODE_16BIT_BGR5650:
		case GE_CMODE_16BIT_ABGR5551:
		case GE_CMODE_16BIT_ABGR4444:
		{
			// The w > 1 check is to not need a case that handles a single pixel
			// in DeIndexTexture4Optimal<u16>.
			if (clutProperties_.clutAlphaLinear && mipmapShareClut && !expandTo32bit && w >= 4) {
				// We don't bother with fullalpha here (clutAlphaLinear)
				// Here, reverseColors means the CLUT is already reversed.
				if (reverseColors) {
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4Optimal((u16 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clutProperties_.clutAlphaLinearColor);
					}
				} else {
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4OptimalRev((u16 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clutProperties_.clutAlphaLinearColor);
					}
				}
			} else {
				// Need to have the "un-reversed" (raw) CLUT here since we are using a generic conversion function.
				if (expandTo32bit) {
					// We simply expand the CLUT to 32-bit, then we deindex as usual. Probably the fastest way.
					const u16 *clut = (const u16 *)(clutBufRaw_) + clutSharingOffset;
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
					const u16 *clut = (const u16 *)(clutBuf_) + clutSharingOffset;
					fullAlphaMask = ClutFormatToFullAlpha(clutformat, reverseColors);
					for (int y = 0; y < h; ++y) {
						DeIndexTexture4<u16>((u16 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clut, &alphaSum);
					}
				}
			}

			if (clutformat == GE_CMODE_16BIT_BGR5650) {
				// Our formula at the end of the function can't handle this cast so we return early.
				return TextureAlpha::Solid;
			}
		}
		break;

		case GE_CMODE_32BIT_ABGR8888:
		{
			const u32 *clut = (const u32 *)(clutBuf_) + clutSharingOffset;
			fullAlphaMask = 0xFF000000;
			for (int y = 0; y < h; ++y) {
				DeIndexTexture4<u32>((u32 *)(out + outPitch * y), texptr + (bufw * y) / 2, w, clut, &alphaSum);
			}
		}
		break;

		default:
			ERROR_LOG_REPORT(Log::G3D, "Unknown CLUT4 texture mode %d", gstate.getClutPaletteFormat());
			return TextureAlpha::Any;
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
			return TextureAlpha::Any;
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
			return TextureAlpha::Solid;
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

	return AlphaSumIsFull(alphaSum, fullAlphaMask) ? TextureAlpha::Solid : TextureAlpha::Any;
}

TextureAlpha TextureCacheCommon::ReadIndexedTex(u8 *out, int outPitch, int level, const u8 *texptr, int bytesPerIndex, int bufw, bool reverseColors, bool expandTo32Bit) {
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
		return TextureAlpha::Solid;
	} else {
		return AlphaSumIsFull(alphaSum, fullAlphaMask) ? TextureAlpha::Solid : TextureAlpha::Any;
	}
}

void TextureCacheCommon::ApplySampler(const TextureApplyResult &result, bool flatZ, bool pixelMapped) {
	SamplerCacheKey samplerKey;
	if (result.texCacheEntry) {
		int maxLevel = (result.texCacheEntry->status & TexStatus::NO_MIPS) ? 0 : result.texCacheEntry->maxLevel;
		samplerKey = GetSamplingParams(maxLevel, result.texCacheEntry, flatZ, pixelMapped);
	} else if (result.framebuffer) {
		samplerKey = GetFramebufferSamplingParams(gstate, result.framebuffer->bufferWidth, result.framebuffer->bufferHeight, pixelMapped);
	} else {
		samplerKey = GetSamplingParams(0, nullptr, flatZ, pixelMapped);
	}
	ApplySamplerByKey(samplerKey);
}

// Can we depalettize the bufferFormat as texFormat at all?
// This refers to both in-fragment-shader depal and "traditional" depal through a separate pass.
static bool CanDepalettizeBufferAs(GETextureFormat texFormat, GEBufferFormat bufferFormat) {
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
		WARN_LOG(Log::TexCache, "Invalid CLUT/framebuffer combination: %s vs %s", GeTextureFormatToString(texFormat), GeBufferFormatToString(bufferFormat));
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
static bool CanUseSmoothDepal(const GEState &gstate, GEBufferFormat framebufferFormat, const ClutTexture &clutTexture) {
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
	const uint32_t clutMode = gstate.clutformat & 0xFFFFFF;

	const bool depth = channel == RASTER_DEPTH;
	const GEBufferFormat fbFormat = depth ? GE_FORMAT_DEPTH16 : framebuffer->fb_format;
	const bool need_depalettize = CanDepalettizeBufferAs(texFormat, fbFormat);
	const bool selfRender = framebufferManager_->GetCurrentRenderVFB() == framebuffer;

	// Shader depal is not supported during 3D texturing or depth texturing, and requires 32-bit integer instructions in the shader.
	bool useShaderDepal = !selfRender && !depth && clutRenderAddress_ == 0xFFFFFFFF &&
		!gstate_c.curTextureIs3D &&
		draw_->GetShaderLanguageDesc().bitwiseOps &&
		!(texFormat == GE_TFMT_CLUT8 && fbFormat == GE_FORMAT_5551);  // socom

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
			clutTexture = clutTextureCache_.GetClutTexture(clutFormat, clutHash_, clutBufRaw_);
			smoothedDepal = CanUseSmoothDepal(gstate, fbFormat, clutTexture);
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
			_dbg_assert_(!depth);

			// Very icky conflation here of native and thin3d rendering. This will need careful work per backend in BindAsClutTexture.
			BindAsClutTexture(clutTexture.texture, smoothedDepal);

			framebufferManager_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET, Draw::ALL_LAYERS);
			// Vulkan needs to do some extra work here to pick out the native handle from Draw.
			BoundFramebufferTexture();

			SamplerCacheKey samplerKey = GetFramebufferSamplingParams(gstate, framebuffer->bufferWidth, framebuffer->bufferHeight, false);
			samplerKey.magFilt = false;
			samplerKey.minFilt = false;
			samplerKey.mipEnable = false;
			ApplySamplerByKey(samplerKey);

			ShaderDepalMode mode = ShaderDepalMode::NORMAL;
			if (texFormat == GE_TFMT_CLUT8 && fbFormat == GE_FORMAT_8888) {
				mode = ShaderDepalMode::CLUT8_8888;
				smoothedDepal = false;  // just in case
			} else if (smoothedDepal) {
				mode = ShaderDepalMode::SMOOTHED;
			}

			gstate_c.Dirty(DIRTY_DEPAL | DIRTY_FRAGMENTSHADER_STATE);
			gstate_c.SetShaderDepal(mode, fbFormat);

			const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
			const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;
			TextureAlpha alphaStatus = CheckCLUTAlpha((const uint8_t *)clutBufRaw_, clutFormat, clutTotalColors);
			gstate_c.SetTextureSolidAlpha(alphaStatus == TextureAlpha::Solid);

			draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
			return;
		}

		depthUpperBits = (depth && framebuffer->fb_format == GE_FORMAT_8888) ? ((gstate.getTextureAddress(0) & 0x600000) >> 20) : 0;

		textureShader = textureShaderCache_.GetDepalettizeShader(clutMode, texFormat, fbFormat, smoothedDepal, depthUpperBits);
		gstate_c.SetShaderDepal(ShaderDepalMode::OFF);
	}

	if (textureShader) {
		bool needsDepthXSwizzle = depthUpperBits == 2;

		int depalWidth = framebuffer->renderWidth;
		int texWidth = framebuffer->bufferWidth;
		if (needsDepthXSwizzle) {
			texWidth = RoundToNextPowerOf2(framebuffer->bufferWidth);
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
		Draw::SamplerState *nearest = textureShaderCache_.GetSampler(false);
		Draw::SamplerState *clutSampler = textureShaderCache_.GetSampler(smoothedDepal);
		draw_->BindSamplerStates(0, 1, &nearest);
		draw_->BindSamplerStates(1, 1, &clutSampler);

		draw2D_->Blit(textureShader, u1, v1, u2, v2, u1, v1, u2, v2, framebuffer->renderWidth, framebuffer->renderHeight, depalWidth, framebuffer->renderHeight, false, framebuffer->renderScaleFactor);

		gpuStats.perFrame.numDepal++;

		gstate_c.curTextureWidth = texWidth;
		gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

		draw_->BindTexture(0, nullptr);
		framebufferManager_->RebindFramebuffer("ApplyTextureFramebuffer");

		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::Aspect::COLOR_BIT, Draw::ALL_LAYERS);
		BoundFramebufferTexture();

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TextureAlpha alphaStatus = CheckCLUTAlpha((const uint8_t *)clutBufRaw_, clutFormat, clutTotalColors);
		gstate_c.SetTextureSolidAlpha(alphaStatus == TextureAlpha::Solid);

		draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
	} else {
		framebufferManager_->RebindFramebuffer("ApplyTextureFramebuffer");
		framebufferManager_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET, Draw::ALL_LAYERS);
		BoundFramebufferTexture();

		gstate_c.SetShaderDepal(ShaderDepalMode::OFF);
		gstate_c.SetTextureSolidAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
	}

	// Since we've drawn using thin3d, might need these.
	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
}

// Applies depal to a normal (non-framebuffer) texture, pre-decoded to CLUT8 format.
// TODO: Merge this function with the above. Or rather, we should try to eliminate this in favor
// of in-shader depal.
void TextureCacheCommon::ApplyTextureDepalFramebufferCLUT(const TexCacheEntry *const entry) {
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

	Draw2DPipeline *textureShader = textureShaderCache_.GetDepalettizeShader(clutMode, GE_TFMT_CLUT8, GE_FORMAT_CLUT8, false, 0);
	gstate_c.SetShaderDepal(ShaderDepalMode::OFF);

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

	// If it nearly covers, just make it cover. This fixes Ridge Racer where the UVs are slightly off and it causes a bright line on the lens flare.
	if (u1 > 0 && u1 < 3) {
		u1 = 0;
	}
	if (v1 > 0 && v1 < 3) {
		v1 = 0;
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
	Draw::SamplerState *nearest = textureShaderCache_.GetSampler(false);
	Draw::SamplerState *clutSampler = textureShaderCache_.GetSampler(false);
	draw_->BindSamplerStates(0, 1, &nearest);
	draw_->BindSamplerStates(1, 1, &clutSampler);

	draw2D_->Blit(textureShader, u1, v1, u2, v2, u1, v1, u2, v2, texWidth, texHeight, texWidth, texHeight, false, 1);

	gpuStats.perFrame.numDepal++;

	gstate_c.curTextureWidth = texWidth;
	gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

	draw_->BindTexture(0, nullptr);
	framebufferManager_->RebindFramebuffer("ApplyTextureFramebuffer");

	draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::Aspect::COLOR_BIT, 0);
	BoundFramebufferTexture();

	const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
	const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

	// We don't know about alpha at all.
	gstate_c.SetTextureSolidAlpha(false);

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

	// Since we've drawn using thin3d, might need these.
	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
}

void TextureCacheCommon::DeviceLost() {
	textureShaderCache_.DeviceLost();
	clutTextureCache_.DeviceLost();
	Clear(false);
}

void TextureCacheCommon::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	textureShaderCache_.DeviceRestore(draw);
	clutTextureCache_.DeviceRestore(draw);
}

void TextureCacheCommon::Clear(bool delete_them) {
	textureShaderCache_.Clear();
	for (TexCache::iterator iter = cache_.begin(); iter != cache_.end(); ++iter) {
		ReleaseTexture(iter->second.get(), delete_them);
	}
	// In case the setting was changed, we ALWAYS clear the secondary cache (enabled or not.)
	for (TexCache::iterator iter = secondCache_.begin(); iter != secondCache_.end(); ++iter) {
		ReleaseTexture(iter->second.get(), delete_them);
	}
	if (cache_.size() + secondCache_.size()) {
		INFO_LOG(Log::TexCache, "Texture cached cleared from %d (s: %d) textures", (int)cache_.size(), (int)secondCache_.size());
		cache_.clear();
		secondCache_.clear();
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

// One type of texture update can happen without the involvement of the CPU: Block transfers.
// This is done for example for the text rendering in Gran Turismo, where it only uses a single letter
// worth of texture memory, and for each letter drawn, it just replaces it and then draws.
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

	const u64 startKey = (u64)(addr - LARGEST_TEXTURE_SIZE) << 32;
	u64 endKey = (u64)(addr + size + LARGEST_TEXTURE_SIZE) << 32;
	if (endKey < startKey) {
		endKey = (u64)-1;
	}

	// We just loop through all textures in range, and tell them to rehash.
	for (TexCache::iterator iter = cache_.lower_bound(startKey), end = cache_.upper_bound(endKey); iter != end; ++iter) {
		auto &entry = iter->second;
		u32 texAddr = entry->addr;
		// Intentional underestimate here.
		u32 texEnd = entry->addr + entry->SizeInRAM() / 2;
		// Quick check for overlap. Yes the check is right.
		if (addr < texEnd && addr_end > texAddr) {
			entry->status |= TexStatus::HASH_RECHECK;
		}
	}
}

void TextureCacheCommon::InvalidateAll(GPUInvalidationType /*unused*/) {
	// We don't really do anything here right now.
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
	gpuStats.perFrame.numTexturesDecoded++;

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

			if (gstate_c.Use(GPU_USE_SAMPLER_LOD_CONTROL)) {
				if (tw != 1 && tw != (lastW >> 1))
					plan.badMipSizes = true;
				else if (th != 1 && th != (lastH >> 1))
					plan.badMipSizes = true;
			}
		}
	}

	plan.scaleFactor = standardScaleFactor_;
	plan.depth = 1;

	if (plan.hardwareScaling) {
		plan.scaleFactor = shaderScaleFactor_;
	}

	bool isFakeMipmapChange = false;
	if (plan.badMipSizes) {
		isFakeMipmapChange = IsFakeMipmapChange();

		// Check for pure 3D texture.
		const int tw = gstate.getTextureWidth(0);
		const int th = gstate.getTextureHeight(0);
		bool pure3D = true;
		for (int i = 0; i < plan.levelsToLoad; i++) {
			if (gstate.getTextureWidth(i) != tw || gstate.getTextureHeight(i) != th) {
				pure3D = false;
				break;
			}
		}

		// Check early for the degenerate case from Tactics Ogre (EN version, japanese is worse!).
		if (pure3D && plan.levelsToLoad == 2 && gstate.getTextureAddress(0) == gstate.getTextureAddress(1)) {
			// Simply treat it as a regular 2D texture, no fake mipmaps or anything.
			// levelsToLoad/Create gets set to 1 on the way out from the surrounding if.
			isFakeMipmapChange = false;
			pure3D = false;
		} else if (isFakeMipmapChange) {
			// We don't want to create a volume texture, if this is a "fake mipmap change".
			// In practice due to the compat flag, the only time we end up here is in JP Tactics Ogre,
			// or with OpenGL ES 2.0.
			pure3D = false;
		}

		if (pure3D && draw_->GetDeviceCaps().texture3DSupported) {
			plan.depth = plan.levelsToLoad;
			plan.scaleFactor = 1;
		}

		plan.levelsToLoad = 1;
		plan.levelsToCreate = 1;
	}

	// We generate missing mipmaps from maxLevel+1 up to this level. maxLevel can get overwritten below
	// such as when using replacement textures - but let's keep the same amount of levels for generation.
	// Not all backends will generate mipmaps, and in GL we can't really control the number of levels.
	plan.levelsToCreate = plan.levelsToLoad;

	plan.w = gstate.getTextureWidth(0);
	plan.h = gstate.getTextureHeight(0);

	// TODO: We should move the PPGE texture entirely out from the PSP's memory space, so we don't save it in every save state.
	const bool isPPGETexture = entry->addr >= PSP_GetKernelMemoryBase() && entry->addr < PSP_GetKernelMemoryEnd();

	// Don't scale the PPGe texture.
	if (isPPGETexture) {
		plan.scaleFactor = 1;
	} else if (!g_DoubleTextureCoordinates) {
		// Refuse to load invalid-ly sized textures, which can happen through display list corruption.
		// However, turns out some games uses huge textures for font rendering for no apparent reason.
		// These will only work correctly in the top 512x512 part. So, I've increased the threshold quite a bit.
		// We probably should handle these differently, by clamping the texture size and texture coordinates, but meh.
		if (plan.w > 2048 || plan.h > 2048) {
			ERROR_LOG(Log::TexCache, "Bad texture dimensions: %dx%d", plan.w, plan.h);
			return false;
		}
	}

	if (PSP_CoreParameter().compat.flags().ForceLowerResolutionForEffectsOn && gstate.FrameBufStride() < 0x1E0) {
		// A bit of an esoteric workaround - force off upscaling for static textures that participate directly in small-resolution framebuffer effects.
		// This fixes the water in Outrun/DiRT 2 with upscaling enabled.
		plan.scaleFactor = 1;
	}

	if (plan.scaleFactor != 1) {
		if (texelsScaledThisFrame_ >= TEXCACHE_MAX_TEXELS_SCALED && plan.slowScaler) {
			entry->status |= TexStatus::TO_SCALE;
			plan.scaleFactor = 1;
		} else {
			entry->status &= ~TexStatus::TO_SCALE;
			entry->status |= TexStatus::IS_SCALED_OR_REPLACED;
			texelsScaledThisFrame_ += plan.w * plan.h;
		}
	}

	plan.isVideo = IsVideo(entry->addr);

	// TODO: Support reading actual mip levels for upscaled images, instead of just generating them.
	// Maybe can just remove this check?
	if (plan.scaleFactor > 1) {
		plan.levelsToLoad = 1;

		if (plan.isVideo) {
			// No upscaling for video textures.
			plan.scaleFactor = 1;
			plan.levelsToCreate = 1;
		}
	}

	bool canReplace = !isPPGETexture;
	if (entry->status & TexStatus::CLUT8_INDEXED) {
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
			replacedInfo.isFinal = (entry->status & TexStatus::TO_SCALE) == 0;
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
		entry->status |= TexStatus::NO_MIPS;
	} else {
		entry->status &= ~TexStatus::NO_MIPS;
	}

	// Will be filled in again during decode.
	entry->SetAlphaStatus(TextureAlpha::Any);
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
		if (entry.status & TexStatus::CLUT8_INDEXED) {
			_dbg_assert_(entry.format == GE_TFMT_CLUT4 || entry.format == GE_TFMT_CLUT8);
			texDecFlags |= TexDecodeFlags::TO_CLUT8;
		}

		TextureAlpha alphaResult = DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, srcLevel, bufw, texDecFlags);
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
				// Note! This is bad because it reads the mapped memory!
				for (int y = scaledH - 1; y >= 0; --y) {
					memcpy((u8 *)data + stride * y, (u8 *)data + decPitch * y, scaledW *4);
				}
				decPitch = stride;
			}
		}

		if (plan.saveTexture) {
			ReplacedTextureDecodeInfo replacedInfo;
			replacedInfo.cachekey = entry.CacheKey();
			replacedInfo.hash = entry.fullhash;
			replacedInfo.addr = entry.addr;
			replacedInfo.isVideo = IsVideo(entry.addr);
			replacedInfo.isFinal = (entry.status & TexStatus::TO_SCALE) == 0;
			replacedInfo.fmt = dstFmt;

			// NOTE: Reading the decoded texture here may be very slow, if we just wrote it to write-combined memory.
			replacer_.NotifyTextureDecoded(plan.replaced, replacedInfo, pixelData, decPitch, srcLevel, w, h, scaledW, scaledH);
		}
	}
}

TextureAlpha TextureCacheCommon::CheckCLUTAlpha(const uint8_t *pixelData, GEPaletteFormat clutFormat, int w) {
	switch (clutFormat) {
	case GE_CMODE_16BIT_ABGR4444:
		return CheckAlpha16((const u16 *)pixelData, w, 0xF000);
	case GE_CMODE_16BIT_ABGR5551:
		return CheckAlpha16((const u16 *)pixelData, w, 0x8000);
	case GE_CMODE_16BIT_BGR5650:
		// Never has any alpha.
		return TextureAlpha::Solid;
	default:
		return CheckAlpha32((const u32 *)pixelData, w, 0xFF000000);
	}
}

std::string TexStatusToString(TexStatus status) {
	std::string result;
	if (status & TexStatus::ALPHA_SOLID) {
		result += "SOLID_ALPHA ";
	}
	if (status & TexStatus::MANY_CLUT_VARIANTS) {
		result += "CLUTVARIANTS ";
	}
	if (status & TexStatus::TO_SCALE) {
		result += "TOSCALE ";
	}
	if (status & TexStatus::IS_SCALED_OR_REPLACED) {
		result += "SCALED/REPL ";
	}
	if (status & TexStatus::NO_MIPS) {
		result += "NO_MIPS ";
	}
	if (status & TexStatus::CLUT_GPU) {
		result += "CLUT_GPU ";
	}
	if (status & TexStatus::CLUT8_INDEXED) {
		result += "CLUT8_INDEXED ";
	}
	if (status & TexStatus::IS_3D) {
		result += "3D ";
	}
	if (status & TexStatus::VIDEO) {
		result += "VIDEO ";
	}
	return result.empty() ? "None" : result;
}
