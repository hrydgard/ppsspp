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

#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "Common/Common.h"
#include "Common/MemoryUtil.h"
#include "GPU/ge_constants.h"

class TextureCacheCommon;
class TextureReplacer;

enum class ReplacedTextureFormat {
	F_5650,
	F_5551,
	F_4444,
	F_8888,
	F_0565_ABGR,
	F_1555_ABGR,
	F_4444_ABGR,
	F_8888_BGRA,
};

// These must match the constants in TextureCacheCommon.
enum class ReplacedTextureAlpha {
	UNKNOWN = 0x04,
	FULL = 0x00,
};

// For forward comatibility, we specify the hash.
enum class ReplacedTextureHash {
	// TODO: Maybe only support crc32c for now?
	QUICK,
	XXH32,
	XXH64,
};

struct ReplacedTextureLevel {
	int w;
	int h;
	ReplacedTextureFormat fmt;
	std::string file;
};

struct ReplacementCacheKey {
	u64 cachekey;
	u32 hash;

	ReplacementCacheKey(u64 c, u32 h) : cachekey(c), hash(h) {
	}

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

	ReplacementAliasKey(u64 c, u32 h, u32 l) : cachekey(c), level(l), hash(h) {
	}

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
	inline bool Valid() {
		return !levels_.empty();
	}

	bool GetSize(int level, int &w, int &h) {
		if ((size_t)level < levels_.size()) {
			w = levels_[level].w;
			h = levels_[level].h;
			return true;
		}
		return false;
	}

	int MaxLevel() {
		return (int)levels_.size() - 1;
	}

	ReplacedTextureFormat Format(int level) {
		if ((size_t)level < levels_.size()) {
			return levels_[level].fmt;
		}
		return ReplacedTextureFormat::F_8888;
	}

	u8 AlphaStatus() {
		return (u8)alphaStatus_;
	}

	void Load(int level, void *out, int rowPitch);

protected:
	std::vector<ReplacedTextureLevel> levels_;
	ReplacedTextureAlpha alphaStatus_;

	friend TextureReplacer;
};

struct ReplacedTextureDecodeInfo {
	u64 cachekey;
	u32 hash;
	u32 addr;
	bool isVideo;
	bool isFinal;
	int scaleFactor;
	ReplacedTextureFormat fmt;
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

	ReplacedTexture &FindReplacement(u64 cachekey, u32 hash, int w, int h);

	void NotifyTextureDecoded(const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int pitch, int level, int w, int h);

protected:
	bool LoadIni();
	void ParseHashRange(const std::string &key, const std::string &value);
	bool LookupHashRange(u32 addr, int &w, int &h);
	std::string LookupHashFile(u64 cachekey, u32 hash, int level);
	std::string HashName(u64 cachekey, u32 hash, int level);
	void PopulateReplacement(ReplacedTexture *result, u64 cachekey, u32 hash, int w, int h);

	SimpleBuf<u32> saveBuf;
	bool enabled_ = false;
	bool allowVideo_ = false;
	bool ignoreAddress_ = false;
	bool reduceHash_ = false;
	std::string gameID_;
	std::string basePath_;
	ReplacedTextureHash hash_ = ReplacedTextureHash::QUICK;
	typedef std::pair<int, int> WidthHeightPair;
	std::unordered_map<u64, WidthHeightPair> hashranges_;
	std::unordered_map<ReplacementAliasKey, std::string> aliases_;

	ReplacedTexture none_;
	std::unordered_map<ReplacementCacheKey, ReplacedTexture> cache_;
	std::unordered_map<ReplacementCacheKey, ReplacedTextureLevel> savedCache_;
};
