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

#include "Core/FileLoaders/RetryingFileLoader.h"

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

bool RetryingFileLoader::ExistsFast() {
	if (!backend_->ExistsFast()) {
		// Retry once, immediately.
		return backend_->ExistsFast();
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

size_t RetryingFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags) {
	size_t readSize = backend_->ReadAt(absolutePos, bytes, data, flags);

	int retries = 0;
	while (readSize < bytes && retries < MAX_RETRIES) {
		u8 *p = (u8 *)data;
		readSize += backend_->ReadAt(absolutePos + readSize, bytes - readSize, p + readSize, flags);
		++retries;
	}

	filepos_ = absolutePos + readSize;
	return readSize;
}
