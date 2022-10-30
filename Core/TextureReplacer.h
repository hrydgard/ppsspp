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
#include <vector>

#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/MemoryUtil.h"
#include "Common/File/Path.h"
#include "Common/GPU/DataFormat.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/ge_constants.h"

class IniFile;
class TextureCacheCommon;
class TextureReplacer;
class ReplacedTextureTask;
class LimitedWaitable;
struct zip;

// These must match the constants in TextureCacheCommon.
enum class ReplacedTextureAlpha {
	UNKNOWN = 0x04,
	FULL = 0x00,
};

// For forward comatibility, we specify the hash.
enum class ReplacedTextureHash {
	QUICK,
	XXH32,
	XXH64,
};

struct ReplacedTextureLevel {
	int w;
	int h;
	Draw::DataFormat fmt;  // NOTE: Right now, the only supported format is Draw::DataFormat::R8G8B8A8_UNORM.
	Path file;
	// Can be ignored for hashing/equal, since file has all uniqueness.
	// To be able to reload, we need to be able to reopen, unfortunate we can't use zip_file_t.
	zip *z = nullptr;
	int64_t zi = -1;

	bool operator ==(const ReplacedTextureLevel &other) const {
		if (w != other.w || h != other.h || fmt != other.fmt)
			return false;
		return file == other.file;
	}
};

namespace std {
	template <>
	struct hash<ReplacedTextureLevel> {
		std::size_t operator()(const ReplacedTextureLevel &k) const {
#if PPSSPP_ARCH(64BIT)
			uint64_t v = (uint64_t)k.w | ((uint64_t)k.h << 32);
			v = __rotl64(v ^ (uint64_t)k.fmt, 13);
#else
			uint32_t v = k.w ^ (uint32_t)k.fmt;
			v = __rotl(__rotl(v, 13) ^ k.h, 13);
#endif
			return v ^ hash<string>()(k.file.ToString());
		}
	};
}

struct ReplacedLevelCache {
	std::mutex lock;
	std::vector<uint8_t> data;
	double lastUsed = 0.0;
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

struct ReplacementAliasKey {
	u64 cachekey;
	union {
		u64 hashAndLevel;
		struct {
			u32 level;
			u32 hash;
		};
	};

	ReplacementAliasKey(u64 c, u32 h, u32 l) : cachekey(c), level(l), hash(h) { }

	bool operator ==(const ReplacementAliasKey &k) const {
		return k.cachekey == cachekey && k.hashAndLevel == hashAndLevel;
	}

