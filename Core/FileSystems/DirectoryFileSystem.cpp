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

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include "FileUtil.h"
#include "DirectoryFileSystem.h"

DirectoryFileSystem::DirectoryFileSystem(IHandleAllocator *_hAlloc, std::string _basePath) : basePath(_basePath)
{
	File::CreateFullPath(basePath);
	hAlloc = _hAlloc;
}

std::string DirectoryFileSystem::GetLocalPath(std::string localpath)
{
	if (localpath.empty())
		return basePath;

	if (localpath[0] == '/')
		localpath.erase(0,1);
  //Convert slashes
#ifdef _WIN32
	for (size_t i = 0; i < localpath.size(); i++)
	{
		if (localpath[i] == '/')
			localpath[i] = '\\';
	}
#endif
	return basePath + localpath;
}


bool DirectoryFileSystem::MkDir(const std::string &dirname)
{
	std::string fullName = GetLocalPath(dirname);

	return File::CreateFullPath(fullName);

}

bool DirectoryFileSystem::RmDir(const std::string &dirname)
{
	std::string fullName = GetLocalPath(dirname);
#ifdef _WIN32
	return RemoveDirectory(fullName.c_str()) == TRUE;
#else
	return 0 == rmdir(fullName.c_str());
#endif
}

bool DirectoryFileSystem::RenameFile(const std::string &from, const std::string &to)
{
	std::string fullFrom = GetLocalPath(from);
	std::string fullTo = to;
	// TO filename may not include path. Intention is that it uses FROM's path
	if (to.find("/") != std::string::npos) {
		int offset = from.find_last_of("/");
		if (offset >= 0) {
			fullTo = from.substr(0, offset + 1) + to;
		}
	}
	fullTo = GetLocalPath(fullTo);
#ifdef _WIN32
	return MoveFile(fullFrom.c_str(), fullTo.c_str()) == TRUE;
#else
	return 0 == rename(fullFrom.c_str(), fullTo.c_str());
#endif
}

bool DirectoryFileSystem::DeleteFile(const std::string &filename)
{
	std::string fullName = GetLocalPath(filename);
#ifdef _WIN32
	return DeleteFile(fullName.c_str()) == TRUE;
#else
	return 0 == unlink(fullName.c_str());
#endif
}

u32 DirectoryFileSystem::OpenFile(std::string filename, FileAccess access)
{
	std::string fullName = GetLocalPath(filename);
	INFO_LOG(HLE,"Actually opening %s (%s)", fullName.c_str(), filename.c_str());

	OpenFileEntry entry;

#ifdef _WIN32
	// Convert parameters to Windows permissions and access
	DWORD desired = 0;
	DWORD sharemode = 0;
	DWORD openmode = 0;
	if (access & FILEACCESS_READ)
	{
		desired   |= GENERIC_READ;
		sharemode |= FILE_SHARE_READ;
	}
	if (access & FILEACCESS_WRITE)
	{
		desired   |= GENERIC_WRITE;
		sharemode |= FILE_SHARE_WRITE;
	}
	if (access & FILEACCESS_CREATE)
	{
		openmode = OPEN_ALWAYS;
	}
	else
		openmode = OPEN_EXISTING;

	//Let's do it!
	entry.hFile = CreateFile(fullName.c_str(), desired, sharemode, 0, openmode, 0, 0);
	bool success = entry.hFile != INVALID_HANDLE_VALUE;
#else
  entry.hFile = fopen(fullName.c_str(), access & FILEACCESS_WRITE ? "wb" : "rb");
  bool success = entry.hFile != 0;
#endif

	if (!success)
	{
#ifdef _WIN32
    ERROR_LOG(HLE, "DirectoryFileSystem::OpenFile: FAILED, %i", GetLastError());
#endif
		//wwwwaaaaahh!!
		return 0;
	}
	else
	{
		u32 newHandle = hAlloc->GetNewHandle();
		entries[newHandle] = entry;

		return newHandle;
	}
}

