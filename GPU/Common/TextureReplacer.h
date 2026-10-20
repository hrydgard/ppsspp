// Copyright (c) 2016- PPSSPP Project.

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

#include "ppsspp_config.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>

#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/MemoryUtil.h"
#include "Common/File/Path.h"
#include "Common/File/VFS/VFS.h"
#include "Common/GPU/DataFormat.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/ReplacedTexture.h"
#include "GPU/ge_constants.h"

class IniFile;
class TextureCacheCommon;
class TextureReplacer;
class ReplacedTextureTask;
class LimitedWaitable;
class VFSBackend;

struct SavedTextureCacheData {
	int levelW[8]{};
	int levelH[8]{};
	bool levelSaved[8]{};
	double lastTimeSaved = 0.0;
};

namespace std {
	template <>
	struct hash<ReplacementCacheKey> {
		size_t operator()(const ReplacementCacheKey &k) const {
			return std::hash<u64>()(k.cachekey ^ ((u64)k.hash << 32));
		}
	};
}

struct ReplacedTextureDecodeInfo {
	u64 cachekey;
	u32 hash;
	u32 addr;
	bool isVideo;
	bool isFinal;
	Draw::DataFormat fmt;
	// Describes the level 0 texture in PSP memory, so shadow textures can snapshot it.
	u16 bufw = 0;
	u16 texW = 0;
	u16 texH = 0;
	bool swizzled = false;
	GETextureFormat texFormat = GE_TFMT_5650;
};

// Area of a texture that draws sample, from (0,0). known = false means the whole texture.
struct ShadowBounds {
	bool known = false;
	u16 maxU = 0;
	u16 maxV = 0;
};

// Inputs for picking the replacement hash of a texture in shadow mode.
struct ShadowHashParams {
	u32 addr;
	int bufw;
	int w;
	int h;
	bool swizzled;
	GETextureFormat fmt;
	u64 cachekey;
	// What draws of this texture sampled so far, including the current one.
	ShadowBounds seen;
};

enum class ReplacerDecimateMode {
	NEW_FRAME,
	FORCE_PRESSURE,
	ALL,
};

class TextureReplacer {
public:
	TextureReplacer(const TextureReplacer &) = delete;
	TextureReplacer &operator=(const TextureReplacer &) = delete;
	// The draw context is checked for supported texture formats.
	TextureReplacer(Draw::DrawContext *draw);
	~TextureReplacer();

	void NotifyConfigChanged();

	bool Enabled() const { return replaceEnabled_ || saveEnabled_; }  // used to check hashing method etc.
	bool ReplaceEnabled() const { return replaceEnabled_; }
	bool SaveEnabled() const { return saveEnabled_; }

	u32 ComputeHash(u32 addr, int bufw, int w, int h, bool swizzled, GETextureFormat fmt, u16 maxSeenV);

	// Shadow textures (shadowTextures = true in [options]): the replacement key only covers the part of the
	// texture that draws sample, and dumps are held back in RAM until that part stops growing, so data the
	// game keeps below the image doesn't produce a new dump each time it changes.
	bool ShadowEnabled() const { return shadowEnabled_ && (replaceEnabled_ || saveEnabled_); }
	// Returns the hash the replacement key uses, and the region of the texture it covers.
	u32 ComputeShadowHash(const ShadowHashParams &params, u16 *regionW, u16 *regionH);
	// Called for every draw that samples a texture, with the key the texture cache gave it.
	void NotifyShadowDraw(u64 cachekey, u32 hash, u32 addr, int w, int h, bool swizzled, GETextureFormat fmt, const ShadowBounds &bounds);
	// The texture cache gave the same content a new key (its region grew). The capture under the old key is
	// dropped, and what it saw goes to the next capture of this texture, so only one dump is made.
	void NotifyShadowRekeyed(u64 cachekey, u32 oldHash, int w, int h, bool swizzled, GETextureFormat fmt, const ShadowBounds &seen);
	// Once per frame: writes out dumps whose region has settled.
	void ShadowTick();
	int GetNumPendingShadows() const { return (int)shadowVariants_.size(); }

	// Returns nullptr if not found.
	ReplacedTexture *FindReplacement(ReplacementCacheKey key, int w, int h);
	// The same, but the replacement covers regionW x regionH of the texture (from shadow textures).
	ReplacedTexture *FindReplacement(ReplacementCacheKey key, int w, int h, int regionW, int regionH);

