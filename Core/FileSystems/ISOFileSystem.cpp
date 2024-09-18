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

#include "Common/CommonTypes.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/HLE/sceKernel.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"

const int sectorSize = 2048;

bool parseLBN(const std::string &filename, u32 *sectorStart, u32 *readSize) {
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
struct u32_le_be_pair {
	u8 valueLE[4];
	u8 valueBE[4];
	operator u32() const {
		return valueLE[0] + (valueLE[1] << 8) + (valueLE[2] << 16) + (valueLE[3] << 24);
	}
};

struct u16_le_be_pair {
	u8 valueLE[2];
	u8 valueBE[2];
	operator u16() const {
		return valueLE[0] + (valueLE[1] << 8);
	}
};

struct DirectoryEntry {
	u8 size;
	u8 sectorsInExtendedRecord;
	u32_le_be_pair firstDataSector;       // LBA
	u32_le_be_pair dataLength;            // Size
	u8 years;
	u8 month;
	u8 day;
	u8 hour;
	u8 minute;
	u8 second;
	u8 offsetFromGMT;
	u8 flags;                       // 2 = directory
	u8 fileUnitSize;
	u8 interleaveGap;
	u16_le_be_pair volSeqNumber;
	u8 identifierLength;            //identifier comes right after
	u8 firstIdChar;
};

struct DirectorySector {
	DirectoryEntry entry;
	char space[2048-sizeof(DirectoryEntry)];
};

struct VolDescriptor {
	char type;
	char cd001[6];
	char version;
	char sysid[32];
	char volid[32];
	char zeros[8];
	u32_le_be_pair numSectors;
	char morezeros[32];
	u16_le_be_pair volSetSize;
	u16_le_be_pair volSeqNum;
	u16_le_be_pair sectorSize;
	u32_le_be_pair pathTableLength;
	u16_le_be_pair firstLETableSector;
	u16_le_be_pair secondLETableSector;
	u16_le_be_pair firstBETableSector;
	u16_le_be_pair secondBETableSector;
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

ISOFileSystem::ISOFileSystem(IHandleAllocator *_hAlloc, BlockDevice *_blockDevice) {
	blockDevice = _blockDevice;
	hAlloc = _hAlloc;

	VolDescriptor desc;
	if (!blockDevice->ReadBlock(16, (u8*)&desc))
		blockDevice->NotifyReadError();

	entireISO.name.clear();
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
	treeroot->valid = false;

	if (memcmp(desc.cd001, "CD001", 5)) {
		ERROR_LOG(Log::FileSystem, "ISO looks bogus, expected CD001 signature not present? Giving up...");
		return;
	}

	treeroot->startsector = desc.root.firstDataSector;
	treeroot->dirsize = desc.root.dataLength;
}

ISOFileSystem::~ISOFileSystem() {
	delete blockDevice;
	delete treeroot;
}

std::string ISOFileSystem::TreeEntry::BuildPath() {
	if (parent) {
		return parent->BuildPath() + "/" + name;
	} else {
		return name;
	}
}

void ISOFileSystem::ReadDirectory(TreeEntry *root) {
	for (u32 secnum = root->startsector, endsector = root->startsector + (root->dirsize + 2047) / 2048; secnum < endsector; ++secnum) {
		u8 theSector[2048];
		if (!blockDevice->ReadBlock(secnum, theSector)) {
			blockDevice->NotifyReadError();
			ERROR_LOG(Log::FileSystem, "Error reading block for directory '%s' in sector %d - skipping", root->name.c_str(), secnum);
			root->valid = true;  // Prevents re-reading
			return;
		}
		lastReadBlock_ = secnum;  // Hm, this could affect timing... but lazy loading is probably more realistic.

		for (int offset = 0; offset < 2048; ) {
			DirectoryEntry &dir = *(DirectoryEntry *)&theSector[offset];
			u8 sz = theSector[offset];

			// Nothing left in this sector.  There might be more in the next one.
			if (sz == 0)
				break;

			const int IDENTIFIER_OFFSET = 33;
			if (offset + IDENTIFIER_OFFSET + dir.identifierLength > 2048) {
				blockDevice->NotifyReadError();
				ERROR_LOG(Log::FileSystem, "Directory entry crosses sectors, corrupt iso?");
				return;
			}

			offset += dir.size;

			bool isFile = (dir.flags & 2) ? false : true;
			bool relative;

			TreeEntry *entry = new TreeEntry();
			if (dir.identifierLength == 1 && (dir.firstIdChar == '\x00' || dir.firstIdChar == '.')) {
				entry->name = ".";
				relative = true;
			} else if (dir.identifierLength == 1 && dir.firstIdChar == '\x01') {
				entry->name = "..";
				relative = true;
			} else {
				entry->name = std::string((const char *)&dir.firstIdChar, dir.identifierLength);
				relative = false;
			}

			entry->size = dir.dataLength;
			entry->startingPosition = dir.firstDataSector * 2048;
			entry->isDirectory = !isFile;
			entry->flags = dir.flags;
			entry->parent = root;
			entry->startsector = dir.firstDataSector;
			entry->dirsize = dir.dataLength;
			entry->valid = isFile;  // Can pre-mark as valid if file, as we don't recurse into those.
			VERBOSE_LOG(Log::FileSystem, "%s: %s %08x %08x %d", entry->isDirectory ? "D" : "F", entry->name.c_str(), (u32)dir.firstDataSector, entry->startingPosition, entry->startingPosition);

			// Round down to avoid any false reports.
			if (isFile && dir.firstDataSector + (dir.dataLength / 2048) > blockDevice->GetNumBlocks()) {
				blockDevice->NotifyReadError();
				ERROR_LOG(Log::FileSystem, "File '%s' starts or ends outside ISO. firstDataSector: %d len: %d", entry->BuildPath().c_str(), (int)dir.firstDataSector, (int)dir.dataLength);
			}

			if (entry->isDirectory && !relative) {
				if (entry->startsector == root->startsector) {
					blockDevice->NotifyReadError();
					ERROR_LOG(Log::FileSystem, "WARNING: Appear to have a recursive file system, breaking recursion. Probably corrupt ISO.");
				}
			}
			root->children.push_back(entry);
		}
	}
	root->valid = true;
}

ISOFileSystem::TreeEntry *ISOFileSystem::GetFromPath(const std::string &path, bool catchError) {
	const size_t pathLength = path.length();

	if (pathLength == 0) {
		// Ah, the device!	"umd0:"
		return &entireISO;
	}

	size_t pathIndex = 0;

	// Skip "./"
	if (pathLength > pathIndex + 1 && path[pathIndex] == '.' && path[pathIndex + 1] == '/')
		pathIndex += 2;

	// Skip "/"
	if (pathLength > pathIndex && path[pathIndex] == '/')
		++pathIndex;

	if (pathLength <= pathIndex)
		return treeroot;

	TreeEntry *entry = treeroot;
	while (true) {
		if (!entry->valid) {
			ReadDirectory(entry);
		}
		TreeEntry *nextEntry = nullptr;
		std::string name = "";
		if (pathLength > pathIndex) {
			size_t nextSlashIndex = path.find_first_of('/', pathIndex);
			if (nextSlashIndex == std::string::npos)
				nextSlashIndex = pathLength;

			const std::string firstPathComponent = path.substr(pathIndex, nextSlashIndex - pathIndex);
			for (size_t i = 0; i < entry->children.size(); i++) {
				const std::string &n = entry->children[i]->name;
				if (firstPathComponent == n) {
					//yay we got it
					nextEntry = entry->children[i];
					name = n;
					break;
				}
			}
		}
		
		if (nextEntry) {
			entry = nextEntry;
			if (!entry->valid)
				ReadDirectory(entry);
			pathIndex += name.length();
			if (pathIndex < pathLength && path[pathIndex] == '/')
				++pathIndex;

			if (pathLength <= pathIndex)
				return entry;
		} else {
			if (catchError)
				ERROR_LOG(Log::FileSystem, "File '%s' not found", path.c_str());

			return 0;
		}
	}
}

int ISOFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	OpenFileEntry entry;
	entry.isRawSector = false;
	entry.isBlockSectorMode = false;

	if (access & FILEACCESS_WRITE) {
		ERROR_LOG(Log::FileSystem, "Can't open file '%s' with write access on an ISO partition", filename.c_str());
		return SCE_KERNEL_ERROR_ERRNO_INVALID_FLAG;
	}

	if (filename.compare(0, 8, "/sce_lbn") == 0) {
		// Raw sector read.
		u32 sectorStart = 0xFFFFFFFF, readSize = 0xFFFFFFFF;
		parseLBN(filename, &sectorStart, &readSize);
		if (sectorStart > blockDevice->GetNumBlocks()) {
			WARN_LOG(Log::FileSystem, "Unable to open raw sector, out of range: '%s', sector %08x, max %08x", filename.c_str(), sectorStart, blockDevice->GetNumBlocks());
			return SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
		}
		else if (sectorStart == blockDevice->GetNumBlocks())
		{
			ERROR_LOG(Log::FileSystem, "Should not be able to open the block after the last on disc! %08x", sectorStart);
		}

		DEBUG_LOG(Log::FileSystem, "Got a raw sector open: '%s', sector %08x, size %08x", filename.c_str(), sectorStart, readSize);
		u32 newHandle = hAlloc->GetNewHandle();
		entry.seekPos = 0;
		entry.file = 0;
		entry.isRawSector = true;
		entry.sectorStart = sectorStart;
		entry.openSize = readSize;
		// when open as "umd1:/sce_lbn0x0_size0x6B49D200", that mean open umd1 as a block device.
		// the param in sceIoLseek and sceIoRead is lba mode. we must mark it.
		if (strncmp(devicename, "umd0:", 5) == 0 || strncmp(devicename, "umd1:", 5) == 0)
			entry.isBlockSectorMode = true;

		entries[newHandle] = entry;
		return newHandle;
	}

	// May return entireISO for "umd0:".
	entry.file = GetFromPath(filename, false);
	if (!entry.file) {
		return SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	}

	if (entry.file == &entireISO)
		entry.isBlockSectorMode = true;

	entry.seekPos = 0;

	u32 newHandle = hAlloc->GetNewHandle();
	entries[newHandle] = entry;
	return newHandle;
}

void ISOFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		//CloseHandle((*iter).second.hFile);
		hAlloc->FreeHandle(handle);
		entries.erase(iter);
	} else {
		//This shouldn't happen...
		ERROR_LOG(Log::FileSystem, "Hey, what are you doing? Closing non-open files?");
	}
}

