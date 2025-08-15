// Copyright (c) 2013- PPSSPP Project.

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

#include <string>
#include <cstring>

#include "Common/Log.h"
#include "Core/Loaders.h"
#include "Core/ELF/PBPReader.h"

PBPReader::PBPReader(FileLoader *fileLoader) {
	if (!fileLoader->Exists()) {
		ERROR_LOG(Log::Loader, "Failed to open PBP file %s", fileLoader->GetPath().c_str());
		return;
	}

	fileSize_ = (size_t)fileLoader->FileSize();
	if (fileLoader->ReadAt(0, sizeof(header_), (u8 *)&header_) != sizeof(header_)) {
		ERROR_LOG(Log::Loader, "PBP is too small to be valid: %s", fileLoader->GetPath().c_str());
		return;
	}
	if (memcmp(header_.magic, "\0PBP", 4) != 0) {
		if (memcmp(header_.magic, "\nFLE", 4) != 0) {
			VERBOSE_LOG(Log::Loader, "%s: File actually an ELF, not a PBP", fileLoader->GetPath().c_str());
			isELF_ = true;
		} else {
			ERROR_LOG(Log::Loader, "Magic number in %s indicated no PBP: %s", fileLoader->GetPath().c_str(), header_.magic);
		}
		return;
	}

	VERBOSE_LOG(Log::Loader, "Loading PBP, version = %08x", header_.version);
	file_ = fileLoader;
}

bool PBPReader::GetSubFile(PBPSubFile file, std::vector<u8> *out) const {
	if (!file_) {
		return false;
	}

	const size_t expected = GetSubFileSize(file);

	// This is only used to get the PARAM.SFO, so let's have a strict 256MB file size limit for sanity.
	if (expected > 256 * 1024 * 1024) {
		ERROR_LOG(Log::Loader, "Bad subfile size: %d", (int)expected);
		return false;
	}

	const u32 off = header_.offsets[(int)file];

	out->resize(expected);
	size_t bytes = file_->ReadAt(off, expected, &(*out)[0]);
	if (bytes != expected) {
		ERROR_LOG(Log::Loader, "PBP file read truncated: %d -> %d", (int)expected, (int)bytes);
		if (bytes < expected) {
			out->resize(bytes);
		}
	}
	return true;
}

bool PBPReader::GetSubFileAsString(PBPSubFile file, std::string *out) const {
	if (!file_) {
		out->clear();
		return false;
	}

	const size_t expected = GetSubFileSize(file);

	// This is only used to get the PNG, AT3 etc, so let's have a strict 256MB file size limit for sanity.
	if (expected > 256 * 1024 * 1024) {
		ERROR_LOG(Log::Loader, "Bad subfile size: %d", (int)expected);
		return false;
	}
	const u32 off = header_.offsets[(int)file];

	out->resize(expected);
	size_t bytes = file_->ReadAt(off, expected, (void *)out->data());
	if (bytes != expected) {
		ERROR_LOG(Log::Loader, "PBP file read truncated: %d -> %d", (int)expected, (int)bytes);
		if (bytes < expected) {
			out->resize(bytes);
			// should we still return true here?
		}
	}
	return true;
}

PBPReader::~PBPReader() {
	// Does not take ownership.
	file_ = nullptr;
}
