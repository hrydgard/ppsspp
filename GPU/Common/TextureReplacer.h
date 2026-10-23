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
#include <unordered_set>
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
class TextureScalerCommon;
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

// The texels a draw samples, [minU, maxU) x [minV, maxV). known = false means it can be any part of the texture.
struct ShadowBounds {
	bool known = false;
	u16 minU = 0;
	u16 minV = 0;
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
	// The key the texture had before (0 if new), so what its draws sampled carries over.
	u32 oldHash;
	// What the current draw samples, if it's known. nullptr if this draw tells nothing.
	const ShadowBounds *current;
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
	// game keeps around the image doesn't produce a new dump each time it changes.
	bool ShadowEnabled() const { return shadowEnabled_ && (replaceEnabled_ || saveEnabled_); }
	// Returns the hash the replacement key uses. *extentW x *extentH is the part of the texture from (0,0)
	// that the replacement covers, which includes the hashed region.
	u32 ComputeShadowHash(const ShadowHashParams &params, u16 *extentW, u16 *extentH);
	// Called for every draw that samples a texture, with the key the texture cache gave it. bounds is nullptr
	// when the draw's UVs weren't tracked. Returns true if the draw samples outside the key's region, so the
	// texture should be looked up again.
	bool NotifyShadowDraw(u64 cachekey, u32 hash, u32 addr, int w, int h, bool swizzled, GETextureFormat fmt, const ShadowBounds *bounds);
	// The texture cache gave the same content a new key (its region grew). The capture under the old key is
	// dropped, and what it saw goes to the next capture of this texture, so only one dump is made.
	void NotifyShadowRekeyed(u64 cachekey, u32 oldHash);
	// Once per frame: writes out dumps whose region has settled.
	void ShadowTick();
	int GetNumPendingShadows() const { return (int)shadowVariants_.size(); }

	// Layered replacement ([layered] in textures.ini): a texture the game draws text into is replaced by an HD
	// image of it without the text. The texels that differ from the original are the text: each letter (a
	// connected piece of them) is drawn from an HD glyph image if the pack has one, otherwise upscaled.
	bool IsLayered(u32 addr, int w, int h) const;
	// The composed replacement for the current contents of a layered texture, decoded to RGBA8888 (w x h, tightly
	// packed). Valid until the next call for the same texture. Returns nullptr if it can't be made.
	ReplacedTexture *ComposeLayered(u32 addr, int w, int h, const u32 *pixels, TextureScalerCommon *scaler);

	// Returns nullptr if not found.
	ReplacedTexture *FindReplacement(ReplacementCacheKey key, int w, int h);
	// The same, but the replacement covers regionW x regionH of the texture (from shadow textures).
	ReplacedTexture *FindReplacement(ReplacementCacheKey key, int w, int h, int regionW, int regionH);

	// For testing: point the replacer at a texture pack directory and load its
	// ini, without touching the global config. Returns true on success.
	bool LoadPackForTesting(const Path &basePath, std::string *error, bool enableSaving = false);

	// Check if a NotifyTextureDecoded for this texture is desired (used to avoid reads from write-combined memory.)
	bool WillSave(const ReplacedTextureDecodeInfo &replacedInfo) const;

	// Textures matched by [ignore] (address ranges, optionally a size) are neither dumped nor replaced.
	// w and h of 0 match only entries without a size.
	bool IsIgnoredTexture(u32 addr, int w, int h) const;

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
	void ParseIgnore(const std::string &key);
	// With ignoreAddress, files are named with the address they were dumped from, and it's dropped here, when
	// they're read: for [hashes] and the files found, [filtering] and [indexed].
	void ApplyIgnoreAddress();
	// Marks the dumps already in new/ as saved, so they aren't dumped again under another name. With ignoreAddress,
	// a content found at another address than in an earlier session gets a different name.
	void MarkExistingDumps();
	// Adds a line for a written dump to new/dumps.txt, with what its name doesn't tell.
	void LogDump(const std::string &hashfile, u32 addr, ReplacementCacheKey key, int texW, int texH, GETextureFormat fmt, const char *region);
	float LookupReduceHashRange(int w, int h);
	std::string LookupHashFile(ReplacementCacheKey key, bool *foundAlias, bool *ignored);
	// The hashing done by ComputeHash once the hashed size is known, over memory or a snapshot of it.
	u32 HashTextureData(const u8 *checkp, u32 availableBytes, int bufw, int w, int h, GETextureFormat fmt);

