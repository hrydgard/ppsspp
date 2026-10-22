// Copyright (c) 2026- PPSSPP Project.

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
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Core/FileSystems/FileSystem.h"
#include "Core/FileLoaders/ZArchiveFileLoader.h"

// The contents of a ZArchive as a read-only file system.
class ZArchiveFileSystem : public IFileSystem {
public:
	ZArchiveFileSystem(IHandleAllocator *hAlloc, std::shared_ptr<ZArchive> archive, FileSystemFlags flags);
	~ZArchiveFileSystem();

	void DoState(PointerWrap &p) override;
	std::vector<PSPFileInfo> GetDirListing(std::string_view path, bool *exists = nullptr) override;
	int OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override;
	void CloseFile(u32 handle) override;
	size_t ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	size_t WriteFile(u32 handle, const u8 *pointer, s64 size) override { return 0; }
	size_t WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override { return 0; }
	size_t SeekFile(u32 handle, s32 position, FileMove type) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	PSPFileInfo GetFileInfoByHandle(u32 handle) override;
	bool OwnsHandle(u32 handle) override;
	bool MkDir(const std::string &dirname) override { return false; }
	bool RmDir(const std::string &dirname) override { return false; }
	int RenameFile(const std::string &from, const std::string &to) override { return SCE_KERNEL_ERROR_ERRNO_READ_ONLY; }
	bool RemoveFile(const std::string &filename) override { return false; }
	int Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override {
		return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
	}
	PSPDevType DevType(u32 handle) override { return PSPDevType::FILE; }
	FileSystemFlags Flags() const override { return flags_; }
	u64 FreeDiskSpace(const std::string &path) override { return 0; }
	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override;
	void Describe(char *buf, size_t size) const override;

	bool HasOpenFiles();

private:
	struct OpenFileEntry {
		std::string filename;  // As the game opened it, to look it up again after loading a state.
		ZArchive::NodeHandle node = ZArchive::INVALID_NODE;
		u64 size = 0;
		u64 seekPos = 0;
	};

	PSPFileInfo MakeInfo(std::string_view name, ZArchive::NodeHandle node) const;

	IHandleAllocator *hAlloc_;
	std::shared_ptr<ZArchive> archive_;
	FileSystemFlags flags_;
	tm time_{};  // There are no timestamps in a ZArchive, everything gets the archive's.

	std::mutex lock_;
	std::map<u32, OpenFileEntry> entries_;
};

// The memory stick, with the PSP directory of a ZArchive overlaid on it (read-only).
//
// Everything is looked up on the real memory stick first, and only then in the archive, so whatever
// the game writes wins. Writes always go to the real memory stick: writing to a file that only
// exists in the archive copies it over first, and directories that only exist in the archive are
// created as needed. PSP/SAVEDATA in the archive is never shown: save data belongs on the real
// memory stick, where the game, the savedata manager and the user can manage it.
class MemStickOverlayFileSystem : public IFileSystem {
public:
	MemStickOverlayFileSystem(std::shared_ptr<IFileSystem> memstick, std::shared_ptr<ZArchiveFileSystem> archive);

	void DoState(PointerWrap &p) override;
	std::vector<PSPFileInfo> GetDirListing(std::string_view path, bool *exists = nullptr) override;
	int OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override;
	void CloseFile(u32 handle) override;
	size_t ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	size_t WriteFile(u32 handle, const u8 *pointer, s64 size) override;
	size_t WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override;
	size_t SeekFile(u32 handle, s32 position, FileMove type) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	PSPFileInfo GetFileInfoByHandle(u32 handle) override;
	bool OwnsHandle(u32 handle) override;
	bool MkDir(const std::string &dirname) override;
	bool RmDir(const std::string &dirname) override;
	int RenameFile(const std::string &from, const std::string &to) override;
	bool RemoveFile(const std::string &filename) override;
	bool SetFileWritable(const std::string &filename, bool writable) override;
	int Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	PSPDevType DevType(u32 handle) override;
	FileSystemFlags Flags() const override { return memstick_->Flags(); }
	u64 FreeDiskSpace(const std::string &path) override { return memstick_->FreeDiskSpace(path); }
	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override;
	void Describe(char *buf, size_t size) const override;

private:
	// Whether the archive is consulted for a path at all.
	static bool InOverlay(std::string_view path);
	// The archive's info for a path, if it's in the overlay and only in the archive.
	PSPFileInfo ArchiveOnlyInfo(const std::string &path);
	// Creates the directories leading up to path on the memory stick, if they're in the archive.
	void CreateParentDirs(const std::string &path);
	// Copies a file from the archive to the memory stick, so it can be modified.
	bool CopyUp(const std::string &path);

	std::shared_ptr<IFileSystem> memstick_;
	std::shared_ptr<ZArchiveFileSystem> archive_;
};
