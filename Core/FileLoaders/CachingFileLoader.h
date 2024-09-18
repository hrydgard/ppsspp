// Copyright (c) 2012- PPSSPP Project.

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
#include <mutex>
#include <thread>

#include "Common/CommonTypes.h"
#include "Core/Loaders.h"

class CachingFileLoader : public ProxiedFileLoader {
public:
	CachingFileLoader(FileLoader *backend);
	~CachingFileLoader();

	bool Exists() override;
	bool ExistsFast() override;
	bool IsDirectory() override;
	s64 FileSize() override;

	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(absolutePos, bytes * count, data, flags) / bytes;
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override;

private:
	void Prepare();
	void InitCache();
	void ShutdownCache();
	size_t ReadFromCache(s64 pos, size_t bytes, void *data);
	// Guaranteed to read at least one block into the cache.
	void SaveIntoCache(s64 pos, size_t bytes, Flags flags, bool readingAhead = false);
	bool MakeCacheSpaceFor(size_t blocks, bool readingAhead);
	void StartReadAhead(s64 pos);

	enum {
		BLOCK_SIZE = 65536,
		BLOCK_SHIFT = 16,
		MAX_BLOCKS_PER_READ = 16,
		MAX_BLOCKS_CACHED = 4096, // 256 MB
		BLOCK_READAHEAD = 4,
	};

	s64 filesize_ = 0;
	int exists_ = -1;
	int isDirectory_ = -1;
	u64 generation_;
	u64 oldestGeneration_;
	size_t cacheSize_;

	struct BlockInfo {
		u8 *ptr;
		u64 generation;

		BlockInfo() : ptr(nullptr), generation(0) {
		}
		BlockInfo(u8 *p) : ptr(p), generation(0) {
		}
	};

	std::map<s64, BlockInfo> blocks_;
	std::recursive_mutex blocksMutex_;
	bool aheadThreadRunning_ = false;
	std::thread aheadThread_;
	std::once_flag preparedFlag_;
};
