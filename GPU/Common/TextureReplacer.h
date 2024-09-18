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

struct ReplacementCacheKey {
	u64 cachekey;
	u32 hash;

	ReplacementCacheKey(u64 c, u32 h) : cachekey(c), hash(h) { }

	bool operator ==(const ReplacementCacheKey &k) const {
		return k.cachekey == cachekey && k.hash == hash;
	}

	bool operator <(const ReplacementCacheKey &k) const {
		if (k.cachekey == cachekey) {
			return k.hash < hash;
		}
		return k.cachekey < cachekey;
	}
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
};

enum class ReplacerDecimateMode {
	NEW_FRAME,
	FORCE_PRESSURE,
	ALL,
};

class TextureReplacer {
public:
	// The draw context is checked for supported texture formats.
	TextureReplacer(Draw::DrawContext *draw);
	~TextureReplacer();

	void NotifyConfigChanged();

	bool Enabled() const { return replaceEnabled_ || saveEnabled_; }  // used to check hashing method etc.
	bool ReplaceEnabled() const { return replaceEnabled_; }
	bool SaveEnabled() const { return saveEnabled_; }

	bool AllowVideo() const { return allowVideo_; }

	u32 ComputeHash(u32 addr, int bufw, int w, int h, bool swizzled, GETextureFormat fmt, u16 maxSeenV);

	// Returns nullptr if not found.
	ReplacedTexture *FindReplacement(u64 cachekey, u32 hash, int w, int h);

	// Check if a NotifyTextureDecoded for this texture is desired (used to avoid reads from write-combined memory.)
	bool WillSave(const ReplacedTextureDecodeInfo &replacedInfo) const;

	// Notify that a new texture was decoded. May already be upscaled, saves the data passed.
	// If the replacer knows about this one already, texture will be passed in, otherwise nullptr.
	void NotifyTextureDecoded(ReplacedTexture *texture, const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int pitch, int level, int origW, int origH, int scaledW, int scaledH);

	void Decimate(ReplacerDecimateMode mode);

	static bool GenerateIni(const std::string &gameID, Path &generatedFilename);
	static bool IniExists(const std::string &gameID);

	int GetNumTrackedTextures() const { return (int)cache_.size(); }
	int GetNumCachedReplacedTextures() const { return (int)levelCache_.size(); }

	static std::string HashName(u64 cachekey, u32 hash, int level);

protected:
	bool FindFiltering(u64 cachekey, u32 hash, TextureFiltering *forceFiltering);

	bool LoadIni();
	bool LoadIniValues(IniFile &ini, VFSBackend *dir, bool isOverride = false);
	void ParseHashRange(const std::string &key, const std::string &value);
	void ParseFiltering(const std::string &key, const std::string &value);
	void ParseReduceHashRange(const std::string& key, const std::string& value);
	bool LookupHashRange(u32 addr, int w, int h, int *newW, int *newH);
	float LookupReduceHashRange(int w, int h);
	std::string LookupHashFile(u64 cachekey, u32 hash, bool *foundAlias, bool *ignored);

	static void ScanForHashNamedFiles(VFSBackend *dir, std::map<ReplacementCacheKey, std::map<int, std::string>> &filenameMap);
	void ComputeAliasMap(const std::map<ReplacementCacheKey, std::map<int, std::string>> &filenameMap);

	bool replaceEnabled_ = false;
	bool saveEnabled_ = false;
	bool allowVideo_ = false;
	bool ignoreAddress_ = false;
	bool reduceHash_ = false;
	bool ignoreMipmap_ = false;

	float reduceHashSize = 1.0f; // default value with reduceHash to false
	float reduceHashGlobalValue = 0.5f; // Global value for textures dump pngs of all sizes, 0.5 by default but can be set in textures.ini

	double lastTextureCacheSizeGB_ = 0.0;
	std::string gameID_;
	Path basePath_;
	Path newTextureDir_;
	ReplacedTextureHash hash_ = ReplacedTextureHash::QUICK;

	VFSBackend *vfs_ = nullptr;
	bool vfsIsZip_ = false;

	GPUFormatSupport formatSupport_{};

	typedef std::pair<int, int> WidthHeightPair;
	std::unordered_map<u64, WidthHeightPair> hashranges_;
	std::unordered_map<u64, float> reducehashranges_;

	std::unordered_map<ReplacementCacheKey, std::string> aliases_;
	std::unordered_map<ReplacementCacheKey, TextureFiltering> filtering_;

	std::unordered_map<ReplacementCacheKey, ReplacedTextureRef> cache_;
	std::unordered_map<ReplacementCacheKey, SavedTextureCacheData> savedCache_;

	// the key is either from aliases_, in which case it's a |-separated sequence of texture filenames of the levels of a texture.
	// alternatively the key is from the generated texture filename.
	std::unordered_map<std::string, ReplacedTexture *> levelCache_;
};
