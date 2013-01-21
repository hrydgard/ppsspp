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
#include <sys/stat.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#endif

#include "FileUtil.h"
#include "DirectoryFileSystem.h"


#if HOST_IS_CASE_SENSITIVE

static bool FixFilenameCase(const std::string &path, std::string &filename)
{
	// Are we lucky?
	if (File::Exists(path + filename))
		return true;

	size_t filenameSize = filename.size();
	for (size_t i = 0; i < filenameSize; i++)
	{
		filename[i] = tolower(filename[i]);
	}

	//TODO: lookup filename in cache for "path"

	struct dirent_large { struct dirent entry; char padding[FILENAME_MAX+1]; } diren;
	struct dirent_large;
	struct dirent *result = NULL;

	DIR *dirp = opendir(path.c_str());
	if (!dirp)
		return false;

	bool retValue = false;

	while (!readdir_r(dirp, (dirent*) &diren, &result) && result)
	{
		// Hm, is this check UTF-8 compatible? (size vs strlen)
		if (strlen(result->d_name) != filenameSize)
			continue;

		size_t i;
		for (i = 0; i < filenameSize; i++)
		{
			if (filename[i] != tolower(result->d_name[i]))
				break;
		}

		if (i < filenameSize)
			continue;

		filename = result->d_name;
		retValue = true;
	}

	closedir(dirp);

	return retValue;
}

bool DirectoryFileSystem::FixPathCase(std::string &path, FixPathCaseBehavior behavior)
{
	size_t len = path.size();

	if (len == 0)
		return true;

	if (path[len - 1] == '/')
	{
		len--;

		if (len == 0)
			return true;
	}

	std::string fullPath;
	fullPath.reserve(basePath.size() + len + 1);
	fullPath.append(basePath);

	size_t start = 0;
	while (start < len)
	{
		size_t i = path.find('/', start);
		if (i == std::string::npos)
			i = len;

		if (i > start)
		{
			std::string component = path.substr(start, i - start);

			// Fix case and stop on nonexistant path component
			if (FixFilenameCase(fullPath, component) == false) {
				// Still counts as success if partial matches allowed or if this
				// is the last component and only the ones before it are required
				return (behavior == FPC_PARTIAL_ALLOWED || (behavior == FPC_PATH_MUST_EXIST && i >= len));
			}

			path.replace(start, i - start, component);

			fullPath.append(component);
			fullPath.append(1, '/');
		}

		start = i + 1;
	}

	return true;
}

#endif

DirectoryFileSystem::DirectoryFileSystem(IHandleAllocator *_hAlloc, std::string _basePath) : basePath(_basePath) {
	File::CreateFullPath(basePath);
	hAlloc = _hAlloc;
}

DirectoryFileSystem::~DirectoryFileSystem() {
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
#ifdef _WIN32
		CloseHandle((*iter).second.hFile);
#else
		fclose((*iter).second.hFile);
#endif
	}
}

