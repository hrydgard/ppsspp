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

#include "ppsspp_config.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <mutex>
#include <cstring>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/DiskFree.h"
#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Log.h"
#include "Common/CommonWindows.h"
#include "Core/FileLoaders/DiskCachingFileLoader.h"
#include "Core/System.h"

#if PPSSPP_PLATFORM(UWP)
#include <fileapifromapp.h>
#endif

#if PPSSPP_PLATFORM(SWITCH)
// Far from optimal, but I guess it works...
#define fseeko fseek
#endif

static const char * const CACHEFILE_MAGIC = "ppssppDC";
static const s64 SAFETY_FREE_DISK_SPACE = 768 * 1024 * 1024; // 768 MB
// Aim to allow this many files cached at once.
static const u32 CACHE_SPACE_FLEX = 4;

Path DiskCachingFileLoaderCache::cacheDir_;

std::map<Path, DiskCachingFileLoaderCache *> DiskCachingFileLoader::caches_;
std::mutex DiskCachingFileLoader::cachesMutex_;

// Takes ownership of backend.
DiskCachingFileLoader::DiskCachingFileLoader(FileLoader *backend)
	: ProxiedFileLoader(backend) {
}

void DiskCachingFileLoader::Prepare() {
	std::call_once(preparedFlag_, [this]() {
		filesize_ = ProxiedFileLoader::FileSize();
		if (filesize_ > 0) {
			InitCache();
		}
	});
}

DiskCachingFileLoader::~DiskCachingFileLoader() {
	if (filesize_ > 0) {
		ShutdownCache();
	}
}

bool DiskCachingFileLoader::Exists() {
	Prepare();
	return ProxiedFileLoader::Exists();
}

bool DiskCachingFileLoader::ExistsFast() {
	// It may require a slow operation to check - if we have data, let's say yes.
	// This helps initial load, since we check each recent file for existence.
	return true;
}

s64 DiskCachingFileLoader::FileSize() {
	Prepare();
	return filesize_;
}

size_t DiskCachingFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags) {
	Prepare();
	size_t readSize;

	if (absolutePos >= filesize_) {
		bytes = 0;
	} else if (absolutePos + (s64)bytes >= filesize_) {
		bytes = (size_t)(filesize_ - absolutePos);
	}

	if (cache_ && cache_->IsValid() && (flags & Flags::HINT_UNCACHED) == 0) {
		readSize = cache_->ReadFromCache(absolutePos, bytes, data);
		// While in case the cache size is too small for the entire read.
		while (readSize < bytes) {
			readSize += cache_->SaveIntoCache(backend_, absolutePos + readSize, bytes - readSize, (u8 *)data + readSize, flags);
			// We're done, nothing more to read.
			if (readSize == bytes) {
				break;
			}
			// If there are already-cached blocks afterward, we have to read them.
			size_t bytesFromCache = cache_->ReadFromCache(absolutePos + readSize, bytes - readSize, (u8 *)data + readSize);
			readSize += bytesFromCache;
			if (bytesFromCache == 0) {
				// We can't read any more.
				break;
			}
		}
	} else {
		readSize = backend_->ReadAt(absolutePos, bytes, data, flags);
	}

	return readSize;
}

std::vector<Path> DiskCachingFileLoader::GetCachedPathsInUse() {
	std::lock_guard<std::mutex> guard(cachesMutex_);

	// This is on the file loader so that it can manage the caches_.
	std::vector<Path> files;
	files.reserve(caches_.size());

	for (auto it : caches_) {
		files.push_back(it.first);
	}

	return files;
}

void DiskCachingFileLoader::InitCache() {
	std::lock_guard<std::mutex> guard(cachesMutex_);

	Path path = ProxiedFileLoader::GetPath();
	auto &entry = caches_[path];
	if (!entry) {
		entry = new DiskCachingFileLoaderCache(path, filesize_);
	}

	cache_ = entry;
	cache_->AddRef();
}

