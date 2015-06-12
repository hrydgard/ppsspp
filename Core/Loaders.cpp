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
#include <cstdio>

#include "thread/thread.h"
#include "base/mutex.h"
#include "base/stringutil.h"
#include "base/timeutil.h"
#include "file/file_util.h"
#include "net/http_client.h"
#include "net/resolve.h"
#include "net/url.h"

#include "Common/FileUtil.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/PSPLoaders.h"
#include "Core/MemMap.h"
#include "Core/Loaders.h"
#include "Core/System.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"

class LocalFileLoader : public FileLoader {
public:
	LocalFileLoader(const std::string &filename);
	virtual ~LocalFileLoader();

	virtual bool Exists() override;
	virtual bool IsDirectory() override;
	virtual s64 FileSize() override;
	virtual std::string Path() const override;

	virtual void Seek(s64 absolutePos) override;
	virtual size_t Read(size_t bytes, size_t count, void *data) override;
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data) override;

private:
	// First only used by Android, but we can keep it here for everyone.
	int fd_;
	FILE *f_;
	u64 filesize_;
	std::string filename_;
};

class HTTPFileLoader : public FileLoader {
public:
	HTTPFileLoader(const std::string &filename);
	virtual ~HTTPFileLoader() override;

	virtual bool Exists() override;
	virtual bool IsDirectory() override;
	virtual s64 FileSize() override;
	virtual std::string Path() const override;

	virtual void Seek(s64 absolutePos) override;
	virtual size_t Read(size_t bytes, size_t count, void *data) override {
		return ReadAt(filepos_, bytes, count, data);
	}
	virtual size_t Read(size_t bytes, void *data) override {
		return ReadAt(filepos_, bytes, data);
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data) override {
		return ReadAt(absolutePos, bytes * count, data) / bytes;
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, void *data) override;

private:
	void Connect() {
		if (!connected_) {
			connected_ = client_.Connect();
		}
	}

	void Disconnect() {
		if (connected_) {
			client_.Disconnect();
		}
		connected_ = false;
	}

	s64 filesize_;
	s64 filepos_;
	Url url_;
	net::AutoInit netInit_;
	http::Client client_;
	std::string filename_;
	bool connected_;
};

class RetryingFileLoader : public FileLoader {
public:
	RetryingFileLoader(FileLoader *backend);
	virtual ~RetryingFileLoader() override;

	virtual bool Exists() override;
	virtual bool IsDirectory() override;
	virtual s64 FileSize() override;
	virtual std::string Path() const override;

	virtual void Seek(s64 absolutePos) override;
	virtual size_t Read(size_t bytes, size_t count, void *data) override {
		return ReadAt(filepos_, bytes, count, data);
	}
	virtual size_t Read(size_t bytes, void *data) override {
		return ReadAt(filepos_, bytes, data);
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data) override {
		return ReadAt(absolutePos, bytes * count, data) / bytes;
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, void *data) override;

private:
	enum {
		MAX_RETRIES = 3,
	};

	s64 filepos_;
	FileLoader *backend_;
};

class CachingFileLoader : public FileLoader {
public:
	CachingFileLoader(FileLoader *backend);
	virtual ~CachingFileLoader() override;

	virtual bool Exists() override;
	virtual bool IsDirectory() override;
	virtual s64 FileSize() override;
	virtual std::string Path() const override;

	virtual void Seek(s64 absolutePos) override;
	virtual size_t Read(size_t bytes, size_t count, void *data) override {
		return ReadAt(filepos_, bytes, count, data);
	}
	virtual size_t Read(size_t bytes, void *data) override {
		return ReadAt(filepos_, bytes, data);
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data) override {
		return ReadAt(absolutePos, bytes * count, data) / bytes;
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, void *data) override;

private:
	void InitCache();
	void ShutdownCache();
	size_t ReadFromCache(s64 pos, size_t bytes, void *data);
	// Guaranteed to read at least one block into the cache.
	void SaveIntoCache(s64 pos, size_t bytes, bool readingAhead = false);
	bool MakeCacheSpaceFor(size_t blocks, bool readingAhead);
	void StartReadAhead(s64 pos);

