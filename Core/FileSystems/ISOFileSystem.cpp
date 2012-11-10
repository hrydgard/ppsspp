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

#include "Globals.h"
#include "Log.h"
#include "ISOFileSystem.h"
#include <cstring>
#include <cstdio>

const int sectorSize = 2048;

static bool parseLBN(std::string filename, u32 *sectorStart, u32 *readSize)
{
	if (filename.substr(0, 8) != "/sce_lbn")
		return false;
	std::string yo = filename;
	filename.erase(0, 10);
	sscanf(filename.c_str(), "%08x", sectorStart);
	filename.erase(0, filename.find("_size") + 7);
	sscanf(filename.c_str(), "%08x", readSize);
	return true;
}

#pragma pack(push)
#pragma pack(1)
struct DirectoryEntry
{
	u8 size;
	u8 sectorsInExtendedRecord;
	u32 firstDataSectorLE;	// LBA
	u32 firstDataSectorBE;
	u32 dataLengthLE;				// Size
	u32 dataLengthBE;
	u8 years;
	u8 month;
	u8 day;
	u8 hour;
	u8 minute;
	u8 second;
	u8 offsetFromGMT;
	u8 flags; // 2 = directory
	u8 fileUnitSize;
	u8 interleaveGap;
	u16 volSeqNumberLE;
	u16 volSeqNumberBE;
	u8 identifierLength; //identifier comes right after
	u8 firstIdChar;
};
struct DirectorySector
{
	DirectoryEntry entry;
	char space[2048-sizeof(DirectoryEntry)];
};

struct VolDescriptor
{
	char type;
	char cd001[6];
	char version;
	char sysid[32];
	char volid[32];
	char zeros[8];
	u32 numSectorsLE;
	u32 numSectoreBE;
	char morezeros[32];
	u16 volSetSizeLE;
	u16 volSetSizeBE;
	u16 volSeqNumLE;
	u16 volSeqNumBE;
	u16 sectorSizeLE;
	u16 sectorSizeBE;
	u32 pathTableLengthLE;
	u32 pathTableLengthBE;
	u16 firstLETableSectorLE;
	u16 firstLETableSectorBE;
	u16 secondLETableSectorLE;
	u16 secondLETableSectorBE;
	u16 firstBETableSectorLE;
	u16 firstBETableSectorBE;
	u16 secondBETableSectorLE;
	u16 secondBETableSectorBE;
	DirectoryEntry root;
	char volumeSetIdentifier[128];
	char publisherIdentifier[128];
	char dataPreparerIdentifier[128];
	char applicationIdentifier[128];
	char copyrightFileIdentifier[37];
	char abstractFileIdentifier[37];
	char bibliographicalFileIdentifier[37];
	char volCreationDateTime[17];
	char mreModDateTime[17];
	char volExpireDateTime[17];
	char volEffectiveDateTime[17];
	char one;
	char zero;
	char reserved[512];
	char zeroos[653];
};

#pragma pack(pop)

ISOFileSystem::ISOFileSystem(IHandleAllocator *_hAlloc, BlockDevice *_blockDevice) 
{
	blockDevice = _blockDevice;
	hAlloc = _hAlloc;

	VolDescriptor desc;
	blockDevice->ReadBlock(16, (u8*)&desc);

	entireISO.name = "";
	entireISO.isDirectory = false;
	entireISO.startingPosition = 0;
	entireISO.size = _blockDevice->GetNumBlocks() * _blockDevice->GetBlockSize();
	entireISO.isBlockSectorMode = true;
	entireISO.flags = 0;

	if (!memcmp(desc.cd001, "CD001", 5))
	{
		INFO_LOG(FILESYS, "Looks like a valid ISO!");
	}
	else
	{
		ERROR_LOG(FILESYS, "ISO looks bogus? trying anyway...");
	}

	treeroot = new TreeEntry;

	u32 rootSector = desc.root.firstDataSectorLE;
	u32 rootSize = desc.root.dataLengthLE;

	ReadDirectory(rootSector, rootSize, treeroot);
}

ISOFileSystem::~ISOFileSystem()
{
	delete blockDevice;
}