void DiskCachingFileLoader::ShutdownCache() {
	std::lock_guard<std::mutex> guard(cachesMutex_);

	if (cache_->Release()) {
		// If it ran out of counts, delete it.
		delete cache_;
		caches_.erase(ProxiedFileLoader::GetPath());
	}
	cache_ = nullptr;
}

DiskCachingFileLoaderCache::DiskCachingFileLoaderCache(const Path &path, u64 filesize)
	: filesize_(filesize), origPath_(path) {
	InitCache(path);
}

DiskCachingFileLoaderCache::~DiskCachingFileLoaderCache() {
	ShutdownCache();
}

void DiskCachingFileLoaderCache::InitCache(const Path &filename) {
	cacheSize_ = 0;
	indexCount_ = 0;
	oldestGeneration_ = 0;
	maxBlocks_ = MAX_BLOCKS_LOWER_BOUND;
	flags_ = 0;
	generation_ = 0;

	const Path cacheFilePath = MakeCacheFilePath(filename);
	bool fileLoaded = LoadCacheFile(cacheFilePath);

	// We do some basic locking to protect against two things: crashes and concurrency.
	// Concurrency will break the file.  Crashes will probably leave it inconsistent.
	if (fileLoaded && !LockCacheFile(true)) {
		if (RemoveCacheFile(cacheFilePath)) {
			// Create a new one.
			fileLoaded = false;
		} else {
			// Couldn't remove, in use?  Give up on caching.
			CloseFileHandle();
		}
	}
	if (!fileLoaded) {
		CreateCacheFile(cacheFilePath);

		if (!LockCacheFile(true)) {
			CloseFileHandle();
		}
	}
}

void DiskCachingFileLoaderCache::ShutdownCache() {
	if (f_) {
		bool failed = false;
		if (fseek(f_, sizeof(FileHeader), SEEK_SET) != 0) {
			failed = true;
		} else if (fwrite(&index_[0], sizeof(BlockInfo), indexCount_, f_) != indexCount_) {
			failed = true;
		} else if (fflush(f_) != 0) {
			failed = true;
		}
		if (failed) {
			// Leave it locked, it's broken.
			ERROR_LOG(Log::Loader, "Unable to flush disk cache.");
		} else {
			LockCacheFile(false);
		}
		CloseFileHandle();
	}

	index_.clear();
	blockIndexLookup_.clear();
	cacheSize_ = 0;
}

size_t DiskCachingFileLoaderCache::ReadFromCache(s64 pos, size_t bytes, void *data) {
	std::lock_guard<std::mutex> guard(lock_);

	if (!f_) {
		return 0;
	}

	size_t cacheStartPos = (size_t)(pos / blockSize_);
	size_t cacheEndPos = (size_t)((pos + bytes - 1) / blockSize_);
	size_t readSize = 0;
	size_t offset = (size_t)(pos - (cacheStartPos * (u64)blockSize_));
	u8 *p = (u8 *)data;

	for (size_t i = cacheStartPos; i <= cacheEndPos; ++i) {
		auto &info = index_[i];
		if (info.block == INVALID_BLOCK) {
			return readSize;
		}
		info.generation = generation_;
		if (info.hits < std::numeric_limits<u16>::max()) {
			++info.hits;
		}

		size_t toRead = std::min(bytes - readSize, (size_t)blockSize_ - offset);
		if (!ReadBlockData(p + readSize, info, offset, toRead)) {
			return readSize;
		}
		readSize += toRead;

		// Don't need an offset after the first read.
		offset = 0;
	}
	return readSize;
}

