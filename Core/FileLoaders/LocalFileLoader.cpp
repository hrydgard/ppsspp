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

#include <cstdio>

#include "ppsspp_config.h"

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Core/FileLoaders/LocalFileLoader.h"

#if PPSSPP_PLATFORM(ANDROID)
#include "android/jni/app-android.h"
#endif

#ifdef _WIN32
#include "Common/CommonWindows.h"
#else
#include <fcntl.h>
#endif

#ifndef _WIN32

void LocalFileLoader::DetectSizeFd() {
#if PPSSPP_PLATFORM(ANDROID) || (defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64)
	off64_t off = lseek64(fd_, 0, SEEK_END);
	filesize_ = off;
	lseek64(fd_, 0, SEEK_SET);
#else
	off_t off = lseek(fd_, 0, SEEK_END);
	filesize_ = off;
	lseek(fd_, 0, SEEK_SET);
#endif
}
#endif

LocalFileLoader::LocalFileLoader(const Path &filename)
	: filesize_(0), filename_(filename) {
	if (filename.empty()) {
		ERROR_LOG(FILESYS, "LocalFileLoader can't load empty filenames");
		return;
	}

#if PPSSPP_PLATFORM(ANDROID)
	if (filename.Type() == PathType::CONTENT_URI) {
		int fd = Android_OpenContentUriFd(filename.ToString(), Android_OpenContentUriMode::READ);
		INFO_LOG(SYSTEM, "Fd %d for content URI: '%s'", fd, filename.c_str());
		if (fd < 0) {
			ERROR_LOG(FILESYS, "LoadFileLoader failed to open content URI: '%s'", filename.c_str());
			return;
		}
		fd_ = fd;
		isOpenedByFd_ = true;
		DetectSizeFd();
		return;
	}
#endif

#ifndef _WIN32

	fd_ = open(filename.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd_ == -1) {
		return;
	}

	DetectSizeFd();

#else // _WIN32

	const DWORD access = GENERIC_READ, share = FILE_SHARE_READ, mode = OPEN_EXISTING, flags = FILE_ATTRIBUTE_NORMAL;
#if PPSSPP_PLATFORM(UWP)
	handle_ = CreateFile2(filename.ToWString().c_str(), access, share, mode, nullptr);
#else
	handle_ = CreateFile(filename.ToWString().c_str(), access, share, nullptr, mode, flags, nullptr);
#endif
	if (handle_ == INVALID_HANDLE_VALUE) {
		return;
	}
	LARGE_INTEGER end_offset;
	const LARGE_INTEGER zero{};
	if (SetFilePointerEx(handle_, zero, &end_offset, FILE_END) == 0) {
		// Couldn't seek in the file. Close it and give up? This should never happen.
		CloseHandle(handle_);
		handle_ = INVALID_HANDLE_VALUE;
		return;
	}
	filesize_ = end_offset.QuadPart;
	SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN);
#endif // _WIN32
}

LocalFileLoader::~LocalFileLoader() {
#ifndef _WIN32
	if (fd_ != -1) {
		close(fd_);
	}
#else
	if (handle_ != INVALID_HANDLE_VALUE) {
		CloseHandle(handle_);
	}
#endif
}

bool LocalFileLoader::Exists() {
	// If we couldn't open it for reading, we say it does not exist.
#ifndef _WIN32
	if (isOpenedByFd_) {
		return fd_ != -1;
	}
	if (fd_ != -1 || IsDirectory()) {
#else
	if (handle_ != INVALID_HANDLE_VALUE || IsDirectory()) {
#endif
		File::FileInfo info;
		if (File::GetFileInfo(filename_, &info)) {
			return info.exists;
		} else {
			return false;
		}
	}
	return false;
}

bool LocalFileLoader::IsDirectory() {
	File::FileInfo info;
	if (File::GetFileInfo(filename_, &info)) {
		return info.exists && info.isDirectory;
	}
	return false;
}

s64 LocalFileLoader::FileSize() {
	return filesize_;
}

size_t LocalFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	if (bytes == 0)
		return 0;

	if (filesize_ == 0) {
		ERROR_LOG(FILESYS, "ReadAt from 0-sized file: %s", filename_.c_str());
		return 0;
	}

#if PPSSPP_PLATFORM(SWITCH)
	// Toolchain has no fancy IO API.  We must lock.
	std::lock_guard<std::mutex> guard(readLock_);
	lseek(fd_, absolutePos, SEEK_SET);
	return read(fd_, data, bytes * count) / bytes;
#elif PPSSPP_PLATFORM(ANDROID)
	// pread64 doesn't appear to actually be 64-bit safe, though such ISOs are uncommon.  See #10862.
	if (absolutePos <= 0x7FFFFFFF) {
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64
		return pread64(fd_, data, bytes * count, absolutePos) / bytes;
#else
		return pread(fd_, data, bytes * count, absolutePos) / bytes;
#endif
	} else {
		// Since pread64 doesn't change the file offset, it should be safe to avoid the lock in the common case.
		std::lock_guard<std::mutex> guard(readLock_);
		lseek64(fd_, absolutePos, SEEK_SET);
		return read(fd_, data, bytes * count) / bytes;
	}
#elif !defined(_WIN32)
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64
	return pread64(fd_, data, bytes * count, absolutePos) / bytes;
#else
	return pread(fd_, data, bytes * count, absolutePos) / bytes;
#endif
#else
	DWORD read = -1;
	OVERLAPPED offset = { 0 };
	offset.Offset = (DWORD)(absolutePos & 0xffffffff);
	offset.OffsetHigh = (DWORD)((absolutePos & 0xffffffff00000000) >> 32);
	auto result = ReadFile(handle_, data, (DWORD)(bytes * count), &read, &offset);
	return result == TRUE ? (size_t)read / bytes : -1;
#endif
}