	// For testing: point the replacer at a texture pack directory and load its
	// ini, without touching the global config. Returns true on success.
	bool LoadPackForTesting(const Path &basePath, std::string *error, bool enableSaving = false);

	// Check if a NotifyTextureDecoded for this texture is desired (used to avoid reads from write-combined memory.)
	bool WillSave(const ReplacedTextureDecodeInfo &replacedInfo) const;

	// Notify that a new texture was decoded. May already be upscaled, saves the data passed.
	// If the replacer knows about this one already, texture will be passed in, otherwise nullptr.
	void NotifyTextureDecoded(ReplacedTexture *texture, const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int pitch, int level, int origW, int origH, int scaledW, int scaledH);

	// Whether this texture, which must have a palette (CLUT4 or CLUT8), is saved as an 8-bit palette PNG
	// with NotifyIndexedTextureDecoded instead of an RGBA one, see saveIndexed in textures.ini.
	bool WillSaveIndexed(const ReplacedTextureDecodeInfo &replacedInfo) const;
	// Whether the indices of this texture have been saved already, for any of its palettes.
	bool HasSavedIndexed(const ReplacedTextureDecodeInfo &replacedInfo) const;
	// Saves the indices (one byte each) with the palette that they get from the game's CLUT, numColors long.
	// The file name has the CLUT hash zeroed and ends in _indexed.
	void NotifyIndexedTextureDecoded(const ReplacedTextureDecodeInfo &replacedInfo, const u8 *indices, int pitch, const u32 *palette, int numColors, int w, int h);

	void Decimate(ReplacerDecimateMode mode);

	static bool GenerateIni(const std::string &gameID, Path &generatedFilename);
	static bool IniExists(const std::string &gameID);

	int GetNumTrackedTextures() const { return (int)cache_.size(); }
	int GetNumCachedReplacedTextures() const { return (int)levelCache_.size(); }

	static std::string HashName(ReplacementCacheKey key, int level);

protected:
	bool FindFiltering(ReplacementCacheKey key, TextureFiltering *forceFiltering);
	// How the indices of a replacement from the [indexed] section get their colors.
	enum class IndexedMode : uint8_t {
		NONE,
		CPU,  // Colored with the CLUT when the texture is built.
		GPU,  // Uploaded as indices, colored in the shader.
	};
	IndexedMode LookupIndexed(ReplacementCacheKey key) const;
	// Which textures with a palette are saved as palette PNGs.
	enum class SaveIndexedMode : uint8_t {
		OFF,
		LISTED,  // Only the ones in the [indexed] section.
		ALL,
	};
	ReplacementCacheKey IndexedSaveKey(const ReplacedTextureDecodeInfo &replacedInfo) const;

	bool LoadIni(std::string *error, bool notify = true);
	void DeleteVFS();
	bool LoadIniValues(IniFile &ini, VFSBackend *dir, bool isOverride, std::string *error);
	void ParseHashRange(const std::string &key, const std::string &value);
	void ParseFiltering(const std::string &key, const std::string &value);
	void ParseIndexed(const std::string &key, const std::string &value);
	void ParseReduceHashRange(const std::string& key, const std::string& value);
	bool LookupHashRange(u32 addr, int w, int h, int *newW, int *newH);
	float LookupReduceHashRange(int w, int h);
	std::string LookupHashFile(ReplacementCacheKey key, bool *foundAlias, bool *ignored);
	// The hashing done by ComputeHash once the hashed size is known, over memory or a snapshot of it.
	u32 HashTextureData(const u8 *checkp, u32 availableBytes, int bufw, int w, int h, GETextureFormat fmt);