size_t DiskCachingFileLoaderCache::SaveIntoCache(FileLoader *backend, s64 pos, size_t bytes, void *data, FileLoader::Flags flags) {
	std::lock_guard<std::mutex> guard(lock_);

	if (!f_) {
		// Just to keep things working.
		return backend->ReadAt(pos, bytes, data, flags);
	}

	size_t cacheStartPos = (size_t)(pos / blockSize_);
	size_t cacheEndPos = (size_t)((pos + bytes - 1) / blockSize_);
	size_t readSize = 0;
	size_t offset = (size_t)(pos - (cacheStartPos * (u64)blockSize_));
	u8 *p = (u8 *)data;

	size_t blocksToRead = 0;
	for (size_t i = cacheStartPos; i <= cacheEndPos; ++i) {
		auto &info = index_[i];
		if (info.block != INVALID_BLOCK) {
			break;
		}
		++blocksToRead;
		if (blocksToRead >= MAX_BLOCKS_PER_READ) {
			break;
		}
	}

	if (!MakeCacheSpaceFor(blocksToRead) || blocksToRead == 0) {
		return 0;
	}

	if (blocksToRead == 1) {
		auto &info = index_[cacheStartPos];

		u8 *buf = new u8[blockSize_];
		size_t readBytes = backend->ReadAt(cacheStartPos * (u64)blockSize_, blockSize_, buf, flags);

		// Check if it was written while we were busy.  Might happen if we thread.
		if (info.block == INVALID_BLOCK && readBytes != 0) {
			info.block = AllocateBlock((u32)cacheStartPos);
			WriteBlockData(info, buf);
			WriteIndexData((u32)cacheStartPos, info);
		}

		size_t toRead = std::min(bytes - readSize, (size_t)blockSize_ - offset);
		memcpy(p + readSize, buf + offset, toRead);
		readSize += toRead;

		delete [] buf;
	} else {
		u8 *wholeRead = new u8[blocksToRead * blockSize_];
		size_t readBytes = backend->ReadAt(cacheStartPos * (u64)blockSize_, blocksToRead * blockSize_, wholeRead, flags);

		for (size_t i = 0; i < blocksToRead; ++i) {
			auto &info = index_[cacheStartPos + i];
			// Check if it was written while we were busy.  Might happen if we thread.
			if (info.block == INVALID_BLOCK && readBytes != 0) {
				info.block = AllocateBlock((u32)cacheStartPos + (u32)i);
				WriteBlockData(info, wholeRead + (i * blockSize_));
				// TODO: Doing each index together would probably be better.
				WriteIndexData((u32)cacheStartPos + (u32)i, info);
			}

			size_t toRead = std::min(bytes - readSize, (size_t)blockSize_ - offset);
			memcpy(p + readSize, wholeRead + (i * blockSize_) + offset, toRead);
			readSize += toRead;
		}
		delete[] wholeRead;
	}

	cacheSize_ += blocksToRead;
	++generation_;

	if (generation_ == std::numeric_limits<u16>::max()) {
		RebalanceGenerations();
	}

	return readSize;
}

bool DiskCachingFileLoaderCache::MakeCacheSpaceFor(size_t blocks) {
	size_t goal = (size_t)maxBlocks_ - blocks;

	while (cacheSize_ > goal) {
		u16 minGeneration = generation_;

		// We increment the iterator inside because we delete things inside.
		for (size_t i = 0; i < blockIndexLookup_.size(); ++i) {
			if (blockIndexLookup_[i] == INVALID_INDEX) {
				continue;
			}
			auto &info = index_[blockIndexLookup_[i]];

			// Check for the minimum seen generation.
			// TODO: Do this smarter?
			if (info.generation != 0 && info.generation < minGeneration) {
				minGeneration = info.generation;
			}

			// 0 means it was never used yet or was the first read (e.g. block descriptor.)
			if (info.generation == oldestGeneration_ || info.generation == 0) {
				info.block = INVALID_BLOCK;
				info.generation = 0;
				info.hits = 0;
				--cacheSize_;

				// TODO: Doing this in chunks might be a lot better.
				WriteIndexData(blockIndexLookup_[i], info);
				blockIndexLookup_[i] = INVALID_INDEX;

				// Keep going?
				if (cacheSize_ <= goal) {
					break;
				}
			}
		}

		// If we didn't find any, update to the lowest we did find.
		oldestGeneration_ = minGeneration;
	}

	return true;
}

