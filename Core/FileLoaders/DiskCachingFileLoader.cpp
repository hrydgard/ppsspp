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

#include <algorithm>
#include <string.h>
#include "file/file_util.h"
#include "Common/FileUtil.h"
#include "Core/FileLoaders/DiskCachingFileLoader.h"
#include "Core/System.h"

static const char *CACHEFILE_MAGIC = "ppssppDC";

std::string DiskCachingFileLoaderCache::cacheDir_;

std::map<std::string, DiskCachingFileLoaderCache *> DiskCachingFileLoader::caches_;

// Takes ownership of backend.
DiskCachingFileLoader::DiskCachingFileLoader(FileLoader *backend)
	: filesize_(0), filepos_(0), backend_(backend), cache_(nullptr) {
	filesize_ = backend->FileSize();
	if (filesize_ > 0) {
		InitCache();
	}
}

DiskCachingFileLoader::~DiskCachingFileLoader() {
	if (filesize_ > 0) {
		ShutdownCache();
	}
	// Takes ownership.
	delete backend_;
}

bool DiskCachingFileLoader::Exists() {
	return backend_->Exists();
}

bool DiskCachingFileLoader::IsDirectory() {
	return backend_->IsDirectory() ? 1 : 0;
}

s64 DiskCachingFileLoader::FileSize() {
	return filesize_;
}

std::string DiskCachingFileLoader::Path() const {
	return backend_->Path();
}

void DiskCachingFileLoader::Seek(s64 absolutePos) {
	filepos_ = absolutePos;
}

size_t DiskCachingFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data) {
	size_t readSize;

	if (cache_ && cache_->IsValid()) {
		readSize = cache_->ReadFromCache(absolutePos, bytes, data);
		// While in case the cache size is too small for the entire read.
		while (readSize < bytes) {
			readSize += cache_->SaveIntoCache(backend_, absolutePos + readSize, bytes - readSize, (u8 *)data + readSize);
			// If there are already-cached blocks afterward, we have to read them.
			readSize += cache_->ReadFromCache(absolutePos + readSize, bytes - readSize, (u8 *)data + readSize);
		}
	} else {
		readSize = backend_->ReadAt(absolutePos, bytes, data);
	}

	filepos_ = absolutePos + readSize;
	return readSize;
}

void DiskCachingFileLoader::InitCache() {
	std::string path = backend_->Path();
	auto &entry = caches_[path];
	if (!entry) {
		entry = new DiskCachingFileLoaderCache(path, filesize_);
	}

	cache_ = entry;
	cache_->AddRef();
}

void DiskCachingFileLoader::ShutdownCache() {
	if (cache_->Release()) {
		// If it ran out of counts, delete it.
		delete cache_;
		caches_.erase(backend_->Path());
	}
	cache_ = nullptr;
}

DiskCachingFileLoaderCache::DiskCachingFileLoaderCache(const std::string &path, u64 filesize)
	: refCount_(0), filesize_(filesize), f_(nullptr), fd_(0) {
	InitCache(path);
}

DiskCachingFileLoaderCache::~DiskCachingFileLoaderCache() {
	ShutdownCache();
}

void DiskCachingFileLoaderCache::InitCache(const std::string &path) {
	cacheSize_ = 0;
	indexCount_ = 0;
	oldestGeneration_ = 0;
	generation_ = 0;

	const std::string cacheFilePath = MakeCacheFilePath(path);
	if (!LoadCacheFile(cacheFilePath)) {
		CreateCacheFile(cacheFilePath);
	}
}

void DiskCachingFileLoaderCache::ShutdownCache() {
	if (f_) {
		if (fseek(f_, sizeof(FileHeader), SEEK_SET) != 0) {
			ERROR_LOG(LOADER, "Unable to flush disk cache.");
		}
		if (fwrite(&index_[0], sizeof(BlockInfo), indexCount_, f_) != indexCount_) {
			ERROR_LOG(LOADER, "Unable to flush disk cache.");
		}

		fclose(f_);
		f_ = nullptr;
		fd_ = 0;
	}

	index_.clear();
	blockIndexLookup_.clear();
	cacheSize_ = 0;
}

