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
#if PPSSPP_PLATFORM(UWP)
#include <fileapifromapp.h>
#endif
#else
#include <fcntl.h>
#endif

#ifdef HAVE_LIBRETRO_VFS
#include <streams/file_stream.h>
#endif

#if !defined(_WIN32) && !defined(HAVE_LIBRETRO_VFS)

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
		ERROR_LOG(Log::FileSystem, "LocalFileLoader can't load empty filenames");
		return;
	}

#if HAVE_LIBRETRO_VFS
    isOpenedByFd_ = false;
    handle_ = filestream_open(filename.c_str(), RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    filestream_seek(handle_, 0, RETRO_VFS_SEEK_POSITION_END);
    filesize_ = filestream_tell(handle_);
    filestream_seek(handle_, 0, RETRO_VFS_SEEK_POSITION_START);
    return;
#endif

#if PPSSPP_PLATFORM(ANDROID) && !defined(HAVE_LIBRETRO_VFS)
	if (filename.Type() == PathType::CONTENT_URI) {
		int fd = Android_OpenContentUriFd(filename.ToString(), Android_OpenContentUriMode::READ);
		VERBOSE_LOG(Log::System, "LocalFileLoader Fd %d for content URI: '%s'", fd, filename.c_str());
		if (fd < 0) {
			ERROR_LOG(Log::FileSystem, "LocalFileLoader failed to open content URI: '%s'", filename.c_str());
			return;
		}
		fd_ = fd;
		isOpenedByFd_ = true;
		DetectSizeFd();
		return;
	}
#endif

#if defined(HAVE_LIBRETRO_VFS)
    // Nothing to do here...
#elif !defined(_WIN32)

	fd_ = open(filename.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd_ == -1) {
		return;
	}

	DetectSizeFd();

#else // _WIN32

	const DWORD access = GENERIC_READ, share = FILE_SHARE_READ, mode = OPEN_EXISTING, flags = FILE_ATTRIBUTE_NORMAL;
#if PPSSPP_PLATFORM(UWP)
	handle_ = CreateFile2FromAppW(filename.ToWString().c_str(), access, share, mode, nullptr);
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
#if defined(HAVE_LIBRETRO_VFS)
    filestream_close(handle_);
#elif !defined(_WIN32)
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
	// If we opened it for reading, it must exist.  Done.
#if defined(HAVE_LIBRETRO_VFS)
    return handle_ != 0;

#elif !defined(_WIN32)
	if (isOpenedByFd_) {
		// As an optimization, if we already tried and failed, quickly return.
		// This is used because Android Content URIs are so slow.
		return fd_ != -1;
	}
	if (fd_ != -1)
		return true;
#else
	if (handle_ != INVALID_HANDLE_VALUE)
		return true;
#endif

	File::FileInfo info;
	if (File::GetFileInfo(filename_, &info)) {
		return info.exists;
	} else {
		return false;
	}
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
		ERROR_LOG(Log::FileSystem, "ReadAt from 0-sized file: %s", filename_.c_str());
		return 0;
	}

#if defined(HAVE_LIBRETRO_VFS)
    std::lock_guard<std::mutex> guard(readLock_);
	filestream_seek(handle_, absolutePos, RETRO_VFS_SEEK_POSITION_START);
	return filestream_read(handle_, data, bytes * count) / bytes;
#elif PPSSPP_PLATFORM(SWITCH)
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