void DiskCachingFileLoaderCache::RebalanceGenerations() {
	// To make things easy, we will subtract oldestGeneration_ and cut in half.
	// That should give us more space but not break anything.

	for (size_t i = 0; i < index_.size(); ++i) {
		auto &info = index_[i];
		if (info.block == INVALID_BLOCK) {
			continue;
		}

		if (info.generation > oldestGeneration_) {
			info.generation = (info.generation - oldestGeneration_) / 2;
			// TODO: Doing this all at once would be much better.
			WriteIndexData((u32)i, info);
		}
	}

	oldestGeneration_ = 0;
}

u32 DiskCachingFileLoaderCache::AllocateBlock(u32 indexPos) {
	for (size_t i = 0; i < blockIndexLookup_.size(); ++i) {
		if (blockIndexLookup_[i] == INVALID_INDEX) {
			blockIndexLookup_[i] = indexPos;
			return (u32)i;
		}
	}

	_dbg_assert_msg_(false, "Not enough free blocks");
	return INVALID_BLOCK;
}

std::string DiskCachingFileLoaderCache::MakeCacheFilename(const Path &path) {
	static const char *const invalidChars = "?*:/\\^|<>\"'";
	std::string filename = path.ToString();
	for (size_t i = 0; i < filename.size(); ++i) {
		int c = filename[i];
		if (strchr(invalidChars, c) != nullptr) {
			filename[i] = '_';
		}
	}
	return filename + ".ppdc";
}

::Path DiskCachingFileLoaderCache::MakeCacheFilePath(const Path &filename) {
	Path dir = cacheDir_;
	if (dir.empty()) {
		dir = GetSysDirectory(DIRECTORY_CACHE);
	}

	if (!File::Exists(dir)) {
		File::CreateFullPath(dir);
	}

	return dir / MakeCacheFilename(filename);
}

s64 DiskCachingFileLoaderCache::GetBlockOffset(u32 block) {
	// This is where the blocks start.
	s64 blockOffset = (s64)sizeof(FileHeader) + (s64)indexCount_ * (s64)sizeof(BlockInfo);
	// Now to the actual block.
	return blockOffset + (s64)block * (s64)blockSize_;
}

bool DiskCachingFileLoaderCache::ReadBlockData(u8 *dest, BlockInfo &info, size_t offset, size_t size) {
	if (!f_) {
		return false;
	}
	if (size == 0) {
		return true;
	}
	s64 blockOffset = GetBlockOffset(info.block);

	// Before we read, make sure the buffers are flushed.
	// We might be trying to read an area we've recently written.
	fflush(f_);

	bool failed = false;
#ifdef __ANDROID__
	if (lseek64(fd_, blockOffset, SEEK_SET) != blockOffset) {
		failed = true;
	} else if (read(fd_, dest + offset, size) != (ssize_t)size) {
		failed = true;
	}
#else
	if (fseeko(f_, blockOffset, SEEK_SET) != 0) {
		failed = true;
	} else if (fread(dest + offset, size, 1, f_) != 1) {
		failed = true;
	}
#endif

	if (failed) {
		ERROR_LOG(Log::Loader, "Unable to read disk cache data entry.");
		CloseFileHandle();
	}
	return !failed;
}

void DiskCachingFileLoaderCache::WriteBlockData(BlockInfo &info, const u8 *src) {
	if (!f_) {
		return;
	}
	s64 blockOffset = GetBlockOffset(info.block);

	bool failed = false;
#ifdef __ANDROID__
	if (lseek64(fd_, blockOffset, SEEK_SET) != blockOffset) {
		failed = true;
	} else if (write(fd_, src, blockSize_) != (ssize_t)blockSize_) {
		failed = true;
	}
#else
	if (fseeko(f_, blockOffset, SEEK_SET) != 0) {
		failed = true;
	} else if (fwrite(src, blockSize_, 1, f_) != 1) {
		failed = true;
	}
#endif

	if (failed) {
		ERROR_LOG(Log::Loader, "Unable to write disk cache data entry.");
		CloseFileHandle();
	}
}