	enum {
		BLOCK_SIZE = 65536,
		BLOCK_SHIFT = 16,
		MAX_BLOCKS_PER_READ = 16,
		MAX_BLOCKS_CACHED = 4096, // 256 MB
		BLOCK_READAHEAD = 4,
	};

	s64 filesize_;
	s64 filepos_;
	FileLoader *backend_;
	int exists_;
	int isDirectory_;
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
	recursive_mutex blocksMutex_;
	mutable recursive_mutex backendMutex_;
	bool aheadThread_;
};

FileLoader *ConstructFileLoader(const std::string &filename) {
	if (filename.find("http://") == 0 || filename.find("https://") == 0)
		return new CachingFileLoader(new RetryingFileLoader(new HTTPFileLoader(filename)));
	return new LocalFileLoader(filename);
}

LocalFileLoader::LocalFileLoader(const std::string &filename)
	: fd_(0), f_(nullptr), filesize_(0), filename_(filename) {
	f_ = File::OpenCFile(filename, "rb");
	if (!f_) {
		return;
	}

#ifdef ANDROID
	// Android NDK does not support 64-bit file I/O using C streams
	// so we fall back onto syscalls
	fd_ = fileno(f_);

	off64_t off = lseek64(fd_, 0, SEEK_END);
	filesize_ = off;
	lseek64(fd_, 0, SEEK_SET);
#else
	fseek(f_, 0, SEEK_END);
	filesize_ = ftello(f_);
	fseek(f_, 0, SEEK_SET);
#endif
}

LocalFileLoader::~LocalFileLoader() {
	if (f_) {
		fclose(f_);
	}
}

bool LocalFileLoader::Exists() {
	// If we couldn't open it for reading, we say it does not exist.
	if (f_ || IsDirectory()) {
		FileInfo info;
		return getFileInfo(filename_.c_str(), &info);
	}
	return false;
}

bool LocalFileLoader::IsDirectory() {
	FileInfo info;
	if (getFileInfo(filename_.c_str(), &info)) {
		return info.isDirectory;
	}
	return false;
}

s64 LocalFileLoader::FileSize() {
	return filesize_;
}

std::string LocalFileLoader::Path() const {
	return filename_;
}

void LocalFileLoader::Seek(s64 absolutePos) {
#ifdef ANDROID
	lseek64(fd_, absolutePos, SEEK_SET);
#else
	fseeko(f_, absolutePos, SEEK_SET);
#endif
}

size_t LocalFileLoader::Read(size_t bytes, size_t count, void *data) {
#ifdef ANDROID
	return read(fd_, data, bytes * count) / bytes;
#else
	return fread(data, bytes, count, f_);
#endif
}

size_t LocalFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data) {
	Seek(absolutePos);
	return Read(bytes, count, data);
}

HTTPFileLoader::HTTPFileLoader(const std::string &filename)
	: filesize_(0), filepos_(0), url_(filename), filename_(filename), connected_(false) {
	if (!client_.Resolve(url_.Host().c_str(), url_.Port())) {
		return;
	}

	Connect();
	int err = client_.SendRequest("HEAD", url_.Resource().c_str());
	if (err < 0) {
		Disconnect();
		return;
	}

	Buffer readbuf;
	std::vector<std::string> responseHeaders;
	int code = client_.ReadResponseHeaders(&readbuf, responseHeaders);
	if (code != 200) {
		// Leave size at 0, invalid.
		ERROR_LOG(LOADER, "HTTP request failed, got %03d for %s", code, filename.c_str());
		Disconnect();
		return;
	}

	// TODO: Expire cache via ETag, etc.
	bool acceptsRange = false;
	for (std::string header : responseHeaders) {
		if (startsWithNoCase(header, "Content-Length:")) {
			size_t size_pos = header.find_first_of(' ');
			if (size_pos != header.npos) {
				size_pos = header.find_first_not_of(' ', size_pos);
			}
			if (size_pos != header.npos) {
				// TODO: Find a way to get this to work right on Symbian?
#ifndef __SYMBIAN32__
				filesize_ = atoll(&header[size_pos]);
#else
				filesize_ = atoi(&header[size_pos]);
#endif
			}
		}
		if (startsWithNoCase(header, "Accept-Ranges:")) {
			std::string lowerHeader = header;
			std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(), tolower);
			// TODO: Delimited.
			if (lowerHeader.find("bytes") != lowerHeader.npos) {
				acceptsRange = true;
			}
		}
	}

	// TODO: Keepalive instead.
	Disconnect();

	if (!acceptsRange) {
		WARN_LOG(LOADER, "HTTP server did not advertise support for range requests.");
	}
	if (filesize_ == 0) {
		ERROR_LOG(LOADER, "Could not determine file size for %s", filename.c_str());
	}

	// If we didn't end up with a filesize_ (e.g. chunked response), give up.  File invalid.
}

