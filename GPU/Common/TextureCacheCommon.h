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

#pragma once

#include <map>
#include <vector>
#include <memory>

#include "Common/CommonTypes.h"
#include "Common/MemoryUtil.h"
#include "Core/System.h"
#include "GPU/GPU.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/Common/TextureReplacer.h"
#include "GPU/GPUDefinitions.h"

class Draw2D;

enum FramebufferNotification {
	NOTIFY_FB_CREATED,
	NOTIFY_FB_UPDATED,
	NOTIFY_FB_DESTROYED,
};

// Changes more frequent than this will be considered "frequent" and prevent texture scaling.
#define TEXCACHE_FRAME_CHANGE_FREQUENT 6
// Note: only used when hash backoff is disabled.
#define TEXCACHE_FRAME_CHANGE_FREQUENT_REGAIN_TRUST 33

#define TEXCACHE_MAX_TEXELS_SCALED (256*256)  // Per frame

struct VirtualFramebuffer;
class TextureReplacer;
class ShaderManagerCommon;

enum class TexDecodeFlags {
	EXPAND32 = 1,
	REVERSE_COLORS = 2,
	TO_CLUT8 = 4,
};
ENUM_CLASS_BITOPS(TexDecodeFlags);

namespace Draw {
class DrawContext;
class Texture;
}

namespace GPURecord {
class Recorder;
}

// Used by D3D11 and Vulkan, could be used by modern GL
struct SamplerCacheKey {
	union {
		uint64_t fullKey;
		struct {
			// These are 8.8 fixed point.
			int16_t maxLevel;
			int16_t minLevel;
			int16_t lodBias;

			bool mipEnable : 1;
			bool minFilt : 1;
			bool mipFilt : 1;
			bool magFilt : 1;
			bool sClamp : 1;
			bool tClamp : 1;
			bool aniso : 1;
			bool texture3d : 1;
		};
	};
	bool operator < (const SamplerCacheKey &other) const {
		return fullKey < other.fullKey;
	}
	void ToString(std::string *str) const {
		str->resize(sizeof(*this));
		memcpy(&(*str)[0], this, sizeof(*this));
	}
	void FromString(const std::string &str) {
		memcpy(this, &str[0], sizeof(*this));
	}
};

class GLRTexture;
class VulkanTexture;

// Allow the extra bits from the remasters for the purposes of this.
inline int dimWidth(u16 dim) {
	return 1 << (dim & 0xFF);
}

inline int dimHeight(u16 dim) {
	return 1 << ((dim >> 8) & 0xFF);
}

// Enough information about a texture to match it to framebuffers.
struct TextureDefinition {
	u32 addr;
	u16 bufw;
	u16 dim;
	GETextureFormat format;
};

// Texture replacement state machine:
// Call FindReplacement during PrepareBuild.
// If replacedTexture gets set: If not found, -> STATUS_TO_REPLACE, otherwise directly -> STATUS_IS_SCALED.
// If replacedTexture is null, leave it at null.
// If replacedTexture is set in SetTexture and STATUS_IS_SCALED is not set, query status. If ready rebuild texture, which will set STATUS_IS_SCALED.

// NOTE: These only handle textures loaded directly from PSP memory contents.
// Framebuffer textures do not have entries, we bind the framebuffers directly.
// At one point we might merge the concepts of framebuffers and textures, but that
// moment is far away.

// TODO: Shrink this struct. There is some fluff.
struct TexCacheEntry {
	~TexCacheEntry() {
		if (texturePtr || textureName || vkTex)
			Crash();
	}
	// After marking STATUS_UNRELIABLE, if it stays the same this many frames we'll trust it again.
	const static int FRAMES_REGAIN_TRUST = 1000;

	enum TexStatus {
		STATUS_HASHING = 0x00,
		STATUS_RELIABLE = 0x01,        // Don't bother rehashing.
		STATUS_UNRELIABLE = 0x02,      // Always recheck hash.
		STATUS_MASK = 0x03,

