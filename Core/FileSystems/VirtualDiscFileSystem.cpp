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

#include "ppsspp_config.h"
#include <ctime>

#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/SysError.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/HLE/sceKernel.h"
#include "Core/Reporting.h"
#include "Common/Data/Encoding/Utf8.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <sys/stat.h>
#if PPSSPP_PLATFORM(UWP)
#include <fileapifromapp.h>
#endif
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#if !PPSSPP_PLATFORM(SWITCH)
#include <dlfcn.h>
#endif
#endif

const std::string INDEX_FILENAME = ".ppsspp-index.lst";

VirtualDiscFileSystem::VirtualDiscFileSystem(IHandleAllocator *_hAlloc, const Path &_basePath)
	: basePath(_basePath), currentBlockIndex(0) {
	hAlloc = _hAlloc;
	LoadFileListIndex();
}

VirtualDiscFileSystem::~VirtualDiscFileSystem() {
	for (auto iter = entries.begin(), end = entries.end(); iter != end; ++iter) {
		if (iter->second.type != VFILETYPE_ISO) {
			iter->second.Close();
		}
	}
	for (auto iter = handlers.begin(), end = handlers.end(); iter != end; ++iter) {
		delete iter->second;
	}
}

void VirtualDiscFileSystem::LoadFileListIndex() {
	const Path filename = basePath / INDEX_FILENAME;
	if (!File::Exists(filename)) {
		return;
	}

	FILE *f = File::OpenCFile(filename, "r");
	if (!f) {
		return;
	}

	static const int MAX_LINE_SIZE = 2048;
	char linebuf[MAX_LINE_SIZE]{};
	while (fgets(linebuf, MAX_LINE_SIZE, f)) {
		std::string line = linebuf;
		// Strip newline from fgets.
		if (!line.empty() && line.back() == '\n')
			line.resize(line.size() - 1);

		// Ignore any UTF-8 BOM.
		if (line.substr(0, 3) == "\xEF\xBB\xBF") {
			line = line.substr(3);
		}

		if (line.empty() || line[0] == ';') {
			continue;
		}

		FileListEntry entry = {""};

		// Syntax: HEXPOS filename or HEXPOS filename:handler
		size_t filename_pos = line.find(' ');
		if (filename_pos == line.npos) {
			ERROR_LOG(Log::FileSystem, "Unexpected line in %s: %s", INDEX_FILENAME.c_str(), line.c_str());
			continue;
		}

		filename_pos++;
		// Strip any slash prefix.
		while (filename_pos < line.length() && line[filename_pos] == '/') {
			filename_pos++;
		}

		// Check if there's a handler specified.
		size_t handler_pos = line.find(':', filename_pos);
		if (handler_pos != line.npos) {
			entry.fileName = line.substr(filename_pos, handler_pos - filename_pos);

			std::string handler = line.substr(handler_pos + 1);
			size_t trunc = handler.find_last_not_of("\r\n");
			if (trunc != handler.npos && trunc != handler.size())
				handler.resize(trunc + 1);

			if (handlers.find(handler) == handlers.end())
				handlers[handler] = new Handler(handler.c_str(), this);
			if (handlers[handler]->IsValid())
				entry.handler = handlers[handler];
		} else {
			entry.fileName = line.substr(filename_pos);
		}
		size_t trunc = entry.fileName.find_last_not_of("\r\n");
		if (trunc != entry.fileName.npos && trunc != entry.fileName.size())
			entry.fileName.resize(trunc + 1);

		entry.firstBlock = (u32)strtol(line.c_str(), NULL, 16);
		if (entry.handler != NULL && entry.handler->IsValid()) {
			HandlerFileHandle temp = entry.handler;
			if (temp.Open(basePath.ToString(), entry.fileName, FILEACCESS_READ)) {
				entry.totalSize = (u32)temp.Seek(0, FILEMOVE_END);
				temp.Close();
			} else {
				ERROR_LOG(Log::FileSystem, "Unable to open virtual file: %s", entry.fileName.c_str());
			}
		} else {
			entry.totalSize = File::GetFileSize(GetLocalPath(entry.fileName));
		}

		// Try to keep currentBlockIndex sane, in case there are other files.
		u32 nextBlock = entry.firstBlock + (entry.totalSize + 2047) / 2048;
		if (nextBlock > currentBlockIndex) {
			currentBlockIndex = nextBlock;
		}

		fileList.push_back(entry);
	}

	fclose(f);
}

