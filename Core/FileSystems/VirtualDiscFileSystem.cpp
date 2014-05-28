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
#include "Common/StringUtils.h"
#include "Common/ChunkFile.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/HLE/sceKernel.h"
#include "file/zip_read.h"
#include "util/text/utf8.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <sys/stat.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dlfcn.h>
#endif

const std::string INDEX_FILENAME = ".ppsspp-index.lst";

VirtualDiscFileSystem::VirtualDiscFileSystem(IHandleAllocator *_hAlloc, std::string _basePath)
	: basePath(_basePath),currentBlockIndex(0) {

#ifdef _WIN32
		if (!endsWith(basePath, "\\"))
			basePath = basePath + "\\";
#else
		if (!endsWith(basePath, "/"))
			basePath = basePath + "/";
#endif

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
	const std::string filename = basePath + INDEX_FILENAME;
	if (!File::Exists(filename)) {
		return;
	}

	std::ifstream in;
	in.open(filename.c_str(), std::ios::in);
	if (in.fail()) {
		return;
	}

	std::string buf;
	static const int MAX_LINE_SIZE = 1024;
	while (!in.eof()) {
		buf.resize(MAX_LINE_SIZE, '\0');
		in.getline(&buf[0], MAX_LINE_SIZE);

		std::string line = buf.data();

		// Ignore any UTF-8 BOM.
		if (line.substr(0, 3) == "\xEF\xBB\xBF") {
			line = line.substr(3);
		}

		if (strlen(line.data()) < 1 || line[0] == ';') {
			continue;
		}

		FileListEntry entry = {""};

		// Syntax: HEXPOS filename or HEXPOS filename:handler
		size_t filename_pos = line.find(' ');
		if (filename_pos == line.npos) {
			ERROR_LOG(FILESYS, "Unexpected line in %s: %s", INDEX_FILENAME.c_str(), line.c_str());
			continue;
		}

		// Check if there's a handler specified.
		size_t handler_pos = line.find(':', filename_pos);
		if (handler_pos != line.npos) {
			entry.fileName = line.substr(filename_pos + 1, handler_pos - filename_pos - 1);
			std::string handler = line.substr(handler_pos + 1);
			size_t trunc = handler.find_last_not_of("\r\n");
			if (trunc != handler.npos && trunc != handler.size())
				handler.resize(trunc + 1);
			if (handlers.find(handler) == handlers.end())
				handlers[handler] = new Handler(handler.c_str(), this);
			entry.handler = handlers[handler];
		} else {
			entry.fileName = line.substr(filename_pos + 1);
		}
		size_t trunc = entry.fileName.find_last_not_of("\r\n");
		if (trunc != entry.fileName.npos && trunc != entry.fileName.size())
			entry.fileName.resize(trunc + 1);

		entry.firstBlock = strtol(line.c_str(), NULL, 16);
		if (entry.handler != NULL && entry.handler->IsValid()) {
			HandlerFileHandle temp = entry.handler;
			if (temp.Open(basePath, entry.fileName, FILEACCESS_READ)) {
				entry.totalSize = (u32)temp.Seek(0, FILEMOVE_END);
				temp.Close();
			} else {
				ERROR_LOG(FILESYS, "Unable to open virtual file: %s", entry.fileName.c_str());
			}
		} else {
			entry.totalSize = File::GetSize(GetLocalPath(entry.fileName));
		}

		// Try to keep currentBlockIndex sane, in case there are other files.
		u32 nextBlock = entry.firstBlock + (entry.totalSize + 2047) / 2048;
		if (nextBlock > currentBlockIndex) {
			currentBlockIndex = nextBlock;
		}

		fileList.push_back(entry);
	}

	in.close();
}

