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

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Core/Util/DarwinFileSystemServices.h"
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
	: filename_(filename) {
	if (filename.empty()) {
		ERROR_LOG(Log::FileSystem, "LocalFileLoader can't load empty filenames");
		return;
	}

	isDirectory_ = File::IsDirectory(filename);
	if (isDirectory_) {
		// Don't need to continue.
		return;
	}

#if PPSSPP_PLATFORM(ANDROID) && !defined(HAVE_LIBRETRO_VFS)
	if (filename.Type() == PathType::CONTENT_URI) {
		int fd = Android_OpenContentUriFd(filename.ToString(), Android_OpenContentUriMode::READ);
		VERBOSE_LOG(Log::System, "LocalFileLoader Fd %d for content URI: '%s'", fd, filename.c_str());
		if (fd < 0) {
			ERROR_LOG(Log::FileSystem, "LocalFileLoader failed to open content URI: '%s'", filename.c_str());
			return;
		}
		fd_ = fd;
		DetectSizeFd();
		return;
	}
	// else, fall through to normal file loading (legacy build, old Android etc).
#endif

#if defined(HAVE_LIBRETRO_VFS)
	file_ = File::OpenCFile(filename, "rb");
	if (!file_) {
		ERROR_LOG(Log::FileSystem, "LocalFileLoader: failed to open file: '%s'", filename.c_str());
		return;
	}
	filesize_ = File::GetFileSize(file_);
#elif PPSSPP_PLATFORM(IOS)
	if (!File::Exists(filename)) {
		// Try to "unlock" the path before the file loader hits it
		Path newFilename = DarwinFileSystemServices::reauthorizeBookmarkByPath(filename);
		if (!newFilename.empty()) {
			filename_ = newFilename;
		}
	}
	fd_ = open(filename_.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd_ == -1) {
		ERROR_LOG(Log::FileSystem, "LocalFileLoader: failed to open file: '%s'", filename_.c_str());
		return;
	}
	DetectSizeFd();
#elif !defined(_WIN32)
	fd_ = open(filename.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd_ == -1) {
		ERROR_LOG(Log::FileSystem, "LocalFileLoader: failed to open file: '%s'", filename.c_str());
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
	if (file_ != nullptr) {
		fclose(file_);
	}
#elif PPSSPP_PLATFORM(IOS)
	close(fd_);
	DarwinFileSystemServices::stopAccessingPath(filename_);
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
	if (isDirectory_) {
		return true;
	}
	// If we opened it for reading, it must exist.  Done.
#if defined(HAVE_LIBRETRO_VFS)
	return file_ != nullptr;
#elif PPSSPP_PLATFORM(IOS)
	return fd_ != -1;
#elif !defined(_WIN32)
	return fd_ != -1;
#else
	return handle_ != INVALID_HANDLE_VALUE;
#endif
}

s64 LocalFileLoader::FileSize() {
	return filesize_;
}

size_t LocalFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	if (bytes == 0) {
		return 0;
	}

	if (filesize_ == 0) {
		ERROR_LOG(Log::FileSystem, "ReadAt from 0-sized file: %s", filename_.c_str());
		return 0;
	}

#if defined(HAVE_LIBRETRO_VFS)
	std::lock_guard<std::mutex> guard(readLock_);
	File::Fseek(file_, absolutePos, SEEK_SET);
	return fread(data, bytes, count, file_);
#elif PPSSPP_PLATFORM(SWITCH)
	// Toolchain has no fancy IO API.  We must lock.
	std::lock_guard<std::mutex> guard(readLock_);
	lseek(fd_, absolutePos, SEEK_SET);
	// read() returns -1 on error, not a short count. Dividing that (implicitly
	// converted to a huge size_t) by bytes would otherwise report a huge bogus
	// success instead of a failure, so callers would trust unwritten data.
	ssize_t retval = read(fd_, data, bytes * count);
	return retval < 0 ? 0 : (size_t)retval / bytes;
#elif PPSSPP_PLATFORM(ANDROID)
	// pread64 doesn't appear to actually be 64-bit safe, though such ISOs are uncommon.  See #10862.
	if (absolutePos <= 0x7FFFFFFF) {
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64
		ssize_t retval = pread64(fd_, data, bytes * count, absolutePos);
#else
		ssize_t retval = pread(fd_, data, bytes * count, absolutePos);
#endif
		return retval < 0 ? 0 : (size_t)retval / bytes;
	} else {
		// Since pread64 doesn't change the file offset, it should be safe to avoid the lock in the common case.
		std::lock_guard<std::mutex> guard(readLock_);
		lseek64(fd_, absolutePos, SEEK_SET);
		ssize_t retval = read(fd_, data, bytes * count);
		return retval < 0 ? 0 : (size_t)retval / bytes;
	}
#elif !defined(_WIN32)
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64
	ssize_t retval = pread64(fd_, data, bytes * count, absolutePos);
#else
	ssize_t retval = pread(fd_, data, bytes * count, absolutePos);
#endif
	return retval < 0 ? 0 : (size_t)retval / bytes;
#else
	DWORD read = -1;
	OVERLAPPED offset = { 0 };
	offset.Offset = (DWORD)(absolutePos & 0xffffffff);
	offset.OffsetHigh = (DWORD)((absolutePos & 0xffffffff00000000) >> 32);
	auto result = ReadFile(handle_, data, (DWORD)(bytes * count), &read, &offset);
	// On failure, report 0 bytes read rather than (size_t)-1 - callers treat the
	// return value as a byte/unit count, not an error sentinel.
	return result == TRUE ? (size_t)read / bytes : 0;
#endif
}
