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

#include <cstring>
#include <cstdio>
#include <ctype.h>
#include <algorithm>

#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/ChunkFile.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/HLE/sceKernel.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"


const int sectorSize = 2048;

bool parseLBN(std::string filename, u32 *sectorStart, u32 *readSize)
{
	// The format of this is: "/sce_lbn" "0x"? HEX* ANY* "_size" "0x"? HEX* ANY*
	// That means that "/sce_lbn/_size1/" is perfectly valid.
	// Most commonly, it looks like /sce_lbn0x10_size0x100 or /sce_lbn10_size100 (always hex.)

	// If it doesn't starts with /sce_lbn or doesn't have _size, look for a file instead.
	if (filename.compare(0, sizeof("/sce_lbn") - 1, "/sce_lbn") != 0)
		return false;
	size_t size_pos = filename.find("_size");
	if (size_pos == filename.npos)
		return false;

	// TODO: Return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT when >= 32 long but passes above checks.
	if (filename.size() >= 32)
		return false;

	const char *filename_c = filename.c_str();
	size_t pos = strlen("/sce_lbn");

	if (sscanf(filename_c + pos, "%x", sectorStart) != 1)
		*sectorStart = 0;

	pos = size_pos + strlen("_size");

	if (sscanf(filename_c + pos, "%x", readSize) != 1)
		*readSize = 0;

	return true;
}

