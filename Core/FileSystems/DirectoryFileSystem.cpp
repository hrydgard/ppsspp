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

#include "ChunkFile.h"
#include "FileUtil.h"
#include "DirectoryFileSystem.h"
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


#if HOST_IS_CASE_SENSITIVE

static bool FixFilenameCase(const std::string &path, std::string &filename)
{
	// Are we lucky?
	if (File::Exists(path + filename))
		return true;

	size_t filenameSize = filename.size();  // size in bytes, not characters
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

int DirectoryFileSystem::RenameFile(const std::string &from, const std::string &to) {
	std::string fullTo = to;

	// Rename ignores the path (even if specified) on to.
	size_t chop_at = to.find_last_of('/');
	if (chop_at != to.npos)
		fullTo = to.substr(chop_at + 1);

	// Now put it in the same directory as from.
	size_t dirname_end = from.find_last_of('/');
	if (dirname_end != from.npos)
		fullTo = from.substr(0, dirname_end + 1) + fullTo;

	// At this point, we should check if the paths match and give an already exists error.
	if (from == fullTo)
		return SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS;

	std::string fullFrom = GetLocalPath(from);

#if HOST_IS_CASE_SENSITIVE
	// In case TO should overwrite a file with different case
	if ( ! FixPathCase(fullTo, FPC_PATH_MUST_EXIST) )
		return -1;  // or go on and attempt (for a better error code than just false?)
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
			return -1;  // or go on and attempt (for a better error code than just false?)
		fullFrom = GetLocalPath(fullFrom);

#ifdef _WIN32
		retValue = (MoveFile(fullFrom.c_str(), fullToC) == TRUE);
#else
		retValue = (0 == rename(fullFrom.c_str(), fullToC));
#endif
	}
#endif

	// TODO: Better error codes.
	return retValue ? 0 : SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS;
}

bool DirectoryFileSystem::RemoveFile(const std::string &filename) {
	std::string fullName = GetLocalPath(filename);
#ifdef _WIN32
	bool retValue = (::DeleteFileA(fullName.c_str()) == TRUE);
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
		retValue = (::DeleteFileA(fullName.c_str()) == TRUE);
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
		struct stat s;
		stat(fullName.c_str(), &s);
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

#ifdef _WIN32
#define FILETIME_FROM_UNIX_EPOCH_US 11644473600000000ULL

void tmFromFiletime(tm &dest, FILETIME &src)
{
	u64 from_1601_us = (((u64) src.dwHighDateTime << 32ULL) + (u64) src.dwLowDateTime) / 10ULL;
	u64 from_1970_us = from_1601_us - FILETIME_FROM_UNIX_EPOCH_US;

	time_t t = (time_t) (from_1970_us / 1000000UL);
	localtime_r(&t, &dest);
}
#endif

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

		// TODO: Make this more correct?
		entry.access = entry.type == FILETYPE_NORMAL ? 0666 : 0777;
		// TODO: is this just for .. or all subdirectories? Need to add a directory to the test
		// to find out. Also why so different than the old test results?
		if (!strcmp(findData.cFileName, "..") )
			entry.size = 4096;
		else
			entry.size = findData.nFileSizeLow | ((u64)findData.nFileSizeHigh<<32);
		entry.name = findData.cFileName;
		tmFromFiletime(entry.atime, findData.ftLastAccessTime);
		tmFromFiletime(entry.ctime, findData.ftCreationTime);
		tmFromFiletime(entry.mtime, findData.ftLastWriteTime);
		myVector.push_back(entry);

		int retval = FindNextFile(hFind, &findData);
		if (!retval)
			break;
	}
#else
	dirent *dirp;
	std::string localPath = GetLocalPath(path);
	DIR *dp = opendir(localPath.c_str());

#if HOST_IS_CASE_SENSITIVE
	if(dp == NULL && FixPathCase(path, FPC_FILE_MUST_EXIST)) {
		// May have failed due to case sensitivity, try again
		localPath = GetLocalPath(path);
		dp = opendir(localPath.c_str());
	}
#endif

	if (dp == NULL) {
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
		p.SetError(p.ERROR_WARNING);
		ERROR_LOG(FILESYS, "FIXME: Open files during savestate, could go badly.");
	}
}