std::string DirectoryFileSystem::GetLocalPath(std::string localpath) {
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

bool DirectoryFileSystem::MkDir(const std::string &dirname) {

#if HOST_IS_CASE_SENSITIVE
	// Must fix case BEFORE attempting, because MkDir would create
	// duplicate (different case) directories

	std::string fixedCase = dirname;
	if ( ! FixPathCase(fixedCase, FPC_PARTIAL_ALLOWED) )
		return false;

	return File::CreateFullPath(GetLocalPath(fixedCase));
#else
	return File::CreateFullPath(GetLocalPath(dirname));
#endif
}

bool DirectoryFileSystem::RmDir(const std::string &dirname) {
	std::string fullName = GetLocalPath(dirname);

#if HOST_IS_CASE_SENSITIVE
	// Maybe we're lucky?
	if (File::DeleteDirRecursively(fullName))
		return true;

	// Nope, fix case and try again
	fullName = dirname;
	if ( ! FixPathCase(fullName, FPC_FILE_MUST_EXIST) )
		return false;  // or go on and attempt (for a better error code than just false?)

	fullName = GetLocalPath(fullName);
#endif

/*#ifdef _WIN32
	return RemoveDirectory(fullName.c_str()) == TRUE;
#else
	return 0 == rmdir(fullName.c_str());
#endif*/
	return File::DeleteDirRecursively(fullName);
}

bool DirectoryFileSystem::RenameFile(const std::string &from, const std::string &to) {
	std::string fullTo = to;

	// Rename only work for filename in current directory

	std::string fullFrom = GetLocalPath(from);

#if HOST_IS_CASE_SENSITIVE
	// In case TO should overwrite a file with different case
	if ( ! FixPathCase(fullTo, FPC_PATH_MUST_EXIST) )
		return false;  // or go on and attempt (for a better error code than just false?)
#endif

	fullTo = GetLocalPath(fullTo);
	const char * fullToC = fullTo.c_str();

#ifdef _WIN32
	bool retValue = (MoveFile(fullFrom.c_str(), fullToC) == TRUE);
#else
	bool retValue = (0 == rename(fullFrom.c_str(), fullToC));
#endif

#if HOST_IS_CASE_SENSITIVE
	if (! retValue)
	{
		// May have failed due to case sensitivity on FROM, so try again
		fullFrom = from;
		if ( ! FixPathCase(fullFrom, FPC_FILE_MUST_EXIST) )
			return false;  // or go on and attempt (for a better error code than just false?)
		fullFrom = GetLocalPath(fullFrom);

#ifdef _WIN32
		retValue = (MoveFile(fullFrom.c_str(), fullToC) == TRUE);
#else
		retValue = (0 == rename(fullFrom.c_str(), fullToC));
#endif
	}
#endif

	return retValue;
}

bool DirectoryFileSystem::DeleteFile(const std::string &filename) {
	std::string fullName = GetLocalPath(filename);
#ifdef _WIN32
	bool retValue = (::DeleteFile(fullName.c_str()) == TRUE);
#else
	bool retValue = (0 == unlink(fullName.c_str()));
#endif

#if HOST_IS_CASE_SENSITIVE
	if (! retValue)
	{
		// May have failed due to case sensitivity, so try again
		fullName = filename;
		if ( ! FixPathCase(fullName, FPC_FILE_MUST_EXIST) )
			return false;  // or go on and attempt (for a better error code than just false?)
		fullName = GetLocalPath(fullName);

#ifdef _WIN32
		retValue = (::DeleteFile(fullName.c_str()) == TRUE);
#else
		retValue = (0 == unlink(fullName.c_str()));
#endif
	}
#endif

	return retValue;
}

u32 DirectoryFileSystem::OpenFile(std::string filename, FileAccess access) {
#if HOST_IS_CASE_SENSITIVE
	if (access & (FILEACCESS_APPEND|FILEACCESS_CREATE|FILEACCESS_WRITE))
	{
		DEBUG_LOG(HLE, "Checking case for path %s", filename.c_str());
		if ( ! FixPathCase(filename, FPC_PATH_MUST_EXIST) )
			return 0;  // or go on and attempt (for a better error code than just 0?)
	}
	// else we try fopen first (in case we're lucky) before simulating case insensitivity
#endif

	std::string fullName = GetLocalPath(filename);
	const char *fullNameC  = fullName.c_str();
	INFO_LOG(HLE,"Actually opening %s (%s)", fullNameC, filename.c_str());

	OpenFileEntry entry;

	//TODO: tests, should append seek to end of file? seeking in a file opened for append?
#ifdef _WIN32
	// Convert parameters to Windows permissions and access
	DWORD desired = 0;
	DWORD sharemode = 0;
	DWORD openmode = 0;
	if (access & FILEACCESS_READ) {
		desired   |= GENERIC_READ;
		sharemode |= FILE_SHARE_READ;
	}
	if (access & FILEACCESS_WRITE) {
		desired   |= GENERIC_WRITE;
		sharemode |= FILE_SHARE_WRITE;
	}
	if (access & FILEACCESS_CREATE) {
		openmode = OPEN_ALWAYS;
	} else {
		openmode = OPEN_EXISTING;
	}
	//Let's do it!
	entry.hFile = CreateFile(fullNameC, desired, sharemode, 0, openmode, 0, 0);
	bool success = entry.hFile != INVALID_HANDLE_VALUE;
#else
	// Convert flags in access parameter to fopen access mode
	const char *mode = NULL;
	if (access & FILEACCESS_APPEND) {
		if (access & FILEACCESS_READ)
			mode = "ab+";  // append+read, create if needed
		else
			mode = "ab";  // append only, create if needed
	} else if (access & FILEACCESS_WRITE) {
		if (access & FILEACCESS_READ) {
			// FILEACCESS_CREATE is ignored for read only, write only, and append
			// because C++ standard fopen's nonexistant file creation can only be
			// customized for files opened read+write
			if (access & FILEACCESS_CREATE)
				mode = "wb+";  // read+write, create if needed
			else
				mode = "rb+";  // read+write, but don't create
		} else {
			mode = "wb";  // write only, create if needed
		}
	} else {  // neither write nor append, so default to read only
		mode = "rb";  // read only, don't create
	}

	entry.hFile = fopen(fullNameC, mode);
	bool success = entry.hFile != 0;
#endif

#if HOST_IS_CASE_SENSITIVE
	if (!success &&
	    !(access & FILEACCESS_APPEND) &&
	    !(access & FILEACCESS_CREATE) &&
	    !(access & FILEACCESS_WRITE))
	{
		if ( ! FixPathCase(filename, FPC_PATH_MUST_EXIST) )
			return 0;  // or go on and attempt (for a better error code than just 0?)
		fullName = GetLocalPath(filename);
		fullNameC = fullName.c_str();

		DEBUG_LOG(HLE, "Case may have been incorrect, second try opening %s (%s)", fullNameC, filename.c_str());

		// And try again with the correct case this time
#ifdef _WIN32
		entry.hFile = CreateFile(fullNameC, desired, sharemode, 0, openmode, 0, 0);
		success = entry.hFile != INVALID_HANDLE_VALUE;
#else
		entry.hFile = fopen(fullNameC, mode);
		success = entry.hFile != 0;
#endif
	}
#endif

	if (!success) {
#ifdef _WIN32
		ERROR_LOG(HLE, "DirectoryFileSystem::OpenFile: FAILED, %i - access = %i", GetLastError(), (int)access);
#else
		ERROR_LOG(HLE, "DirectoryFileSystem::OpenFile: FAILED, access = %i", (int)access);
#endif
		//wwwwaaaaahh!!
		return 0;
	} else {
#ifdef _WIN32
		if (access & FILEACCESS_APPEND)
			SetFilePointer(entry.hFile, 0, NULL, FILE_END);
#endif

		u32 newHandle = hAlloc->GetNewHandle();
		entries[newHandle] = entry;

		return newHandle;
	}
}

void DirectoryFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		hAlloc->FreeHandle(handle);
#ifdef _WIN32
		CloseHandle((*iter).second.hFile);
#else
		fclose((*iter).second.hFile);
#endif
		entries.erase(iter);
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool DirectoryFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

size_t DirectoryFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
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
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t DirectoryFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
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
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot write to file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t DirectoryFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
#ifdef _WIN32
		DWORD moveMethod = 0;
		switch (type) {
		case FILEMOVE_BEGIN:    moveMethod = FILE_BEGIN;    break;
		case FILEMOVE_CURRENT:  moveMethod = FILE_CURRENT;  break;
		case FILEMOVE_END:      moveMethod = FILE_END;      break;
		}
		DWORD newPos = SetFilePointer((*iter).second.hFile, (LONG)position, 0, moveMethod);
		return newPos;
#else
		int moveMethod = 0;
		switch (type) {
		case FILEMOVE_BEGIN:    moveMethod = SEEK_SET;  break;
		case FILEMOVE_CURRENT:  moveMethod = SEEK_CUR;  break;
		case FILEMOVE_END:      moveMethod = SEEK_END;  break;
		}
		fseek(iter->second.hFile, position, moveMethod);
		return ftell(iter->second.hFile);
#endif
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}

