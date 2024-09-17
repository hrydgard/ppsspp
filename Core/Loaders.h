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
#include "Common/File/Path.h"

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
	UNKNOWN_ISO,

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

// NB: It is a REQUIREMENT that implementations of this class are entirely thread safe!
class FileLoader {
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
	virtual Path GetPath() const = 0;

	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) = 0;
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) {
		return ReadAt(absolutePos, 1, bytes, data, flags);
	}

	// Cancel any operations that might block, if possible.
	virtual void Cancel() {}

	virtual std::string LatestError() const {
		return "";
	}
};

class ProxiedFileLoader : public FileLoader {
public:
	ProxiedFileLoader(FileLoader *backend) : backend_(backend) {}
	~ProxiedFileLoader() {
		// Takes ownership.
		delete backend_;
	}

	bool IsRemote() override {
		return backend_->IsRemote();
	}
	bool Exists() override {
		return backend_->Exists();
	}
	bool ExistsFast() override {
		return backend_->ExistsFast();
	}
	bool IsDirectory() override {
		return backend_->IsDirectory();
	}
	s64 FileSize() override {
		return backend_->FileSize();
	}
	Path GetPath() const override {
		return backend_->GetPath();
	}
	void Cancel() override {
		backend_->Cancel();
	}
	std::string LatestError() const override {
		return backend_->LatestError();
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return backend_->ReadAt(absolutePos, bytes, count, data, flags);
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override {
		return backend_->ReadAt(absolutePos, bytes, data, flags);
	}

protected:
	FileLoader *backend_;
};

inline u32 operator & (const FileLoader::Flags &a, const FileLoader::Flags &b) {
	return (u32)a & (u32)b;
}

FileLoader *ConstructFileLoader(const Path &filename);
// Resolve to the target binary, ISO, or other file (e.g. from a directory.)
FileLoader *ResolveFileLoaderTarget(FileLoader *fileLoader);

Path ResolvePBPDirectory(const Path &filename);
Path ResolvePBPFile(const Path &filename);

IdentifiedFileType Identify_File(FileLoader *fileLoader, std::string *errorString);

// Can modify the string filename, as it calls IdentifyFile above.
bool LoadFile(FileLoader **fileLoaderPtr, std::string *error_string);

bool UmdReplace(const Path &filepath, FileLoader **fileLoader, std::string &error);