void VirtualDiscFileSystem::DoState(PointerWrap &p)
{
	auto s = p.Section("VirtualDiscFileSystem", 1, 2);
	if (!s)
		return;

	int fileListSize = (int)fileList.size();
	int entryCount = (int)entries.size();

	Do(p, fileListSize);
	Do(p, entryCount);
	Do(p, currentBlockIndex);

	FileListEntry dummy = {""};
	fileList.resize(fileListSize, dummy);

	for (int i = 0; i < fileListSize; i++)
	{
		Do(p, fileList[i].fileName);
		Do(p, fileList[i].firstBlock);
		Do(p, fileList[i].totalSize);
	}

	if (p.mode == p.MODE_READ)
	{
		entries.clear();

		for (int i = 0; i < entryCount; i++)
		{
			u32 fd = 0;
			OpenFileEntry of(Flags());

			Do(p, fd);
			Do(p, of.fileIndex);
			Do(p, of.type);
			Do(p, of.curOffset);
			Do(p, of.startOffset);
			Do(p, of.size);

			// open file
			if (of.type != VFILETYPE_ISO) {
				if (fileList[of.fileIndex].handler != NULL) {
					of.handler = fileList[of.fileIndex].handler;
				}

				bool success = of.Open(basePath, fileList[of.fileIndex].fileName, FILEACCESS_READ);
				if (!success) {
					ERROR_LOG(Log::FileSystem, "Failed to create file handle for %s.", fileList[of.fileIndex].fileName.c_str());
				} else {
					if (of.type == VFILETYPE_LBN) {
						of.Seek(of.curOffset + of.startOffset, FILEMOVE_BEGIN);
					} else {
						of.Seek(of.curOffset, FILEMOVE_BEGIN);
					}
				}
			}

			// TODO: I think we only need to write to the map on load?
			entries[fd] = of;
		}
	} else {
		for (EntryMap::iterator it = entries.begin(), end = entries.end(); it != end; ++it)
		{
			OpenFileEntry &of = it->second;

			Do(p, it->first);
			Do(p, of.fileIndex);
			Do(p, of.type);
			Do(p, of.curOffset);
			Do(p, of.startOffset);
			Do(p, of.size);
		}
	}

	if (s >= 2) {
		Do(p, lastReadBlock_);
	} else {
		lastReadBlock_ = 0;
	}

	// We don't savestate handlers (loaded on fs load), but if they change, it may not load properly.
}

Path VirtualDiscFileSystem::GetLocalPath(std::string_view localpath) const {
	if (localpath.empty())
		return basePath;

	if (localpath[0] == '/')
		localpath.remove_prefix(1);
	return basePath / localpath;
}

int VirtualDiscFileSystem::getFileListIndex(std::string &fileName)
{
	std::string normalized;
	if (fileName.length() >= 1 && fileName[0] == '/') {
		normalized = fileName.substr(1);
	} else {
		normalized = fileName;
	}

	for (size_t i = 0; i < fileList.size(); i++)
	{
		if (fileList[i].fileName == normalized)
			return (int)i;
	}

	// unknown file - add it
	Path fullName = GetLocalPath(fileName);
	if (! File::Exists(fullName)) {
#if HOST_IS_CASE_SENSITIVE
		if (! FixPathCase(basePath, fileName, FPC_FILE_MUST_EXIST))
			return -1;
		fullName = GetLocalPath(fileName);

		if (! File::Exists(fullName))
			return -1;
#else
		return -1;
#endif
	}

	if (File::IsDirectory(fullName))
		return -1;

	FileListEntry entry = {""};
	entry.fileName = normalized;
	entry.totalSize = File::GetFileSize(fullName);
	entry.firstBlock = currentBlockIndex;
	currentBlockIndex += (entry.totalSize+2047)/2048;

	fileList.push_back(entry);

	return (int)fileList.size()-1;
}