		STATUS_ALPHA_UNKNOWN = 0x04,
		STATUS_ALPHA_FULL = 0x00,      // Has no alpha channel, or always full alpha.
		STATUS_ALPHA_MASK = 0x04,

		STATUS_CLUT_VARIANTS = 0x08,   // Has multiple CLUT variants.
		STATUS_CHANGE_FREQUENT = 0x10, // Changes often (less than 6 frames in between.)
		STATUS_CLUT_RECHECK = 0x20,    // Another texture with same addr had a hashfail.
		STATUS_TO_SCALE = 0x80,        // Pending texture scaling in a later frame.
		STATUS_IS_SCALED_OR_REPLACED = 0x100,  // Has been scaled already (ignored for replacement checks).
		STATUS_TO_REPLACE = 0x0200,    // Pending texture replacement.
		// When hashing large textures, we optimize 512x512 down to 512x272 by default, since this
		// is commonly the only part accessed.  If access is made above 272, we hash the entire
		// texture, and set this flag to allow scaling the texture just once for the new hash.
		STATUS_FREE_CHANGE = 0x0400,   // Allow one change before marking "frequent".

		STATUS_NO_MIPS = 0x0800,      // Has bad or unusable mipmap levels.

		STATUS_FRAMEBUFFER_OVERLAP = 0x1000,

		STATUS_FORCE_REBUILD = 0x2000,

		STATUS_3D = 0x4000,

		STATUS_CLUT_GPU = 0x8000,

		STATUS_VIDEO = 0x10000,
		STATUS_BGRA = 0x20000,
	};

	// TexStatus enum flag combination.
	u32 status;

	u32 addr;
	u32 minihash;
	u8 format;  // GeTextureFormat
	u8 maxLevel;
	u16 dim;
	u16 bufw;
	union {
		GLRTexture *textureName;
		void *texturePtr;
		VulkanTexture *vkTex;
	};
#ifdef _WIN32
	void *textureView;  // Used by D3D11 only for the shader resource view.
#endif
	int invalidHint;
	int lastFrame;
	int numFrames;
	int numInvalidated;
	u32 framesUntilNextFullHash;
	u32 fullhash;
	u32 cluthash;
	u16 maxSeenV;
	ReplacedTexture *replacedTexture;

	TexStatus GetHashStatus() {
		return TexStatus(status & STATUS_MASK);
	}
	void SetHashStatus(TexStatus newStatus) {
		status = (status & ~STATUS_MASK) | newStatus;
	}
	TexStatus GetAlphaStatus() {
		return TexStatus(status & STATUS_ALPHA_MASK);
	}
	void SetAlphaStatus(TexStatus newStatus) {
		status = (status & ~STATUS_ALPHA_MASK) | newStatus;
	}
	void SetAlphaStatus(TexStatus newStatus, int level) {
		// For non-level zero, only set more restrictive.
		if (newStatus == STATUS_ALPHA_UNKNOWN || level == 0) {
			SetAlphaStatus(newStatus);
		}
	}
	void SetAlphaStatus(CheckAlphaResult alphaResult, int level) {
		TexStatus newStatus = (TexStatus)alphaResult;
		// For non-level zero, only set more restrictive.
		if (newStatus == STATUS_ALPHA_UNKNOWN || level == 0) {
			SetAlphaStatus(newStatus);
		}
	}

	// This is the full size in RAM, not the half size we use sometimes as a "safe" underestimate.
	u32 SizeInRAM() const {
		return (textureBitsPerPixel[format] * bufw * dimHeight(dim)) / 8;
	}

	bool Matches(u16 dim2, u8 format2, u8 maxLevel2) const;
	u64 CacheKey() const;
	static u64 CacheKey(u32 addr, u8 format, u16 dim, u32 cluthash);
};

// Can't be unordered_map, we use lower_bound ... although for some reason that (used to?) compiles on MSVC.
// Would really like to replace this with DenseHashMap but can't as long as we need lower_bound.
typedef std::map<u64, std::unique_ptr<TexCacheEntry>> TexCache;

// Urgh.
#ifdef IGNORE
#undef IGNORE
#endif

