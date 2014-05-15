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

#include "native/base/mutex.h"
#include "Core/FileSystems/FileSystem.h"

class MetaFileSystem : public IHandleAllocator, public IFileSystem
{
private:
	u32 current;
	struct MountPoint
	{
		std::string prefix;
		IFileSystem *system;

		bool operator == (const MountPoint &other) const
		{
			return prefix == other.prefix && system == other.system;
		}
	};
	std::vector<MountPoint> fileSystems;

	typedef std::map<int, std::string> currentDir_t;
	currentDir_t currentDir;

	std::string startingDirectory;
	int lastOpenError;
	recursive_mutex lock;

public:
	MetaFileSystem()
	{
		current = 6;  // what?
	}

	void Mount(std::string prefix, IFileSystem *system);
	void Unmount(std::string prefix, IFileSystem *system);
	void Remount(IFileSystem *oldSystem, IFileSystem *newSystem);

	IFileSystem *GetSystem(const std::string &prefix);
	IFileSystem *GetSystemFromFilename(const std::string &filename);

	void ThreadEnded(int threadID);

	void Shutdown();

	u32 GetNewHandle() {return current++;}
	void FreeHandle(u32 handle) {}

	virtual void DoState(PointerWrap &p);

	IFileSystem *GetHandleOwner(u32 handle);
	bool MapFilePath(const std::string &inpath, std::string &outpath, MountPoint **system);

	inline bool MapFilePath(const std::string &_inpath, std::string &outpath, IFileSystem **system)
	{
		MountPoint *mountPoint;
		if (MapFilePath(_inpath, outpath, &mountPoint))
		{
			*system = mountPoint->system;
			return true;
		}

		return false;
	}

	std::string NormalizePrefix(std::string prefix) const;

	// Only possible if a file system is a DirectoryFileSystem or similar.
	bool GetHostPath(const std::string &inpath, std::string &outpath);
	
	std::vector<PSPFileInfo> GetDirListing(std::string path);
	u32      OpenFile(std::string filename, FileAccess access, const char *devicename = NULL);
	u32      OpenWithError(int &error, std::string filename, FileAccess access, const char *devicename = NULL);
	void     CloseFile(u32 handle);
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size);
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size);
	size_t   SeekFile(u32 handle, s32 position, FileMove type);
	PSPFileInfo GetFileInfo(std::string filename);
	bool     OwnsHandle(u32 handle) {return false;}
	inline size_t GetSeekPos(u32 handle)
	{
		return SeekFile(handle, 0, FILEMOVE_CURRENT);
	}

	virtual int ChDir(const std::string &dir);

	virtual bool MkDir(const std::string &dirname);
	virtual bool RmDir(const std::string &dirname);
	virtual int  RenameFile(const std::string &from, const std::string &to);
	virtual bool RemoveFile(const std::string &filename);
	virtual int  Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec);
	virtual int  DevType(u32 handle);
	virtual int  Flags() { return 0; }

	// Convenience helper - returns < 0 on failure.
	int ReadEntireFile(const std::string &filename, std::vector<u8> &data);

	void SetStartingDirectory(const std::string &dir) {
		lock_guard guard(lock);
		startingDirectory = dir;
	}
};