int VirtualDiscFileSystem::getFileListIndex(u32 accessBlock, u32 accessSize, bool blockMode) const {
	for (size_t i = 0; i < fileList.size(); i++) {
		if (fileList[i].firstBlock <= accessBlock) {
			u32 sectorOffset = (accessBlock-fileList[i].firstBlock)*2048;
			u32 totalFileSize = blockMode ? (fileList[i].totalSize+2047) & ~2047 : fileList[i].totalSize;

			u32 endOffset = sectorOffset+accessSize;
			if (endOffset <= totalFileSize) {
				return (int)i;
			}
		}
	}

	return -1;
}

int VirtualDiscFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename)
{
	OpenFileEntry entry(Flags());
	entry.curOffset = 0;
	entry.size = 0;
	entry.startOffset = 0;

	if (filename.empty())
	{
		entry.type = VFILETYPE_ISO;
		entry.fileIndex = -1;

		u32 newHandle = hAlloc->GetNewHandle();
		entries[newHandle] = entry;

		return newHandle;
	}

	if (filename.compare(0, 8, "/sce_lbn") == 0)
	{
		u32 sectorStart = 0xFFFFFFFF, readSize = 0xFFFFFFFF;
		parseLBN(filename, &sectorStart, &readSize);

		entry.type = VFILETYPE_LBN;
		entry.size = readSize;

		int fileIndex = getFileListIndex(sectorStart,readSize);
		if (fileIndex == -1)
		{
			ERROR_LOG(Log::FileSystem, "VirtualDiscFileSystem: sce_lbn used without calling fileinfo.");
			return 0;
		}
		entry.fileIndex = (u32)fileIndex;

		entry.startOffset = (sectorStart-fileList[entry.fileIndex].firstBlock)*2048;

		// now we just need an actual file handle
		if (fileList[entry.fileIndex].handler != NULL) {
			entry.handler = fileList[entry.fileIndex].handler;
		}
		bool success = entry.Open(basePath, fileList[entry.fileIndex].fileName, FILEACCESS_READ);

		if (!success) {
			if (!(access & FILEACCESS_PPSSPP_QUIET)) {
#ifdef _WIN32
				ERROR_LOG(Log::FileSystem, "VirtualDiscFileSystem::OpenFile: FAILED, %i", (int)GetLastError());
#else
				ERROR_LOG(Log::FileSystem, "VirtualDiscFileSystem::OpenFile: FAILED");
#endif
			}
			return 0;
		}

		// seek to start
		entry.Seek(entry.startOffset, FILEMOVE_BEGIN);

		u32 newHandle = hAlloc->GetNewHandle();
		entries[newHandle] = entry;

		return newHandle;
	}

	entry.type = VFILETYPE_NORMAL;
	entry.fileIndex = getFileListIndex(filename);

	if (entry.fileIndex != (u32)-1 && fileList[entry.fileIndex].handler != NULL) {
		entry.handler = fileList[entry.fileIndex].handler;
	}
	bool success = entry.Open(basePath, filename, (FileAccess)(access & FILEACCESS_PSP_FLAGS));

	if (!success) {
		if (!(access & FILEACCESS_PPSSPP_QUIET)) {
#ifdef _WIN32
			ERROR_LOG(Log::FileSystem, "VirtualDiscFileSystem::OpenFile: FAILED, %i - access = %i", (int)GetLastError(), (int)access);
#else
			ERROR_LOG(Log::FileSystem, "VirtualDiscFileSystem::OpenFile: FAILED, access = %i", (int)access);
#endif
		}
		return SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	} else {
		u32 newHandle = hAlloc->GetNewHandle();
		entries[newHandle] = entry;

		return newHandle;
	}
}

