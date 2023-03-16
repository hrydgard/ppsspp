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
#include "Common/Log.h"

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
	DDS,
	BASIS,  // TODO: Might not even do this, KTX2 is a better container.
	KTX2,
	INVALID,
};

static const int MAX_REPLACEMENT_MIP_LEVELS = 12;  // 12 should be plenty, 8 is the max mip levels supported by the PSP.

enum class ReplacementState : uint32_t {
	UNINITIALIZED,
	POPULATED,  // We located the texture files but have not started the thread.
	PENDING,
	NOT_FOUND,  // Also used on error loading the images.
	ACTIVE,
	CANCEL_INIT,
};

const char *StateString(ReplacementState state);

struct GPUFormatSupport {
	bool bc123;
	bool astc;
	bool bc7;
	bool etc2;
};

struct ReplacementDesc {
	int newW;
	int newH;
	uint64_t cachekey;
	uint32_t hash;
	int w;
	int h;
	std::string hashfiles;
	Path basePath;
	std::vector<std::string> filenames;
	std::string logId;
	GPUFormatSupport formatSupport;
};

class ReplacedTexture;

// These aren't actually all replaced, they can also represent a placeholder for a not-found
// replacement (texture == nullptr).
struct ReplacedTextureRef {
	ReplacedTexture *texture;  // shortcut
	std::string hashfiles;  // key into the cache
};

// Metadata about a given texture level.
struct ReplacedTextureLevel {
	int w = 0;
	int h = 0;

	// To be able to reload, we need to be able to reopen, unfortunate we can't use zip_file_t.
	// TODO: This really belongs on the level in the cache, not in the individual ReplacedTextureLevel objects.
	VFSFileReference *fileRef = nullptr;
};

class ReplacedTexture {
public:
	ReplacedTexture(VFSBackend *vfs, const ReplacementDesc &desc);
	~ReplacedTexture();

	inline ReplacementState State() const {
		return state_;
	}

	void SetState(ReplacementState state) {
		_dbg_assert_(state != state_);
#ifdef _DEBUG
		// WARN_LOG(G3D, "Texture %s changed state from %s to %s", logId_.c_str(), StateString(state_), StateString(state));
#endif
		state_ = state;
	}

	void GetSize(int level, int *w, int *h) const {
		_dbg_assert_(State() == ReplacementState::ACTIVE);
		_dbg_assert_(level < levels_.size());
		*w = levels_[level].w;
		*h = levels_[level].h;
	}

	int GetLevelDataSize(int level) const {
		_dbg_assert_(State() == ReplacementState::ACTIVE);
		return (int)data_[level].size();
	}

	size_t GetTotalDataSize() const {
		if (State() != ReplacementState::ACTIVE) {
			return 0;
		}
		size_t sz = 0;
		for (auto &data : data_) {
			sz += data.size();
		}
		return sz;
	}

	int NumLevels() const {
		_dbg_assert_(State() == ReplacementState::ACTIVE);
		return (int)levels_.size();
	}

	Draw::DataFormat Format() const {
		_dbg_assert_(State() == ReplacementState::ACTIVE);
		return fmt;
	}

	u8 AlphaStatus() const {
		return (u8)alphaStatus_;
	}

	bool IsReady(double budget);
	bool CopyLevelTo(int level, void *out, int rowPitch);

	std::string logId_;

private:
	enum class LoadLevelResult {
		LOAD_ERROR = 0,
		CONTINUE = 1,
		DONE = 2,
	};

	void Prepare(VFSBackend *vfs);
	LoadLevelResult LoadLevelData(VFSFileReference *fileRef, const std::string &filename, int level, Draw::DataFormat *pixelFormat);
	void PurgeIfNotUsedSinceTime(double t);

	std::vector<std::vector<uint8_t>> data_;
	std::vector<ReplacedTextureLevel> levels_;

	double lastUsed_ = 0.0;
	LimitedWaitable *threadWaitable_ = nullptr;
	std::mutex lock_;
	Draw::DataFormat fmt = Draw::DataFormat::UNDEFINED;  // NOTE: Right now, the only supported format is Draw::DataFormat::R8G8B8A8_UNORM.
	ReplacedTextureAlpha alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
	double lastUsed = 0.0;

	std::atomic<ReplacementState> state_ = ReplacementState::POPULATED;

	VFSBackend *vfs_ = nullptr;
	ReplacementDesc desc_;

	friend class TextureReplacer;
	friend class ReplacedTextureTask;
};