PSPFileInfo DirectoryFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo x;
	x.name = filename;

	std::string fullName = GetLocalPath(filename);
	if (! File::Exists(fullName)) {
#if HOST_IS_CASE_SENSITIVE
		if (! FixPathCase(filename, FPC_FILE_MUST_EXIST))
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
		struct stat64 s;
		stat64(fullName.c_str(), &s);
#ifdef _WIN32
		WIN32_FILE_ATTRIBUTE_DATA data;
		GetFileAttributesEx(fullName.c_str(), GetFileExInfoStandard, &data);

		x.size = data.nFileSizeLow | ((u64)data.nFileSizeHigh<<32);
#else
		x.size = s.st_size;
#endif
		x.access = s.st_mode & 0x1FF;
		localtime_r((time_t*)&s.st_atime,&x.atime);
		localtime_r((time_t*)&s.st_ctime,&x.ctime);
		localtime_r((time_t*)&s.st_mtime,&x.mtime);
	}

	return x;
}

bool DirectoryFileSystem::GetHostPath(const std::string &inpath, std::string &outpath) {
	outpath = GetLocalPath(inpath);
	return true;
}

std::vector<PSPFileInfo> DirectoryFileSystem::GetDirListing(std::string path) {
	std::vector<PSPFileInfo> myVector;
#ifdef _WIN32
	WIN32_FIND_DATA findData;
	HANDLE hFind;

	std::string w32path = GetLocalPath(path) + "\\*.*";

	hFind = FindFirstFile(w32path.c_str(), &findData);

	if (hFind == INVALID_HANDLE_VALUE) {
		return myVector; //the empty list
	}

	while (true) {
		PSPFileInfo entry;
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			entry.type = FILETYPE_DIRECTORY;
		else
			entry.type = FILETYPE_NORMAL;
		// TODO: is this just for .. or all subdirectories? Need to add a directory to the test
		// to find out. Also why so different than the old test results?
		if (!strcmp(findData.cFileName, "..") )
			entry.size = 4096;
		else
			entry.size = findData.nFileSizeLow | ((u64)findData.nFileSizeHigh<<32);
		entry.name = findData.cFileName;
		myVector.push_back(entry);

		int retval = FindNextFile(hFind, &findData);
		if (!retval)
			break;
	}
#else
	DIR *dp;
	dirent *dirp;
	if((dp  = opendir(GetLocalPath(path).c_str())) == NULL) {
		ERROR_LOG(HLE,"Error opening directory %s\n",path.c_str());
		return myVector;
	}

	while ((dirp = readdir(dp)) != NULL) {
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
		myVector.push_back(entry);
	}
	closedir(dp);
#endif
	return myVector;
}

void DirectoryFileSystem::DoState(PointerWrap &p) {
	if (!entries.empty()) {
		ERROR_LOG(FILESYS, "FIXME: Open files during savestate, could go badly.");
	}
}