#pragma pack(push)
#pragma pack(1)
struct DirectoryEntry
{
	u8 size;
	u8 sectorsInExtendedRecord;
	u32_le firstDataSectorLE;	// LBA
	u32_be firstDataSectorBE;
	u32_le dataLengthLE;				// Size
	u32_be dataLengthBE;
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
	u16_le volSeqNumberLE;
	u16_be volSeqNumberBE;
	u8 identifierLength; //identifier comes right after
	u8 firstIdChar;

#if COMMON_LITTLE_ENDIAN
	u32 firstDataSector() const
	{
		return firstDataSectorLE;
	}
	u32 dataLength() const
	{
		return dataLengthLE;
	}
	u32 volSeqNumber() const
	{
		return volSeqNumberLE;
	}
#else
	u32 firstDataSector() const
	{
		return firstDataSectorBE;
	}
	u32 dataLength() const
	{
		return dataLengthBE;
	}
	u32 volSeqNumber() const
	{
		return volSeqNumberBE;
	}
#endif
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
	u32_le numSectorsLE;
	u32_be numSectoreBE;
	char morezeros[32];
	u16_le volSetSizeLE;
	u16_be volSetSizeBE;
	u16_le volSeqNumLE;
	u16_be volSeqNumBE;
	u16_le sectorSizeLE;
	u16_be sectorSizeBE;
	u32_le pathTableLengthLE;
	u32_be pathTableLengthBE;
	u16_le firstLETableSectorLE;
	u16_be firstLETableSectorBE;
	u16_le secondLETableSectorLE;
	u16_be secondLETableSectorBE;
	u16_le firstBETableSectorLE;
	u16_be firstBETableSectorBE;
	u16_le secondBETableSectorLE;
	u16_be secondBETableSectorBE;
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

ISOFileSystem::ISOFileSystem(IHandleAllocator *_hAlloc, BlockDevice *_blockDevice, std::string _restrictPath) 
{
	if (!_restrictPath.empty())
	{
		size_t pos = _restrictPath.find_first_not_of('/');
		while (pos != _restrictPath.npos)
		{
			size_t endPos = _restrictPath.find_first_of('/', pos);
			if (endPos == _restrictPath.npos)
				endPos = _restrictPath.length();
			if (pos != endPos)
				restrictTree.push_back(_restrictPath.substr(pos, endPos - pos));
			pos = _restrictPath.find_first_not_of('/', endPos);
		}
	}

	blockDevice = _blockDevice;
	hAlloc = _hAlloc;

	VolDescriptor desc;
	blockDevice->ReadBlock(16, (u8*)&desc);

	entireISO.name = "";
	entireISO.isDirectory = false;
	entireISO.startingPosition = 0;
	entireISO.size = _blockDevice->GetNumBlocks();
	entireISO.flags = 0;
	entireISO.parent = NULL;

	treeroot = new TreeEntry();
	treeroot->isDirectory = true;
	treeroot->startingPosition = 0;
	treeroot->size = 0;
	treeroot->flags = 0;
	treeroot->parent = NULL;

	if (memcmp(desc.cd001, "CD001", 5)) {
		ERROR_LOG(FILESYS, "ISO looks bogus? Giving up...");
		return;
	}

	u32 rootSector = desc.root.firstDataSector();
	u32 rootSize = desc.root.dataLength();

	ReadDirectory(rootSector, rootSize, treeroot, 0);
}

ISOFileSystem::~ISOFileSystem()
{
	delete blockDevice;
	delete treeroot;
}

void ISOFileSystem::ReadDirectory(u32 startsector, u32 dirsize, TreeEntry *root, size_t level)
{
	for (u32 secnum = startsector, endsector = dirsize/2048 + startsector; secnum < endsector; ++secnum)
	{
		u8 theSector[2048];
		blockDevice->ReadBlock(secnum, theSector);
		lastReadBlock_ = secnum;

		for (int offset = 0; offset < 2048; )
		{
			DirectoryEntry &dir = *(DirectoryEntry *)&theSector[offset];
			u8 sz = theSector[offset];

			// Nothing left in this sector.  There might be more in the next one.
			if (sz == 0)
				break;

			const int IDENTIFIER_OFFSET = 33;
			if (offset + IDENTIFIER_OFFSET + dir.identifierLength > 2048)
			{
				ERROR_LOG(FILESYS, "Directory entry crosses sectors, corrupt iso?");
				return;
			}

			offset += dir.size;

			bool isFile = (dir.flags & 2) ? false : true;
			bool relative;

			TreeEntry *e = new TreeEntry();
			if (dir.identifierLength == 1 && (dir.firstIdChar == '\x00' || dir.firstIdChar == '.'))
			{
				e->name = ".";
				relative = true;
			}
			else if (dir.identifierLength == 1 && dir.firstIdChar == '\x01')
			{
				e->name = "..";
				relative = true;
			}
			else
			{
				e->name = std::string((char *)&dir.firstIdChar, dir.identifierLength);
				relative = false;
			}

			e->size = dir.dataLength();
			e->startingPosition = dir.firstDataSector() * 2048;
			e->isDirectory = !isFile;
			e->flags = dir.flags;
			e->parent = root;

			// Let's not excessively spam the log - I commented this line out.
			//DEBUG_LOG(FILESYS, "%s: %s %08x %08x %i", e->isDirectory?"D":"F", e->name.c_str(), dir.firstDataSectorLE, e->startingPosition, e->startingPosition);

			if (e->isDirectory && !relative)
			{
				if (dir.firstDataSector() == startsector)
				{
					ERROR_LOG(FILESYS, "WARNING: Appear to have a recursive file system, breaking recursion");
				}
				else
				{
					bool doRecurse = true;
					if (!restrictTree.empty())
						doRecurse = level < restrictTree.size() && restrictTree[level] == e->name;

					if (doRecurse)
						ReadDirectory(dir.firstDataSector(), dir.dataLength(), e, level + 1);
					else
						continue;
				}
			}
			root->children.push_back(e);
		}
	}
}

ISOFileSystem::TreeEntry *ISOFileSystem::GetFromPath(std::string path, bool catchError)
{
	if (path.length() == 0) {
		// Ah, the device!	"umd0:"
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
				std::string n = (e->children[i]->name);
				for (size_t j = 0; j < n.size(); j++) {
					n[j] = tolower(n[j]);
				}
				std::string curPath = path.substr(0, path.find_first_of('/'));
				for (size_t j = 0; j < curPath.size(); j++) {
					curPath[j] = tolower(curPath[j]);
				}

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
			if (catchError)
			{
				ERROR_LOG(FILESYS,"File %s not found", path.c_str());
			}
			return 0;
		}
	}
}