struct FramebufferMatchInfo {
	int16_t xOffset;
	int16_t yOffset;
	bool reinterpret;
	GEBufferFormat reinterpretTo;
};

struct AttachCandidate {
	VirtualFramebuffer *fb;
	FramebufferMatchInfo match;
	RasterChannel channel;
	int relevancy;

	std::string ToString() const;
};

class FramebufferManagerCommon;

struct BuildTexturePlan {
	// Inputs
	bool hardwareScaling = false;
	bool slowScaler = true;

	// Set if the PSP software specified an unusual mip chain,
	// such as the same size throughout, or anything else that doesn't divide by
	// two on each level. If this is set, we won't generate mips nor use any.
	// However, we still respect baseLevelSrc.
	bool badMipSizes;

	// Number of mip levels to load from PSP memory (or replacement).
	int levelsToLoad;

	// The number of levels in total to create.
	// If greater than maxLevelToLoad, the backend is expected to either generate
	// the missing levels, or limit itself to levelsToLoad levels.
	int levelsToCreate;

	// The maximum number of mips levels we can create for this texture.
	int maxPossibleLevels;

	// Load the 0-mip from this PSP texture level instead of 0.
	// If non-zero, we are only loading one level.
	int baseLevelSrc;

	// The scale factor of the final texture.
	int scaleFactor;

	// Whether it's a video texture or not. Some decisions might depend on this.
	bool isVideo;

	// Unscaled size of the 0-mip of the original texture.
	// Don't really need to have it here, but convenient.
	int w;
	int h;

	// Scaled (or replaced) size of the 0-mip of the final texture.
	int createW;
	int createH;

	// Used for 3D textures only. If not a 3D texture, will be 1.
	int depth;

	// The replacement for the texture.
	ReplacedTexture *replaced;
	// Need to only check once since it can change during the load!
	bool doReplace;
	bool saveTexture;

	// TODO: Expand32 should probably also be decided in PrepareBuildTexture.
	bool decodeToClut8;

	void GetMipSize(int level, int *w, int *h) const {
		if (doReplace) {
			replaced->GetSize(level, w, h);
			return;
		}
		if (depth == 1) {
			*w = createW >> level;
			*h = createH >> level;
		} else {
			// 3D texture, we look for layers instead of levels.
			*w = createW;
			*h = createH;
		}
	}
};

class TextureCacheCommon {
public:
	TextureCacheCommon(Draw::DrawContext *draw, Draw2D *draw2D);
	virtual ~TextureCacheCommon();

	void LoadClut(u32 clutAddr, u32 loadBytes, GPURecord::Recorder *recorder);
	bool GetCurrentClutBuffer(GPUDebugBuffer &buffer);

	// This updates nextTexture_ / nextFramebufferTexture_, which is then used by ApplyTexture.
	// TODO: Return stuff directly instead of keeping state.
	TexCacheEntry *SetTexture();

	void SetShaderManager(ShaderManagerCommon *sm) {
		shaderManager_ = sm;
	}

	void ApplyTexture(bool doBind = true);
	bool SetOffsetTexture(u32 yOffset);
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ClearNextFrame();

	TextureShaderCache *GetTextureShaderCache() { return textureShaderCache_; }

	virtual void ForgetLastTexture() = 0;
	virtual void Clear(bool delete_them);
	virtual void NotifyConfigChanged();
	virtual void ApplySamplingParams(const SamplerCacheKey &key) = 0;

