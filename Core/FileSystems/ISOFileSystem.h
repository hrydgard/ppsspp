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
#include <list>

#include "FileSystem.h"

#include "BlockDevices.h"

bool parseLBN(std::string filename, u32 *sectorStart, u32 *readSize);

class ISOFileSystem : public IFileSystem
{
public:
	ISOFileSystem(IHandleAllocator *_hAlloc, BlockDevice *_blockDevice, std::string _restrictPath = "");
	~ISOFileSystem();

	void DoState(PointerWrap &p) override;
	std::vector<PSPFileInfo> GetDirListing(std::string path) override;
	u32      OpenFile(std::string filename, FileAccess access, const char *devicename = NULL) override;
	void     CloseFile(u32 handle) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	bool     OwnsHandle(u32 handle) override;
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	int      DevType(u32 handle) override;
	int      Flags() override { return 0; }
	u64      FreeSpace(const std::string &path) override { return 0; }

	size_t WriteFile(u32 handle, const u8 *pointer, s64 size) override;
	size_t WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override;

	bool GetHostPath(const std::string &inpath, std::string &outpath) {return false;}
	bool MkDir(const std::string &dirname) override {return false;}
	bool RmDir(const std::string &dirname) override { return false; }
	int  RenameFile(const std::string &from, const std::string &to) override { return -1; }
	bool RemoveFile(const std::string &filename) override { return false; }

private:
	struct TreeEntry
	{
		TreeEntry(){}
		~TreeEntry();

		std::string name;
		u32 flags;
		u32 startingPosition;
		s64 size;
		bool isDirectory;

		TreeEntry *parent;
		std::vector<TreeEntry*> children;
	};

	struct OpenFileEntry
	{
		TreeEntry *file;
		unsigned int seekPos;  // TODO: Make 64-bit?
		bool isRawSector;   // "/sce_lbn" mode
		bool isBlockSectorMode;  // "umd:" mode: all sizes and offsets are in 2048 byte chunks
		u32 sectorStart;
		u32 openSize;
	};
	
	typedef std::map<u32,OpenFileEntry> EntryMap;
	EntryMap entries;
	IHandleAllocator *hAlloc;
	TreeEntry *treeroot;
	BlockDevice *blockDevice;
	u32 lastReadBlock_;

	TreeEntry entireISO;

	// Don't use this in the emu, not savestated.
	std::vector<std::string> restrictTree;

	void ReadDirectory(u32 startsector, u32 dirsize, TreeEntry *root, size_t level);
	TreeEntry *GetFromPath(std::string path, bool catchError = true);
	std::string EntryFullPath(TreeEntry *e);
};

// On the "umd0:" device, any file you open is the entire ISO.
// Simply wrap around an ISOFileSystem which has all the necessary machinery, while changing
// the filenames to "", to achieve this.
class ISOBlockSystem : public IFileSystem {
public:
	ISOBlockSystem(ISOFileSystem *isoFileSystem) : isoFileSystem_(isoFileSystem) {}

	void DoState(PointerWrap &p) override {
		// This is a bit iffy, as block device savestates already are iffy (loads/saves multiple times for multiple mounts..)
		isoFileSystem_->DoState(p);
	}

	std::vector<PSPFileInfo> GetDirListing(std::string path) override { return std::vector<PSPFileInfo>(); }
	u32      OpenFile(std::string filename, FileAccess access, const char *devicename = NULL) override {
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
	bool     OwnsHandle(u32 handle) override {
		return isoFileSystem_->OwnsHandle(handle);
	}
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override {
		return isoFileSystem_->Ioctl(handle, cmd, indataPtr, inlen, outdataPtr, outlen, usec);
	}
	int      DevType(u32 handle) override {
		return isoFileSystem_->DevType(handle);
	}
	int      Flags() override { return isoFileSystem_->Flags(); }
	u64      FreeSpace(const std::string &path) override { return isoFileSystem_->FreeSpace(path); }

	size_t WriteFile(u32 handle, const u8 *pointer, s64 size) override {
		return isoFileSystem_->WriteFile(handle, pointer, size);
	}
	size_t WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override {
		return isoFileSystem_->WriteFile(handle, pointer, size, usec);
	}
	bool GetHostPath(const std::string &inpath, std::string &outpath) { return false; }
	bool MkDir(const std::string &dirname) override { return false; }
	bool RmDir(const std::string &dirname) override { return false; }
	int  RenameFile(const std::string &from, const std::string &to) override { return -1; }
	bool RemoveFile(const std::string &filename) override { return false; }

private:
	ISOFileSystem *isoFileSystem_;
};