VFSFileSystem::VFSFileSystem(IHandleAllocator *_hAlloc, std::string _basePath) : basePath(_basePath) {
	INFO_LOG(HLE, "Creating VFS file system");
	hAlloc = _hAlloc;
}

VFSFileSystem::~VFSFileSystem() {
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		delete [] iter->second.fileData;
	}
	entries.clear();
}

std::string VFSFileSystem::GetLocalPath(std::string localPath) {
	return basePath + localPath;
}

bool VFSFileSystem::MkDir(const std::string &dirname) {
	// NOT SUPPORTED - READ ONLY
	return false;
}

bool VFSFileSystem::RmDir(const std::string &dirname) {
	// NOT SUPPORTED - READ ONLY
	return false;
}

int VFSFileSystem::RenameFile(const std::string &from, const std::string &to) {
	// NOT SUPPORTED - READ ONLY
	return -1;
}

bool VFSFileSystem::RemoveFile(const std::string &filename) {
	// NOT SUPPORTED - READ ONLY
	return false;
}

u32 VFSFileSystem::OpenFile(std::string filename, FileAccess access) {
	if (access != FILEACCESS_READ) {
		ERROR_LOG(HLE, "VFSFileSystem only supports plain reading");
		return 0;
	}

	std::string fullName = GetLocalPath(filename);
	const char *fullNameC = fullName.c_str();
	INFO_LOG(HLE,"VFSFileSystem actually opening %s (%s)", fullNameC, filename.c_str());

	size_t size;
	u8 *data = VFSReadFile(fullNameC, &size);
	if (!data) {
		ERROR_LOG(HLE, "VFSFileSystem failed to open %s", filename.c_str());
		return 0;
	}

	OpenFileEntry entry;
	entry.fileData = data;
	entry.size = size;
	entry.seekPos = 0;
	u32 newHandle = hAlloc->GetNewHandle();
	entries[newHandle] = entry;
	return newHandle;
}

PSPFileInfo VFSFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo x;
	x.name = filename;

	std::string fullName = GetLocalPath(filename);
	INFO_LOG(HLE,"Getting VFS file info %s (%s)", fullName.c_str(), filename.c_str());
	FileInfo fo;
	VFSGetFileInfo(fullName.c_str(), &fo);
	x.exists = fo.exists;
	if (x.exists) {
		x.size = fo.size;
		x.type = fo.isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
	}
	INFO_LOG(HLE,"Got VFS file info: size = %i", (int)x.size);
	return x;
}

void VFSFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		delete [] iter->second.fileData;
		entries.erase(iter);
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool VFSFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

size_t VFSFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	INFO_LOG(HLE,"VFSFileSystem::ReadFile %08x %p %i", handle, pointer, (u32)size);
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		size_t bytesRead = size;
		memcpy(pointer, iter->second.fileData + iter->second.seekPos, size);
		iter->second.seekPos += size;
		return bytesRead;
	} else {
		ERROR_LOG(HLE,"Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t VFSFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	// NOT SUPPORTED - READ ONLY
	return 0;
}

size_t VFSFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		switch (type) {
		case FILEMOVE_BEGIN:    iter->second.seekPos = position; break;
		case FILEMOVE_CURRENT:  iter->second.seekPos += position;  break;
		case FILEMOVE_END:      iter->second.seekPos = iter->second.size + position; break;
		}
		return iter->second.seekPos;
	} else {
		//This shouldn't happen...
		ERROR_LOG(HLE,"Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}


bool VFSFileSystem::GetHostPath(const std::string &inpath, std::string &outpath) {
	// NOT SUPPORTED
	return false;
}

std::vector<PSPFileInfo> VFSFileSystem::GetDirListing(std::string path) {
	std::vector<PSPFileInfo> myVector;
	// TODO
	return myVector;
}

void VFSFileSystem::DoState(PointerWrap &p) {
	if (!entries.empty()) {
		p.SetError(p.ERROR_WARNING);
		ERROR_LOG(FILESYS, "FIXME: Open files during savestate, could go badly.");
	}
}
