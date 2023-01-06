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

#include <algorithm>
#include <thread>
#include <cstring>
#include <cstdlib>

#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"
#include "Core/FileLoaders/RamCachingFileLoader.h"

#include "Common/Log.h"

// Takes ownership of backend.
RamCachingFileLoader::RamCachingFileLoader(FileLoader *backend)
	: ProxiedFileLoader(backend) {
	filesize_ = backend->FileSize();
	if (filesize_ > 0) {
		InitCache();
	}
}

RamCachingFileLoader::~RamCachingFileLoader() {
	if (filesize_ > 0) {
		ShutdownCache();
	}
}

bool RamCachingFileLoader::Exists() {
	if (exists_ == -1) {
		exists_ = ProxiedFileLoader::Exists() ? 1 : 0;
	}
	return exists_ == 1;
}

bool RamCachingFileLoader::ExistsFast() {
	if (exists_ == -1) {
		return ProxiedFileLoader::ExistsFast();
	}
	return exists_ == 1;
}

bool RamCachingFileLoader::IsDirectory() {
	if (isDirectory_ == -1) {
		isDirectory_ = ProxiedFileLoader::IsDirectory() ? 1 : 0;
	}
	return isDirectory_ == 1;
}

s64 RamCachingFileLoader::FileSize() {
	return filesize_;
}

size_t RamCachingFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags) {
	size_t readSize = 0;
	if (cache_ == nullptr || (flags & Flags::HINT_UNCACHED) != 0) {
		readSize = backend_->ReadAt(absolutePos, bytes, data, flags);
	} else {
		readSize = ReadFromCache(absolutePos, bytes, data);
		// While in case the cache size is too small for the entire read.
		while (readSize < bytes) {
			SaveIntoCache(absolutePos + readSize, bytes - readSize, flags);
			size_t bytesFromCache = ReadFromCache(absolutePos + readSize, bytes - readSize, (u8 *)data + readSize);
			readSize += bytesFromCache;
			if (bytesFromCache == 0) {
				// We can't read any more.
				break;
			}
		}

		StartReadAhead(absolutePos + readSize);
	}
	return readSize;
}

void RamCachingFileLoader::InitCache() {
	std::lock_guard<std::mutex> guard(blocksMutex_);
	u32 blockCount = (u32)((filesize_ + BLOCK_SIZE - 1) >> BLOCK_SHIFT);
	// Overallocate for the last block.
	cache_ = (u8 *)malloc((size_t)blockCount << BLOCK_SHIFT);
	if (cache_ == nullptr) {
		return;
	}
	aheadRemaining_ = blockCount;
	blocks_.resize(blockCount);
}

void RamCachingFileLoader::ShutdownCache() {
	Cancel();

	// We can't delete while the thread is running, so have to wait.
	// This should only happen from the menu.
	while (aheadThreadRunning_) {
		sleep_ms(1);
	}
	if (aheadThread_.joinable())
		aheadThread_.join();

	std::lock_guard<std::mutex> guard(blocksMutex_);
	blocks_.clear();
	if (cache_ != nullptr) {
		free(cache_);
		cache_ = nullptr;
	}
}

void RamCachingFileLoader::Cancel() {
	if (aheadThreadRunning_) {
		std::lock_guard<std::mutex> guard(blocksMutex_);
		aheadCancel_ = true;
	}

	ProxiedFileLoader::Cancel();
}

size_t RamCachingFileLoader::ReadFromCache(s64 pos, size_t bytes, void *data) {
	s64 cacheStartPos = pos >> BLOCK_SHIFT;
	s64 cacheEndPos = (pos + bytes - 1) >> BLOCK_SHIFT;
	if ((size_t)cacheEndPos >= blocks_.size()) {
		cacheEndPos = blocks_.size() - 1;
	}

	size_t readSize = 0;
	size_t offset = (size_t)(pos - (cacheStartPos << BLOCK_SHIFT));
	u8 *p = (u8 *)data;

	// Clamp bytes to what's actually available.
	if (pos + (s64)bytes > filesize_) {
		// Should've been caught above, but just in case.
		if (pos >= filesize_) {
			return 0;
		}
		bytes = (size_t)(filesize_ - pos);
	}

	std::lock_guard<std::mutex> guard(blocksMutex_);
	for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
		if (blocks_[(size_t)i] == 0) {
			return readSize;
		}

		size_t toRead = std::min(bytes - readSize, (size_t)BLOCK_SIZE - offset);
		s64 cachePos = (i << BLOCK_SHIFT) + offset;
		memcpy(p + readSize, &cache_[cachePos], toRead);
		readSize += toRead;

		// Don't need an offset after the first read.
		offset = 0;
	}
	return readSize;
}