u32 ISOFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename)
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
	entry.isRawSector = false;
	entry.isBlockSectorMode = false;

	if (filename.compare(0,8,"/sce_lbn") == 0)
	{
		u32 sectorStart = 0xFFFFFFFF, readSize = 0xFFFFFFFF;
		parseLBN(filename, &sectorStart, &readSize);
		if (sectorStart > blockDevice->GetNumBlocks())
		{
			WARN_LOG(FILESYS, "Unable to open raw sector, out of range: %s, sector %08x, max %08x", filename.c_str(), sectorStart, blockDevice->GetNumBlocks());
			return 0;
		}
		else if (sectorStart == blockDevice->GetNumBlocks())
		{
			ERROR_LOG(FILESYS, "Should not be able to open the block after the last on disc! %08x", sectorStart);
		}

		DEBUG_LOG(FILESYS, "Got a raw sector open: %s, sector %08x, size %08x", filename.c_str(), sectorStart, readSize);
		u32 newHandle = hAlloc->GetNewHandle();
		entry.seekPos = 0;
		entry.file = 0;
		entry.isRawSector = true;
		entry.sectorStart = sectorStart;
		entry.openSize = readSize;
		// when open as "umd1:/sce_lbn0x0_size0x6B49D200", that mean open umd1 as a block device.
		// the param in sceIoLseek and sceIoRead is lba mode. we must mark it.
		if(strncmp(devicename, "umd0:", 5)==0 || strncmp(devicename, "umd1:", 5)==0)
			entry.isBlockSectorMode = true;

		entries[newHandle] = entry;
		return newHandle;
	}

	if (access & FILEACCESS_WRITE)
	{
		ERROR_LOG(FILESYS, "Can't open file %s with write access on an ISO partition", filename.c_str());
		return 0;
	}

	// May return entireISO for "umd0:"
	entry.file = GetFromPath(filename);
	if (!entry.file){
		return 0;
	}

	if (entry.file==&entireISO)
		entry.isBlockSectorMode = true;

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
		ERROR_LOG(FILESYS, "Hey, what are you doing? Closing non-open files?");
	}
}

bool ISOFileSystem::OwnsHandle(u32 handle)
{
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

int ISOFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter == entries.end()) {
		ERROR_LOG(FILESYS, "Ioctl on a bad file handle");
		return SCE_KERNEL_ERROR_BADF;
	}

	OpenFileEntry &e = iter->second;

	switch (cmd) {
	// Get ISO9660 volume descriptor (from open ISO9660 file.)
	case 0x01020001:
		if (e.isBlockSectorMode) {
			ERROR_LOG(FILESYS, "Unsupported read volume descriptor command on a umd block device");
			return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
		}

		if (!Memory::IsValidAddress(outdataPtr) || outlen < 0x800) {
			WARN_LOG_REPORT(FILESYS, "sceIoIoctl: Invalid out pointer while reading ISO9660 volume descriptor");
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}

		INFO_LOG(SCEIO, "sceIoIoctl: reading ISO9660 volume descriptor read");
		blockDevice->ReadBlock(16, Memory::GetPointer(outdataPtr));
		return 0;

	// Get ISO9660 path table (from open ISO9660 file.)
	case 0x01020002:
		if (e.isBlockSectorMode) {
			ERROR_LOG(FILESYS, "Unsupported read path table command on a umd block device");
			return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
		}

		VolDescriptor desc;
		blockDevice->ReadBlock(16, (u8 *)&desc);
		if (outlen < (u32)desc.pathTableLengthLE) {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		} else {
			int block = (u16)desc.firstLETableSectorLE;
			u32 size = (u32)desc.pathTableLengthLE;
			u8 *out = Memory::GetPointer(outdataPtr);

			int blocks = size / blockDevice->GetBlockSize();
			blockDevice->ReadBlocks(block, blocks, out);
			size -= blocks * blockDevice->GetBlockSize();
			out += blocks * blockDevice->GetBlockSize();

			// The remaining (or, usually, only) partial sector.
			if (size > 0) {
				u8 temp[2048];
				blockDevice->ReadBlock(block, temp);
				memcpy(out, temp, size);
			}
			return 0;
		}
	}
	return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
}