	// Shadow textures.
	// A rectangle of the texture, in texels.
	struct ShadowRegion {
		u16 x = 0;
		u16 y = 0;
		u16 w = 0;
		u16 h = 0;
		bool operator ==(const ShadowRegion &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
		bool Empty() const { return w == 0 || h == 0; }
		int Right() const { return x + w; }
		int Bottom() const { return y + h; }
		bool Covers(const ShadowRegion &o, int slack = 0) const {
			return x <= o.x + slack && y <= o.y + slack && Right() + slack >= o.Right() && Bottom() + slack >= o.Bottom();
		}
		u32 Area() const { return (u32)w * h; }
		void Merge(const ShadowRegion &o);
	};
	struct ShadowLevel {
		std::vector<u8> rgba;  // Tightly packed.
		int w = 0;  // As saved (scaled).
		int h = 0;
	};
	// One content of a texture, waiting to be dumped.
	struct ShadowVariant {
		u64 cachekey = 0;
		// cachekey as the texture cache had it (an indexed capture's cachekey has no CLUT hash), for dumps.txt.
		u64 sourceCachekey = 0;
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
		// Saved as palette indices instead (saveIndexed), one for every palette, so the key has no CLUT hash.
		bool indexed = false;
		std::vector<u8> indices;  // Tightly packed, texW x texH.
		u32 palette[256]{};
		int numColors = 0;
		// Which texels of the texture changed while this content stayed (declared textures only), per row as
		// [volatileX0, volatileX1). A declared region with such texels in it holds data that isn't the image.
		std::vector<u16> volatileX0;
		std::vector<u16> volatileX1;
		// The content isn't in memory anymore, so changes there don't tell anything.
		bool contentGone = false;
		// What draws sampled while this content was bound.
		bool drawn = false;
		bool unbounded = false;
		ShadowRegion seen;
		// The separate rectangles drawn, which seen is the bounds of. A texture can hold an image in two parts,
		// with other data between or beside them.
		std::vector<ShadowRegion> drawnParts;
		double firstDrawn = 0.0;
		double lastDrawn = 0.0;
		double created = 0.0;
		double lastChange = 0.0;
		size_t memoryUsed = 0;
	};
	// Keyed like hashranges_ (address, w, h).
	struct ShadowSlot {
		// Union of everything drawn from this texture, for the provisional key of contents not dumped yet.
		bool unbounded = false;
		ShadowRegion drawn;
		// From [shadowranges] in textures.ini. When there are any, dumps of this texture only use these, and
		// nothing is learned for it. Largest first, like the others.
		std::vector<ShadowRegion> declared;
		// Regions made of several declared rectangles, hashed together: from [shadowranges] (written with +), and the
		// ones dumps of this session used.
		std::vector<std::vector<ShadowRegion>> declaredCombos;
		// From [learnedshadowranges] and the dumps of this session.
		std::vector<ShadowRegion> learned;
		// From [hashranges] (0 or 1). It fixes the region: every key and dump of this texture uses exactly it, over
		// [shadowranges] and what's drawn.
		std::vector<ShadowRegion> hashRange;
		bool IsDeclared() const { return !declared.empty() || !declaredCombos.empty(); }
		bool IsFixed() const { return !hashRange.empty(); }
	};
	// What the draws of one replacement key sampled, and the region it was hashed over.
	struct ShadowKeyInfo {
		bool unbounded = false;
		ShadowRegion seen;
		ShadowRegion region;
		double lastUsed = 0.0;
	};

	static u64 ShadowSlotKey(u32 addr, int w, int h) { return ((u64)addr << 32) | ((u64)w << 16) | h; }
	ShadowRegion NormalizeShadowRegion(const ShadowBounds &bounds, int w, int h, bool swizzled, GETextureFormat fmt) const;
	static ShadowRegion FullRegion(int w, int h);
	u32 HashShadowRegion(const u8 *base, u32 availableBytes, int bufw, const ShadowRegion &region, GETextureFormat fmt);
	// Adds it if it isn't there, largest first. Returns true if it was added.
	static bool AddShadowRegion(std::vector<ShadowRegion> &regions, ShadowRegion region);
	// The regions to try for a texture's contents when looking up a key, largest first, the whole texture too.
	static std::vector<ShadowRegion> ShadowLookupRegions(const ShadowSlot &slot, int w, int h);
	void ParseShadowRange(const std::string &key, const std::string &value, bool declared);
	// Updates the volatile texels of a waiting content of a declared texture from its memory now.
	void CheckShadowStability(ShadowVariant &variant);
	bool ShadowRegionStable(const ShadowVariant &variant, const ShadowRegion &region) const;
	// The region to dump a content of a declared texture with: one rectangle, or several hashed together.
	std::vector<ShadowRegion> PickDeclaredParts(const ShadowSlot &slot, const ShadowVariant &variant, const ShadowRegion &seen) const;
	// Of the declared regions covering something, largest first, the one to dump with.
	ShadowRegion PickCovering(const std::vector<ShadowRegion> &covering, const ShadowVariant &variant) const;
	// True if the texels of larger that aren't in smaller all have the same value, so they hold no image.
	bool ExtraAreaUniform(const ShadowVariant &variant, const ShadowRegion &larger, const ShadowRegion &smaller) const;
	// A texel of the captured copy, as stored (an index for CLUT formats). False for DXT, or outside the copy.
	bool ShadowTexel(const ShadowVariant &variant, int x, int y, u32 *value) const;
	static bool AddDrawnPart(std::vector<ShadowRegion> &parts, const ShadowRegion &region);
	static ShadowRegion PartsBounds(const std::vector<ShadowRegion> &parts);
	static std::string ShadowRegionString(const ShadowRegion &region);
	static std::string PartsString(const std::vector<ShadowRegion> &parts);
	static bool InParts(const std::vector<ShadowRegion> &parts, int x, int y);
	u32 HashShadowParts(const u8 *base, u32 availableBytes, int bufw, const std::vector<ShadowRegion> &parts, GETextureFormat fmt);
	bool ShadowKeyKnown(ReplacementCacheKey key);
	ReplacementCacheKey NamingKey(u64 cachekey, u32 hash) const;
	void CaptureShadowLevel(const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int srcPitch, int level, int scaledW, int scaledH);
	void CaptureShadowIndexed(const ReplacedTextureDecodeInfo &replacedInfo, const u8 *indices, int srcPitch, const u32 *palette, int numColors, int w, int h);
	// Starts a capture, with the copy of the texture's memory. Returns nullptr if there's nothing to copy.
	ShadowVariant *CreateShadowVariant(const ReplacedTextureDecodeInfo &replacedInfo, ReplacementCacheKey variantKey);
	std::unordered_map<ReplacementCacheKey, ShadowVariant>::iterator FindShadowVariant(u64 cachekey, u32 hash);
	void EnforceShadowBudget(ReplacementCacheKey keep);
	void CommitShadowIndexed(ShadowVariant &variant, const std::vector<ShadowRegion> &parts, u32 hash);
	// Remembers what a dump used, so its key is found again.
	void NoteDumpRegion(ShadowSlot &slot, const std::vector<ShadowRegion> &parts);
	void CommitShadow(ShadowVariant &variant);
	void CommitAllShadows();
	void SaveShadowRanges();

