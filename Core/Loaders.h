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

#pragma	once

#include <string>

enum IdentifiedFileType {
	FILETYPE_ERROR,

	FILETYPE_PSP_PBP_DIRECTORY,

	FILETYPE_PSP_PBP,
	FILETYPE_PSP_ELF,
	FILETYPE_PSP_ISO,
	FILETYPE_PSP_ISO_NP,

	FILETYPE_PSP_DISC_DIRECTORY,

	FILETYPE_UNKNOWN_BIN,
	FILETYPE_UNKNOWN_ELF,

	// Try to reduce support emails...
	FILETYPE_ARCHIVE_RAR,
	FILETYPE_ARCHIVE_ZIP,
	FILETYPE_ARCHIVE_7Z,
	FILETYPE_PSP_PS1_PBP,
	FILETYPE_ISO_MODE2,

	FILETYPE_NORMAL_DIRECTORY,

	FILETYPE_PSP_SAVEDATA_DIRECTORY,
	FILETYPE_PPSSPP_SAVESTATE,

	FILETYPE_UNKNOWN
};

class FileLoader {
public:
	virtual ~FileLoader() {}

	virtual bool Exists() = 0;
	virtual bool IsDirectory() = 0;
	virtual s64 FileSize() = 0;
	virtual std::string Path() const = 0;
	virtual std::string Extension() {
		const std::string filename = Path();
		size_t pos = filename.find_last_of('.');
		if (pos == filename.npos) {
			return "";
		} else {
			return filename.substr(pos);
		}
	}

	virtual void Seek(s64 absolutePos) = 0;
	virtual size_t Read(size_t bytes, size_t count, void *data) = 0;
	virtual size_t Read(size_t bytes, void *data) {
		return Read(1, bytes, data);
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data) = 0;
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, void *data) {
		return ReadAt(absolutePos, 1, bytes, data);
	}
};

FileLoader *ConstructFileLoader(const std::string &filename);

// This can modify the string, for example for stripping off the "/EBOOT.PBP"
// for a FILETYPE_PSP_PBP_DIRECTORY.
IdentifiedFileType Identify_File(FileLoader *fileLoader);

// Can modify the string filename, as it calls IdentifyFile above.
bool LoadFile(FileLoader **fileLoaderPtr, std::string *error_string);
