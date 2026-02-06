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

// TODO: Remove the Windows-specific code, FILE is fine there too.

#include <map>

#include "Common/File/Path.h"
#include "Core/FileSystems/FileSystem.h"

#if defined(_WIN32) && !defined(HAVE_LIBRETRO_VFS)
typedef void * HANDLE;
#endif

struct DirectoryFileHandle {
	enum Flags {
		NORMAL,
		SKIP_REPLAY,
	};

#ifdef HAVE_LIBRETRO_VFS
	FILE *hFile = nullptr;
#elif defined(_WIN32)
	HANDLE hFile = (HANDLE)-1;
#else
	int hFile = -1;
#endif
	s64 needsTrunc_ = -1;
	bool replay_ = true;
	bool inGameDir_ = false;
	FileSystemFlags fileSystemFlags_ = (FileSystemFlags)0;

	DirectoryFileHandle() {}

	DirectoryFileHandle(Flags flags, FileSystemFlags fileSystemFlags)
		: replay_(flags != SKIP_REPLAY), fileSystemFlags_(fileSystemFlags) {}

	Path GetLocalPath(const Path &basePath, std::string_view localPath) const;
	bool Open(const Path &basePath, std::string &fileName, FileAccess access, u32 &err);
	size_t Read(u8* pointer, s64 size);
	size_t Write(const u8* pointer, s64 size);
	size_t Seek(s32 position, FileMove type);
	void Close();
};

class DirectoryFileSystem : public IFileSystem {
public:
	DirectoryFileSystem(IHandleAllocator *_hAlloc, const Path &_basePath, FileSystemFlags _flags = FileSystemFlags::NONE);
	~DirectoryFileSystem();

	void CloseAll();

	void DoState(PointerWrap &p) override;
	std::vector<PSPFileInfo> GetDirListing(const std::string &path, bool *exists = nullptr) override;
	int      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override;
	void     CloseFile(u32 handle) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size) override;
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override;
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	PSPFileInfo GetFileInfoByHandle(u32 handle) override;
	bool     OwnsHandle(u32 handle) override;
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	PSPDevType DevType(u32 handle) override;

	bool MkDir(const std::string &dirname) override;
	bool RmDir(const std::string &dirname) override;
	int  RenameFile(const std::string &from, const std::string &to) override;
	bool RemoveFile(const std::string &filename) override;
	FileSystemFlags Flags() const override { return flags; }
	u64 FreeDiskSpace(const std::string &path) override;

	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override;
	void Describe(char *buf, size_t size) const override { snprintf(buf, size, "Dir: %s", basePath.c_str()); }

private:
	struct OpenFileEntry {
		DirectoryFileHandle hFile;
		std::string guestFilename;
		FileAccess access = FILEACCESS_NONE;
	};

	typedef std::map<u32, OpenFileEntry> EntryMap;
	EntryMap entries;
	Path basePath;
	IHandleAllocator *hAlloc;
	FileSystemFlags flags;

	Path GetLocalPath(std::string_view internalPath) const;
};

// VFSFileSystem: Ability to map in Android APK paths as well! Does not support all features, only meant for fonts.
// Very inefficient - always load the whole file on open.
class VFSFileSystem : public IFileSystem {
public:
	VFSFileSystem(IHandleAllocator *_hAlloc, std::string _basePath);
	~VFSFileSystem();

	void DoState(PointerWrap &p) override;
	std::vector<PSPFileInfo> GetDirListing(const std::string &path, bool *exists = nullptr) override;
	int      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override;
	void     CloseFile(u32 handle) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size) override;
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override;
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	PSPFileInfo GetFileInfoByHandle(u32 handle) override;
	bool     OwnsHandle(u32 handle) override;
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	PSPDevType DevType(u32 handle) override;

	bool MkDir(const std::string &dirname) override;
	bool RmDir(const std::string &dirname) override;
	int  RenameFile(const std::string &from, const std::string &to) override;
	bool RemoveFile(const std::string &filename) override;
	FileSystemFlags Flags() const override { return FileSystemFlags::FLASH; }
	u64 FreeDiskSpace(const std::string &path) override { return 0; }

	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override { return false; }
	void Describe(char *buf, size_t size) const override { snprintf(buf, size, "VFS: %s", basePath.c_str()); }

private:
	struct OpenFileEntry {
		u8 *fileData;
		size_t size;
		size_t seekPos;
	};

	typedef std::map<u32, OpenFileEntry> EntryMap;
	EntryMap entries;
	std::string basePath;
	IHandleAllocator *hAlloc;

	std::string GetLocalPath(std::string_view localpath) const;
};