bool ISOFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

int ISOFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter == entries.end()) {
		ERROR_LOG(Log::FileSystem, "Ioctl on a bad file handle");
		return SCE_KERNEL_ERROR_BADF;
	}

	OpenFileEntry &e = iter->second;

	switch (cmd) {
	// Get ISO9660 volume descriptor (from open ISO9660 file.)
	case 0x01020001:
		if (e.isBlockSectorMode) {
			ERROR_LOG(Log::FileSystem, "Unsupported read volume descriptor command on a umd block device");
			return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
		}

		if (!Memory::IsValidRange(outdataPtr, 0x800) || outlen < 0x800) {
			WARN_LOG_REPORT(Log::FileSystem, "sceIoIoctl: Invalid out pointer %08x while reading ISO9660 volume descriptor", outdataPtr);
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}

		INFO_LOG(Log::sceIo, "sceIoIoctl: reading ISO9660 volume descriptor read");
		blockDevice->ReadBlock(16, Memory::GetPointerWriteUnchecked(outdataPtr));
		return 0;

	// Get ISO9660 path table (from open ISO9660 file.)
	case 0x01020002:
		if (e.isBlockSectorMode) {
			ERROR_LOG(Log::FileSystem, "Unsupported read path table command on a umd block device");
			return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
		}

		VolDescriptor desc;
		blockDevice->ReadBlock(16, (u8 *)&desc);
		if (outlen < (u32)desc.pathTableLength) {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		} else {
			int block = (u16)desc.firstLETableSector;
			u32 size = Memory::ValidSize(outdataPtr, (u32)desc.pathTableLength);
			u8 *out = Memory::GetPointerWriteRange(outdataPtr, size);

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

PSPDevType ISOFileSystem::DevType(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter == entries.end())
		return PSPDevType::FILE;
	PSPDevType type = iter->second.isBlockSectorMode ? PSPDevType::BLOCK : PSPDevType::FILE;
	if (iter->second.isRawSector)
		type |= PSPDevType::EMU_LBN;
	return type;
}

FileSystemFlags ISOFileSystem::Flags() {
	// TODO: Here may be a good place to force things, in case users recompress games
	// as PBP or CSO when they were originally the other type.
	return blockDevice->IsDisc() ? FileSystemFlags::UMD : FileSystemFlags::CARD;
}

size_t ISOFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size)
{
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t ISOFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		OpenFileEntry &e = iter->second;

		if (size < 0) {
			ERROR_LOG_REPORT(Log::FileSystem, "Invalid read for %lld bytes from umd %s", size, e.file ? e.file->name.c_str() : "device");
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
		} else if (e.file == nullptr) {
			ERROR_LOG(Log::FileSystem, "File no longer exists (loaded savestate with different ISO?)");
			return 0;
		} else {
			positionOnIso = e.file->startingPosition + e.seekPos;
			fileSize = e.file->size;
		}

		if ((s64)e.seekPos > fileSize) {
			WARN_LOG(Log::FileSystem, "Read starting outside of file, at %lld / %lld", (s64)e.seekPos, fileSize);
			return 0;
		}
		if ((s64)e.seekPos + size > fileSize) {
			// Clamp to the remaining size, but read what we can.
			const s64 newSize = fileSize - (s64)e.seekPos;
			// Reading beyond the file is really quite normal behavior (if return value handled correctly), so
			// not doing WARN here. Still, can potentially be useful to see so leaving at INFO.
			if (newSize == 0) {
				INFO_LOG(Log::FileSystem, "Attempted read at end of file, 0-size read simulated");
			} else {
				INFO_LOG(Log::FileSystem, "Reading beyond end of file from seekPos %d, clamping size %lld to %lld", e.seekPos, size, newSize);
			}
			size = newSize;
		}

		// Okay, we have size and position, let's rock.
		const int firstBlockOffset = positionOnIso & 2047;
		const int firstBlockSize = firstBlockOffset == 0 ? 0 : (int)std::min(size, 2048LL - firstBlockOffset);
		const int lastBlockSize = (size - firstBlockSize) & 2047;
		const s64 middleSize = size - firstBlockSize - lastBlockSize;
		u32 secNum = (u32)(positionOnIso / 2048);
		u8 theSector[2048];

		if ((middleSize & 2047) != 0) {
			ERROR_LOG(Log::FileSystem, "Remaining size should be aligned");
		}

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
		ERROR_LOG(Log::FileSystem, "Hey, what are you doing? Reading non-open files?");
		return 0;
	}
}

