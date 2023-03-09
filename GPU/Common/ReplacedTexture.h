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

#include <mutex>
#include <string>

#include "Common/File/VFS/VFS.h"
#include "Common/GPU/thin3d.h"

struct ReplacedLevelsCache;
class TextureReplacer;
class LimitedWaitable;

// These must match the constants in TextureCacheCommon.
enum class ReplacedTextureAlpha {
	UNKNOWN = 0x04,
	FULL = 0x00,
};

// For forward compatibility, we specify the hash.
enum class ReplacedTextureHash {
	QUICK,
	XXH32,
	XXH64,
};

enum class ReplacedImageType {
	PNG,
	ZIM,
	INVALID,
};

// Metadata about a given texture level.
struct ReplacedTextureLevel {
	int w = 0;
	int h = 0;
	Path file;

	// To be able to reload, we need to be able to reopen, unfortunate we can't use zip_file_t.
	// TODO: This really belongs on the level in the cache, not in the individual ReplacedTextureLevel objects.
	VFSFileReference *fileRef = nullptr;
};

ReplacedImageType Identify(VFSBackend *vfs, VFSOpenFile *openFile, std::string *outMagic);

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

	Draw::DataFormat Format() const {
		if (initDone_) {
			return fmt;
		} else {
			// Shouldn't get here.
			return Draw::DataFormat::UNDEFINED;
		}
	}

	u8 AlphaStatus() const {
		return (u8)alphaStatus_;
	}

	bool IsReady(double budget);
	bool CopyLevelTo(int level, void *out, int rowPitch);

protected:
	void Prepare(VFSBackend *vfs);
	void PrepareData(int level);
	void PurgeIfOlder(double t);

	std::vector<ReplacedTextureLevel> levels_;
	ReplacedLevelsCache *levelData_;

	ReplacedTextureAlpha alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
	double lastUsed_ = 0.0;
	LimitedWaitable *threadWaitable_ = nullptr;
	std::mutex mutex_;
	Draw::DataFormat fmt = Draw::DataFormat::UNDEFINED;  // NOTE: Right now, the only supported format is Draw::DataFormat::R8G8B8A8_UNORM.

	bool cancelPrepare_ = false;
	bool initDone_ = false;
	bool prepareDone_ = false;

	VFSBackend *vfs_ = nullptr;

	friend class TextureReplacer;
	friend class ReplacedTextureTask;
};