HTTPFileLoader::~HTTPFileLoader() {
	Disconnect();
}

bool HTTPFileLoader::Exists() {
	return url_.Valid() && filesize_ > 0;
}

bool HTTPFileLoader::IsDirectory() {
	// Only files.
	return false;
}

s64 HTTPFileLoader::FileSize() {
	return filesize_;
}

std::string HTTPFileLoader::Path() const {
	return filename_;
}

void HTTPFileLoader::Seek(s64 absolutePos) {
	filepos_ = absolutePos;
}

size_t HTTPFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data) {
	s64 absoluteEnd = std::min(absolutePos + (s64)bytes, filesize_);
	if (absolutePos >= filesize_ || bytes == 0) {
		// Read outside of the file or no read at all, just fail immediately.
		return 0;
	}

	Connect();

	char requestHeaders[4096];
	// Note that the Range header is *inclusive*.
	snprintf(requestHeaders, sizeof(requestHeaders),
		"Range: bytes=%lld-%lld\r\n", absolutePos, absoluteEnd - 1);

	int err = client_.SendRequest("GET", url_.Resource().c_str(), requestHeaders, nullptr);
	if (err < 0) {
		Disconnect();
		return 0;
	}

	Buffer readbuf;
	std::vector<std::string> responseHeaders;
	int code = client_.ReadResponseHeaders(&readbuf, responseHeaders);
	if (code != 206) {
		ERROR_LOG(LOADER, "HTTP server did not respond with range, received code=%03d", code);
		Disconnect();
		return 0;
	}

	// TODO: Expire cache via ETag, etc.
	// We don't support multipart/byteranges responses.
	bool supportedResponse = false;
	for (std::string header : responseHeaders) {
		if (startsWithNoCase(header, "Content-Range:")) {
			// TODO: More correctness.  Whitespace can be missing or different.
			s64 first = -1, last = -1, total = -1;
			std::string lowerHeader = header;
			std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(), tolower);
			if (sscanf(lowerHeader.c_str(), "content-range: bytes %lld-%lld/%lld", &first, &last, &total) >= 2) {
				if (first == absolutePos && last == absoluteEnd - 1) {
					supportedResponse = true;
				} else {
					ERROR_LOG(LOADER, "Unexpected HTTP range: got %lld-%lld, wanted %lld-%lld.", first, last, absolutePos, absoluteEnd - 1);
				}
			} else {
				ERROR_LOG(LOADER, "Unexpected HTTP range response: %s", header.c_str());
			}
		}
	}

	// TODO: Would be nice to read directly.
	Buffer output;
	int res = client_.ReadResponseEntity(&readbuf, responseHeaders, &output);
	if (res != 0) {
		ERROR_LOG(LOADER, "Unable to read HTTP response entity: %d", res);
		// Let's take anything we got anyway.  Not worse than returning nothing?
	}

	// TODO: Keepalive instead.
	Disconnect();

	if (!supportedResponse) {
		ERROR_LOG(LOADER, "HTTP server did not respond with the range we wanted.");
		return 0;
	}

	size_t readBytes = output.size();
	output.Take(readBytes, (char *)data);
	filepos_ = absolutePos + readBytes;
	return readBytes;
}