size_t ISOFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	ERROR_LOG(Log::FileSystem, "Hey, what are you doing? You can't write to an ISO!");
	return 0;
}

size_t ISOFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) {
	ERROR_LOG(Log::FileSystem, "Hey, what are you doing? You can't write to an ISO!");
	return 0;
}

size_t ISOFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
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
	} else {
		//This shouldn't happen...
		ERROR_LOG(Log::FileSystem, "Hey, what are you doing? Seeking in non-open files?");
		return 0;
	}
}

PSPFileInfo ISOFileSystem::GetFileInfo(std::string filename) {
	if (filename.compare(0,8,"/sce_lbn") == 0) {
		u32 sectorStart = 0xFFFFFFFF, readSize = 0xFFFFFFFF;
		parseLBN(filename, &sectorStart, &readSize);

		PSPFileInfo fileInfo;
		fileInfo.name = filename;
		fileInfo.exists = true;
		fileInfo.type = FILETYPE_NORMAL;
		fileInfo.size = readSize;
		fileInfo.access = 0444;
		fileInfo.startSector = sectorStart;
		fileInfo.isOnSectorSystem = true;
		fileInfo.numSectors = (readSize + sectorSize - 1) / sectorSize;
		return fileInfo;
	}

	TreeEntry *entry = GetFromPath(filename, false);
	PSPFileInfo x; 
	if (entry) {
		x.name = entry->name;
		// Strangely, it seems to be executable even for files.
		x.access = 0555;
		x.size = entry->size;
		x.exists = true;
		x.type = entry->isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
		x.isOnSectorSystem = true;
		x.startSector = entry->startingPosition / 2048;
	}
	return x;
}

