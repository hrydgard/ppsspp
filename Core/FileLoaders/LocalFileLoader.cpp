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
#include "file/file_util.h"
#include "Common/FileUtil.h"
#include "Core/FileLoaders/LocalFileLoader.h"

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

size_t LocalFileLoader::Read(size_t bytes, size_t count, void *data, Flags flags) {
#ifdef ANDROID
	return read(fd_, data, bytes * count) / bytes;
#else
	return fread(data, bytes, count, f_);
#endif
}

size_t LocalFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	Seek(absolutePos);
	return Read(bytes, count, data);
}
