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

#include <string>
#include <vector>
#include <mutex>
#include <memory>

#include "Core/FileSystems/FileSystem.h"

class MetaFileSystem : public IHandleAllocator, public IFileSystem {
private:
	s32 current;
	struct MountPoint {
		std::string prefix;
		std::shared_ptr<IFileSystem> system;

		bool operator == (const MountPoint &other) const {
			return prefix == other.prefix && system == other.system;
		}
	};

	// The order of this vector is meaningful - lookups are always a linear search from the start.
	std::vector<MountPoint> fileSystems;

	typedef std::map<int, std::string> currentDir_t;
	currentDir_t currentDir;

	std::string startingDirectory;
	std::recursive_mutex lock;  // must be recursive. TODO: fix that

	// Assumes the lock is held
	void Reset() {
		// This used to be 6, probably an attempt to replicate PSP handles.
		// However, that's an artifact of using psplink anyway...
		current = 1;
		startingDirectory.clear();
	}

public:
	MetaFileSystem() {
		Reset();
	}

	void Mount(const std::string &prefix, std::shared_ptr<IFileSystem> system);
	// Fails if there's not already a file system at prefix.
	bool Remount(const std::string &prefix, std::shared_ptr<IFileSystem> system);

	void UnmountAll();
	void Unmount(const std::string &prefix);

	// The pointer returned from these are for temporary usage only. Do not store.
	IFileSystem *GetSystem(const std::string &prefix);
	IFileSystem *GetSystemFromFilename(const std::string &filename);
	IFileSystem *GetHandleOwner(u32 handle);
	FileSystemFlags FlagsFromFilename(const std::string &filename) {
		IFileSystem *sys = GetSystemFromFilename(filename);
		return sys ? sys->Flags() : FileSystemFlags::NONE;
	}

	void ThreadEnded(int threadID);
	void Shutdown();

	u32 GetNewHandle() override {
		u32 res = current++;
		if (current < 0) {
			// Some code assumes it'll never become 0.
			current = 1;
		}
		return res;
	}
	void FreeHandle(u32 handle) override {}

	void DoState(PointerWrap &p) override;

	int MapFilePath(const std::string &inpath, std::string &outpath, MountPoint **system);

	inline int MapFilePath(const std::string &_inpath, std::string &outpath, IFileSystem **system) {
		MountPoint *mountPoint = nullptr;
		int error = MapFilePath(_inpath, outpath, &mountPoint);
		if (error == 0) {
			*system = mountPoint->system.get();
			return error;
		}

		return error;
	}

	std::string NormalizePrefix(std::string prefix) const;

	std::vector<PSPFileInfo> GetDirListing(const std::string &path, bool *exists = nullptr) override;
	int      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override;
	void     CloseFile(u32 handle) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size) override;
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override;
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	bool     OwnsHandle(u32 handle) override { return false; }
	inline size_t GetSeekPos(u32 handle) {
		return SeekFile(handle, 0, FILEMOVE_CURRENT);
	}

	virtual int ChDir(const std::string &dir);

	bool MkDir(const std::string &dirname) override;
	bool RmDir(const std::string &dirname) override;
	int  RenameFile(const std::string &from, const std::string &to) override;
	bool RemoveFile(const std::string &filename) override;
	int  Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	PSPDevType DevType(u32 handle) override;
	FileSystemFlags Flags() override { return FileSystemFlags::NONE; }
	u64  FreeSpace(const std::string &path) override;

	// Convenience helper - returns < 0 on failure.
	int ReadEntireFile(const std::string &filename, std::vector<u8> &data, bool quiet = false);

	void SetStartingDirectory(const std::string &dir) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		startingDirectory = dir;
	}

	int64_t ComputeRecursiveDirectorySize(const std::string &dirPath);

	// Shouldn't ever be called, but meh.
	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override {
		int64_t sizeTemp = ComputeRecursiveDirectorySize(path);
		if (sizeTemp >= 0) {
			*size = sizeTemp;
			return true;
		} else {
			return false;
		}
	}

private:
	int64_t RecursiveSize(const std::string &dirPath);
};
