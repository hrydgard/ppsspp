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
#include "base/mutex.h"
#include "Common/CommonTypes.h"
#include "Core/Loaders.h"

class RamCachingFileLoader : public FileLoader {
public:
	RamCachingFileLoader(FileLoader *backend);
	~RamCachingFileLoader() override;

	bool Exists() override;
	bool ExistsFast() override;
	bool IsDirectory() override;
	s64 FileSize() override;
	std::string Path() const override;

	void Seek(s64 absolutePos) override;
	size_t Read(size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(filepos_, bytes, count, data, flags);
	}
	size_t Read(size_t bytes, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(filepos_, bytes, data, flags);
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(absolutePos, bytes * count, data, flags) / bytes;
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override;

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

	s64 filesize_;
	s64 filepos_;
	FileLoader *backend_;
	u8 *cache_;
	int exists_;
	int isDirectory_;

	std::vector<u8> blocks_;
	recursive_mutex blocksMutex_;
	mutable recursive_mutex backendMutex_;
	u32 aheadRemaining_;
	s64 aheadPos_;
	bool aheadThread_;
};
