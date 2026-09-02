// Copyright (c) 2012- PPSSPP Project.

#pragma once

#include <map>

#include "Core/FileSystems/FileSystem.h"

// Minimal PSP Memory Stick partition/block-device surface. The ordinary files
// remain owned by the ms0:/fatms0: DirectoryFileSystem; this lower-level alias
// supplies the insertion/mount/MSID ioctls used by Sony VSH applications.
class MemoryStickStorageFileSystem : public IFileSystem {
public:
	explicit MemoryStickStorageFileSystem(IHandleAllocator *allocator) : allocator_(allocator) {}

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
	bool MkDir(const std::string &dirname) override { return false; }
	bool RmDir(const std::string &dirname) override { return false; }
	int RenameFile(const std::string &from, const std::string &to) override { return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED; }
	bool RemoveFile(const std::string &filename) override { return false; }
	int Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	PSPDevType DevType(u32 handle) override { return OwnsHandle(handle) ? PSPDevType::BLOCK : PSPDevType::INVALID; }
	FileSystemFlags Flags() const override { return FileSystemFlags::CARD; }
	u64 FreeDiskSpace(const std::string &path) override;
	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override { return false; }
	void Describe(char *buf, size_t size) const override { snprintf(buf, size, "MemoryStickStorage"); }

private:
	IHandleAllocator *allocator_ = nullptr;
	std::map<u32, u64> positions_;
};

