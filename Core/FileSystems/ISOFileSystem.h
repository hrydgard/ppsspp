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

#pragma once

#include <map>
#include <memory>

#include "FileSystem.h"

#include "BlockDevices.h"

bool parseLBN(const std::string &filename, u32 *sectorStart, u32 *readSize);

class ISOFileSystem : public IFileSystem {
public:
	ISOFileSystem(IHandleAllocator *_hAlloc, BlockDevice *_blockDevice);
	~ISOFileSystem();

	void DoState(PointerWrap &p) override;
	std::vector<PSPFileInfo> GetDirListing(const std::string &path, bool *exists = nullptr) override;
	int      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override;
	void     CloseFile(u32 handle) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	PSPFileInfo GetFileInfoByHandle(u32 handle) override;
	bool     OwnsHandle(u32 handle) override;
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	PSPDevType DevType(u32 handle) override;
	FileSystemFlags Flags() const override;
	u64      FreeDiskSpace(const std::string &path) override { return 0; }

	size_t WriteFile(u32 handle, const u8 *pointer, s64 size) override;
	size_t WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override;

	bool MkDir(const std::string &dirname) override {return false;}
	bool RmDir(const std::string &dirname) override { return false; }
	int  RenameFile(const std::string &from, const std::string &to) override { return -1; }
	bool RemoveFile(const std::string &filename) override { return false; }

	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override { return false; }
	void Describe(char *buf, size_t size) const override { snprintf(buf, size, "ISO"); }  // TODO: Ask the fileLoader about the origins

private:
	struct TreeEntry {
		~TreeEntry();

		// Recursive function that reconstructs the path by looking at the parent pointers.
		std::string BuildPath();

		std::string name;
		u32 flags = 0;
		u32 startingPosition = 0;
		s64 size = 0;
		bool isDirectory = false;

		u32 startsector = 0;
		u32 dirsize = 0;

		TreeEntry *parent = nullptr;

		bool valid = false;
		std::vector<TreeEntry *> children;
	};

	struct OpenFileEntry {
		const TreeEntry *file;
		unsigned int seekPos;  // TODO: Make 64-bit?
		bool isRawSector;   // "/sce_lbn" mode
		bool isBlockSectorMode;  // "umd:" mode: all sizes and offsets are in 2048 byte chunks
		u32 sectorStart;
		u32 openSize;
	};

	typedef std::map<u32, OpenFileEntry> EntryMap;
	EntryMap entries;
	IHandleAllocator *hAlloc;
	TreeEntry *treeroot;
	BlockDevice *blockDevice;
	mutable u32 lastReadBlock_;

	TreeEntry entireISO;

	void ReadDirectory(TreeEntry *root) const;
	const TreeEntry *GetFromPath(std::string_view path, bool catchError = true);
	std::string EntryFullPath(const TreeEntry *e);
};

// On the "umd0:" device, any file you open is the entire ISO.
// Simply wrap around an ISOFileSystem which has all the necessary machinery, while changing
// the filenames to "", to achieve this.
class ISOBlockSystem : public IFileSystem {
public:
	ISOBlockSystem(std::shared_ptr<IFileSystem> isoFileSystem) : isoFileSystem_(std::move(isoFileSystem)) {}

	void DoState(PointerWrap &p) override {
		// This is a bit iffy, as block device savestates already are iffy (loads/saves multiple times for multiple mounts..)
		isoFileSystem_->DoState(p);
	}

	std::vector<PSPFileInfo> GetDirListing(const std::string &path, bool *exists = nullptr) override {
		if (exists)
			*exists = true;
		return std::vector<PSPFileInfo>();
	}
	int      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override {
		return isoFileSystem_->OpenFile("", access, devicename);
	}
	void     CloseFile(u32 handle) override {
		isoFileSystem_->CloseFile(handle);
	}
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override {
		return isoFileSystem_->ReadFile(handle, pointer, size);
	}
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override {
		return isoFileSystem_->ReadFile(handle, pointer, size, usec);
	}
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override {
		return isoFileSystem_->SeekFile(handle, position, type);
	}
	PSPFileInfo GetFileInfo(std::string filename) override {
		return isoFileSystem_->GetFileInfo("");
	}
	PSPFileInfo GetFileInfoByHandle(u32 handle) override {
		return isoFileSystem_->GetFileInfoByHandle(handle);
	}
	bool     OwnsHandle(u32 handle) override {
		return isoFileSystem_->OwnsHandle(handle);
	}
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override {
		return isoFileSystem_->Ioctl(handle, cmd, indataPtr, inlen, outdataPtr, outlen, usec);
	}
	PSPDevType DevType(u32 handle) override {
		return isoFileSystem_->DevType(handle);
	}
	FileSystemFlags Flags() const override { return isoFileSystem_->Flags(); }
	u64      FreeDiskSpace(const std::string &path) override { return isoFileSystem_->FreeDiskSpace(path); }

	size_t WriteFile(u32 handle, const u8 *pointer, s64 size) override {
		return isoFileSystem_->WriteFile(handle, pointer, size);
	}
	size_t WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override {
		return isoFileSystem_->WriteFile(handle, pointer, size, usec);
	}
	bool MkDir(const std::string &dirname) override { return false; }
	bool RmDir(const std::string &dirname) override { return false; }
	int  RenameFile(const std::string &from, const std::string &to) override { return -1; }
	bool RemoveFile(const std::string &filename) override { return false; }

	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override { return false; }

	void Describe(char *buf, size_t size) const override { snprintf(buf, size, "ISOBlock"); }

private:
	std::shared_ptr<IFileSystem> isoFileSystem_;
};
