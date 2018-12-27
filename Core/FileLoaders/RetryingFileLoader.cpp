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
	: ProxiedFileLoader(backend) {
}

bool RetryingFileLoader::Exists() {
	if (!ProxiedFileLoader::Exists()) {
		// Retry once, immediately.
		return ProxiedFileLoader::Exists();
	}
	return true;
}

bool RetryingFileLoader::ExistsFast() {
	if (!ProxiedFileLoader::ExistsFast()) {
		// Retry once, immediately.
		return ProxiedFileLoader::ExistsFast();
	}
	return true;
}

bool RetryingFileLoader::IsDirectory() {
	// Can't tell if it's an error either way.
	return ProxiedFileLoader::IsDirectory();
}

s64 RetryingFileLoader::FileSize() {
	s64 filesize = ProxiedFileLoader::FileSize();
	if (filesize == 0) {
		return ProxiedFileLoader::FileSize();
	}
	return filesize;
}

size_t RetryingFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags) {
	size_t readSize = backend_->ReadAt(absolutePos, bytes, data, flags);

	int retries = 0;
	while (readSize < bytes && retries < MAX_RETRIES) {
		u8 *p = (u8 *)data;
		readSize += backend_->ReadAt(absolutePos + readSize, bytes - readSize, p + readSize, flags);
		++retries;
	}

	return readSize;
}
