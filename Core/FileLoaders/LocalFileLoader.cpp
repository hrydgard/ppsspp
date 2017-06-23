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

#include "file/file_util.h"
#include "Common/FileUtil.h"
#include "Core/FileLoaders/LocalFileLoader.h"
#ifdef _WIN32
#include "Common/CommonWindows.h"
#endif

LocalFileLoader::LocalFileLoader(const std::string &filename)
	: fd_(0), f_(nullptr), filesize_(0), filename_(filename) {
    // FIXME: perhaps at this point we should just use plain open?
	f_ = File::OpenCFile(filename, "rb");
	fd_ = fileno(f_);
	if (!f_) {
		return;
	}

#ifdef __ANDROID__
	// Android NDK does not support 64-bit file I/O using C streams
	// so we fall back onto syscalls
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

size_t LocalFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
#ifndef _WIN32
	return pread(fd_, data, bytes * count, absolutePos) / bytes;
#else
	DWORD read = -1;
	intptr_t handle = fileno_to_handle(fd_);
	if (handle == 0) {
		return -1;
	}
	OVERLAPPED offset = { 0 };
	offset.Offset = (DWORD)(absolutePos & 0xffffffff);
	offset.OffsetHigh = (DWORD)((absolutePos & 0xffffffff00000000) >> 32);
	auto result = ReadFile((HANDLE)handle, data, bytes * count, &read, &offset);
	return result == TRUE ? (size_t)read / bytes : -1;
#endif
}
