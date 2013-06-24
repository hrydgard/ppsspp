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


class ISOFileSystem : public IFileSystem
{
public:
	ISOFileSystem(IHandleAllocator *_hAlloc, BlockDevice *_blockDevice, std::string _restrictPath = "");
	~ISOFileSystem();
	void DoState(PointerWrap &p);
	std::vector<PSPFileInfo> GetDirListing(std::string path);
	u32      OpenFile(std::string filename, FileAccess access);
	void     CloseFile(u32 handle);
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size);
	size_t   SeekFile(u32 handle, s32 position, FileMove type);
	PSPFileInfo GetFileInfo(std::string filename);
	bool     OwnsHandle(u32 handle);

	size_t WriteFile(u32 handle, const u8 *pointer, s64 size);
	bool GetHostPath(const std::string &inpath, std::string &outpath) {return false;}
	virtual bool MkDir(const std::string &dirname) {return false;}
	virtual bool RmDir(const std::string &dirname) {return false;}
	virtual int  RenameFile(const std::string &from, const std::string &to) {return -1;}
	virtual bool RemoveFile(const std::string &filename) {return false;}

private:
	struct TreeEntry
	{
		TreeEntry(){}
		~TreeEntry()
		{
			for (size_t i = 0; i < children.size(); ++i)
				delete children[i];
			children.clear();
		}

		std::string name;
		u32 flags;
		u32 startingPosition;
		s64 size;
		bool isDirectory;
		bool isBlockSectorMode;  // "umd:" mode: all sizes and offsets are in 2048 byte chunks

		TreeEntry *parent;
		std::vector<TreeEntry*> children;
	};

	struct OpenFileEntry
	{
		TreeEntry *file;
		unsigned int seekPos;  // TODO: Make 64-bit?
		bool isRawSector;   // "/sce_lbn" mode
		u32 sectorStart;
		u32 openSize;
	};
	

	typedef std::map<u32,OpenFileEntry> EntryMap;
	EntryMap entries;
	IHandleAllocator *hAlloc;
	TreeEntry *treeroot;
	BlockDevice *blockDevice;

	TreeEntry entireISO;

	// Don't use this in the emu, not savestated.
	std::vector<std::string> restrictTree;

	void ReadDirectory(u32 startsector, u32 dirsize, TreeEntry *root, size_t level);
	TreeEntry *GetFromPath(std::string path, bool catchError=true);
	std::string EntryFullPath(TreeEntry *e);
};