void DirectoryFileSystem::CloseFile(u32 handle)
{
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		hAlloc->FreeHandle(handle);
#ifdef _WIN32
		CloseHandle((*iter).second.hFile);
#else
    fclose((*iter).second.hFile);
#endif
		entries.erase(iter);
	}
	else
	{
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool DirectoryFileSystem::OwnsHandle(u32 handle)
{
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

size_t DirectoryFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size)
{
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
    size_t bytesRead;
#ifdef _WIN32
		::ReadFile(iter->second.hFile, (LPVOID)pointer, (DWORD)size, (LPDWORD)&bytesRead, 0);
#else
    bytesRead = fread(pointer, 1, size, iter->second.hFile);
#endif
		return bytesRead;
	}
	else
	{
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t DirectoryFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) 
{
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		size_t bytesWritten;
#ifdef _WIN32
		::WriteFile(iter->second.hFile, (LPVOID)pointer, (DWORD)size, (LPDWORD)&bytesWritten, 0);
#else
    bytesWritten = fwrite(pointer, 1, size, iter->second.hFile);
#endif
		return bytesWritten;
	}
	else
	{
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot write to file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t DirectoryFileSystem::SeekFile(u32 handle, s32 position, FileMove type) 
{
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
#ifdef _WIN32
		DWORD moveMethod = 0;
		switch (type)
		{
		case FILEMOVE_BEGIN: moveMethod = FILE_BEGIN; break;
		case FILEMOVE_CURRENT: moveMethod = FILE_CURRENT; break;
		case FILEMOVE_END: moveMethod = FILE_END; break;
		}
		DWORD newPos = SetFilePointer((*iter).second.hFile, (LONG)position, 0, moveMethod);
    return newPos;
#else
    int moveMethod = 0;
    switch (type) {
    case FILEMOVE_BEGIN: moveMethod = SEEK_SET; break;
    case FILEMOVE_CURRENT: moveMethod = SEEK_CUR; break;
    case FILEMOVE_END: moveMethod = SEEK_END; break;
    }
    fseek(iter->second.hFile, position, moveMethod);
		return ftell(iter->second.hFile);
#endif
	}
	else
	{
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}

PSPFileInfo DirectoryFileSystem::GetFileInfo(std::string filename) 
{
	PSPFileInfo x; 
	x.name = filename;
	

	std::string fullName = GetLocalPath(filename);
	if (!File::Exists(fullName)) {
		return x;
	}
	x.type = File::IsDirectory(fullName) ? FILETYPE_NORMAL : FILETYPE_DIRECTORY;
	x.exists = true;

#ifdef _WIN32

	WIN32_FILE_ATTRIBUTE_DATA data;
	GetFileAttributesEx(fullName.c_str(), GetFileExInfoStandard, &data);

	x.size = data.nFileSizeLow | ((u64)data.nFileSizeHigh<<32);
#else
	x.size = File::GetSize(fullName);
	//TODO
#endif

	return x;
}

std::vector<PSPFileInfo> DirectoryFileSystem::GetDirListing(std::string path)
{
	std::vector<PSPFileInfo> myVector;
#ifdef _WIN32
	WIN32_FIND_DATA findData;
	HANDLE hFind;

	std::string w32path = GetLocalPath(path) + "\\*.*";

	hFind = FindFirstFile(w32path.c_str(), &findData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		return myVector; //the empty list
	}

	while (true)
	{
		PSPFileInfo entry;

		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			entry.type = FILETYPE_DIRECTORY;
		else
			entry.type = FILETYPE_NORMAL;

		if (!strcmp(findData.cFileName, "..") )// TODO: is this just for .. or all sub directories? Need to add a directory to the test to find out. Also why so different than the old test results?
			entry.size = 4096;
		else
			entry.size = findData.nFileSizeLow | ((u64)findData.nFileSizeHigh<<32);
		entry.name = findData.cFileName;
		
		myVector.push_back(entry);

		int retval = FindNextFile(hFind, &findData);
		if (!retval)
			break;
	}
#endif
	return myVector;
}