size_t DiskCachingFileLoaderCache::ReadFromCache(s64 pos, size_t bytes, void *data) {
	lock_guard guard(lock_);

	s64 cacheStartPos = pos / blockSize_;
	s64 cacheEndPos = (pos + bytes - 1) / blockSize_;
	size_t readSize = 0;
	size_t offset = (size_t)(pos - (cacheStartPos * (u64)blockSize_));
	u8 *p = (u8 *)data;

	for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
		auto &info = index_[i];
		if (info.block == INVALID_BLOCK) {
			return readSize;
		}
		info.generation = generation_;
		if (info.hits < std::numeric_limits<u16>::max()) {
			++info.hits;
		}

		size_t toRead = std::min(bytes - readSize, (size_t)blockSize_ - offset);
		ReadBlockData(p + readSize, info, offset, toRead);
		readSize += toRead;

		// Don't need an offset after the first read.
		offset = 0;
	}
	return readSize;
}

size_t DiskCachingFileLoaderCache::SaveIntoCache(FileLoader *backend, s64 pos, size_t bytes, void *data) {
	lock_guard guard(lock_);

	s64 cacheStartPos = pos / blockSize_;
	s64 cacheEndPos = (pos + bytes - 1) / blockSize_;
	size_t readSize = 0;
	size_t offset = (size_t)(pos - (cacheStartPos * (u64)blockSize_));
	u8 *p = (u8 *)data;

	size_t blocksToRead = 0;
	for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
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
		size_t readBytes = backend->ReadAt(cacheStartPos * (u64)blockSize_, blockSize_, buf);

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
		size_t readBytes = backend->ReadAt(cacheStartPos * (u64)blockSize_, blocksToRead * blockSize_, wholeRead);

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
	size_t goal = MAX_BLOCKS_CACHED - blocks;

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

	_dbg_assert_msg_(LOADER, false, "Not enough free blocks");
	return INVALID_BLOCK;
}

std::string DiskCachingFileLoaderCache::MakeCacheFilePath(const std::string &path) {
	std::string dir = cacheDir_;
	if (dir.empty()) {
		dir = GetSysDirectory(DIRECTORY_CACHE);
	}

	static const char *const invalidChars = "?*:/\\^|<>\"'";
	std::string filename = path;
	for (size_t i = 0; i < filename.size(); ++i) {
		int c = filename[i];
		if (strchr(invalidChars, c) != nullptr) {
			filename[i] = '_';
		}
	}

	if (!File::Exists(dir)) {
		File::CreateFullPath(dir);
	}

	return dir + "/" + filename;
}

s64 DiskCachingFileLoaderCache::GetBlockOffset(u32 block) {
	// This is where the blocks start.
	s64 blockOffset = (s64)sizeof(FileHeader) + (s64)indexCount_ * (s64)sizeof(BlockInfo);
	// Now to the actual block.
	return blockOffset + (s64)block * (s64)blockSize_;
}

void DiskCachingFileLoaderCache::ReadBlockData(u8 *dest, BlockInfo &info, size_t offset, size_t size) {
	s64 blockOffset = GetBlockOffset(info.block);

#ifdef ANDROID
	if (lseek64(fd_, blockOffset, SEEK_SET) != blockOffset) {
		ERROR_LOG(LOADER, "Unable to read disk cache data entry.");
	} else if (read(fd_, dest + offset, size) != (ssize_t)size) {
		ERROR_LOG(LOADER, "Unable to read disk cache data entry.");
	}
#else
	if (fseeko(f_, blockOffset, SEEK_SET) != 0) {
		ERROR_LOG(LOADER, "Unable to read disk cache data entry.");
	} else if (fread(dest + offset, size, 1, f_) != 1) {
		ERROR_LOG(LOADER, "Unable to read disk cache data entry.");
	}
#endif
}