	// FramebufferManager keeps TextureCache updated about what regions of memory are being rendered to,
	// so that it can invalidate TexCacheEntries pointed at those addresses.
	void NotifyFramebuffer(VirtualFramebuffer *framebuffer, FramebufferNotification msg);
	void NotifyWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat fmt);

	size_t NumLoadedTextures() const {
		return cache_.size();
	}

	bool IsFakeMipmapChange() {
		return PSP_CoreParameter().compat.flags().FakeMipmapChange && gstate.getTexLevelMode() == GE_TEXLEVEL_MODE_CONST;
	}
	bool VideoIsPlaying() {
		return !videos_.empty();
	}
	virtual bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) { return false; }

	virtual void StartFrame();

	virtual void DeviceLost() = 0;
	virtual void DeviceRestore(Draw::DrawContext *draw) = 0;

	// Accessors for the debugger.
	virtual void *GetNativeTextureView(const TexCacheEntry *entry, bool flat) const = 0;

	const TexCache &Cache() const { return cache_; }
	const TexCache &SecondCache() const { return secondCache_; }

	const size_t CacheSizeEstimate() const { return cacheSizeEstimate_; }
	const size_t SecondCacheSizeEstimate() const { return secondCacheSizeEstimate_; }

	struct VideoInfo {
		u32 addr;
		u32 size;
		int flips;
	};

	const std::vector<VideoInfo> &Videos() const {
		return videos_;
	}

