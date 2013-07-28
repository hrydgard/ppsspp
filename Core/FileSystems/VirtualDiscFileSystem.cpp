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

#include "Common/FileUtil.h"
#include "Common/ChunkFile.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/HLE/sceKernel.h"
#include "file/zip_read.h"

#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#endif

VirtualDiscFileSystem::VirtualDiscFileSystem(IHandleAllocator *_hAlloc, std::string _basePath)
	: basePath(_basePath),currentBlockIndex(0) {

#ifdef _WIN32
		if (basePath.size() > 0 && basePath[basePath.size()-1] != '\\')
			basePath = basePath + "\\";
#else
		if (basePath.size() > 0 && basePath[basePath.size()-1] != '/')
			basePath = basePath + "/";
#endif

	hAlloc = _hAlloc;
}

VirtualDiscFileSystem::~VirtualDiscFileSystem() {
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		if (iter->second.type != VFILETYPE_ISO)
			iter->second.hFile.Close();
	}
}

void VirtualDiscFileSystem::DoState(PointerWrap &p)
{
	int fileListSize = fileList.size();
	int entryCount = entries.size();

	p.Do(fileListSize);
	p.Do(entryCount);
	p.Do(currentBlockIndex);

	if (p.mode == p.MODE_READ)
	{
		fileList.clear();
		entries.clear();

		for (int i = 0; i < fileListSize; i++)
		{
			FileListEntry entry;
			p.Do(entry.fileName);
			p.Do(entry.firstBlock);
			p.Do(entry.totalSize);
			fileList.push_back(entry);
		}

		for (int i = 0; i < entryCount; i++)
		{
			u32 fd;
			OpenFileEntry of;

			p.Do(fd);
			p.Do(of.fileIndex);
			p.Do(of.type);
			p.Do(of.curOffset);
			p.Do(of.startOffset);
			p.Do(of.size);

			// open file
			if (of.type != VFILETYPE_ISO)
			{
				bool success = of.hFile.Open(basePath,fileList[of.fileIndex].fileName,FILEACCESS_READ);
				if (!success)
				{
					ERROR_LOG(FILESYS, "Failed to create file handle for %s.",fileList[of.fileIndex].fileName.c_str());
				} else {
					if (of.type == VFILETYPE_LBN)
					{
						of.hFile.Seek(of.curOffset+of.startOffset,FILEMOVE_BEGIN);
					} else {
						of.hFile.Seek(of.curOffset,FILEMOVE_BEGIN);
					}
				}
			}

			entries[fd] = of;
		}
	} else {
		for (int i = 0; i < fileListSize; i++)
		{
			p.Do(fileList[i].fileName);
			p.Do(fileList[i].firstBlock);
			p.Do(fileList[i].totalSize);
		}

		for (EntryMap::iterator it = entries.begin(), end = entries.end(); it != end; ++it)
		{
			OpenFileEntry &of = it->second;

			p.Do(it->first);
			p.Do(of.fileIndex);
			p.Do(of.type);
			p.Do(of.curOffset);
			p.Do(of.startOffset);
			p.Do(of.size);
		}
	}

	p.DoMarker("VirtualDiscFileSystem");
}

std::string VirtualDiscFileSystem::GetLocalPath(std::string localpath) {
	if (localpath.empty())
		return basePath;

	if (localpath[0] == '/')
		localpath.erase(0,1);
	//Convert slashes
#ifdef _WIN32
	for (size_t i = 0; i < localpath.size(); i++) {
		if (localpath[i] == '/')
			localpath[i] = '\\';
	}
#endif
	return basePath + localpath;
}

int VirtualDiscFileSystem::getFileListIndex(std::string& fileName)
{
	for (size_t i = 0; i < fileList.size(); i++)
	{
		if (fileList[i].fileName == fileName)
			return (int)i;
	}

	// unknown file - add it
	std::string fullName = GetLocalPath(fileName);
	if (! File::Exists(fullName)) {
#if HOST_IS_CASE_SENSITIVE
		if (! FixPathCase(basePath,fileName, FPC_FILE_MUST_EXIST))
			return -1;
		fullName = GetLocalPath(fileName);

		if (! File::Exists(fullName))
			return -1;
#else
		return -1;
#endif
	}

	FileType type = File::IsDirectory(fullName) ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
	if (type == FILETYPE_DIRECTORY)
		return -1;

	FileListEntry entry;
	entry.fileName = fileName;
	entry.totalSize = File::GetSize(fullName);
	entry.firstBlock = currentBlockIndex;
	currentBlockIndex += (entry.totalSize+2047)/2048;

	fileList.push_back(entry);

	return fileList.size()-1;
}