void DiskCachingFileLoaderCache::WriteIndexData(u32 indexPos, BlockInfo &info) {
	if (!f_) {
		return;
	}

	u32 offset = (u32)sizeof(FileHeader) + indexPos * (u32)sizeof(BlockInfo);

	bool failed = false;
	if (fseek(f_, offset, SEEK_SET) != 0) {
		failed = true;
	} else if (fwrite(&info, sizeof(BlockInfo), 1, f_) != 1) {
		failed = true;
	}

	if (failed) {
		ERROR_LOG(Log::Loader, "Unable to write disk cache index entry.");
		CloseFileHandle();
	}
}

bool DiskCachingFileLoaderCache::LoadCacheFile(const Path &path) {
	FILE *fp = File::OpenCFile(path, "rb+");
	if (!fp) {
		return false;
	}

	FileHeader header;
	bool valid = true;
	if (fread(&header, sizeof(FileHeader), 1, fp) != 1) {
		valid = false;
	} else if (memcmp(header.magic, CACHEFILE_MAGIC, sizeof(header.magic)) != 0) {
		valid = false;
	} else if (header.version != CACHE_VERSION) {
		valid = false;
	} else if (header.filesize != filesize_) {
		valid = false;
	} else if (header.maxBlocks < MAX_BLOCKS_LOWER_BOUND || header.maxBlocks > MAX_BLOCKS_UPPER_BOUND) {
		// This means it's not in our safety bounds, reject.
		valid = false;
	}

	// If it's valid, retain the file pointer.
	if (valid) {
		f_ = fp;

#ifdef __ANDROID__
		// Android NDK does not support 64-bit file I/O using C streams
		fd_ = fileno(f_);
#endif

		// Now let's load the index.
		blockSize_ = header.blockSize;
		maxBlocks_ = header.maxBlocks;
		flags_ = header.flags;
		LoadCacheIndex();
	} else {
		ERROR_LOG(Log::Loader, "Disk cache file header did not match, recreating cache file");
		fclose(fp);
	}

	return valid;
}

void DiskCachingFileLoaderCache::LoadCacheIndex() {
	if (fseek(f_, sizeof(FileHeader), SEEK_SET) != 0) {
		CloseFileHandle();
		return;
	}

	indexCount_ = (size_t)((filesize_ + blockSize_ - 1) / blockSize_);
	index_.resize(indexCount_);
	blockIndexLookup_.resize(maxBlocks_);
	memset(&blockIndexLookup_[0], INVALID_INDEX, maxBlocks_ * sizeof(blockIndexLookup_[0]));

	if (fread(&index_[0], sizeof(BlockInfo), indexCount_, f_) != indexCount_) {
		CloseFileHandle();
		return;
	}

	// Now let's set some values we need.
	oldestGeneration_ = std::numeric_limits<u16>::max();
	generation_ = 0;
	cacheSize_ = 0;

	for (size_t i = 0; i < index_.size(); ++i) {
		if (index_[i].block > maxBlocks_) {
			index_[i].block = INVALID_BLOCK;
		}
		if (index_[i].block == INVALID_BLOCK) {
			continue;
		}

		if (index_[i].generation < oldestGeneration_) {
			oldestGeneration_ = index_[i].generation;
		}
		if (index_[i].generation > generation_) {
			generation_ = index_[i].generation;
		}
		++cacheSize_;

		blockIndexLookup_[index_[i].block] = (u32)i;
	}
}