int ISOFileSystem::DevType(u32 handle)
{
	EntryMap::iterator iter = entries.find(handle);
	return iter->second.isBlockSectorMode ? PSP_DEV_TYPE_BLOCK : PSP_DEV_TYPE_FILE;
}

size_t ISOFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size)
{
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t ISOFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec)
{
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		OpenFileEntry &e = iter->second;

		if (size < 0) {
			ERROR_LOG_REPORT(FILESYS, "Invalid read for %lld bytes from umd %s", size, e.file ? e.file->name.c_str() : "device");
			return 0;
		}
		
		if (e.isBlockSectorMode) {
			// Whole sectors! Shortcut to this simple code.
			blockDevice->ReadBlocks(e.seekPos, (int)size, pointer);
			if (abs((int)lastReadBlock_ - (int)e.seekPos) > 100) {
				// This is an estimate, sometimes it takes 1+ seconds, but it definitely takes time.
				usec = 100000;
			}
			e.seekPos += (int)size;
			lastReadBlock_ = e.seekPos;
			return (int)size;
		}

		u64 positionOnIso;
		s64 fileSize;
		if (e.isRawSector) {
			positionOnIso = e.sectorStart * 2048ULL + e.seekPos;
			fileSize = (s64)e.openSize;
		} else {
			_dbg_assert_msg_(FILESYS, e.file != 0, "Expecting non-raw fd to have a tree entry.");
			positionOnIso = e.file->startingPosition + e.seekPos;
			fileSize = e.file->size;
		}

		if ((s64)e.seekPos > fileSize) {
			WARN_LOG(FILESYS, "Read starting outside of file, at %lld / %lld", (s64)e.seekPos, fileSize);
			return 0;
		}
		if ((s64)e.seekPos + size > fileSize) {
			// Clamp to the remaining size, but read what we can.
			const s64 newSize = fileSize - (s64)e.seekPos;
			WARN_LOG(FILESYS, "Reading beyond end of file, clamping size %lld to %lld", size, newSize);
			size = newSize;
		}

		// Okay, we have size and position, let's rock.
		const int firstBlockOffset = positionOnIso & 2047;
		const int firstBlockSize = firstBlockOffset == 0 ? 0 : (int)std::min(size, 2048LL - firstBlockOffset);
		const int lastBlockSize = (size - firstBlockSize) & 2047;
		const s64 middleSize = size - firstBlockSize - lastBlockSize;
		u32 secNum = (u32)(positionOnIso / 2048);
		u8 theSector[2048];

		_dbg_assert_msg_(FILESYS, (middleSize & 2047) == 0, "Remaining size should be aligned");

		const u8 *const start = pointer;
		if (firstBlockSize > 0) {
			blockDevice->ReadBlock(secNum++, theSector);
			memcpy(pointer, theSector + firstBlockOffset, firstBlockSize);
			pointer += firstBlockSize;
		}
		if (middleSize > 0) {
			const u32 sectors = (u32)(middleSize / 2048);
			blockDevice->ReadBlocks(secNum, sectors, pointer);
			secNum += sectors;
			pointer += middleSize;
		}
		if (lastBlockSize > 0) {
			blockDevice->ReadBlock(secNum++, theSector);
			memcpy(pointer, theSector, lastBlockSize);
			pointer += lastBlockSize;
		}

		size_t totalBytes = pointer - start;
		if (abs((int)lastReadBlock_ - (int)secNum) > 100) {
			// This is an estimate, sometimes it takes 1+ seconds, but it definitely takes time.
			usec = 100000;
		}
		lastReadBlock_ = secNum;
		e.seekPos += (unsigned int)totalBytes;
		return (size_t)totalBytes;
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS, "Hey, what are you doing? Reading non-open files?");
		return 0;
	}
}

