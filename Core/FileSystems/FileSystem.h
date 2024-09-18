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

#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <ctime>

#include "Common.h"
#include "Common/File/Path.h"
#include "Core/HLE/sceKernel.h"

enum FileAccess {
	FILEACCESS_NONE     = 0,
	FILEACCESS_READ     = 1,
	FILEACCESS_WRITE    = 2,
	FILEACCESS_APPEND   = 4,
	FILEACCESS_CREATE   = 8,
	FILEACCESS_TRUNCATE = 16,
	FILEACCESS_EXCL     = 32,

	FILEACCESS_PSP_FLAGS = 63,  // Sum of all the above.

	// Non-PSP flags
	FILEACCESS_PPSSPP_QUIET = 128,
};

enum FileMove {
	FILEMOVE_BEGIN   = 0,
	FILEMOVE_CURRENT = 1,
	FILEMOVE_END     = 2
};

enum FileType {
	FILETYPE_NORMAL    = 1,
	FILETYPE_DIRECTORY = 2
};

enum class PSPDevType {
	INVALID = 0,
	BLOCK = 0x04,
	FILE  = 0x10,
	ALIAS = 0x20,
	EMU_MASK = 0xFF,
	EMU_LBN = 0x10000,
};
ENUM_CLASS_BITOPS(PSPDevType);

enum class FileSystemFlags {
	NONE = 0,
	SIMULATE_FAT32 = 1,
	UMD = 2,
	CARD = 4,
	FLASH = 8,
	STRIP_PSP = 16,
};
ENUM_CLASS_BITOPS(FileSystemFlags);

class IHandleAllocator {
public:
	virtual ~IHandleAllocator() {}
	virtual u32 GetNewHandle() = 0;
	virtual void FreeHandle(u32 handle) = 0;
};

class SequentialHandleAllocator : public IHandleAllocator {
public:
	SequentialHandleAllocator() : handle_(1) {}

	SequentialHandleAllocator(SequentialHandleAllocator &) = delete;
	void operator =(SequentialHandleAllocator &) = delete;

	u32 GetNewHandle() override {
		u32 res = handle_++;
		if (handle_ < 0) {
			// Some code assumes it'll never become 0.
			handle_ = 1;
		}
		return res;
	}
	void FreeHandle(u32 handle) override {}
private:
	int handle_;
};

struct PSPFileInfo {
	PSPFileInfo() {
	}

	void DoState(PointerWrap &p);

	std::string name;
	s64 size = 0;
	u32 access = 0; //unix 777
	bool exists = false;
	FileType type = FILETYPE_NORMAL;

	tm atime{};
	tm ctime{};
	tm mtime{};

	bool isOnSectorSystem = false;
	u32 startSector = 0;
	u32 numSectors = 0;
	u32 sectorSize = 0;
};


class IFileSystem {
public:
	virtual ~IFileSystem() {}

	virtual void DoState(PointerWrap &p) = 0;
	virtual std::vector<PSPFileInfo> GetDirListing(const std::string &path, bool *exists = nullptr) = 0;
	virtual int      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) = 0;
	virtual void     CloseFile(u32 handle) = 0;
	virtual size_t   ReadFile(u32 handle, u8 *pointer, s64 size) = 0;
	virtual size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) = 0;
	virtual size_t   WriteFile(u32 handle, const u8 *pointer, s64 size) = 0;
	virtual size_t   WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) = 0;
	virtual size_t   SeekFile(u32 handle, s32 position, FileMove type) = 0;
	virtual PSPFileInfo GetFileInfo(std::string filename) = 0;
	virtual bool     OwnsHandle(u32 handle) = 0;
	virtual bool     MkDir(const std::string &dirname) = 0;
	virtual bool     RmDir(const std::string &dirname) = 0;
	virtual int      RenameFile(const std::string &from, const std::string &to) = 0;
	virtual bool     RemoveFile(const std::string &filename) = 0;
	virtual int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) = 0;
	virtual PSPDevType DevType(u32 handle) = 0;
	virtual FileSystemFlags Flags() = 0;
	virtual u64      FreeSpace(const std::string &path) = 0;
	virtual bool     ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) = 0;
};


class EmptyFileSystem : public IFileSystem {
public:
	void DoState(PointerWrap &p) override {}
	std::vector<PSPFileInfo> GetDirListing(const std::string &path, bool *exists = nullptr) override {
		if (exists)
			*exists = false;
		std::vector<PSPFileInfo> vec;
		return vec;
	}
	int      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override {return SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;}
	void     CloseFile(u32 handle) override {}
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override {return 0;}
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override {return 0;}
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size) override {return 0;}
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override {return 0;}
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override {return 0;}
	PSPFileInfo GetFileInfo(std::string filename) override {PSPFileInfo f; return f;}
	bool     OwnsHandle(u32 handle) override {return false;}
	bool MkDir(const std::string &dirname) override {return false;}
	bool RmDir(const std::string &dirname) override {return false;}
	int RenameFile(const std::string &from, const std::string &to) override {return -1;}
	bool RemoveFile(const std::string &filename) override {return false;}
	int Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override { return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED; }
	PSPDevType DevType(u32 handle) override { return PSPDevType::INVALID; }
	FileSystemFlags Flags() override { return FileSystemFlags::NONE; }
	u64 FreeSpace(const std::string &path) override { return 0; }
	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override { return false; }
};


