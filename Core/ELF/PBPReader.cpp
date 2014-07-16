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
	if (!File::OpenCPPFile(file_, filename, std::ios::in | std::ios::binary | std::ios::ate)) {
		ERROR_LOG(LOADER, "Failed to open PBP file %s", filename);
		return;
	}

	fileSize_ = file_.tellg();
	file_.seekg(0, std::ios::beg);
	file_.read((char*)&header_, sizeof(header_));
	if (memcmp(header_.magic, "\0PBP", 4) != 0) {
		if (memcmp(header_.magic, "\nFLE", 4) != 0) {
			DEBUG_LOG(LOADER, "%s: File actually an ELF, not a PBP", filename);
			isELF_ = true;
		} else {
			ERROR_LOG(LOADER, "Magic number in %s indicated no PBP: %s", filename, header_.magic);
		}
		file_.close();
	} else { 
		DEBUG_LOG(LOADER, "Loading PBP, version = %08x", header_.version);
	}
}

u8 *PBPReader::GetSubFile(PBPSubFile file, size_t *outSize) {
	*outSize = GetSubFileSize(file);
	u8 *buffer = new u8[*outSize];
	file_.seekg(header_.offsets[(int)file], std::ios::beg);
	file_.read((char*)buffer, *outSize);
	return buffer;
}

void PBPReader::GetSubFileAsString(PBPSubFile file, std::string *out) {
	out->resize(GetSubFileSize(file));
	file_.seekg(header_.offsets[(int)file], std::ios::beg);
	file_.read((char*)out->data(), out->size());
}