int VirtualDiscFileSystem::getFileListIndex(u32 accessBlock, u32 accessSize)
{
	for (size_t i = 0; i < fileList.size(); i++)
	{
		if (fileList[i].firstBlock <= accessBlock)
		{
			u32 sectorOffset = (accessBlock-fileList[i].firstBlock)*2048;
			u32 endOffset = sectorOffset+accessSize;
			if (endOffset <= fileList[i].totalSize)
			{
				return (int)i;
			}
		}
	}

	return -1;
}

u32 VirtualDiscFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename)
{
	OpenFileEntry entry;
	entry.curOffset = 0;
	entry.size = 0;
	entry.startOffset = 0;

	if (filename == "")
	{
		entry.type = VFILETYPE_ISO;
		entry.fileIndex = -1;

		u32 newHandle = hAlloc->GetNewHandle();
		entries[newHandle] = entry;

		return newHandle;
	}

	if (filename.compare(0,8,"/sce_lbn") == 0)
	{
		u32 sectorStart = 0xFFFFFFFF, readSize = 0xFFFFFFFF;
		parseLBN(filename, &sectorStart, &readSize);

		entry.type = VFILETYPE_LBN;
		entry.size = readSize;

		int fileIndex = getFileListIndex(sectorStart,readSize);
		if (fileIndex == -1)
		{
			ERROR_LOG(FILESYS, "VirtualDiscFileSystem: sce_lbn used without calling fileinfo.");
			return 0;
		}
		entry.fileIndex = (u32)fileIndex;

		entry.startOffset = (sectorStart-fileList[entry.fileIndex].firstBlock)*2048;

		// now we just need an actual file handle
		bool success = entry.hFile.Open(basePath,fileList[entry.fileIndex].fileName,FILEACCESS_READ);

		if (!success)
		{
#ifdef _WIN32
			ERROR_LOG(HLE, "VirtualDiscFileSystem::OpenFile: FAILED, %i", GetLastError());
#else
			ERROR_LOG(HLE, "VirtualDiscFileSystem::OpenFile: FAILED");
#endif
			return 0;
		}

		// seek to start
		entry.hFile.Seek(entry.startOffset,FILEMOVE_BEGIN);

		u32 newHandle = hAlloc->GetNewHandle();
		entries[newHandle] = entry;

		return newHandle;
	}

	entry.type = VFILETYPE_NORMAL;
	bool success = entry.hFile.Open(basePath,filename,access);

	if (!success) {
#ifdef _WIN32
		ERROR_LOG(HLE, "VirtualDiscFileSystem::OpenFile: FAILED, %i - access = %i", GetLastError(), (int)access);
#else
		ERROR_LOG(HLE, "VirtualDiscFileSystem::OpenFile: FAILED, access = %i", (int)access);
#endif
		//wwwwaaaaahh!!
		return 0;
	} else {
		entry.fileIndex = getFileListIndex(filename);

		u32 newHandle = hAlloc->GetNewHandle();
		entries[newHandle] = entry;

		return newHandle;
	}
}