size_t ISOFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size)
{
	ERROR_LOG(FILESYS, "Hey, what are you doing? You can't write to an ISO!");
	return 0;
}

size_t ISOFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec)
{
	ERROR_LOG(FILESYS, "Hey, what are you doing? You can't write to an ISO!");
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
				e.seekPos = e.openSize + position;
			else
				e.seekPos = (unsigned int)(e.file->size + position);
			break;
		}
		return (size_t)e.seekPos;
	}
	else
	{
		//This shouldn't happen...
		ERROR_LOG(FILESYS, "Hey, what are you doing? Seeking in non-open files?");
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

	TreeEntry *entry = GetFromPath(filename, false);
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

		if(!strcmp(e->name.c_str(), ".") || !strcmp(e->name.c_str(), "..")) // do not include the relative entries in the list
			continue;

		PSPFileInfo x;
		x.name = e->name;
		x.access = FILEACCESS_READ;
		x.size = e->size;
		x.type = e->isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
		x.isOnSectorSystem = true;
		x.startSector = e->startingPosition/2048;
		myVector.push_back(x);
	}
	return myVector;
}

std::string ISOFileSystem::EntryFullPath(TreeEntry *e)
{
	if (e == &entireISO)
		return "";

	size_t fullLen = 0;
	TreeEntry *cur = e;
	while (cur != NULL && cur != treeroot)
	{
		// For the "/".
		fullLen += 1 + cur->name.size();
		cur = cur->parent;
	}

	std::string path;
	path.resize(fullLen);

	cur = e;
	while (cur != NULL && cur != treeroot)
	{
		path.replace(fullLen - cur->name.size(), cur->name.size(), cur->name);
		path.replace(fullLen - cur->name.size() - 1, 1, "/");
		fullLen -= 1 + cur->name.size();
		cur = cur->parent;
	}

	return path;
}

ISOFileSystem::TreeEntry::~TreeEntry() {
	for (size_t i = 0; i < children.size(); ++i)
		delete children[i];
	children.clear();
}

void ISOFileSystem::DoState(PointerWrap &p)
{
	auto s = p.Section("ISOFileSystem", 1, 2);
	if (!s)
		return;

	int n = (int) entries.size();
	p.Do(n);

	if (p.mode == p.MODE_READ)
	{
		entries.clear();
		for (int i = 0; i < n; ++i)
		{
			u32 fd = 0;
			OpenFileEntry of;

			p.Do(fd);
			p.Do(of.seekPos);
			p.Do(of.isRawSector);
			p.Do(of.isBlockSectorMode);
			p.Do(of.sectorStart);
			p.Do(of.openSize);

			bool hasFile = false;
			p.Do(hasFile);
			if (hasFile) {
				std::string path;
				p.Do(path);
				of.file = GetFromPath(path);
			} else {
				of.file = NULL;
			}

			entries[fd] = of;
		}
	}
	else
	{
		for (EntryMap::iterator it = entries.begin(), end = entries.end(); it != end; ++it)
		{
			OpenFileEntry &of = it->second;
			p.Do(it->first);
			p.Do(of.seekPos);
			p.Do(of.isRawSector);
			p.Do(of.isBlockSectorMode);
			p.Do(of.sectorStart);
			p.Do(of.openSize);

			bool hasFile = of.file != NULL;
			p.Do(hasFile);
			if (hasFile) {
				std::string path = "";
				path = EntryFullPath(of.file);
				p.Do(path);
			}
		}
	}

	if (s >= 2) {
		p.Do(lastReadBlock_);
	} else {
		lastReadBlock_ = 0;
	}
}