size_t VirtualDiscFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		auto &entry = iter->second;
		switch (entry.type)
		{
		case VFILETYPE_NORMAL:
			{
				return entry.Seek(position, type);
			}
		case VFILETYPE_LBN:
			{
				switch (type)
				{
				case FILEMOVE_BEGIN:    entry.curOffset = position;                     break;
				case FILEMOVE_CURRENT:  entry.curOffset += position;                    break;
				case FILEMOVE_END:      entry.curOffset = entry.size + position;        break;
				}

				u32 off = entry.startOffset + entry.curOffset;
				entry.Seek(off, FILEMOVE_BEGIN);
				return entry.curOffset;
			}
		case VFILETYPE_ISO:
			{
				switch (type)
				{
				case FILEMOVE_BEGIN:    entry.curOffset = position;                     break;
				case FILEMOVE_CURRENT:  entry.curOffset += position;                    break;
				case FILEMOVE_END:      entry.curOffset = currentBlockIndex + position; break;
				}

				return entry.curOffset;
			}
		}
		return 0;
	} else {
		//This shouldn't happen...
		ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t VirtualDiscFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t VirtualDiscFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		if (size < 0) {
			ERROR_LOG_REPORT(Log::FileSystem, "Invalid read for %lld bytes from virtual umd", size);
			return 0;
		}

		// it's the whole iso... it could reference any of the files on the disc.
		// For now let's just open and close the files on demand. Can certainly be done
		// better though
		if (iter->second.type == VFILETYPE_ISO)
		{
			int fileIndex = getFileListIndex(iter->second.curOffset,size*2048,true);
			if (fileIndex == -1)
			{
				ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Reading from unknown address in %08x at %08llx", handle, iter->second.curOffset);
				return 0;
			}

			OpenFileEntry temp(Flags());
			if (fileList[fileIndex].handler != NULL) {
				temp.handler = fileList[fileIndex].handler;
			}
			bool success = temp.Open(basePath, fileList[fileIndex].fileName, FILEACCESS_READ);

			if (!success)
			{
				ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Error opening file %s", fileList[fileIndex].fileName.c_str());
				return 0;
			}

			u32 startOffset = (iter->second.curOffset-fileList[fileIndex].firstBlock)*2048;
			size_t bytesRead;

			temp.Seek(startOffset, FILEMOVE_BEGIN);

			u32 remainingSize = fileList[fileIndex].totalSize-startOffset;
			if (remainingSize < size * 2048)
			{
				// the file doesn't fill the whole last sector
				// read what's there and zero fill the rest like on a real disc
				bytesRead = temp.Read(pointer, remainingSize);
				memset(&pointer[bytesRead], 0, size * 2048 - bytesRead);
			} else {
				bytesRead = temp.Read(pointer, size * 2048);
			}

			temp.Close();

			iter->second.curOffset += size;
			// TODO: This probably isn't enough...
			if (abs((int)lastReadBlock_ - (int)iter->second.curOffset) > 100) {
				// This is an estimate, sometimes it takes 1+ seconds, but it definitely takes time.
				usec = 100000;
			}
			lastReadBlock_ = iter->second.curOffset;
			return size;
		}

		if (iter->second.type == VFILETYPE_LBN && iter->second.curOffset + size > iter->second.size) {
			// Clamp to the remaining size, but read what we can.
			const s64 newSize = iter->second.size - iter->second.curOffset;
			WARN_LOG(Log::FileSystem, "VirtualDiscFileSystem: Reading beyond end of file, clamping size %lld to %lld", size, newSize);
			size = newSize;
		}

		size_t bytesRead = iter->second.Read(pointer, size);
		iter->second.curOffset += bytesRead;
		return bytesRead;
	} else {
		//This shouldn't happen...
		ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

void VirtualDiscFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		hAlloc->FreeHandle(handle);
		iter->second.Close();
		entries.erase(iter);
	} else {
		//This shouldn't happen...
		ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool VirtualDiscFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

int VirtualDiscFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	// TODO: How to support these?
	return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
}

