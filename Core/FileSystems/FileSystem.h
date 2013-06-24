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

#include "../../Globals.h"
#include "ChunkFile.h"

enum FileAccess
{
	FILEACCESS_NONE=0,
	FILEACCESS_READ=1,
	FILEACCESS_WRITE=2,
	FILEACCESS_APPEND=4,
	FILEACCESS_CREATE=8
};

enum FileMove
{
	FILEMOVE_BEGIN=0,
	FILEMOVE_CURRENT=1,
	FILEMOVE_END=2
};

enum FileType
{
	FILETYPE_NORMAL=1,
	FILETYPE_DIRECTORY=2
};


class IHandleAllocator {
public:
	virtual ~IHandleAllocator() {}
	virtual u32 GetNewHandle() = 0;
	virtual void FreeHandle(u32 handle) = 0;
};

class SequentialHandleAllocator : public IHandleAllocator {
public:
	SequentialHandleAllocator() : handle_(1) {}
	virtual u32 GetNewHandle() { return handle_++; }
	virtual void FreeHandle(u32 handle) {}
private:
	int handle_;
};

struct PSPFileInfo
{
	PSPFileInfo() 
		: size(0), access(0), exists(false), type(FILETYPE_NORMAL), isOnSectorSystem(false), startSector(0), numSectors(0) {}

	void DoState(PointerWrap &p)
	{
		p.Do(name);
		p.Do(size);
		p.Do(access);
		p.Do(exists);
		p.Do(type);
		p.Do(atime);
		p.Do(ctime);
		p.Do(mtime);
		p.Do(isOnSectorSystem);
		p.Do(startSector);
		p.Do(numSectors);
		p.Do(fpointer);
		p.Do(sectorSize);
		p.DoMarker("PSPFileInfo");
	}

	std::string name;
	s64 size;
	u32 access; //unix 777
	bool exists;
	FileType type;

	tm atime;
	tm ctime;
	tm mtime;

	bool isOnSectorSystem;
	u32 startSector;
	u32 numSectors;
	u32 fpointer;
	u32 sectorSize;
};


class IFileSystem
{
public:
	virtual ~IFileSystem() {}

	virtual void DoState(PointerWrap &p) = 0;
	virtual std::vector<PSPFileInfo> GetDirListing(std::string path) = 0;
	virtual u32      OpenFile(std::string filename, FileAccess access) = 0;
	virtual void     CloseFile(u32 handle) = 0;
	virtual size_t   ReadFile(u32 handle, u8 *pointer, s64 size) = 0;
	virtual size_t   WriteFile(u32 handle, const u8 *pointer, s64 size) = 0;
	virtual size_t   SeekFile(u32 handle, s32 position, FileMove type) = 0;
	virtual PSPFileInfo GetFileInfo(std::string filename) = 0;
	virtual bool     OwnsHandle(u32 handle) = 0;
	virtual bool     MkDir(const std::string &dirname) = 0;
	virtual bool     RmDir(const std::string &dirname) = 0;
	virtual int      RenameFile(const std::string &from, const std::string &to) = 0;
	virtual bool     RemoveFile(const std::string &filename) = 0;
	virtual bool     GetHostPath(const std::string &inpath, std::string &outpath) = 0;
};


class EmptyFileSystem : public IFileSystem
{
public:
	virtual void DoState(PointerWrap &p) {}
	std::vector<PSPFileInfo> GetDirListing(std::string path) {std::vector<PSPFileInfo> vec; return vec;}
	u32      OpenFile(std::string filename, FileAccess access) {return 0;}
	void     CloseFile(u32 handle) {}
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) {return 0;}
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size) {return 0;}
	size_t   SeekFile(u32 handle, s32 position, FileMove type) {return 0;}
	PSPFileInfo GetFileInfo(std::string filename) {PSPFileInfo f; return f;}
	bool     OwnsHandle(u32 handle) {return false;}
	virtual bool MkDir(const std::string &dirname) {return false;}
	virtual bool RmDir(const std::string &dirname) {return false;}
	virtual int RenameFile(const std::string &from, const std::string &to) {return -1;}
	virtual bool RemoveFile(const std::string &filename) {return false;}
	virtual bool GetHostPath(const std::string &inpath, std::string &outpath) {return false;}
};