void ISOFileSystem::ReadDirectory(u32 startsector, u32 dirsize, TreeEntry *root)
{
	u8 buffer[2048];
	int offset = 0;
	u32 secnum = startsector;

	u8 theSector[2048];
	blockDevice->ReadBlock(secnum, theSector);

	while (secnum < (dirsize/2048 + startsector))
	{
		DirectoryEntry &dir = *((DirectoryEntry *)buffer);
		u8 sz = theSector[offset];
		if (sz == 0) // NOT the correct way
			goto nextblock; //done

		memcpy(&dir, theSector + offset, sz);
		
		buffer[2047]=0;
		offset += dir.size;
		if (offset >= 2048)
		{
nextblock:
			offset=0;
			secnum++;
			blockDevice->ReadBlock(secnum, theSector);
			memcpy(&dir, theSector + offset, sz);
		}
		bool isFile = (dir.flags & 2) ? false : true;

		int fnLength = dir.identifierLength;

		char name[256];
		for (int i = 0; i < fnLength; i++)
			name[i] = buffer[33+i] ? buffer[33+i] : '.';
		name[fnLength] = '\0';

		bool relative = false;

		if (!strcmp(name, "."))	// "." record
		{
			relative = true;
		}

		if (strlen(name) == 1 && name[0] == '\x01')	 // ".." record
		{
			strcpy(name,"..");
			relative=true;
		}

		TreeEntry *e = new TreeEntry;
		e->name = name;
		e->size = dir.dataLengthLE;
		e->startingPosition = dir.firstDataSectorLE * 2048;
		e->isDirectory = !isFile;
		e->flags = dir.flags;
		e->isBlockSectorMode = false;
				
		DEBUG_LOG(FILESYS, "%s: %s %08x %08x %i", e->isDirectory?"D":"F", name, dir.firstDataSectorLE, e->startingPosition, e->startingPosition);

		if (e->isDirectory && !relative)
		{
			if (dir.firstDataSectorLE == startsector)
			{
				ERROR_LOG(FILESYS, "WARNING: Appear to have a recursive file system, breaking recursion");
			}
			else
			{
				ReadDirectory(dir.firstDataSectorLE, dir.dataLengthLE, e);
			}
		}
		root->children.push_back(e);
	}

}

ISOFileSystem::TreeEntry *ISOFileSystem::GetFromPath(std::string path)
{
	if (path.length() == 0)
	{
		//Ah, the device!	"umd0:"
		return &entireISO;
	}

	if (path.substr(0,2) == "./")
		path.erase(0,2);

	if (path[0] == '/')
		path.erase(0,1);

	TreeEntry *e = treeroot;
	if (path.length() == 0)
		return e;

	while (true)
	{
		TreeEntry *ne = 0;
		std::string name = "";
		if (path.length()>0)
		{
			for (size_t i=0; i<e->children.size(); i++)
			{
				std::string n = e->children[i]->name;
				std::string curPath = path.substr(0, path.find_first_of('/'));
				if (curPath == n)
				{
					//yay we got it
					ne = e->children[i];
					name = n;
					break;
				}
			}
		}
		if (ne)
		{
			e = ne;
			size_t l = name.length();
			path.erase(0, l);
			if (path.length() == 0 || (path.length()==1 && path[0] == '/'))
				return e;
			path.erase(0, 1);
			while (path[0] == '/')
				path.erase(0, 1);
		}
		else
		{
			ERROR_LOG(FILESYS,"File %s not found", path.c_str());
			return 0;
		}
	}
}

u32 ISOFileSystem::OpenFile(std::string filename, FileAccess access)
{
	// LBN unittest
	/*
	u32 a, b;
	if (parseLBN("/sce_lbn0x307aa_size0xefffe000", &a, &b)) {
		ERROR_LOG(FILESYS, "lbn: %08x %08x", a, b);
	} else {
		ERROR_LOG(FILESYS, "faillbn: %08x %08x", a, b);
	}*/


	OpenFileEntry entry;
	if (filename.compare(0,8,"/sce_lbn") == 0)
	{
		u32 sectorStart = 0xFFFFFFFF, readSize = 0xFFFFFFFF;
		parseLBN(filename, &sectorStart, &readSize);
		INFO_LOG(FILESYS, "Got a raw sector open: %s, sector %08x, size %08x", filename.c_str(), sectorStart, readSize);
		u32 newHandle = hAlloc->GetNewHandle();
		entry.seekPos = 0;
		entry.file = 0;
		entry.isRawSector = true;
		entry.sectorStart = sectorStart;
		entry.openSize = readSize;
		entries[newHandle] = entry;
		return newHandle;
	}

	entry.isRawSector = false;

	if (access & FILEACCESS_WRITE)
	{
		ERROR_LOG(FILESYS, "Can't open file %s with write access on an ISO partition", filename.c_str());
		return 0;
	}

	// May return entireISO for "umd0:"
	entry.file = GetFromPath(filename);
	if (!entry.file)
		return 0;

	entry.seekPos = 0;

	u32 newHandle = hAlloc->GetNewHandle();
	entries[newHandle] = entry;
	return newHandle;
}

void ISOFileSystem::CloseFile(u32 handle)
{
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		//CloseHandle((*iter).second.hFile);
		hAlloc->FreeHandle(handle);
		entries.erase(iter);
	}
	else
	{
		//This shouldn't happen...
		ERROR_LOG(HLE,"Hey, what are you doing? Closing non-open files?");
	}
}

