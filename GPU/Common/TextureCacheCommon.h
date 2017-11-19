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
#include "Core/TextureReplacer.h"
#include "Core/System.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/TextureDecoder.h"

enum TextureFiltering {
	TEX_FILTER_AUTO = 1,
	TEX_FILTER_NEAREST = 2,
	TEX_FILTER_LINEAR = 3,
	TEX_FILTER_LINEAR_VIDEO = 4,
};

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

class CachedTextureVulkan;

namespace Draw {
class DrawContext;
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

		// 0x08 free.

		STATUS_CHANGE_FREQUENT = 0x10, // Changes often (less than 6 frames in between.)
		STATUS_CLUT_RECHECK = 0x20,    // Another texture with same addr had a hashfail.
		STATUS_DEPALETTIZE = 0x40,     // Needs to go through a depalettize pass.
		STATUS_TO_SCALE = 0x80,        // Pending texture scaling in a later frame.
		STATUS_IS_SCALED = 0x100,      // Has been scaled (can't be replaceImages'd.)
		// When hashing large textures, we optimize 512x512 down to 512x272 by default, since this
		// is commonly the only part accessed.  If access is made above 272, we hash the entire
		// texture, and set this flag to allow scaling the texture just once for the new hash.
		STATUS_FREE_CHANGE = 0x200,    // Allow one change before marking "frequent".

		STATUS_BAD_MIPS = 0x400,       // Has bad or unusable mipmap levels.
	};

	// Status, but int so we can zero initialize.
	int status;
	u32 addr;
	u32 hash;
	VirtualFramebuffer *framebuffer;  // if null, not sourced from an FBO. TODO: Collapse into texturePtr
	u32 sizeInRAM;  // Could be computed
	u8 format;
	u8 maxLevel;
	u16 dim;
	u16 bufw;
	union {
		GLRTexture *textureName;
		void *texturePtr;
		CachedTextureVulkan *vkTex;
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
	float lodBias;
	u16 maxSeenV;

	// Cache the current filter settings so we can avoid setting it again.
	// (OpenGL ES 2.0 madness where filter settings are attached to each texture. Unused in other backends).
	u8 magFilt;
	u8 minFilt;
	bool sClamp;
	bool tClamp;

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

	bool Matches(u16 dim2, u8 format2, u8 maxLevel2) const;
	u64 CacheKey() const;
	static u64 CacheKey(u32 addr, u8 format, u16 dim, u32 cluthash);
};

class FramebufferManagerCommon;
// Can't be unordered_map, we use lower_bound ... although for some reason that compiles on MSVC.
// Would really like to replace this with DenseHashMap but can't as long as we need lower_bound.
typedef std::map<u64, std::unique_ptr<TexCacheEntry>> TexCache;

class TextureCacheCommon {
public:
	TextureCacheCommon(Draw::DrawContext *draw);
	virtual ~TextureCacheCommon();

	void LoadClut(u32 clutAddr, u32 loadBytes);
	bool GetCurrentClutBuffer(GPUDebugBuffer &buffer);

	void SetTexture(bool force = false);
	void ApplyTexture();
	bool SetOffsetTexture(u32 offset);
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ClearNextFrame();

	virtual void ForgetLastTexture() = 0;
	virtual void InvalidateLastTexture(TexCacheEntry *entry = nullptr) = 0;
	virtual void Clear(bool delete_them);

	// FramebufferManager keeps TextureCache updated about what regions of memory are being rendered to.
	void NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg);
	void NotifyConfigChanged();
	void NotifyVideoUpload(u32 addr, int size, int width, GEBufferFormat fmt);

	int AttachedDrawingHeight();

	size_t NumLoadedTextures() const {
		return cache_.size();
	}

	bool IsFakeMipmapChange() {
		return PSP_CoreParameter().compat.flags().FakeMipmapChange && gstate.getTexLevelMode() == GE_TEXLEVEL_MODE_CONST;
	}
	bool VideoIsPlaying() {
		return !videos_.empty();
	}
	virtual bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) { return false; }