	bool shadowEnabled_ = false;
	double shadowSettleSeconds_ = 4.0;
	double shadowMinVisibleSeconds_ = 0.0;
	int shadowMaxRegions_ = 8;
	int shadowRegionAlign_ = 1;
	size_t shadowMemoryBudget_ = (size_t)1024 * 1024 * 1024;
	size_t shadowMemoryUsed_ = 0;
	double shadowNow_ = 0.0;
	double shadowLastTick_ = 0.0;
	bool shadowRangesDirty_ = false;
	double shadowRangesLastSaved_ = 0.0;
	std::unordered_map<u64, ShadowSlot> shadowSlots_;
	std::unordered_map<ReplacementCacheKey, ShadowVariant> shadowVariants_;
	std::unordered_map<ReplacementCacheKey, ShadowKeyInfo> shadowKeys_;
	struct ShadowRekey {
		bool valid = false;
		bool unbounded = false;
		u64 cachekey = 0;
		ShadowRegion seen;
		std::vector<ShadowRegion> drawnParts;
		bool drawn = false;
		double firstDrawn = 0.0;
	};
	ShadowRekey shadowRekey_;

	// Layered replacement, keyed like hashranges_.
	struct LayeredSlot {
		std::string originalFile;  // The texture without the text, as dumped.
		std::string hdFile;  // The same, at a higher resolution.
		bool loaded = false;
		bool failed = false;
		int w = 0;
		int h = 0;
		int scale = 1;
		std::vector<u32> original;
		std::vector<u32> hd;
		// original, upscaled like the text, to tell which upscaled texels the text changed.
		std::vector<u32> scaledOriginal;
		int scaledOriginalType = -1;
		// Holds the last composed image. Never deleted while the replacer lives, since cache entries point to it.
		ReplacedTexture *texture = nullptr;
	};
	// One letter of layered text: an HD image for it from the pack, or that there's none.
	struct LayeredGlyph {
		bool found = false;
		int w = 0;
		int h = 0;
		std::vector<u32> pixels;
	};
	void ParseLayered(const std::string &key, const std::string &value);
	bool LoadLayered(LayeredSlot &slot);
	const LayeredGlyph &LookupLayeredGlyph(u64 key);
	void SaveLayeredGlyph(u64 key, const u32 *pixels, int w, int h);
	// -1 for no upscaling (blocks), otherwise a TextureScalerCommon type.
	int layeredTextScaler_ = 0;
	bool layeredGlyphs_ = true;
	std::unordered_map<u64, LayeredGlyph> layeredGlyphCache_;
	std::unordered_set<u64> savedLayeredGlyphs_;
	std::unordered_map<u64, LayeredSlot> layered_;
	// Layered textures from before an ini reload, kept alive for the same reason.
	std::vector<ReplacedTexture *> retiredLayered_;

	struct IgnoreRange {
		u32 start = 0;
		u32 end = 0;  // Inclusive.
		u16 w = 0;  // 0 for any size.
		u16 h = 0;
	};
	std::vector<IgnoreRange> ignoreRanges_;
	bool dumpLogStarted_ = false;

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