// Takes ownership of backend.
CachingFileLoader::CachingFileLoader(FileLoader *backend)
	: filesize_(0), filepos_(0), backend_(backend), exists_(-1), isDirectory_(-1), aheadThread_(false) {
	filesize_ = backend->FileSize();
	if (filesize_ > 0) {
		InitCache();
	}
}

CachingFileLoader::~CachingFileLoader() {
	if (filesize_ > 0) {
		ShutdownCache();
	}
	// Takes ownership.
	delete backend_;
}

bool CachingFileLoader::Exists() {
	if (exists_ == -1) {
		lock_guard guard(backendMutex_);
		exists_ = backend_->Exists() ? 1 : 0;
	}
	return exists_ == 1;
}

bool CachingFileLoader::IsDirectory() {
	if (isDirectory_ == -1) {
		lock_guard guard(backendMutex_);
		isDirectory_ = backend_->IsDirectory() ? 1 : 0;
	}
	return isDirectory_ == 1;
}

s64 CachingFileLoader::FileSize() {
	return filesize_;
}

std::string CachingFileLoader::Path() const {
	lock_guard guard(backendMutex_);
	return backend_->Path();
}

void CachingFileLoader::Seek(s64 absolutePos) {
	filepos_ = absolutePos;
}

size_t CachingFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data) {
	size_t readSize = ReadFromCache(absolutePos, bytes, data);
	// While in case the cache size is too small for the entire read.
	while (readSize < bytes) {
		SaveIntoCache(absolutePos + readSize, bytes - readSize);
		readSize += ReadFromCache(absolutePos + readSize, bytes - readSize, (u8 *)data + readSize);
	}

	StartReadAhead(absolutePos + readSize);

	filepos_ = absolutePos + readSize;
	return readSize;
}

void CachingFileLoader::InitCache() {
	cacheSize_ = 0;
	oldestGeneration_ = 0;
	generation_ = 0;
}

void CachingFileLoader::ShutdownCache() {
	// TODO: Maybe add some hint that deletion is coming soon?
	// We can't delete while the thread is running, so have to wait.
	// This should only happen from the menu.
	while (aheadThread_) {
		sleep_ms(1);
	}

	lock_guard guard(blocksMutex_);
	for (auto block : blocks_) {
		delete [] block.second.ptr;
	}
	blocks_.clear();
	cacheSize_ = 0;
}

size_t CachingFileLoader::ReadFromCache(s64 pos, size_t bytes, void *data) {
	s64 cacheStartPos = pos >> BLOCK_SHIFT;
	s64 cacheEndPos = (pos + bytes - 1) >> BLOCK_SHIFT;
	// TODO: Smarter.
	size_t readSize = 0;
	size_t offset = (size_t)(pos - (cacheStartPos << BLOCK_SHIFT));
	u8 *p = (u8 *)data;

	lock_guard guard(blocksMutex_);
	for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
		auto block = blocks_.find(i);
		if (block == blocks_.end()) {
			return readSize;
		}
		block->second.generation = generation_;

		size_t toRead = std::min(bytes - readSize, (size_t)BLOCK_SIZE - offset);
		memcpy(p + readSize, block->second.ptr + offset, toRead);
		readSize += toRead;

		// Don't need an offset after the first read.
		offset = 0;
	}
	return readSize;
}

