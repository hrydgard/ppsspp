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
#include <memory>

#include "Common/CommonTypes.h"

enum class IdentifiedFileType {
	ERROR_IDENTIFYING,

	PSP_PBP_DIRECTORY,

	PSP_PBP,
	PSP_ELF,
	PSP_ISO,
	PSP_ISO_NP,

	PSP_DISC_DIRECTORY,

	UNKNOWN_BIN,
	UNKNOWN_ELF,

	// Try to reduce support emails...
	ARCHIVE_RAR,
	ARCHIVE_ZIP,
	ARCHIVE_7Z,
	PSP_PS1_PBP,
	ISO_MODE2,

	NORMAL_DIRECTORY,

	PSP_SAVEDATA_DIRECTORY,
	PPSSPP_SAVESTATE,

	PPSSPP_GE_DUMP,

	UNKNOWN,
};


class FileLoader {
// NB: It is a REQUIREMENT that implementations of this class are entirely thread safe!
public:
	enum class Flags {
		NONE,
		// Not necessary to read from / store into cache.
		HINT_UNCACHED,
	};

	virtual ~FileLoader() {}

	virtual bool IsRemote() {
		return false;
	}
	virtual bool Exists() = 0;
	virtual bool ExistsFast() {
		return Exists();
	}
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
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) = 0;
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) {
		return ReadAt(absolutePos, 1, bytes, data, flags);
	}

	// Cancel any operations that might block, if possible.
	virtual void Cancel() {
	}
};

inline u32 operator & (const FileLoader::Flags &a, const FileLoader::Flags &b) {
	return (u32)a & (u32)b;
}

FileLoader *ConstructFileLoader(const std::string &filename);
// Resolve to the target binary, ISO, or other file (e.g. from a directory.)
FileLoader *ResolveFileLoaderTarget(FileLoader *fileLoader);

std::string ResolvePBPDirectory(const std::string &filename);
std::string ResolvePBPFile(const std::string &filename);

IdentifiedFileType Identify_File(FileLoader *fileLoader);

class FileLoaderFactory {
public:
	virtual ~FileLoaderFactory() {}
	virtual FileLoader *ConstructFileLoader(const std::string &filename) = 0;
};
void RegisterFileLoaderFactory(std::string name, std::unique_ptr<FileLoaderFactory> factory);

// Can modify the string filename, as it calls IdentifyFile above.
bool LoadFile(FileLoader **fileLoaderPtr, std::string *error_string);