protected:
	bool PrepareBuildTexture(BuildTexturePlan &plan, TexCacheEntry *entry);

	virtual void BindTexture(TexCacheEntry *entry) = 0;
	virtual void Unbind() = 0;
	virtual void ReleaseTexture(TexCacheEntry *entry, bool delete_them) = 0;
	void DeleteTexture(TexCache::iterator it);
	void Decimate(TexCacheEntry *exceptThisOne, bool forcePressure);  // forcePressure defaults to false.

	void ApplyTextureFramebuffer(VirtualFramebuffer *framebuffer, GETextureFormat texFormat, RasterChannel channel);
	void ApplyTextureDepal(TexCacheEntry *entry);

	void HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete);
	virtual void BuildTexture(TexCacheEntry *const entry) = 0;
	virtual void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) = 0;
	bool CheckFullHash(TexCacheEntry *entry, bool &doDelete);

	virtual void BindAsClutTexture(Draw::Texture *tex, bool smooth) {}

	CheckAlphaResult DecodeTextureLevel(u8 *out, int outPitch, GETextureFormat format, GEPaletteFormat clutformat, uint32_t texaddr, int level, int bufw, TexDecodeFlags flags);
	static void UnswizzleFromMem(u32 *dest, u32 destPitch, const u8 *texptr, u32 bufw, u32 height, u32 bytesPerPixel);
	CheckAlphaResult ReadIndexedTex(u8 *out, int outPitch, int level, const u8 *texptr, int bytesPerIndex, int bufw, bool reverseColors, bool expandTo32Bit);
	ReplacedTexture *FindReplacement(TexCacheEntry *entry, int *w, int *h, int *d);
	void PollReplacement(TexCacheEntry *entry, int *w, int *h, int *d);

	// Return value is mapData normally, but could be another buffer allocated with AllocateAlignedMemory.
	void LoadTextureLevel(TexCacheEntry &entry, uint8_t *mapData, size_t dataSize, int mapRowPitch, BuildTexturePlan &plan, int srcLevel, Draw::DataFormat dstFmt, TexDecodeFlags texDecFlags);

	template <typename T>
	inline const T *GetCurrentClut() {
		return (const T *)clutBuf_;
	}

	template <typename T>
	inline const T *GetCurrentRawClut() {
		return (const T *)clutBufRaw_;
	}

	static u32 EstimateTexMemoryUsage(const TexCacheEntry *entry);

	SamplerCacheKey GetSamplingParams(int maxLevel, const TexCacheEntry *entry);
	SamplerCacheKey GetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight);
	void UpdateMaxSeenV(TexCacheEntry *entry, bool throughMode);

	bool MatchFramebuffer(const TextureDefinition &entry, VirtualFramebuffer *framebuffer, u32 texaddrOffset, RasterChannel channel, FramebufferMatchInfo *matchInfo) const;

	bool GetBestFramebufferCandidate(const TextureDefinition &entry, u32 texAddrOffset, AttachCandidate *bestCandidate, const char *context) const;

	void SetTextureFramebuffer(const AttachCandidate &candidate);
	bool GetCurrentFramebufferTextureDebug(GPUDebugBuffer &buffer, bool *isFramebuffer);

	virtual void BoundFramebufferTexture() {}

	void DecimateVideos();
	bool IsVideo(u32 texaddr) const;

	static CheckAlphaResult CheckCLUTAlpha(const uint8_t *pixelData, GEPaletteFormat clutFmt, int w);

	static inline u32 QuickTexHash(TextureReplacer &replacer, u32 addr, int bufw, int w, int h, bool swizzled, GETextureFormat format, const TexCacheEntry *entry) {
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

		gpuStats.numTextureDataBytesHashed += sizeInRAM;

		if (Memory::IsValidAddress(addr + sizeInRAM)) {
			return StableQuickTexHash(checkp, sizeInRAM);
		} else {
			return 0;
		}
	}

	static inline u32 MiniHash(const u32 *ptr) {
		return ptr[0];
	}

	Draw::DrawContext *draw_;
	Draw2D *draw2D_;

	TextureReplacer replacer_;
	TextureScalerCommon scaler_;
	FramebufferManagerCommon *framebufferManager_;
	TextureShaderCache *textureShaderCache_;
	ShaderManagerCommon *shaderManager_;

	bool clearCacheNextFrame_ = false;
	bool lowMemoryMode_ = false;

	int decimationCounter_;
	int texelsScaledThisFrame_ = 0;
	int timesInvalidatedAllThisFrame_ = 0;
	double replacementTimeThisFrame_ = 0;
	// Recomputed once per frame. Depends FPS and soon also config.
	double replacementFrameBudgetSeconds_ = 0.5 / 60.0;

	TexCache cache_;
	u32 cacheSizeEstimate_ = 0;

	TexCache secondCache_;
	u32 secondCacheSizeEstimate_ = 0;

	std::vector<VideoInfo> videos_;

	AlignedVector<u32, 16> tmpTexBuf32_;
	AlignedVector<u32, 16> tmpTexBufRearrange_;

	TexCacheEntry *nextTexture_ = nullptr;
	bool failedTexture_ = false;
	VirtualFramebuffer *nextFramebufferTexture_ = nullptr;
	RasterChannel nextFramebufferTextureChannel_ = RASTER_COLOR;

	u32 clutHash_ = 0;

	// Raw is where we keep the original bytes.  Converted is where we swap colors if necessary.
	u32 *clutBufRaw_;
	u32 *clutBufConverted_;
	// This is the active one.
	u32 *clutBuf_;
	u32 clutLastFormat_ = 0xFFFFFFFF;
	u32 clutTotalBytes_ = 0;
	u32 clutMaxBytes_ = 0;
	u32 clutRenderAddress_ = 0xFFFFFFFF;
	u32 clutRenderOffset_;
	GEBufferFormat clutRenderFormat_;

	// True if the clut is just alpha values in the same order (RGBA4444-bit only.)
	bool clutAlphaLinear_ = false;
	u16 clutAlphaLinearColor_;

	// Facilities for GPU depal of static textures.
	Draw::Framebuffer *dynamicClutTemp_ = nullptr;
	Draw::Framebuffer *dynamicClutFbo_ = nullptr;

	int standardScaleFactor_;
	int shaderScaleFactor_ = 0;

	const char *nextChangeReason_;
	bool nextNeedsRehash_;
	bool nextNeedsChange_;
	bool nextNeedsRebuild_;

	u32 *expandClut_;
};

inline bool TexCacheEntry::Matches(u16 dim2, u8 format2, u8 maxLevel2) const {
	return dim == dim2 && format == format2 && maxLevel == maxLevel2;
}

inline u64 TexCacheEntry::CacheKey() const {
	return CacheKey(addr, format, dim, cluthash);
}

inline u64 TexCacheEntry::CacheKey(u32 addr, u8 format, u16 dim, u32 cluthash) {
	u64 cachekey = ((u64)(addr & 0x3FFFFFFF) << 32) | dim;
	bool hasClut = (format & 4) != 0;
	if (hasClut) {
		cachekey ^= cluthash;
	}
	return cachekey;
}