void CachingFileLoader::SaveIntoCache(s64 pos, size_t bytes, bool readingAhead) {
	s64 cacheStartPos = pos >> BLOCK_SHIFT;
	s64 cacheEndPos = (pos + bytes - 1) >> BLOCK_SHIFT;

	lock_guard guard(blocksMutex_);
	size_t blocksToRead = 0;
	for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
		auto block = blocks_.find(i);
		if (block != blocks_.end()) {
			break;
		}
		++blocksToRead;
		if (blocksToRead >= MAX_BLOCKS_PER_READ) {
			break;
		}
	}

	if (!MakeCacheSpaceFor(blocksToRead, readingAhead) || blocksToRead == 0) {
		return;
	}

	if (blocksToRead == 1) {
		blocksMutex_.unlock();

		u8 *buf = new u8[BLOCK_SIZE];
		backendMutex_.lock();
		backend_->ReadAt(cacheStartPos << BLOCK_SHIFT, BLOCK_SIZE, buf);
		backendMutex_.unlock();

		blocksMutex_.lock();
		// While blocksMutex_ was unlocked, another thread may have read.
		// If so, free the one we just read.
		if (blocks_.find(cacheStartPos) == blocks_.end()) {
			blocks_[cacheStartPos] = BlockInfo(buf);
		} else {
			delete [] buf;
		}
	} else {
		blocksMutex_.unlock();

		u8 *wholeRead = new u8[blocksToRead << BLOCK_SHIFT];
		backendMutex_.lock();
		backend_->ReadAt(cacheStartPos << BLOCK_SHIFT, blocksToRead << BLOCK_SHIFT, wholeRead);
		backendMutex_.unlock();

		blocksMutex_.lock();
		for (size_t i = 0; i < blocksToRead; ++i) {
			if (blocks_.find(cacheStartPos + i) != blocks_.end()) {
				// Written while we were busy, just skip it.  Keep the existing block.
				continue;
			}
			u8 *buf = new u8[BLOCK_SIZE];
			memcpy(buf, wholeRead + (i << BLOCK_SHIFT), BLOCK_SIZE);
			blocks_[cacheStartPos + i] = BlockInfo(buf);
		}
		delete[] wholeRead;
	}

	cacheSize_ += blocksToRead;
	++generation_;
}

bool CachingFileLoader::MakeCacheSpaceFor(size_t blocks, bool readingAhead) {
	size_t goal = MAX_BLOCKS_CACHED - blocks;

	if (readingAhead && cacheSize_ > goal) {
		return false;
	}

	lock_guard guard(blocksMutex_);
	while (cacheSize_ > goal) {
		u64 minGeneration = generation_;

		// We increment the iterator inside because we delete things inside.
		for (auto it = blocks_.begin(); it != blocks_.end(); ) {
			// Check for the minimum seen generation.
			// TODO: Do this smarter?
			if (it->second.generation != 0 && it->second.generation < minGeneration) {
				minGeneration = it->second.generation;
			}

			// 0 means it was never used yet or was the first read (e.g. block descriptor.)
			if (it->second.generation == oldestGeneration_ || it->second.generation == 0) {
				s64 pos = it->first;
				delete it->second.ptr;
				blocks_.erase(it);
				--cacheSize_;

				// Our iterator is invalid now.  Keep going?
				if (cacheSize_ > goal) {
					// This finds the one at that position.
					it = blocks_.lower_bound(pos);
				} else {
					break;
				}
			} else {
				++it;
			}
		}

		// If we didn't find any, update to the lowest we did find.
		oldestGeneration_ = minGeneration;
	}

	return true;
}

void CachingFileLoader::StartReadAhead(s64 pos) {
	lock_guard guard(blocksMutex_);
	if (aheadThread_) {
		// Already going.
		return;
	}
	if (cacheSize_ + BLOCK_READAHEAD > MAX_BLOCKS_CACHED) {
		// Not enough space to readahead.
		return;
	}

	aheadThread_ = true;
	std::thread th([this, pos] {
		lock_guard guard(blocksMutex_);
		s64 cacheStartPos = pos >> BLOCK_SHIFT;
		s64 cacheEndPos = cacheStartPos + BLOCK_READAHEAD - 1;

		for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
			auto block = blocks_.find(i);
			if (block == blocks_.end()) {
				blocksMutex_.unlock();
				SaveIntoCache(i << BLOCK_SHIFT, BLOCK_SIZE * BLOCK_READAHEAD, true);
				break;
			}
		}

		aheadThread_ = false;
	});
	th.detach();
}