bool ISOFileSystem::OwnsHandle(u32 handle)
{
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

size_t ISOFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size)
{
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		OpenFileEntry &e = iter->second;
		
		if (e.file != 0 && e.file->isBlockSectorMode)
		{
			// Whole sectors! Shortcut to this simple code.
			for (int i = 0; i < size; i++)
			{
				blockDevice->ReadBlock(e.seekPos, pointer + i * 2048);
				e.seekPos++;
			}
			return (size_t)size;
		}

		u32 positionOnIso;
		if (e.isRawSector)
		{
			// TODO: this seems bogus
			positionOnIso = e.sectorStart * 2048;
			
			if (e.seekPos + size > e.openSize)
			{
				size = e.openSize - e.seekPos;
			}
		}
		else
		{
			//clamp read length
			if ((s64)e.seekPos > e.file->size - (s64)size)
			{
				size = e.file->size - (s64)e.seekPos;
			}

			positionOnIso = e.file->startingPosition + e.seekPos;
		}
		//okay, we have size and position, let's rock

		u32 totalRead = 0;
		int secNum = positionOnIso / 2048;
		int posInSector = positionOnIso & 2047;
		s64 remain = size;		

		u8 theSector[2048];

		while (remain > 0)
		{
			blockDevice->ReadBlock(secNum, theSector);
			size_t bytesToCopy = 2048 - posInSector;
			if ((s64)bytesToCopy > remain)
				bytesToCopy = (size_t)remain;

			memcpy(pointer, theSector + posInSector, bytesToCopy);
			totalRead += (u32)bytesToCopy;
			pointer += bytesToCopy;
			remain -= bytesToCopy;
			posInSector = 0;
			secNum++;
		}
		e.seekPos += size;
		return totalRead;
	}
	else
	{
		//This shouldn't happen...
		ERROR_LOG(HLE,"Hey, what are you doing? Reading non-open files?");
		return 0;
	}
}

size_t ISOFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) 
{
	ERROR_LOG(HLE,"Hey, what are you doing? You can't write to an ISO!");
	return 0;
}

size_t ISOFileSystem::SeekFile(u32 handle, s32 position, FileMove type) 
{
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		OpenFileEntry &e = iter->second;
		switch (type)
		{
		case FILEMOVE_BEGIN:
			e.seekPos = position;
			break;
		case FILEMOVE_CURRENT:
			e.seekPos += position;
			break;
		case FILEMOVE_END:
			if (e.isRawSector)
				e.seekPos = e.openSize;
			else
				e.seekPos = e.file->size + position;		
			break;
		}
		return (size_t)e.seekPos;
	}
	else
	{
		//This shouldn't happen...
		ERROR_LOG(HLE,"Hey, what are you doing? Seeking in non-open files?");
		return 0;
	}
}

PSPFileInfo ISOFileSystem::GetFileInfo(std::string filename) 
{
	if (filename.compare(0,8,"/sce_lbn") == 0)
	{
		u32 sectorStart = 0xFFFFFFFF, readSize = 0xFFFFFFFF;
		parseLBN(filename, &sectorStart, &readSize);

		PSPFileInfo fileInfo;
		fileInfo.name = filename;
		fileInfo.exists = true;
		fileInfo.size = readSize;
		fileInfo.startSector = sectorStart;
		fileInfo.isOnSectorSystem = true;
		fileInfo.numSectors = (readSize + sectorSize - 1) / sectorSize;
		return fileInfo;
	}

	TreeEntry *entry = GetFromPath(filename);
	PSPFileInfo x; 
	if (!entry)
	{
		x.size = 0;
		x.exists = false;
	}
	else
	{
		x.name = entry->name;
		x.access = FILEACCESS_READ;
		x.size = entry->size;
		x.exists = true;
		x.type = entry->isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
		x.isOnSectorSystem = true;
		x.startSector = entry->startingPosition/2048;
	}
	return x;
}

std::vector<PSPFileInfo> ISOFileSystem::GetDirListing(std::string path)
{
	std::vector<PSPFileInfo> myVector;
	TreeEntry *entry = GetFromPath(path);
	if (!entry)
	{
		return myVector;
	}

	for (size_t i=0; i<entry->children.size(); i++)
	{
		TreeEntry *e = entry->children[i];


		if(e->name == "." || e->name == "..") // do not include the relative entries in the list
			continue;

		PSPFileInfo x;
		x.name = e->name;
		x.access = FILEACCESS_READ;
		x.size = e->size;
		x.type = e->isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
		x.isOnSectorSystem = true;
		x.startSector = entry->startingPosition/2048;
		myVector.push_back(x);
	}
	return myVector;
}