void VirtualDiscFileSystem::DoState(PointerWrap &p)
{
	auto s = p.Section("VirtualDiscFileSystem", 1);
	if (!s)
		return;

	int fileListSize = (int)fileList.size();
	int entryCount = (int)entries.size();

	p.Do(fileListSize);
	p.Do(entryCount);
	p.Do(currentBlockIndex);

	FileListEntry dummy = {""};
	fileList.resize(fileListSize, dummy);

	for (int i = 0; i < fileListSize; i++)
	{
		p.Do(fileList[i].fileName);
		p.Do(fileList[i].firstBlock);
		p.Do(fileList[i].totalSize);
	}

	if (p.mode == p.MODE_READ)
	{
		entries.clear();

		for (int i = 0; i < entryCount; i++)
		{
			u32 fd = 0;
			OpenFileEntry of;

			p.Do(fd);
			p.Do(of.fileIndex);
			p.Do(of.type);
			p.Do(of.curOffset);
			p.Do(of.startOffset);
			p.Do(of.size);

			// open file
			if (of.type != VFILETYPE_ISO) {
				if (fileList[of.fileIndex].handler != NULL) {
					of.handler = fileList[of.fileIndex].handler;
				}

				bool success = of.Open(basePath, fileList[of.fileIndex].fileName, FILEACCESS_READ);
				if (!success) {
					ERROR_LOG(FILESYS, "Failed to create file handle for %s.", fileList[of.fileIndex].fileName.c_str());
				} else {
					if (of.type == VFILETYPE_LBN) {
						of.Seek(of.curOffset + of.startOffset, FILEMOVE_BEGIN);
					} else {
						of.Seek(of.curOffset, FILEMOVE_BEGIN);
					}
				}
			}

			entries[fd] = of;
		}
	} else {
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

	// We don't savestate handlers (loaded on fs load), but if they change, it may not load properly.
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

	FileListEntry entry = {""};
	entry.fileName = fileName;
	entry.totalSize = File::GetSize(fullName);
	entry.firstBlock = currentBlockIndex;
	currentBlockIndex += (entry.totalSize+2047)/2048;

	fileList.push_back(entry);

	return (int)fileList.size()-1;
}

int VirtualDiscFileSystem::getFileListIndex(u32 accessBlock, u32 accessSize, bool blockMode)
{
	for (size_t i = 0; i < fileList.size(); i++)
	{
		if (fileList[i].firstBlock <= accessBlock)
		{
			u32 sectorOffset = (accessBlock-fileList[i].firstBlock)*2048;
			u32 totalFileSize = blockMode ? (fileList[i].totalSize+2047) & ~2047 : fileList[i].totalSize;

			u32 endOffset = sectorOffset+accessSize;
			if (endOffset <= totalFileSize)
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
		if (fileList[entry.fileIndex].handler != NULL) {
			entry.handler = fileList[entry.fileIndex].handler;
		}
		bool success = entry.Open(basePath, fileList[entry.fileIndex].fileName, FILEACCESS_READ);

		if (!success) {
#ifdef _WIN32
			ERROR_LOG(FILESYS, "VirtualDiscFileSystem::OpenFile: FAILED, %i", GetLastError());
#else
			ERROR_LOG(FILESYS, "VirtualDiscFileSystem::OpenFile: FAILED");
#endif
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
	bool success = entry.Open(basePath, filename, access);

	if (!success) {
#ifdef _WIN32
		ERROR_LOG(FILESYS, "VirtualDiscFileSystem::OpenFile: FAILED, %i - access = %i", GetLastError(), (int)access);
#else
		ERROR_LOG(FILESYS, "VirtualDiscFileSystem::OpenFile: FAILED, access = %i", (int)access);
#endif
		//wwwwaaaaahh!!
		return 0;
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
		ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Cannot seek in file that hasn't been opened: %08x", handle);
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
			int fileIndex = getFileListIndex(iter->second.curOffset,size*2048,true);
			if (fileIndex == -1)
			{
				ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Reading from unknown address in %08x at %08llx", handle, iter->second.curOffset);
				return 0;
			}

			OpenFileEntry temp;
			if (fileList[fileIndex].handler != NULL) {
				temp.handler = fileList[fileIndex].handler;
			}
			bool success = temp.Open(basePath, fileList[fileIndex].fileName, FILEACCESS_READ);

			if (!success)
			{
				ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Error opening file %s", fileList[fileIndex].fileName.c_str());
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
			return size;
		}

		size_t bytesRead = iter->second.Read(pointer, size);
		iter->second.curOffset += bytesRead;
		return bytesRead;
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Cannot read file that hasn't been opened: %08x", handle);
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
		ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Cannot close file that hasn't been opened: %08x", handle);
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

int VirtualDiscFileSystem::DevType(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return iter->second.type == VFILETYPE_ISO ? PSP_DEV_TYPE_BLOCK : PSP_DEV_TYPE_FILE;
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
		fileInfo.size = readSize;
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

		HandlerFileHandle temp;
		if (temp.Open(basePath, filename, FILEACCESS_READ)) {
			x.exists = true;
			x.size = temp.Seek(0, FILEMOVE_END);
			temp.Close();
		}

		// TODO: Probably should include dates or something...
		return x;
	}

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
	if (fileIndex != -1) {
		x.isOnSectorSystem = true;
		x.startSector = fileList[fileIndex].firstBlock;
	}

	if (x.type != FILETYPE_DIRECTORY) {
		struct stat s;
		stat(fullName.c_str(), &s);

		x.size = File::GetSize(fullName);

		x.startSector = fileList[fileIndex].firstBlock;
		x.numSectors = (x.size+2047)/2048;

		localtime_r((time_t*)&s.st_atime,&x.atime);
		localtime_r((time_t*)&s.st_ctime,&x.ctime);
		localtime_r((time_t*)&s.st_mtime,&x.mtime);
	}

	return x;
}

bool VirtualDiscFileSystem::GetHostPath(const std::string &inpath, std::string &outpath)
{
	ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Retrieving host path");
	return false;
}

#ifdef _WIN32
#define FILETIME_FROM_UNIX_EPOCH_US 11644473600000000ULL

static void tmFromFiletime(tm &dest, FILETIME &src)
{
	u64 from_1601_us = (((u64) src.dwHighDateTime << 32ULL) + (u64) src.dwLowDateTime) / 10ULL;
	u64 from_1970_us = from_1601_us - FILETIME_FROM_UNIX_EPOCH_US;

	time_t t = (time_t) (from_1970_us / 1000000UL);
	localtime_r(&t, &dest);
}
#endif

std::vector<PSPFileInfo> VirtualDiscFileSystem::GetDirListing(std::string path)
{
	std::vector<PSPFileInfo> myVector;
#ifdef _WIN32
	WIN32_FIND_DATA findData;
	HANDLE hFind;

	// TODO: Handler files that are virtual might not be listed.

	std::string w32path = GetLocalPath(path) + "\\*.*";

	hFind = FindFirstFile(ConvertUTF8ToWString(w32path).c_str(), &findData);

	if (hFind == INVALID_HANDLE_VALUE) {
		return myVector; //the empty list
	}

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

		entry.access = FILEACCESS_READ;
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
	std::string localPath = GetLocalPath(path);
	DIR *dp = opendir(localPath.c_str());

#if HOST_IS_CASE_SENSITIVE
	if(dp == NULL && FixPathCase(basePath,path, FPC_FILE_MUST_EXIST)) {
		// May have failed due to case sensitivity, try again
		localPath = GetLocalPath(path);
		dp = opendir(localPath.c_str());
	}
#endif

	if (dp == NULL) {
		ERROR_LOG(FILESYS,"Error opening directory %s\n", path.c_str());
		return myVector;
	}

	while ((dirp = readdir(dp)) != NULL) {
		if (!strcmp(dirp->d_name, "..") || !strcmp(dirp->d_name, ".")) {
			continue;
		}

		PSPFileInfo entry;
		struct stat s;
		std::string fullName = GetLocalPath(path) + "/"+dirp->d_name;
		stat(fullName.c_str(), &s);
		if (S_ISDIR(s.st_mode))
			entry.type = FILETYPE_DIRECTORY;
		else
			entry.type = FILETYPE_NORMAL;
		entry.access = s.st_mode & 0x1FF;
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
	ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Cannot write to file on virtual disc");
	return 0;
}

bool VirtualDiscFileSystem::MkDir(const std::string &dirname)
{
	ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Cannot create directory on virtual disc");
	return false;
}

bool VirtualDiscFileSystem::RmDir(const std::string &dirname)
{
	ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Cannot remove directory on virtual disc");
	return false;
}

int VirtualDiscFileSystem::RenameFile(const std::string &from, const std::string &to)
{
	ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Cannot rename file on virtual disc");
	return -1;
}

bool VirtualDiscFileSystem::RemoveFile(const std::string &filename)
{
	ERROR_LOG(FILESYS,"VirtualDiscFileSystem: Cannot remove file on virtual disc");
	return false;
}

void VirtualDiscFileSystem::HandlerLogger(void *arg, HandlerHandle handle, LogTypes::LOG_LEVELS level, const char *msg) {
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
		GENERIC_LOG(LogTypes::FILESYS, level, "%s: %s", filename, msg);
	} else {
		GENERIC_LOG(LogTypes::FILESYS, level, "%s", msg);
	}
}

VirtualDiscFileSystem::Handler::Handler(const char *filename, VirtualDiscFileSystem *const sys) {
#ifdef _WIN32
#define dlopen(name, ignore) (void *)LoadLibrary(ConvertUTF8ToWString(name).c_str())
#define dlsym(mod, name) GetProcAddress((HMODULE)mod, name)
#define dlclose(mod) FreeLibrary((HMODULE)mod)
#endif

	library = dlopen(filename, RTLD_LOCAL | RTLD_NOW);
	if (library != NULL) {
		Init = (InitFunc)dlsym(library, "Init");
		Shutdown = (ShutdownFunc)dlsym(library, "Shutdown");
		Open = (OpenFunc)dlsym(library, "Open");
		Seek = (SeekFunc)dlsym(library, "Seek");
		Read = (ReadFunc)dlsym(library, "Read");
		Close = (CloseFunc)dlsym(library, "Close");

		if (Init == NULL || Shutdown == NULL || Open == NULL || Seek == NULL || Read == NULL || Close == NULL) {
			ERROR_LOG(FILESYS, "Unable to find all handler functions: %s", filename);
			dlclose(library);
			library = NULL;
		} else if (!Init(&HandlerLogger, sys)) {
			ERROR_LOG(FILESYS, "Unable to initialize handler: %s", filename);
			dlclose(library);
			library = NULL;
		}
	}
#ifdef _WIN32
#undef dlopen
#undef dlsym
#undef dlclose
#endif
}

VirtualDiscFileSystem::Handler::~Handler() {
	if (library != NULL) {
		Shutdown();

#ifdef _WIN32
		FreeLibrary((HMODULE)library);
#else
		dlclose(library);
#endif
	}
}