// Takes ownership of backend.
RetryingFileLoader::RetryingFileLoader(FileLoader *backend)
	: filepos_(0), backend_(backend) {
}

RetryingFileLoader::~RetryingFileLoader() {
	// Takes ownership.
	delete backend_;
}

bool RetryingFileLoader::Exists() {
	if (!backend_->Exists()) {
		// Retry once, immediately.
		return backend_->Exists();
	}
	return true;
}

bool RetryingFileLoader::IsDirectory() {
	// Can't tell if it's an error either way.
	return backend_->IsDirectory();
}

s64 RetryingFileLoader::FileSize() {
	s64 filesize = backend_->FileSize();
	if (filesize == 0) {
		return backend_->FileSize();
	}
	return filesize;
}

std::string RetryingFileLoader::Path() const {
	return backend_->Path();
}

void RetryingFileLoader::Seek(s64 absolutePos) {
	filepos_ = absolutePos;
}

size_t RetryingFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data) {
	size_t readSize = backend_->ReadAt(absolutePos, bytes, data);

	int retries = 0;
	while (readSize < bytes && retries < MAX_RETRIES) {
		u8 *p = (u8 *)data;
		readSize += backend_->ReadAt(absolutePos + readSize, bytes - readSize, p + readSize);
		++retries;
	}

	filepos_ = absolutePos + readSize;
	return readSize;
}