PSPDevType VirtualDiscFileSystem::DevType(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter == entries.end())
		return PSPDevType::FILE;
	PSPDevType type = iter->second.type == VFILETYPE_ISO ? PSPDevType::BLOCK : PSPDevType::FILE;
	if (iter->second.type == VFILETYPE_LBN)
		type |= PSPDevType::EMU_LBN;
	return type;
}

PSPFileInfo VirtualDiscFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo x;
	x.name = filename;
	x.access = FILEACCESS_READ;

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
		fileInfo.numSectors = (readSize + 2047) / 2048;
		return fileInfo;
	}

	int fileIndex = getFileListIndex(filename);
	if (fileIndex != -1 && fileList[fileIndex].handler != NULL) {
		x.type = FILETYPE_NORMAL;
		x.isOnSectorSystem = true;
		x.startSector = fileList[fileIndex].firstBlock;
		x.access = 0555;

		HandlerFileHandle temp = fileList[fileIndex].handler;
		if (temp.Open(basePath.ToString(), filename, FILEACCESS_READ)) {
			x.exists = true;
			x.size = temp.Seek(0, FILEMOVE_END);
			temp.Close();
		}

		// TODO: Probably should include dates or something...
		return x;
	}

	Path fullName = GetLocalPath(filename);
	if (!File::Exists(fullName)) {
#if HOST_IS_CASE_SENSITIVE
		if (! FixPathCase(basePath, filename, FPC_FILE_MUST_EXIST))
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
	x.access = 0555;
	if (fileIndex != -1) {
		x.isOnSectorSystem = true;
		x.startSector = fileList[fileIndex].firstBlock;
	}

	if (x.type != FILETYPE_DIRECTORY) {
		File::FileInfo details;
		if (!File::GetFileInfo(fullName, &details)) {
			ERROR_LOG(Log::FileSystem, "DirectoryFileSystem::GetFileInfo: GetFileInfo failed: %s", fullName.c_str());
			x.size = 0;
			x.access = 0;
		} else {
			x.size = details.size;
			time_t atime = details.atime;
			time_t ctime = details.ctime;
			time_t mtime = details.mtime;

			localtime_r((time_t*)&atime, &x.atime);
			localtime_r((time_t*)&ctime, &x.ctime);
			localtime_r((time_t*)&mtime, &x.mtime);
		}

		// x.startSector was set above in "if (fileIndex != -1)".
		x.numSectors = (x.size+2047)/2048;
	}

	return x;
}

PSPFileInfo VirtualDiscFileSystem::GetFileInfoByHandle(u32 handle) {
	WARN_LOG(Log::FileSystem, "GetFileInfoByHandle not yet implemented for VirtualDiscFileSystem");
	return PSPFileInfo();
}

#ifdef _WIN32
#define FILETIME_FROM_UNIX_EPOCH_US 11644473600000000ULL

static void tmFromFiletime(tm &dest, const FILETIME &src)
{
	u64 from_1601_us = (((u64) src.dwHighDateTime << 32ULL) + (u64) src.dwLowDateTime) / 10ULL;
	u64 from_1970_us = from_1601_us - FILETIME_FROM_UNIX_EPOCH_US;

	time_t t = (time_t) (from_1970_us / 1000000UL);
	localtime_s(&dest, &t);
}
#endif