void DiskCachingFileLoaderCache::CreateCacheFile(const Path &path) {
	maxBlocks_ = DetermineMaxBlocks();
	if (maxBlocks_ < MAX_BLOCKS_LOWER_BOUND) {
		GarbageCollectCacheFiles(MAX_BLOCKS_LOWER_BOUND * DEFAULT_BLOCK_SIZE);
		maxBlocks_ = DetermineMaxBlocks();
	}
	if (maxBlocks_ < MAX_BLOCKS_LOWER_BOUND) {
		// There's not enough free space to cache, disable.
		f_ = nullptr;
		ERROR_LOG(Log::Loader, "Not enough free space; disabling disk cache");
		return;
	}
	flags_ = 0;

	f_ = File::OpenCFile(path, "wb+");
	if (!f_) {
		ERROR_LOG(Log::Loader, "Could not create disk cache file");
		return;
	}
#ifdef __ANDROID__
	// Android NDK does not support 64-bit file I/O using C streams
	fd_ = fileno(f_);
#endif

	blockSize_ = DEFAULT_BLOCK_SIZE;

	FileHeader header;
	memcpy(header.magic, CACHEFILE_MAGIC, sizeof(header.magic));
	header.version = CACHE_VERSION;
	header.blockSize = blockSize_;
	header.filesize = filesize_;
	header.maxBlocks = maxBlocks_;
	header.flags = flags_;

	if (fwrite(&header, sizeof(header), 1, f_) != 1) {
		CloseFileHandle();
		return;
	}

	indexCount_ = (size_t)((filesize_ + blockSize_ - 1) / blockSize_);
	index_.clear();
	index_.resize(indexCount_);
	blockIndexLookup_.resize(maxBlocks_);
	memset(&blockIndexLookup_[0], INVALID_INDEX, maxBlocks_ * sizeof(blockIndexLookup_[0]));

	if (fwrite(&index_[0], sizeof(BlockInfo), indexCount_, f_) != indexCount_) {
		CloseFileHandle();
		return;
	}
	if (fflush(f_) != 0) {
		CloseFileHandle();
		return;
	}

	INFO_LOG(Log::Loader, "Created new disk cache file for %s", origPath_.c_str());
}

bool DiskCachingFileLoaderCache::LockCacheFile(bool lockStatus) {
	if (!f_) {
		return false;
	}

	u32 offset = (u32)offsetof(FileHeader, flags);

	bool failed = false;
	if (fseek(f_, offset, SEEK_SET) != 0) {
		failed = true;
	} else if (fread(&flags_, sizeof(u32), 1, f_) != 1) {
		failed = true;
	}

	if (failed) {
		ERROR_LOG(Log::Loader, "Unable to read current flags during disk cache locking");
		CloseFileHandle();
		return false;
	}

	// TODO: Also use flock where supported?
	if (lockStatus) {
		if ((flags_ & FLAG_LOCKED) != 0) {
			ERROR_LOG(Log::Loader, "Could not lock disk cache file for %s (already locked)", origPath_.c_str());
			return false;
		}
		flags_ |= FLAG_LOCKED;
	} else {
		if ((flags_ & FLAG_LOCKED) == 0) {
			ERROR_LOG(Log::Loader, "Could not unlock disk cache file for %s", origPath_.c_str());
			return false;
		}
		flags_ &= ~FLAG_LOCKED;
	}

	if (fseek(f_, offset, SEEK_SET) != 0) {
		failed = true;
	} else if (fwrite(&flags_, sizeof(u32), 1, f_) != 1) {
		failed = true;
	} else if (fflush(f_) != 0) {
		failed = true;
	}

	if (failed) {
		ERROR_LOG(Log::Loader, "Unable to write updated flags during disk cache locking");
		CloseFileHandle();
		return false;
	}

	if (lockStatus) {
		INFO_LOG(Log::Loader, "Locked disk cache file for %s", origPath_.c_str());
	} else {
		INFO_LOG(Log::Loader, "Unlocked disk cache file for %s", origPath_.c_str());
	}
	return true;
}

bool DiskCachingFileLoaderCache::RemoveCacheFile(const Path &path) {
	// Note that some platforms, you can't delete open files.  So we check.
	CloseFileHandle();
	return File::Delete(path);
}

void DiskCachingFileLoaderCache::CloseFileHandle() {
	if (f_) {
		fclose(f_);
	}
	f_ = nullptr;
	fd_ = 0;
}

