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

#include <fstream>
#include <string>
#include <cstring>

#include "Common/Log.h"
#include "Common/FileUtil.h"
#include "Core/ELF/PBPReader.h"

PBPReader::PBPReader(const char *filename) : header_(), isELF_(false) {
	file_ = File::OpenCFile(filename, "rb");
	if (!file_) {
		ERROR_LOG(LOADER, "Failed to open PBP file %s", filename);
		return;
	}

	fseek(file_, 0, SEEK_END);
	fileSize_ = ftell(file_);
	fseek(file_, 0, SEEK_SET);
	if (fread((char *)&header_, 1, sizeof(header_), file_) != sizeof(header_)) {
		ERROR_LOG(LOADER, "PBP is too small to be valid: %s", filename);
		fclose(file_);
		file_ = nullptr;
		return;
	}
	if (memcmp(header_.magic, "\0PBP", 4) != 0) {
		if (memcmp(header_.magic, "\nFLE", 4) != 0) {
			DEBUG_LOG(LOADER, "%s: File actually an ELF, not a PBP", filename);
			isELF_ = true;
		} else {
			ERROR_LOG(LOADER, "Magic number in %s indicated no PBP: %s", filename, header_.magic);
		}
		fclose(file_);
		file_ = nullptr;
		return;
	}

	DEBUG_LOG(LOADER, "Loading PBP, version = %08x", header_.version);
}

u8 *PBPReader::GetSubFile(PBPSubFile file, size_t *outSize) {
	if (!file_) {
		*outSize = 0;
		return new u8[0];
	}

	const size_t expected = GetSubFileSize(file);
	const u32 off = header_.offsets[(int)file];

	*outSize = expected;
	if (fseek(file_, off, SEEK_SET) != 0) {
		ERROR_LOG(LOADER, "PBP file offset invalid: %d", off);
		*outSize = 0;
		return new u8[0];
	} else {
		u8 *buffer = new u8[expected];
		size_t bytes = fread(buffer, 1, expected, file_);
		if (bytes != expected) {
			ERROR_LOG(LOADER, "PBP file read truncated: %d -> %d", (int)expected, (int)bytes);
			if (bytes < expected) {
				*outSize = bytes;
			}
		}
		return buffer;
	}
}

void PBPReader::GetSubFileAsString(PBPSubFile file, std::string *out) {
	if (!file_) {
		out->clear();
		return;
	}

	const size_t expected = GetSubFileSize(file);
	const u32 off = header_.offsets[(int)file];

	out->resize(expected);
	if (fseek(file_, off, SEEK_SET) != 0) {
		ERROR_LOG(LOADER, "PBP file offset invalid: %d", off);
		out->clear();
	} else {
		size_t bytes = fread((void *)out->data(), 1, expected, file_);
		if (bytes != expected) {
			ERROR_LOG(LOADER, "PBP file read truncated: %d -> %d", (int)expected, (int)bytes);
			if (bytes < expected) {
				out->resize(bytes);
			}
		}
	}
}

PBPReader::~PBPReader() {
	if (file_)
		fclose(file_);
}
