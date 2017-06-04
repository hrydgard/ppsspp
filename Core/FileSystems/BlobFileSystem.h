// Copyright (c) 2017- PPSSPP Project.

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

// This is used for opening a debug file as a blob, and mounting it.
// Importantly, uses a fileLoader for all access, so http:// URLs are supported.
// As of writing, only used by GE dump replay.

#include <map>
#include <string>
#include "Core/Loaders.h"
#include "Core/FileSystems/FileSystem.h"

class BlobFileSystem : public IFileSystem {
public:
	BlobFileSystem(IHandleAllocator *hAlloc, FileLoader *fileLoader, std::string alias);
	~BlobFileSystem();

	void DoState(PointerWrap &p) override;
	std::vector<PSPFileInfo> GetDirListing(std::string path) override;
	u32      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override;
	void     CloseFile(u32 handle) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size) override;
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override;
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	bool     OwnsHandle(u32 handle) override;
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	int      DevType(u32 handle) override;
	int      Flags() override { return 0; }

	bool MkDir(const std::string &dirname) override;
	bool RmDir(const std::string &dirname) override;
	int  RenameFile(const std::string &from, const std::string &to) override;
	bool RemoveFile(const std::string &filename) override;
	bool GetHostPath(const std::string &inpath, std::string &outpath) override;
	u64 FreeSpace(const std::string &path) override;

private:
	// File positions.
	std::map<u32, s64> entries_;

	IHandleAllocator *alloc_;
	FileLoader *fileLoader_;
	std::string alias_;
};