	bool operator <(const ReplacementAliasKey &k) const {
		if (k.cachekey == cachekey) {
			return k.hashAndLevel < hashAndLevel;
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

	template <>
	struct hash<ReplacementAliasKey> {
		size_t operator()(const ReplacementAliasKey &k) const {
			return std::hash<u64>()(k.cachekey ^ k.hashAndLevel);
		}
	};
}

struct ReplacedTexture {
	~ReplacedTexture();

	inline bool Valid() const {
		if (!initDone_)
			return false;
		return !levels_.empty();
	}

	inline bool IsInvalid() const {
		if (!initDone_)
			return false;
		return levels_.empty();
	}

	bool GetSize(int level, int &w, int &h) const {
		if (!initDone_)
			return false;
		if ((size_t)level < levels_.size()) {
			w = levels_[level].w;
			h = levels_[level].h;
			return true;
		}
		return false;
	}

	int NumLevels() const {
		if (!initDone_)
			return 0;
		return (int)levels_.size();
	}

	Draw::DataFormat Format(int level) const {
		if (initDone_) {
			if ((size_t)level < levels_.size()) {
				return levels_[level].fmt;
			}
		}
		return Draw::DataFormat::R8G8B8A8_UNORM;
	}

	u8 AlphaStatus() const {
		return (u8)alphaStatus_;
	}

	bool IsReady(double budget);

	bool Load(int level, void *out, int rowPitch);

protected:
	void Prepare();
	void PrepareData(int level);
	void PurgeIfOlder(double t);

	std::vector<ReplacedTextureLevel> levels_;
	std::vector<ReplacedLevelCache *> levelData_;
	ReplacedTextureAlpha alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
	double lastUsed_ = 0.0;
	LimitedWaitable *threadWaitable_ = nullptr;
	std::mutex mutex_;
	bool cancelPrepare_ = false;
	bool initDone_ = false;
	bool prepareDone_ = false;

	friend TextureReplacer;
	friend ReplacedTextureTask;
};

struct ReplacedTextureDecodeInfo {
	u64 cachekey;
	u32 hash;
	u32 addr;
	bool isVideo;
	bool isFinal;
	int scaleFactor;
	Draw::DataFormat fmt;
};

enum class ReplacerDecimateMode {
	NEW_FRAME,
	FORCE_PRESSURE,
	ALL,
};

class TextureReplacer {
public:
	TextureReplacer();
	~TextureReplacer();

	void Init();
	void NotifyConfigChanged();

	inline bool Enabled() {
		return enabled_;
	}

	u32 ComputeHash(u32 addr, int bufw, int w, int h, GETextureFormat fmt, u16 maxSeenV);

	ReplacedTexture &FindReplacement(u64 cachekey, u32 hash, int w, int h, double budget);
	bool FindFiltering(u64 cachekey, u32 hash, TextureFiltering *forceFiltering);
	ReplacedTexture &FindNone() {
		return none_;
	}

	// Check if a NotifyTextureDecoded for this texture is desired (used to avoid reads from write-combined memory.)
	bool WillSave(const ReplacedTextureDecodeInfo &replacedInfo);

	// Notify that a new texture was decoded.  May already be upscaled, saves the data passed.
	void NotifyTextureDecoded(const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int pitch, int level, int w, int h);

	void Decimate(ReplacerDecimateMode mode);

	static bool GenerateIni(const std::string &gameID, Path &generatedFilename);
	static bool IniExists(const std::string &gameID);

protected:
	bool LoadIni();
	bool LoadIniValues(IniFile &ini, bool isOverride = false);
	void ParseHashRange(const std::string &key, const std::string &value);
	void ParseFiltering(const std::string &key, const std::string &value);
	void ParseReduceHashRange(const std::string& key, const std::string& value);
	bool LookupHashRange(u32 addr, int &w, int &h);
	float LookupReduceHashRange(int& w, int& h);
	std::string LookupHashFile(u64 cachekey, u32 hash, int level);
	std::string HashName(u64 cachekey, u32 hash, int level);
	void PopulateReplacement(ReplacedTexture *result, u64 cachekey, u32 hash, int w, int h);
	bool PopulateLevelFromPath(ReplacedTextureLevel &level, bool ignoreError);
	bool PopulateLevelFromZip(ReplacedTextureLevel &level, bool ignoreError);

	bool enabled_ = false;
	bool allowVideo_ = false;
	bool ignoreAddress_ = false;
	bool reduceHash_ = false;
	float reduceHashSize = 1.0; // default value with reduceHash to false
	float reduceHashGlobalValue = 0.5; // Global value for textures dump pngs of all sizes, 0.5 by default but can be set in textures.ini
	double lastTextureCacheSizeGB_ = 0.0;
	bool ignoreMipmap_ = false;
	std::string gameID_;
	Path basePath_;
	ReplacedTextureHash hash_ = ReplacedTextureHash::QUICK;
	zip *zip_ = nullptr;

	typedef std::pair<int, int> WidthHeightPair;
	std::unordered_map<u64, WidthHeightPair> hashranges_;
	std::unordered_map<u64, float> reducehashranges_;
	std::unordered_map<ReplacementAliasKey, std::string> aliases_;
	std::unordered_map<ReplacementCacheKey, TextureFiltering> filtering_;

	ReplacedTexture none_;
	std::unordered_map<ReplacementCacheKey, ReplacedTexture> cache_;
	std::unordered_map<ReplacementCacheKey, std::pair<ReplacedTextureLevel, double>> savedCache_;
	std::unordered_map<ReplacedTextureLevel, ReplacedLevelCache> levelCache_;
};