void RamCachingFileLoader::SaveIntoCache(s64 pos, size_t bytes, Flags flags) {
	s64 cacheStartPos = pos >> BLOCK_SHIFT;
	s64 cacheEndPos = (pos + bytes - 1) >> BLOCK_SHIFT;
	if ((size_t)cacheEndPos >= blocks_.size()) {
		cacheEndPos = blocks_.size() - 1;
	}

	size_t blocksToRead = 0;
	{
		std::lock_guard<std::mutex> guard(blocksMutex_);
		for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
			if (blocks_[(size_t)i] == 0) {
				++blocksToRead;
				if (blocksToRead >= MAX_BLOCKS_PER_READ) {
					break;
				}
			}
		}
	}

	s64 cacheFilePos = cacheStartPos << BLOCK_SHIFT;
	size_t bytesRead = backend_->ReadAt(cacheFilePos, blocksToRead << BLOCK_SHIFT, &cache_[cacheFilePos], flags);

	// In case there was an error, let's not mark blocks that failed to read as read.
	u32 blocksActuallyRead = (u32)((bytesRead + BLOCK_SIZE - 1) >> BLOCK_SHIFT);
	{
		std::lock_guard<std::mutex> guard(blocksMutex_);

		// In case they were simultaneously read.
		u32 blocksRead = 0;
		for (size_t i = 0; i < blocksActuallyRead; ++i) {
			if (blocks_[(size_t)cacheStartPos + i] == 0) {
				blocks_[(size_t)cacheStartPos + i] = 1;
				++blocksRead;
			}
		}

		if (aheadRemaining_ != 0) {
			aheadRemaining_ -= blocksRead;
		}
	}
}

void RamCachingFileLoader::StartReadAhead(s64 pos) {
	if (cache_ == nullptr) {
		return;
	}

	std::lock_guard<std::mutex> guard(blocksMutex_);
	aheadPos_ = pos;
	if (aheadThreadRunning_) {
		// Already going.
		return;
	}

	aheadThreadRunning_ = true;
	aheadCancel_ = false;
	if (aheadThread_.joinable())
		aheadThread_.join();
	aheadThread_ = std::thread([this] {
		SetCurrentThreadName("FileLoaderReadAhead");

		AndroidJNIThreadContext jniContext;

		while (aheadRemaining_ != 0 && !aheadCancel_) {
			// Where should we look?
			const u32 cacheStartPos = NextAheadBlock();
			if (cacheStartPos == 0xFFFFFFFF) {
				// Must be full.
				break;
			}
			u32 cacheEndPos = cacheStartPos + BLOCK_READAHEAD - 1;
			if (cacheEndPos >= blocks_.size()) {
				cacheEndPos = (u32)blocks_.size() - 1;
			}

			for (u32 i = cacheStartPos; i <= cacheEndPos; ++i) {
				if (blocks_[i] == 0) {
					SaveIntoCache((u64)i << BLOCK_SHIFT, BLOCK_SIZE * BLOCK_READAHEAD, Flags::NONE);
					break;
				}
			}
		}

		aheadThreadRunning_ = false;
	});
}

u32 RamCachingFileLoader::NextAheadBlock() {
	std::lock_guard<std::mutex> guard(blocksMutex_);

	// If we had an aheadPos_ set, start reading from there and go forward.
	u32 startFrom = (u32)(aheadPos_ >> BLOCK_SHIFT);
	// But next time, start from the beginning again.
	aheadPos_ = 0;

	for (u32 i = startFrom; i < blocks_.size(); ++i) {
		if (blocks_[i] == 0) {
			return i;
		}
	}

	return 0xFFFFFFFF;
}
