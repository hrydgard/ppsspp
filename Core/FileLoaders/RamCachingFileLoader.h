// Copyright (c) 2015- PPSSPP Project.

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

#include <vector>
#include <mutex>
#include <thread>

#include "Common/CommonTypes.h"
#include "Core/Loaders.h"

class RamCachingFileLoader : public ProxiedFileLoader {
public:
	RamCachingFileLoader(FileLoader *backend);
	~RamCachingFileLoader();

	bool Exists() override;
	bool ExistsFast() override;
	bool IsDirectory() override;
	s64 FileSize() override;

	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(absolutePos, bytes * count, data, flags) / bytes;
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override;

	void Cancel() override;

private:
	void InitCache();
	void ShutdownCache();
	size_t ReadFromCache(s64 pos, size_t bytes, void *data);
	// Guaranteed to read at least one block into the cache.
	void SaveIntoCache(s64 pos, size_t bytes, Flags flags);
	void StartReadAhead(s64 pos);
	u32 NextAheadBlock();

	enum {
		BLOCK_SIZE = 65536,
		BLOCK_SHIFT = 16,
		MAX_BLOCKS_PER_READ = 16,
		BLOCK_READAHEAD = 4,
	};

	s64 filesize_ = 0;
	u8 *cache_ = nullptr;
	int exists_ = -1;
	int isDirectory_ = -1;

	std::vector<u8> blocks_;
	std::mutex blocksMutex_;
	u32 aheadRemaining_;
	s64 aheadPos_;
	std::thread aheadThread_;
	bool aheadThreadRunning_ = false;
	bool aheadCancel_ = false;
};