// TODO : improve, look in the file more
IdentifiedFileType Identify_File(FileLoader *fileLoader)
{
	if (fileLoader == nullptr) {
		ERROR_LOG(LOADER, "Invalid fileLoader");
		return FILETYPE_ERROR;
	}
	if (fileLoader->Path().size() == 0) {
		ERROR_LOG(LOADER, "Invalid filename %s", fileLoader->Path().c_str());
		return FILETYPE_ERROR;
	}

	if (!fileLoader->Exists()) {
		return FILETYPE_ERROR;
	}

	std::string extension = fileLoader->Extension();
	if (!strcasecmp(extension.c_str(), ".iso"))
	{
		// may be a psx iso, they have 2352 byte sectors. You never know what some people try to open
		if ((fileLoader->FileSize() % 2352) == 0)
		{
			unsigned char sync[12];
			fileLoader->ReadAt(0, 12, sync);

			// each sector in a mode2 image starts with these 12 bytes
			if (memcmp(sync,"\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00",12) == 0)
			{
				return FILETYPE_ISO_MODE2;
			}

			// maybe it also just happened to have that size, 
		}
		return FILETYPE_PSP_ISO;
	}
	else if (!strcasecmp(extension.c_str(),".cso"))
	{
		return FILETYPE_PSP_ISO;
	}
	else if (!strcasecmp(extension.c_str(),".ppst"))
	{
		return FILETYPE_PPSSPP_SAVESTATE;
	}

	// First, check if it's a directory with an EBOOT.PBP in it.
	if (fileLoader->IsDirectory()) {
		std::string filename = fileLoader->Path();
		if (filename.size() > 4) {
			FileInfo fileInfo;
			// Check for existence of EBOOT.PBP, as required for "Directory games".
			if (getFileInfo((filename + "/EBOOT.PBP").c_str(), &fileInfo)) {
				if (fileInfo.exists) {
					return FILETYPE_PSP_PBP_DIRECTORY;
				}
			}

			// check if it's a disc directory
			if (getFileInfo((filename + "/PSP_GAME").c_str(), &fileInfo)) {
				if (fileInfo.exists) {
					return FILETYPE_PSP_DISC_DIRECTORY;
				}
			}

			// Not that, okay, let's guess it's a savedata directory if it has a param.sfo...
			if (getFileInfo((filename + "/PARAM.SFO").c_str(), &fileInfo)) {
				if (fileInfo.exists) {
					return FILETYPE_PSP_SAVEDATA_DIRECTORY;
				}
			}
		}

		return FILETYPE_NORMAL_DIRECTORY;
	}

	u32_le id;

	size_t readSize = fileLoader->ReadAt(0, 4, 1, &id);
	if (readSize != 1) {
		return FILETYPE_ERROR;
	}

	u32 psar_offset = 0, psar_id = 0;
	u32 _id = id;
	switch (_id) {
	case 'PBP\x00':
		fileLoader->ReadAt(0x24, 4, 1, &psar_offset);
		fileLoader->ReadAt(psar_offset, 4, 1, &psar_id);
		break;
	case '!raR':
		return FILETYPE_ARCHIVE_RAR;
	case '\x04\x03KP':
	case '\x06\x05KP':
	case '\x08\x07KP':
		return FILETYPE_ARCHIVE_ZIP;
	}

	if (id == 'FLE\x7F') {
		std::string filename = fileLoader->Path();
		// There are a few elfs misnamed as pbp (like Trig Wars), accept that.
		if (!strcasecmp(extension.c_str(), ".plf") || strstr(filename.c_str(),"BOOT.BIN") ||
				!strcasecmp(extension.c_str(), ".elf") || !strcasecmp(extension.c_str(), ".prx") ||
				!strcasecmp(extension.c_str(), ".pbp")) {
			return FILETYPE_PSP_ELF;
		}
		return FILETYPE_UNKNOWN_ELF;
	}
	else if (id == 'PBP\x00') {
		// Do this PS1 eboot check FIRST before checking other eboot types.
		// It seems like some are malformed and slip through the PSAR check below.
		// TODO: Change PBPReader to read FileLoader objects?
		std::string filename = fileLoader->Path();
		PBPReader pbp(filename.c_str());
		if (pbp.IsValid()) {
			if (!pbp.IsELF()) {
				size_t sfoSize;
				u8 *sfoData = pbp.GetSubFile(PBP_PARAM_SFO, &sfoSize);
				{
					recursive_mutex _lock;
					lock_guard lock(_lock);
					ParamSFOData paramSFO;
					paramSFO.ReadSFO(sfoData, sfoSize);
					// PS1 Eboots are supposed to use "ME" as their PARAM SFO category.
					// If they don't, and they're still malformed (e.g. PSISOIMG0000 isn't found), there's nothing we can do.
					if (paramSFO.GetValueString("CATEGORY") == "ME")
						return FILETYPE_PSP_PS1_PBP;
				}
				delete[] sfoData;
			}
		}

		if (psar_id == 'MUPN') {
			return FILETYPE_PSP_ISO_NP;
		}
		// PS1 PSAR begins with "PSISOIMG0000"
		if (psar_id == 'SISP') {
			return FILETYPE_PSP_PS1_PBP;
		}

		// Let's check if we got pointed to a PBP within such a directory.
		// If so we just move up and return the directory itself as the game.
		std::string path = getDir(filename);
		// If loading from memstick...
		size_t pos = path.find("/PSP/GAME/");
		if (pos != std::string::npos) {
			filename = path;
			return FILETYPE_PSP_PBP_DIRECTORY;
		}
		return FILETYPE_PSP_PBP;
	}
	else if (!strcasecmp(extension.c_str(),".pbp")) {
		ERROR_LOG(LOADER, "A PBP with the wrong magic number?");
		return FILETYPE_PSP_PBP;
	} else if (!strcasecmp(extension.c_str(),".bin")) {
		return FILETYPE_UNKNOWN_BIN;
	} else if (!strcasecmp(extension.c_str(),".zip")) {
		return FILETYPE_ARCHIVE_ZIP;
	} else if (!strcasecmp(extension.c_str(),".rar")) {
		return FILETYPE_ARCHIVE_RAR;
	} else if (!strcasecmp(extension.c_str(),".r00")) {
		return FILETYPE_ARCHIVE_RAR;
	} else if (!strcasecmp(extension.c_str(),".r01")) {
		return FILETYPE_ARCHIVE_RAR;
	} else if (!strcasecmp(extension.substr(1).c_str(), ".7z")) {
		return FILETYPE_ARCHIVE_7Z;
	}
	return FILETYPE_UNKNOWN;
}