std::vector<PSPFileInfo> VirtualDiscFileSystem::GetDirListing(const std::string &path, bool *exists) {
	std::vector<PSPFileInfo> myVector;

	// TODO(scoped): Switch this over to GetFilesInDir!

#ifdef _WIN32
	WIN32_FIND_DATA findData;
	HANDLE hFind;

	// TODO: Handler files that are virtual might not be listed.

	std::wstring w32path = GetLocalPath(path).ToWString() + L"\\*.*";

#if PPSSPP_PLATFORM(UWP)
	hFind = FindFirstFileExFromAppW(w32path.c_str(), FindExInfoStandard, &findData, FindExSearchNameMatch, NULL, 0);
#else
	hFind = FindFirstFileEx(w32path.c_str(), FindExInfoStandard, &findData, FindExSearchNameMatch, NULL, 0);
#endif
	if (hFind == INVALID_HANDLE_VALUE) {
		if (exists)
			*exists = false;
		return myVector; //the empty list
	}

	if (exists)
		*exists = true;

	for (BOOL retval = 1; retval; retval = FindNextFile(hFind, &findData)) {
		if (!wcscmp(findData.cFileName, L"..") || !wcscmp(findData.cFileName, L".")) {
			continue;
		}

		PSPFileInfo entry;
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			entry.type = FILETYPE_DIRECTORY;
		} else {
			entry.type = FILETYPE_NORMAL;
		}

		entry.access = 0555;
		entry.exists = true;
		entry.size = findData.nFileSizeLow | ((u64)findData.nFileSizeHigh<<32);
		entry.name = ConvertWStringToUTF8(findData.cFileName);
		tmFromFiletime(entry.atime, findData.ftLastAccessTime);
		tmFromFiletime(entry.ctime, findData.ftCreationTime);
		tmFromFiletime(entry.mtime, findData.ftLastWriteTime);
		entry.isOnSectorSystem = true;

		std::string fullRelativePath = path + "/" + entry.name;
		int fileIndex = getFileListIndex(fullRelativePath);
		if (fileIndex != -1)
			entry.startSector = fileList[fileIndex].firstBlock;
		myVector.push_back(entry);
	}
	FindClose(hFind);
#else
	dirent *dirp;
	Path localPath = GetLocalPath(path);
	DIR *dp = opendir(localPath.c_str());

#if HOST_IS_CASE_SENSITIVE
	std::string fixedPath = path;
	if(dp == NULL && FixPathCase(basePath, fixedPath, FPC_FILE_MUST_EXIST)) {
		// May have failed due to case sensitivity, try again
		localPath = GetLocalPath(fixedPath);
		dp = opendir(localPath.c_str());
	}
#endif

	if (dp == NULL) {
		ERROR_LOG(Log::FileSystem,"Error opening directory %s\n", path.c_str());
		if (exists)
			*exists = false;
		return myVector;
	}

	if (exists)
		*exists = true;

	while ((dirp = readdir(dp)) != NULL) {
		if (!strcmp(dirp->d_name, "..") || !strcmp(dirp->d_name, ".")) {
			continue;
		}

		PSPFileInfo entry;
		struct stat s;
		std::string fullName = (localPath / std::string(dirp->d_name)).ToString();
		stat(fullName.c_str(), &s);
		if (S_ISDIR(s.st_mode))
			entry.type = FILETYPE_DIRECTORY;
		else
			entry.type = FILETYPE_NORMAL;
		entry.access = 0555;
		entry.exists = true;
		entry.name = dirp->d_name;
		entry.size = s.st_size;
		localtime_r((time_t*)&s.st_atime,&entry.atime);
		localtime_r((time_t*)&s.st_ctime,&entry.ctime);
		localtime_r((time_t*)&s.st_mtime,&entry.mtime);
		entry.isOnSectorSystem = true;

		std::string fullRelativePath = path + "/" + entry.name;
		int fileIndex = getFileListIndex(fullRelativePath);
		if (fileIndex != -1)
			entry.startSector = fileList[fileIndex].firstBlock;
		myVector.push_back(entry);
	}
	closedir(dp);
#endif
	return myVector;
}

size_t VirtualDiscFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size)
{
	ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot write to file on virtual disc");
	return 0;
}

size_t VirtualDiscFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec)
{
	ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot write to file on virtual disc");
	return 0;
}

bool VirtualDiscFileSystem::MkDir(const std::string &dirname)
{
	ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot create directory on virtual disc");
	return false;
}