size_t VirtualDiscFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		switch (iter->second.type)
		{
		case VFILETYPE_NORMAL:
			{
				return iter->second.hFile.Seek(position,type);
			}
		case VFILETYPE_LBN:
			{
				switch (type)
				{
				case FILEMOVE_BEGIN:    iter->second.curOffset = position;    break;
				case FILEMOVE_CURRENT:  iter->second.curOffset += position;  break;
				case FILEMOVE_END:      iter->second.curOffset = iter->second.size-position;      break;
				}

				u32 off = iter->second.startOffset+iter->second.curOffset;
				iter->second.hFile.Seek(off,FILEMOVE_BEGIN);
				return iter->second.curOffset;
			}
		case VFILETYPE_ISO:
			{
				switch (type)
				{
				case FILEMOVE_BEGIN:    iter->second.curOffset = position;    break;
				case FILEMOVE_CURRENT:  iter->second.curOffset += position;  break;
				case FILEMOVE_END:      return -1;	// unsupported
				}

				return iter->second.curOffset;
			}
		}
		return 0;
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"VirtualDiscFileSystem: Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t VirtualDiscFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		// it's the whole iso... it could reference any of the files on the disc.
		// For now let's just open and close the files on demand. Can certainly be done
		// better though
		if (iter->second.type == VFILETYPE_ISO)
		{
			int fileIndex = getFileListIndex(iter->second.curOffset,size*2048);
			if (fileIndex == -1)
			{
				ERROR_LOG(HLE,"VirtualDiscFileSystem: Reading from unknown address %08x", handle);
				return 0;
			}

			DirectoryFileHandle hFile;
			bool success = hFile.Open(basePath,fileList[fileIndex].fileName,FILEACCESS_READ);

			if (!success)
			{
				ERROR_LOG(HLE,"VirtualDiscFileSystem: Error opening file %s", fileList[fileIndex].fileName.c_str());
				return 0;
			}

			u32 startOffset = (iter->second.curOffset-fileList[fileIndex].firstBlock)*2048;
			size_t bytesRead;

			hFile.Seek(startOffset,FILEMOVE_BEGIN);
			bytesRead = hFile.Read(pointer,size*2048);
			hFile.Close();

			return bytesRead;
		}

		size_t bytesRead = iter->second.hFile.Read(pointer,size);
		iter->second.curOffset += bytesRead;
		return bytesRead;
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"VirtualDiscFileSystem: Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

void VirtualDiscFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		hAlloc->FreeHandle(handle);
		iter->second.hFile.Close();
		entries.erase(iter);
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"VirtualDiscFileSystem: Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool VirtualDiscFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

PSPFileInfo VirtualDiscFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo x;
	x.name = filename;

	std::string fullName = GetLocalPath(filename);
	if (! File::Exists(fullName)) {
#if HOST_IS_CASE_SENSITIVE
		if (! FixPathCase(basePath,filename, FPC_FILE_MUST_EXIST))
			return x;
		fullName = GetLocalPath(filename);

		if (! File::Exists(fullName))
			return x;
#else
		return x;
#endif
	}

	x.type = File::IsDirectory(fullName) ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
	x.exists = true;

	if (x.type != FILETYPE_DIRECTORY)
	{
		struct stat s;
		stat(fullName.c_str(), &s);

		x.size = File::GetSize(fullName);

		int fileIndex = getFileListIndex(filename);
		x.startSector = fileList[fileIndex].firstBlock;
		x.numSectors = (x.size+2047)/2048;

		x.access = s.st_mode & 0x1FF;
		localtime_r((time_t*)&s.st_atime,&x.atime);
		localtime_r((time_t*)&s.st_ctime,&x.ctime);
		localtime_r((time_t*)&s.st_mtime,&x.mtime);
	}

	return x;
}

bool VirtualDiscFileSystem::GetHostPath(const std::string &inpath, std::string &outpath)
{
	ERROR_LOG(HLE,"VirtualDiscFileSystem: Retrieving host path");
	return false;
}

std::vector<PSPFileInfo> VirtualDiscFileSystem::GetDirListing(std::string path)
{
	// todo
	std::vector<PSPFileInfo> result;
	return result;
}

size_t VirtualDiscFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size)
{
	ERROR_LOG(HLE,"VirtualDiscFileSystem: Cannot write to file on virtual disc");
	return 0;
}

bool VirtualDiscFileSystem::MkDir(const std::string &dirname)
{
	ERROR_LOG(HLE,"VirtualDiscFileSystem: Cannot create directory on virtual disc");
	return false;
}

bool VirtualDiscFileSystem::RmDir(const std::string &dirname)
{
	ERROR_LOG(HLE,"VirtualDiscFileSystem: Cannot remove directory on virtual disc");
	return false;
}

int VirtualDiscFileSystem::RenameFile(const std::string &from, const std::string &to)
{
	ERROR_LOG(HLE,"VirtualDiscFileSystem: Cannot rename file on virtual disc");
	return -1;
}

bool VirtualDiscFileSystem::RemoveFile(const std::string &filename)
{
	ERROR_LOG(HLE,"VirtualDiscFileSystem: Cannot remove file on virtual disc");
	return false;
}