bool DiskCachingFileLoaderCache::HasData() const {
	if (!f_) {
		return false;
	}

	for (size_t i = 0; i < blockIndexLookup_.size(); ++i) {
		if (blockIndexLookup_[i] != INVALID_INDEX) {
			return true;
		}
	}
	return false;
}

u64 DiskCachingFileLoaderCache::FreeDiskSpace() {
	Path dir = cacheDir_;
	if (dir.empty()) {
		dir = GetSysDirectory(DIRECTORY_CACHE);
	}

	int64_t result = 0;
	if (free_disk_space(dir, result)) {
		return (u64)result;
	}

	// We can't know for sure how much is free, so we have to assume none.
	return 0;
}

u32 DiskCachingFileLoaderCache::DetermineMaxBlocks() {
	const s64 freeBytes = FreeDiskSpace();
	// We want to leave them some room for other stuff.
	const u64 availBytes = std::max(0LL, freeBytes - SAFETY_FREE_DISK_SPACE);
	const u64 freeBlocks = availBytes / (u64)DEFAULT_BLOCK_SIZE;

	const u32 alreadyCachedCount = CountCachedFiles();
	// This is how many more files of free space we will aim for.
	const u32 flex = CACHE_SPACE_FLEX > alreadyCachedCount ? CACHE_SPACE_FLEX - alreadyCachedCount : 1;

	const u64 freeBlocksWithFlex = freeBlocks / flex;
	if (freeBlocksWithFlex > MAX_BLOCKS_LOWER_BOUND) {
		if (freeBlocksWithFlex > MAX_BLOCKS_UPPER_BOUND) {
			return MAX_BLOCKS_UPPER_BOUND;
		}
		// This might be smaller than what's free, but if they try to launch a second game,
		// they'll be happier when it can be cached too.
		return (u32)freeBlocksWithFlex;
	}

	// Might be lower than LOWER_BOUND, but that's okay.  That means not enough space.
	// We abandon the idea of flex since there's not enough space free anyway.
	return (u32)freeBlocks;
}

u32 DiskCachingFileLoaderCache::CountCachedFiles() {
	Path dir = cacheDir_;
	if (dir.empty()) {
		dir = GetSysDirectory(DIRECTORY_CACHE);
	}

	std::vector<File::FileInfo> files;
	return (u32)GetFilesInDir(dir, &files, "ppdc:");
}

void DiskCachingFileLoaderCache::GarbageCollectCacheFiles(u64 goalBytes) {
	// We attempt to free up at least enough files from the cache to get goalBytes more space.
	const std::vector<Path> usedPaths = DiskCachingFileLoader::GetCachedPathsInUse();
	std::set<std::string> used;
	for (const Path &path : usedPaths) {
		used.insert(MakeCacheFilename(path));
	}

	Path dir = cacheDir_;
	if (dir.empty()) {
		dir = GetSysDirectory(DIRECTORY_CACHE);
	}

	std::vector<File::FileInfo> files;
	File::GetFilesInDir(dir, &files, "ppdc:");

	u64 remaining = goalBytes;
	// TODO: Could order by LRU or etc.
	for (File::FileInfo &file : files) {
		if (file.isDirectory) {
			continue;
		}
		if (used.find(file.name) != used.end()) {
			// In use, must leave alone.
			continue;
		}

#ifdef _WIN32
		const std::wstring w32path = file.fullName.ToWString();
#if PPSSPP_PLATFORM(UWP)
		bool success = DeleteFileFromAppW(w32path.c_str()) != 0;
#else
		bool success = DeleteFileW(w32path.c_str()) != 0;
#endif
#else
		bool success = unlink(file.fullName.c_str()) == 0;
#endif

		if (success) {
			if (file.size > remaining) {
				// We're done, huzzah.
				break;
			}

			// A little bit more.
			remaining -= file.size;
		}
	}

	// At this point, we've done all we can.
}
