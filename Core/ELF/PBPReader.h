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


#pragma once

#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

enum PBPSubFile {
	PBP_PARAM_SFO,
	PBP_ICON0_PNG,
	PBP_ICON1_PMF,
	PBP_PIC0_PNG,
	PBP_PIC1_PNG,
	PBP_SND0_AT3,
	PBP_EXECUTABLE_PSP,
	PBP_UNKNOWN_PSAR,
};

struct PBPHeader {
	char magic[4];
	u32_le version;
	u32_le offsets[8];
};

class FileLoader;

class PBPReader {
public:
	PBPReader(FileLoader *fileLoader);
	~PBPReader();

	bool IsValid() const { return file_ != nullptr; }
	bool IsELF() const { return file_ == nullptr && isELF_; }

	bool GetSubFile(PBPSubFile file, std::vector<u8> *out);
	void GetSubFileAsString(PBPSubFile file, std::string *out);

	size_t GetSubFileSize(PBPSubFile file) {
		int num = (int)file;
		if (num < 7) {
			return header_.offsets[file + 1] - header_.offsets[file];
		} else {
			return fileSize_ - header_.offsets[file];
		}
	}

private:
	FileLoader *file_;
	size_t fileSize_;
	const PBPHeader header_;
	bool isELF_;
};
