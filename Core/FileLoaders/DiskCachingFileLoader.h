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

#include <vector>
#include <map>
#include <mutex>

#include "Common/CommonTypes.h"
#include "Common/File/Path.h"
#include "Common/Swap.h"
#include "Core/Loaders.h"

class DiskCachingFileLoaderCache;

class DiskCachingFileLoader : public ProxiedFileLoader {
public:
	DiskCachingFileLoader(FileLoader *backend);
	~DiskCachingFileLoader();

	bool Exists() override;
	bool ExistsFast() override;
	s64 FileSize() override;

	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(absolutePos, bytes * count, data, flags) / bytes;
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override;

	static std::vector<Path> GetCachedPathsInUse();

private:
	void Prepare();
	void InitCache();
	void ShutdownCache();

	std::once_flag preparedFlag_;
	s64 filesize_ = 0;
	DiskCachingFileLoaderCache *cache_ = nullptr;

	// We don't support concurrent disk cache access (we use memory cached indexes.)
	// So we have to ensure there's only one of these per.
	static std::map<Path, DiskCachingFileLoaderCache *> caches_;
	static std::mutex cachesMutex_;
};

class DiskCachingFileLoaderCache {
public:
	DiskCachingFileLoaderCache(const Path &path, u64 filesize);
	~DiskCachingFileLoaderCache();

	bool IsValid() {
		return f_ != nullptr;
	}

	void AddRef() {
		++refCount_;
	}

	bool Release() {
		return --refCount_ == 0;
	}

	static void SetCacheDir(const Path &path) {
		cacheDir_ = path;
	}

	size_t ReadFromCache(s64 pos, size_t bytes, void *data);
	// Guaranteed to read at least one block into the cache.
	size_t SaveIntoCache(FileLoader *backend, s64 pos, size_t bytes, void *data, FileLoader::Flags flags);

	bool HasData() const;

private:
	void InitCache(const Path &path);
	void ShutdownCache();
	bool MakeCacheSpaceFor(size_t blocks);
	void RebalanceGenerations();
	u32 AllocateBlock(u32 indexPos);

	struct BlockInfo;
	bool ReadBlockData(u8 *dest, BlockInfo &info, size_t offset, size_t size);
	void WriteBlockData(BlockInfo &info, const u8 *src);
	void WriteIndexData(u32 indexPos, BlockInfo &info);
	s64 GetBlockOffset(u32 block);

	Path MakeCacheFilePath(const Path &filename);
	std::string MakeCacheFilename(const Path &path);
	bool LoadCacheFile(const Path &path);
	void LoadCacheIndex();
	void CreateCacheFile(const Path &path);
	bool LockCacheFile(bool lockStatus);
	bool RemoveCacheFile(const Path &path);
	void CloseFileHandle();

	u64 FreeDiskSpace();
	u32 DetermineMaxBlocks();
	u32 CountCachedFiles();
	void GarbageCollectCacheFiles(u64 goalBytes);

	// File format:
	// 64 magic
	// 32 version
	// 32 blockSize
	// 64 filesize
	// 32 maxBlocks
	// 32 flags
	// index[filesize / blockSize] <-- ~500 KB for 4GB
	//   32 (fileoffset - headersize) / blockSize -> -1=not present
	//   16 generation?
	//   16 hits?
	// blocks[up to maxBlocks]
	//   8 * blockSize

	enum {
		CACHE_VERSION = 3,
		DEFAULT_BLOCK_SIZE = 65536,
		MAX_BLOCKS_PER_READ = 16,
		MAX_BLOCKS_LOWER_BOUND = 256, // 16 MB
		MAX_BLOCKS_UPPER_BOUND = 8192, // 512 MB
		INVALID_BLOCK = 0xFFFFFFFF,
		INVALID_INDEX = 0xFFFFFFFF,
	};

	int refCount_ = 0;
	s64 filesize_;
	u32 blockSize_;
	u16 generation_;
	u16 oldestGeneration_;
	u32 maxBlocks_;
	u32 flags_;
	size_t cacheSize_;
	size_t indexCount_;
	std::mutex lock_;
	Path origPath_;

	struct FileHeader {
		char magic[8];
		u32_le version;
		u32_le blockSize;
		s64_le filesize;
		u32_le maxBlocks;
		u32_le flags;
	};

	enum FileFlags {
		FLAG_LOCKED = 1 << 0,
	};

	struct BlockInfo {
		u32 block;
		u16 generation;
		u16 hits;

		BlockInfo() : block(-1), generation(0), hits(0) {
		}
	};

	std::vector<BlockInfo> index_;
	std::vector<u32> blockIndexLookup_;

	FILE *f_ = nullptr;
	int fd_ = 0;

	static Path cacheDir_;
};