	// Shadow textures.
	struct ShadowRegion {
		u16 w = 0;
		u16 h = 0;
		bool operator ==(const ShadowRegion &o) const { return w == o.w && h == o.h; }
		bool Covers(const ShadowRegion &o) const { return w >= o.w && h >= o.h; }
		u32 Area() const { return (u32)w * h; }
	};
	struct ShadowLevel {
		std::vector<u8> rgba;  // Tightly packed.
		int w = 0;  // As saved (scaled).
		int h = 0;
	};
	// One content of a texture, waiting to be dumped.
	struct ShadowVariant {
		u64 cachekey = 0;
		u32 addr = 0;
		int bufw = 0;
		int texW = 0;
		int texH = 0;
		bool swizzled = false;
		GETextureFormat fmt = GE_TFMT_5650;
		// The texture's bytes in PSP memory when it was decoded, so it can be hashed again with its final region.
		std::vector<u8> raw;
		ShadowLevel levels[8];
		int numLevels = 0;
		// What draws sampled while this content was bound.
		bool drawn = false;
		bool unbounded = false;
		ShadowRegion seen;
		double created = 0.0;
		double lastChange = 0.0;
		size_t memoryUsed = 0;
	};
	// Keyed like hashranges_ (address, w, h).
	struct ShadowSlot {
		// Union of everything drawn from this texture, for the provisional key of contents not dumped yet.
		bool unbounded = false;
		ShadowRegion learned;
		// Regions dumps of this texture were made with, from the ini and this session. Largest first.
		std::vector<ShadowRegion> regions;
	};

	static u64 ShadowSlotKey(u32 addr, int w, int h) { return ((u64)addr << 32) | ((u64)w << 16) | h; }
	ShadowRegion NormalizeShadowRegion(const ShadowBounds &bounds, int w, int h) const;
	static void AddShadowRegion(ShadowSlot &slot, ShadowRegion region);
	void ParseShadowRange(const std::string &key, const std::string &value);
	bool ShadowKeyKnown(ReplacementCacheKey key);
	ReplacementCacheKey NamingKey(u64 cachekey, u32 hash) const;
	void CaptureShadowLevel(const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int srcPitch, int level, int scaledW, int scaledH);
	void CommitShadow(ShadowVariant &variant);
	void CommitAllShadows();
	void SaveShadowRanges();

	bool shadowEnabled_ = false;
	double shadowSettleSeconds_ = 4.0;
	int shadowMaxRegions_ = 8;
	int shadowRegionAlign_ = 1;
	size_t shadowMemoryBudget_ = (size_t)1024 * 1024 * 1024;
	size_t shadowMemoryUsed_ = 0;
	double shadowLastTick_ = 0.0;
	bool shadowRangesDirty_ = false;
	double shadowRangesLastSaved_ = 0.0;
	std::unordered_map<u64, ShadowSlot> shadowSlots_;
	std::unordered_map<ReplacementCacheKey, ShadowVariant> shadowVariants_;
	struct ShadowRekey {
		bool valid = false;
		bool unbounded = false;
		u64 cachekey = 0;
		ShadowRegion seen;
	};
	ShadowRekey shadowRekey_;

	bool replaceEnabled_ = false;
	bool saveEnabled_ = false;
	bool ignoreAddress_ = false;
	bool reduceHash_ = false;
	bool ignoreMipmap_ = false;
	SaveIndexedMode saveIndexed_ = SaveIndexedMode::OFF;
	int skipLastDXT1Blocks128x64_ = 0;
	int skipLastDXT1Blocks128x128_ = 0;

	float reduceHashGlobalValue = 0.5f; // Global value for textures dump pngs of all sizes, 0.5 by default but can be set in textures.ini

	double lastTextureCacheSizeGB_ = 0.0;
	std::string gameID_;
	Path basePath_;
	Path newTextureDir_;
	ReplacedTextureHash textureHash_ = ReplacedTextureHash::QUICK;

	VFSBackend *vfs_ = nullptr;
	bool vfsIsZip_ = false;

	GPUFormatSupport formatSupport_{};

	typedef std::pair<int, int> WidthHeightPair;
	std::unordered_map<u64, WidthHeightPair> hashranges_;
	std::unordered_map<u64, float> reducehashranges_;

	std::unordered_map<ReplacementCacheKey, std::string> aliases_;
	std::unordered_map<ReplacementCacheKey, TextureFiltering> filtering_;
	// Replacements that hold palette indices instead of colors, from the [indexed] section.
	std::unordered_map<ReplacementCacheKey, IndexedMode> indexed_;

	std::unordered_map<ReplacementCacheKey, ReplacedTextureRef> cache_;
	std::unordered_map<ReplacementCacheKey, SavedTextureCacheData> savedCache_;

	// the key is either from aliases_, in which case it's a |-separated sequence of texture filenames of the levels of a texture.
	// alternatively the key is from the generated texture filename.
	std::unordered_map<std::string, ReplacedTexture *> levelCache_;
};