bool VirtualDiscFileSystem::RmDir(const std::string &dirname)
{
	ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot remove directory on virtual disc");
	return false;
}

int VirtualDiscFileSystem::RenameFile(const std::string &from, const std::string &to)
{
	ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot rename file on virtual disc");
	return -1;
}

bool VirtualDiscFileSystem::RemoveFile(const std::string &filename)
{
	ERROR_LOG(Log::FileSystem,"VirtualDiscFileSystem: Cannot remove file on virtual disc");
	return false;
}

void VirtualDiscFileSystem::HandlerLogger(void *arg, HandlerHandle handle, LogLevel level, const char *msg) {
	VirtualDiscFileSystem *sys = static_cast<VirtualDiscFileSystem *>(arg);

	// TODO: Probably could do this smarter / use a lookup.
	const char *filename = NULL;
	for (auto it = sys->entries.begin(), end = sys->entries.end(); it != end; ++it) {
		if (it->second.fileIndex != (u32)-1 && it->second.handler.handle == handle) {
			filename = sys->fileList[it->second.fileIndex].fileName.c_str();
			break;
		}
	}

	if (filename != NULL) {
		GENERIC_LOG(Log::FileSystem, level, "%s: %s", filename, msg);
	} else {
		GENERIC_LOG(Log::FileSystem, level, "%s", msg);
	}
}

VirtualDiscFileSystem::Handler::Handler(const char *filename, VirtualDiscFileSystem *const sys)
: sys_(sys) {
#if !PPSSPP_PLATFORM(SWITCH)
#ifdef _WIN32
#if PPSSPP_PLATFORM(UWP)
#define dlopen(name, ignore) (void *)LoadPackagedLibrary(ConvertUTF8ToWString(name).c_str(), 0)
#define dlsym(mod, name) GetProcAddress((HMODULE)mod, name)
#define dlclose(mod) FreeLibrary((HMODULE)mod)
#else
#define dlopen(name, ignore) (void *)LoadLibrary(ConvertUTF8ToWString(name).c_str())
#define dlsym(mod, name) GetProcAddress((HMODULE)mod, name)
#define dlclose(mod) FreeLibrary((HMODULE)mod)
#endif
#endif

	library = dlopen(filename, RTLD_LOCAL | RTLD_NOW);
	if (library != NULL) {
		Init = (InitFunc)dlsym(library, "Init");
		Shutdown = (ShutdownFunc)dlsym(library, "Shutdown");
		Open = (OpenFunc)dlsym(library, "Open");
		Seek = (SeekFunc)dlsym(library, "Seek");
		Read = (ReadFunc)dlsym(library, "Read");
		Close = (CloseFunc)dlsym(library, "Close");

		VersionFunc Version = (VersionFunc)dlsym(library, "Version");
		if (Version && Version() >= 2) {
			ShutdownV2 = (ShutdownV2Func)Shutdown;
		}

		if (!Init || !Shutdown || !Open || !Seek || !Read || !Close) {
			ERROR_LOG(Log::FileSystem, "Unable to find all handler functions: %s", filename);
			dlclose(library);
			library = NULL;
		} else if (!Init(&HandlerLogger, sys)) {
			ERROR_LOG(Log::FileSystem, "Unable to initialize handler: %s", filename);
			dlclose(library);
			library = NULL;
		}
	} else {
		ERROR_LOG(Log::FileSystem, "Unable to load handler '%s': %s", filename, GetLastErrorMsg().c_str());
	}
#ifdef _WIN32
#undef dlopen
#undef dlsym
#undef dlclose
#endif
#endif
}

VirtualDiscFileSystem::Handler::~Handler() {
	if (library != NULL) {
		if (ShutdownV2)
			ShutdownV2(sys_);
		else
			Shutdown();

#if !PPSSPP_PLATFORM(UWP) && !PPSSPP_PLATFORM(SWITCH)
#ifdef _WIN32
		FreeLibrary((HMODULE)library);
#else
		dlclose(library);
#endif
#endif
	}
}