protected:
	virtual void BindTexture(TexCacheEntry *entry) = 0;
	virtual void Unbind() = 0;
	virtual void ReleaseTexture(TexCacheEntry *entry, bool delete_them) = 0;
	void DeleteTexture(TexCache::iterator it);
	void Decimate();

	virtual void ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) = 0;
	bool HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete);
	virtual void BuildTexture(TexCacheEntry *const entry, bool replaceImages) = 0;
	virtual void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) = 0;
	bool CheckFullHash(TexCacheEntry *entry, bool &doDelete);

	// Separate to keep main texture cache size down.
	struct AttachedFramebufferInfo {
		u32 xOffset;
		u32 yOffset;
	};

	void DecodeTextureLevel(u8 *out, int outPitch, GETextureFormat format, GEPaletteFormat clutformat, uint32_t texaddr, int level, int bufw, bool reverseColors, bool useBGRA, bool expandTo32Bit);
	void UnswizzleFromMem(u32 *dest, u32 destPitch, const u8 *texptr, u32 bufw, u32 height, u32 bytesPerPixel);
	void ReadIndexedTex(u8 *out, int outPitch, int level, const u8 *texptr, int bytesPerIndex, int bufw, bool expandTo32Bit);

	template <typename T>
	inline const T *GetCurrentClut() {
		return (const T *)clutBuf_;
	}

	u32 EstimateTexMemoryUsage(const TexCacheEntry *entry);
	void GetSamplingParams(int &minFilt, int &magFilt, bool &sClamp, bool &tClamp, float &lodBias, int maxLevel, u32 addr, GETexLevelMode &mode);
	void UpdateSamplingParams(TexCacheEntry &entry, SamplerCacheKey &key);  // Used by D3D11 and Vulkan.
	void UpdateMaxSeenV(TexCacheEntry *entry, bool throughMode);

	bool AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset = 0);
	void AttachFramebufferValid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo);
	void AttachFramebufferInvalid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo);
	void DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer);

	void SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer);

	void DecimateVideos();

	inline u32 QuickTexHash(TextureReplacer &replacer, u32 addr, int bufw, int w, int h, GETextureFormat format, TexCacheEntry *entry) const {
		if (replacer.Enabled()) {
			return replacer.ComputeHash(addr, bufw, w, h, format, entry->maxSeenV);
		}

		if (h == 512 && entry->maxSeenV < 512 && entry->maxSeenV != 0) {
			h = (int)entry->maxSeenV;
		}

		const u32 sizeInRAM = (textureBitsPerPixel[format] * bufw * h) / 8;
		const u32 *checkp = (const u32 *)Memory::GetPointer(addr);

		if (Memory::IsValidAddress(addr + sizeInRAM)) {
			return DoQuickTexHash(checkp, sizeInRAM);
		} else {
			return 0;
		}
	}

	static inline u32 MiniHash(const u32 *ptr) {
		return ptr[0];
	}

	Draw::DrawContext *draw_;
	TextureReplacer replacer_;
	FramebufferManagerCommon *framebufferManager_;

	bool clearCacheNextFrame_;
	bool lowMemoryMode_;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	TexCache cache_;
	u32 cacheSizeEstimate_;

	TexCache secondCache_;
	u32 secondCacheSizeEstimate_;

	std::vector<VirtualFramebuffer *> fbCache_;
	std::map<u64, AttachedFramebufferInfo> fbTexInfo_;

	std::map<u32, int> videos_;

	SimpleBuf<u32> tmpTexBuf32_;
	SimpleBuf<u16> tmpTexBuf16_;
	SimpleBuf<u32> tmpTexBufRearrange_;

	TexCacheEntry *nextTexture_;

	u32 clutHash_ = 0;

	// Raw is where we keep the original bytes.  Converted is where we swap colors if necessary.
	u32 *clutBufRaw_;
	u32 *clutBufConverted_;
	// This is the active one.
	u32 *clutBuf_;
	u32 clutLastFormat_;
	u32 clutTotalBytes_;
	u32 clutMaxBytes_;
	u32 clutRenderAddress_;
	u32 clutRenderOffset_;
	// True if the clut is just alpha values in the same order (RGBA4444-bit only.)
	bool clutAlphaLinear_;
	u16 clutAlphaLinearColor_;

	int standardScaleFactor_;

	const char *nextChangeReason_;
	bool nextNeedsRehash_;
	bool nextNeedsChange_;
	bool nextNeedsRebuild_;

	bool isBgraBackend_;

	u32 expandClut_[256];
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
