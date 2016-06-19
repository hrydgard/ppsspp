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

#include "Common/CommonTypes.h"
#include "Common/MemoryUtil.h"
#include "Core/TextureReplacer.h"
#include "GPU/Common/GPUDebugInterface.h"

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

struct VirtualFramebuffer;

class CachedTextureVulkan;

class TextureCacheCommon {
public:
	TextureCacheCommon();
	virtual ~TextureCacheCommon();

	void LoadClut(u32 clutAddr, u32 loadBytes);
	bool GetCurrentClutBuffer(GPUDebugBuffer &buffer);

	virtual bool SetOffsetTexture(u32 offset);

	// FramebufferManager keeps TextureCache updated about what regions of memory are being rendered to.
	void NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg);
	void NotifyConfigChanged();
	void NotifyVideoUpload(u32 addr, int size, int width, GEBufferFormat fmt);

	int AttachedDrawingHeight();

	// Wow this is starting to grow big. Soon need to start looking at resizing it.
	// Must stay a POD.
	struct TexCacheEntry {
		// After marking STATUS_UNRELIABLE, if it stays the same this many frames we'll trust it again.
		const static int FRAMES_REGAIN_TRUST = 1000;

		enum Status {
			STATUS_HASHING = 0x00,
			STATUS_RELIABLE = 0x01,        // Don't bother rehashing.
			STATUS_UNRELIABLE = 0x02,      // Always recheck hash.
			STATUS_MASK = 0x03,

			STATUS_ALPHA_UNKNOWN = 0x04,
			STATUS_ALPHA_FULL = 0x00,      // Has no alpha channel, or always full alpha.
			STATUS_ALPHA_SIMPLE = 0x08,    // Like above, but also has 0 alpha (e.g. 5551.)
			STATUS_ALPHA_MASK = 0x0c,

			STATUS_CHANGE_FREQUENT = 0x10, // Changes often (less than 6 frames in between.)
			STATUS_CLUT_RECHECK = 0x20,    // Another texture with same addr had a hashfail.
			STATUS_DEPALETTIZE = 0x40,     // Needs to go through a depalettize pass.
			STATUS_TO_SCALE = 0x80,        // Pending texture scaling in a later frame.
			STATUS_IS_SCALED = 0x100,      // Has been scaled (can't be replaceImages'd.)
			STATUS_FREE_CHANGE = 0x200,    // Allow one change before marking "frequent".
		};

		// Status, but int so we can zero initialize.
		int status;
		u32 addr;
		u32 hash;
		VirtualFramebuffer *framebuffer;  // if null, not sourced from an FBO.
		u32 sizeInRAM;
		int lastFrame;
		int numFrames;
		int numInvalidated;
		u32 framesUntilNextFullHash;
		u8 format;
		u8 maxLevel;
		u16 dim;
		u16 bufw;
		union {
			u32 textureName;
			void *texturePtr;
			CachedTextureVulkan *vkTex;
		};
		int invalidHint;
		u32 fullhash;
		u32 cluthash;
		float lodBias;
		u16 maxSeenV;

		// Cache the current filter settings so we can avoid setting it again.
		// (OpenGL madness where filter settings are attached to each texture. Unused in Vulkan).
		u8 magFilt;
		u8 minFilt;
		bool sClamp;
		bool tClamp;

		Status GetHashStatus() {
			return Status(status & STATUS_MASK);
		}
		void SetHashStatus(Status newStatus) {
			status = (status & ~STATUS_MASK) | newStatus;
		}
		Status GetAlphaStatus() {
			return Status(status & STATUS_ALPHA_MASK);
		}
		void SetAlphaStatus(Status newStatus) {
			status = (status & ~STATUS_ALPHA_MASK) | newStatus;
		}
		void SetAlphaStatus(Status newStatus, int level) {
			// For non-level zero, only set more restrictive.
			if (newStatus == STATUS_ALPHA_UNKNOWN || level == 0) {
				SetAlphaStatus(newStatus);
			} else if (newStatus == STATUS_ALPHA_SIMPLE && GetAlphaStatus() == STATUS_ALPHA_FULL) {
				SetAlphaStatus(STATUS_ALPHA_SIMPLE);
			}
		}

		bool Matches(u16 dim2, u8 format2, u8 maxLevel2) const;
		u64 CacheKey() const;
		static u64 CacheKey(u32 addr, u8 format, u16 dim, u32 cluthash);
	};

protected:
	// Can't be unordered_map, we use lower_bound ... although for some reason that compiles on MSVC.
	typedef std::map<u64, TexCacheEntry> TexCache;

	// Separate to keep main texture cache size down.
	struct AttachedFramebufferInfo {
		u32 xOffset;
		u32 yOffset;
	};

	bool DecodeTextureLevel(u8 *out, int outPitch, GETextureFormat format, GEPaletteFormat clutformat, uint32_t texaddr, int level, int bufw, bool reverseColors, bool useBGRA = false);
	void UnswizzleFromMem(u32 *dest, u32 destPitch, const u8 *texptr, u32 bufw, u32 height, u32 bytesPerPixel);
	bool ReadIndexedTex(u8 *out, int outPitch, int level, const u8 *texptr, int bytesPerIndex, int bufw);

	template <typename T>
	inline const T *GetCurrentClut() {
		return (const T *)clutBuf_;
	}

	u32 EstimateTexMemoryUsage(const TexCacheEntry *entry);
	void GetSamplingParams(int &minFilt, int &magFilt, bool &sClamp, bool &tClamp, float &lodBias, u8 maxLevel, u32 addr);
	void UpdateMaxSeenV(TexCacheEntry *entry, bool throughMode);

	virtual bool AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset = 0) = 0;
	void AttachFramebufferValid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo);
	void AttachFramebufferInvalid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo);
	void DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer);

	virtual void DownloadFramebufferForClut(u32 clutAddr, u32 bytes) = 0;

	void DecimateVideos();

	TextureReplacer replacer;

	TexCache cache;
	u32 cacheSizeEstimate_;

	std::vector<VirtualFramebuffer *> fbCache_;
	std::map<u64, AttachedFramebufferInfo> fbTexInfo_;

	std::map<u32, int> videos_;

	SimpleBuf<u32> tmpTexBuf32;
	SimpleBuf<u16> tmpTexBuf16;
	SimpleBuf<u32> tmpTexBufRearrange;

	TexCacheEntry *nextTexture_;

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
};

inline bool TextureCacheCommon::TexCacheEntry::Matches(u16 dim2, u8 format2, u8 maxLevel2) const {
	return dim == dim2 && format == format2 && maxLevel == maxLevel2;
}

inline u64 TextureCacheCommon::TexCacheEntry::CacheKey() const {
	return CacheKey(addr, format, dim, cluthash);
}

inline u64 TextureCacheCommon::TexCacheEntry::CacheKey(u32 addr, u8 format, u16 dim, u32 cluthash) {
	u64 cachekey = ((u64)(addr & 0x3FFFFFFF) << 32) | dim;
	bool hasClut = (format & 4) != 0;
	if (hasClut) {
		cachekey ^= cluthash;
	}
	return cachekey;
}