bool LoadFile(FileLoader **fileLoaderPtr, std::string *error_string) {
	FileLoader *&fileLoader = *fileLoaderPtr;
	// Note that this can modify filename!
	switch (Identify_File(fileLoader)) {
	case FILETYPE_PSP_PBP_DIRECTORY:
		{
			std::string filename = fileLoader->Path();
			std::string ebootFilename = filename + "/EBOOT.PBP";

			// Switch fileLoader to the EBOOT.
			delete fileLoader;
			fileLoader = ConstructFileLoader(ebootFilename);

			if (fileLoader->Exists()) {
				INFO_LOG(LOADER, "File is a PBP in a directory!");
				IdentifiedFileType ebootType = Identify_File(fileLoader);
				if (ebootType == FILETYPE_PSP_ISO_NP) {
					InitMemoryForGameISO(fileLoader);
					pspFileSystem.SetStartingDirectory("disc0:/PSP_GAME/USRDIR");
					return Load_PSP_ISO(fileLoader, error_string);
				}
				else if (ebootType == FILETYPE_PSP_PS1_PBP) {
					*error_string = "PS1 EBOOTs are not supported by PPSSPP.";
					return false;
				}
				std::string path = filename;
				size_t pos = path.find("/PSP/GAME/");
				if (pos != std::string::npos)
					pspFileSystem.SetStartingDirectory("ms0:" + path.substr(pos));
				return Load_PSP_ELF_PBP(fileLoader, error_string);
			} else {
				*error_string = "No EBOOT.PBP, misidentified game";
				return false;
			}
		}

	case FILETYPE_PSP_PBP:
	case FILETYPE_PSP_ELF:
		{
			INFO_LOG(LOADER,"File is an ELF or loose PBP!");
			return Load_PSP_ELF_PBP(fileLoader, error_string);
		}

	case FILETYPE_PSP_ISO:
	case FILETYPE_PSP_ISO_NP:
	case FILETYPE_PSP_DISC_DIRECTORY:	// behaves the same as the mounting is already done by now
		pspFileSystem.SetStartingDirectory("disc0:/PSP_GAME/USRDIR");
		return Load_PSP_ISO(fileLoader, error_string);

	case FILETYPE_PSP_PS1_PBP:
		*error_string = "PS1 EBOOTs are not supported by PPSSPP.";
		break;

	case FILETYPE_ERROR:
		ERROR_LOG(LOADER, "Could not read file");
		*error_string = "Error reading file";
		break;

	case FILETYPE_ARCHIVE_RAR:
#ifdef WIN32
		*error_string = "RAR file detected (Require WINRAR)";
#else
		*error_string = "RAR file detected (Require UnRAR)";
#endif
		break;

	case FILETYPE_ARCHIVE_ZIP:
#ifdef WIN32
		*error_string = "ZIP file detected (Require WINRAR)";
#else
		*error_string = "ZIP file detected (Require UnRAR)";
#endif
		break;

	case FILETYPE_ARCHIVE_7Z:
#ifdef WIN32
		*error_string = "7z file detected (Require 7-Zip)";
#else
		*error_string = "7z file detected (Require 7-Zip)";
#endif
		break;

	case FILETYPE_ISO_MODE2:
		*error_string = "PSX game image detected.";
		break;

	case FILETYPE_NORMAL_DIRECTORY:
		ERROR_LOG(LOADER, "Just a directory.");
		*error_string = "Just a directory.";
		break;

	case FILETYPE_PPSSPP_SAVESTATE:
		*error_string = "This is a saved state, not a game.";  // Actually, we could make it load it...
		break;

	case FILETYPE_PSP_SAVEDATA_DIRECTORY:
		*error_string = "This is save data, not a game."; // Actually, we could make it load it...
		break;

	case FILETYPE_UNKNOWN_BIN:
	case FILETYPE_UNKNOWN_ELF:
	case FILETYPE_UNKNOWN:
	default:
		ERROR_LOG(LOADER, "Failed to identify file");
		*error_string = "Failed to identify file";
		break;
	}
	return false;
}