void DiskCachingFileLoaderCache::WriteBlockData(BlockInfo &info, u8 *src) {
	s64 blockOffset = GetBlockOffset(info.block);

#ifdef ANDROID
	if (lseek64(fd_, blockOffset, SEEK_SET) != blockOffset) {
		ERROR_LOG(LOADER, "Unable to write disk cache data entry.");
	} else if (write(fd_, src, blockSize_) != (ssize_t)blockSize_) {
		ERROR_LOG(LOADER, "Unable to write disk cache data entry.");
	}
#else
	if (fseeko(f_, blockOffset, SEEK_SET) != 0) {
		ERROR_LOG(LOADER, "Unable to write disk cache data entry.");
	} else if (fwrite(src, blockSize_, 1, f_) != 1) {
		ERROR_LOG(LOADER, "Unable to write disk cache data entry.");
	}
#endif
}

void DiskCachingFileLoaderCache::WriteIndexData(u32 indexPos, BlockInfo &info) {
	u32 offset = (u32)sizeof(FileHeader) + indexPos * (u32)sizeof(BlockInfo);

	if (fseek(f_, offset, SEEK_SET) != 0) {
		ERROR_LOG(LOADER, "Unable to write disk cache index entry.");
	} else if (fwrite(&info, sizeof(BlockInfo), 1, f_) != 1) {
		ERROR_LOG(LOADER, "Unable to write disk cache index entry.");
	}
}

bool DiskCachingFileLoaderCache::LoadCacheFile(const std::string &path) {
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
	}

	// If it's valid, retain the file pointer.
	if (valid) {
		f_ = fp;

#ifdef ANDROID
		// Android NDK does not support 64-bit file I/O using C streams
		fd_ = fileno(f_);
#endif

		// Now let's load the index.
		blockSize_ = header.blockSize;
		LoadCacheIndex();
	} else {
		ERROR_LOG(LOADER, "Disk cache file header did not match, recreating cache file");
		fclose(fp);
	}

	return valid;
}

void DiskCachingFileLoaderCache::LoadCacheIndex() {
	if (fseek(f_, sizeof(FileHeader), SEEK_SET) != 0) {
		fclose(f_);
		f_ = nullptr;
		fd_ = 0;
		return;
	}

	indexCount_ = (filesize_ + blockSize_ - 1) / blockSize_;
	index_.resize(indexCount_);
	blockIndexLookup_.resize(MAX_BLOCKS_CACHED);
	memset(&blockIndexLookup_[0], INVALID_INDEX, MAX_BLOCKS_CACHED * sizeof(blockIndexLookup_[0]));

	if (fread(&index_[0], sizeof(BlockInfo), indexCount_, f_) != indexCount_) {
		fclose(f_);
		f_ = nullptr;
		fd_ = 0;
		return;
	}

	// Now let's set some values we need.
	oldestGeneration_ = std::numeric_limits<u16>::max();
	generation_ = 0;
	cacheSize_ = 0;

	for (size_t i = 0; i < index_.size(); ++i) {
		if (index_[i].block > MAX_BLOCKS_CACHED) {
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

void DiskCachingFileLoaderCache::CreateCacheFile(const std::string &path) {
	f_ = File::OpenCFile(path, "wb+");
	if (!f_) {
		ERROR_LOG(LOADER, "Could not create disk cache file");
		return;
	}
#ifdef ANDROID
	// Android NDK does not support 64-bit file I/O using C streams
	fd_ = fileno(f_);
#endif

	blockSize_ = DEFAULT_BLOCK_SIZE;

	FileHeader header;
	memcpy(header.magic, CACHEFILE_MAGIC, sizeof(header.magic));
	header.version = CACHE_VERSION;
	header.blockSize = blockSize_;
	header.filesize = filesize_;

	if (fwrite(&header, sizeof(header), 1, f_) != 1) {
		fclose(f_);
		f_ = nullptr;
		fd_ = 0;
		return;
	}

	indexCount_ = (filesize_ + blockSize_ - 1) / blockSize_;
	index_.resize(indexCount_);
	blockIndexLookup_.resize(MAX_BLOCKS_CACHED);
	memset(&blockIndexLookup_[0], INVALID_INDEX, MAX_BLOCKS_CACHED * sizeof(blockIndexLookup_[0]));

	if (fwrite(&index_[0], sizeof(BlockInfo), indexCount_, f_) != indexCount_) {
		fclose(f_);
		f_ = nullptr;
		fd_ = 0;
		return;
	}
}