std::vector<PSPFileInfo> ISOFileSystem::GetDirListing(const std::string &path, bool *exists) {
	std::vector<PSPFileInfo> myVector;
	TreeEntry *entry = GetFromPath(path);
	if (!entry) {
		if (exists)
			*exists = false;
		return myVector;
	}
	if (entry == &entireISO) {
		entry = GetFromPath("/");
	}

	const std::string dot(".");
	const std::string dotdot("..");

	for (size_t i = 0; i < entry->children.size(); i++) {
		TreeEntry *e = entry->children[i];

		// do not include the relative entries in the list
		if (e->name == dot || e->name == dotdot)
			continue;

		PSPFileInfo x;
		x.name = e->name;
		// Strangely, it seems to be executable even for files.
		x.access = 0555;
		x.exists = true;
		x.size = e->size;
		x.type = e->isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
		x.isOnSectorSystem = true;
		x.startSector = e->startingPosition/2048;
		x.sectorSize = sectorSize;
		x.numSectors = (u32)((e->size + sectorSize - 1) / sectorSize);
		myVector.push_back(x);
	}
	if (exists)
		*exists = true;
	return myVector;
}

std::string ISOFileSystem::EntryFullPath(TreeEntry *e) {
	if (e == &entireISO)
		return "";

	size_t fullLen = 0;
	TreeEntry *cur = e;
	while (cur != NULL && cur != treeroot) {
		// For the "/".
		fullLen += 1 + cur->name.size();
		cur = cur->parent;
	}

	std::string path;
	path.resize(fullLen);

	cur = e;
	while (cur != NULL && cur != treeroot) {
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

void ISOFileSystem::DoState(PointerWrap &p) {
	auto s = p.Section("ISOFileSystem", 1, 2);
	if (!s)
		return;

	int n = (int) entries.size();
	Do(p, n);

	if (p.mode == p.MODE_READ) {
		entries.clear();
		for (int i = 0; i < n; ++i) {
			u32 fd = 0;
			OpenFileEntry of;

			Do(p, fd);
			Do(p, of.seekPos);
			Do(p, of.isRawSector);
			Do(p, of.isBlockSectorMode);
			Do(p, of.sectorStart);
			Do(p, of.openSize);

			bool hasFile = false;
			Do(p, hasFile);
			if (hasFile) {
				std::string path;
				Do(p, path);
				of.file = GetFromPath(path);
			} else {
				of.file = NULL;
			}

			entries[fd] = of;
		}
	} else {
		for (EntryMap::iterator it = entries.begin(), end = entries.end(); it != end; ++it) {
			OpenFileEntry &of = it->second;
			Do(p, it->first);
			Do(p, of.seekPos);
			Do(p, of.isRawSector);
			Do(p, of.isBlockSectorMode);
			Do(p, of.sectorStart);
			Do(p, of.openSize);

			bool hasFile = of.file != NULL;
			Do(p, hasFile);
			if (hasFile) {
				std::string path = EntryFullPath(of.file);
				Do(p, path);
			}
		}
	}

	if (s >= 2) {
		Do(p, lastReadBlock_);
	} else {
		lastReadBlock_ = 0;
	}
}
