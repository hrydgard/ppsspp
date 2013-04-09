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
#include "Core/ELF/PBPReader.h"

PBPReader::PBPReader(const char *filename) : header_() {
	file_ = fopen(filename, "rb");
	if (!file_) {
		return;
	}

	fseek(file_, 0, SEEK_END);
	fileSize_ = ftell(file_);
	fseek(file_, 0, SEEK_SET);
	fread((char *)&header_, 1, sizeof(header_), file_);
	if (memcmp(header_.magic, "\0PBP", 4) != 0) {
		fclose(file_);
		file_ = 0;
		return;
	}

	INFO_LOG(LOADER, "Loading PBP, version = %08x", header_.version);
}

u8 *PBPReader::GetSubFile(PBPSubFile file, size_t *outSize) {
	*outSize = GetSubFileSize(file);
	u8 *buffer = new u8[*outSize];
	fseek(file_, header_.offsets[(int)file], SEEK_SET);
	fread(buffer, 1, *outSize, file_);
	return buffer;
}

void PBPReader::GetSubFileAsString(PBPSubFile file, std::string *out) {
	out->resize(GetSubFileSize(file));
	fseek(file_, header_.offsets[(int)file], SEEK_SET);
	fread((void *)out->data(), 1, out->size(), file_);
}

PBPReader::~PBPReader() {
	if (file_)
		fclose(file_);
}
