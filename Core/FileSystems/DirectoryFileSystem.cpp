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

#include <algorithm>
#include <limits>
#include "file/free.h"
#include "file/zip_read.h"
#include "i18n/i18n.h"
#include "util/text/utf8.h"
#include "Common/ChunkFile.h"
#include "Common/FileUtil.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HW/MemoryStick.h"
#include "Core/CoreTiming.h"
#include "Core/Host.h"
#include "Core/Replay.h"
#include "Core/Reporting.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <sys/stat.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#if defined(__ANDROID__)
#include <sys/types.h>
#include <sys/vfs.h>
#define statvfs statfs
#else
#include <sys/statvfs.h>
#endif
#include <ctype.h>
#include <fcntl.h>
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

bool FixPathCase(std::string& basePath, std::string &path, FixPathCaseBehavior behavior)
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

DirectoryFileSystem::DirectoryFileSystem(IHandleAllocator *_hAlloc, std::string _basePath, int _flags) : basePath(_basePath), flags(_flags) {
	File::CreateFullPath(basePath);
	hAlloc = _hAlloc;
}

std::string DirectoryFileHandle::GetLocalPath(std::string& basePath, std::string localpath)
{
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

bool DirectoryFileHandle::Open(std::string &basePath, std::string &fileName, FileAccess access, u32 &error)
{
	error = 0;

#if HOST_IS_CASE_SENSITIVE
	if (access & (FILEACCESS_APPEND|FILEACCESS_CREATE|FILEACCESS_WRITE)) {
		DEBUG_LOG(FILESYS, "Checking case for path %s", fileName.c_str());
		if (!FixPathCase(basePath, fileName, FPC_PATH_MUST_EXIST)) {
			error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
			return false;  // or go on and attempt (for a better error code than just 0?)
		}
	}
	// else we try fopen first (in case we're lucky) before simulating case insensitivity
#endif

	std::string fullName = GetLocalPath(basePath,fileName);
	VERBOSE_LOG(FILESYS,"Actually opening %s", fullName.c_str());

	// On the PSP, truncating doesn't lose data.  If you seek later, you'll recover it.
	// This is abnormal, so we deviate from the PSP's behavior and truncate on write/close.
	// This means it's incorrectly not truncated before the write.
	if (access & FILEACCESS_TRUNCATE) {
		needsTrunc_ = 0;
	}

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
		if (access & FILEACCESS_EXCL) {
			openmode = CREATE_NEW;
		} else {
			openmode = OPEN_ALWAYS;
		}
	} else {
		openmode = OPEN_EXISTING;
	}

	// Let's do it!
#if PPSSPP_PLATFORM(UWP)
	hFile = CreateFile2(ConvertUTF8ToWString(fullName).c_str(), desired, sharemode, openmode, nullptr);
#else
	hFile = CreateFile(ConvertUTF8ToWString(fullName).c_str(), desired, sharemode, 0, openmode, 0, 0);
#endif
	bool success = hFile != INVALID_HANDLE_VALUE;
	if (!success) {
		DWORD w32err = GetLastError();

		if (w32err == ERROR_SHARING_VIOLATION) {
			// Sometimes, the file is locked for write, let's try again.
			sharemode |= FILE_SHARE_WRITE;
#if PPSSPP_PLATFORM(UWP)
			hFile = CreateFile2(ConvertUTF8ToWString(fullName).c_str(), desired, sharemode, openmode, nullptr);
#else
			hFile = CreateFile(ConvertUTF8ToWString(fullName).c_str(), desired, sharemode, 0, openmode, 0, 0);
#endif
			success = hFile != INVALID_HANDLE_VALUE;
			if (!success) {
				w32err = GetLastError();
			}
		}

		if (w32err == ERROR_DISK_FULL || w32err == ERROR_NOT_ENOUGH_QUOTA) {
			// This is returned when the disk is full.
			I18NCategory *err = GetI18NCategory("Error");
			host->NotifyUserMessage(err->T("Disk full while writing data"));
			error = SCE_KERNEL_ERROR_ERRNO_NO_PERM;
		} else if (!success) {
			error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
		}
	}
#else
	int flags = 0;
	if (access & FILEACCESS_APPEND) {
		flags |= O_APPEND;
	}
	if ((access & FILEACCESS_READ) && (access & FILEACCESS_WRITE)) {
		flags |= O_RDWR;
	} else if (access & FILEACCESS_READ) {
		flags |= O_RDONLY;
	} else if (access & FILEACCESS_WRITE) {
		flags |= O_WRONLY;
	}
	if (access & FILEACCESS_CREATE) {
		flags |= O_CREAT;
	}
	if (access & FILEACCESS_EXCL) {
		flags |= O_EXCL;
	}

	hFile = open(fullName.c_str(), flags, 0666);
	bool success = hFile != -1;
#endif

#if HOST_IS_CASE_SENSITIVE
	if (!success && !(access & FILEACCESS_CREATE)) {
		if (!FixPathCase(basePath,fileName, FPC_PATH_MUST_EXIST) )
			return 0;  // or go on and attempt (for a better error code than just 0?)
		fullName = GetLocalPath(basePath,fileName); 
		const char *fullNameC = fullName.c_str();

		DEBUG_LOG(FILESYS, "Case may have been incorrect, second try opening %s (%s)", fullNameC, fileName.c_str());

		// And try again with the correct case this time
#ifdef _WIN32
		hFile = CreateFile(fullNameC, desired, sharemode, 0, openmode, 0, 0);
		success = hFile != INVALID_HANDLE_VALUE;
#else
		hFile = open(fullNameC, flags, 0666);
		success = hFile != -1;
#endif
	}
#endif

#ifndef _WIN32
	if (success) {
		// Reject directories, even if we succeed in opening them.
		// TODO: Might want to do this stat first...
		struct stat st;
		if (fstat(hFile, &st) == 0 && S_ISDIR(st.st_mode)) {
			close(hFile);
			errno = EISDIR;
			success = false;
		}
	} else if (errno == ENOSPC) {
		// This is returned when the disk is full.
		I18NCategory *err = GetI18NCategory("Error");
		host->NotifyUserMessage(err->T("Disk full while writing data"));
		error = SCE_KERNEL_ERROR_ERRNO_NO_PERM;
	} else {
		error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	}
#endif

	return success;
}

size_t DirectoryFileHandle::Read(u8* pointer, s64 size)
{
	size_t bytesRead = 0;
	if (needsTrunc_ != -1) {
		// If the file was marked to be truncated, pretend there's nothing.
		// On a PSP. it actually is truncated, but the data wasn't erased.
		off_t off = (off_t)Seek(0, FILEMOVE_CURRENT);
		if (needsTrunc_ <= off) {
			return replay_ ? ReplayApplyDiskRead(pointer, 0, (uint32_t)size, CoreTiming::GetGlobalTimeUs()) : 0;
		}
		if (needsTrunc_ < off + size) {
			size = needsTrunc_ - off;
		}
	}
#ifdef _WIN32
	::ReadFile(hFile, (LPVOID)pointer, (DWORD)size, (LPDWORD)&bytesRead, 0);
#else
	bytesRead = read(hFile, pointer, size);
#endif
	return replay_ ? ReplayApplyDiskRead(pointer, (uint32_t)bytesRead, (uint32_t)size, CoreTiming::GetGlobalTimeUs()) : bytesRead;
}

size_t DirectoryFileHandle::Write(const u8* pointer, s64 size)
{
	size_t bytesWritten = 0;
	bool diskFull = false;

#ifdef _WIN32
	BOOL success = ::WriteFile(hFile, (LPVOID)pointer, (DWORD)size, (LPDWORD)&bytesWritten, 0);
	if (success == FALSE) {
		DWORD err = GetLastError();
		diskFull = err == ERROR_DISK_FULL || err == ERROR_NOT_ENOUGH_QUOTA;
	}
#else
	bytesWritten = write(hFile, pointer, size);
	if (bytesWritten == (size_t)-1) {
		diskFull = errno == ENOSPC;
	}
#endif
	if (needsTrunc_ != -1) {
		off_t off = (off_t)Seek(0, FILEMOVE_CURRENT);
		if (needsTrunc_ < off) {
			needsTrunc_ = off;
		}
	}

	if (diskFull) {
		ERROR_LOG(FILESYS, "Disk full");
		I18NCategory *err = GetI18NCategory("Error");
		host->NotifyUserMessage(err->T("Disk full while writing data"));
		// We only return an error when the disk is actually full.
		// When writing this would cause the disk to be full, so it wasn't written, we return 0.
		if (MemoryStick_FreeSpace() == 0) {
			// Sign extend on 64-bit.
			return (size_t)(s64)(s32)SCE_KERNEL_ERROR_ERRNO_DEVICE_NO_FREE_SPACE;
		}
	}

	return bytesWritten;
}

size_t DirectoryFileHandle::Seek(s32 position, FileMove type)
{
	if (needsTrunc_ != -1) {
		// If the file is "currently truncated" move to the end based on that position.
		// The actual, underlying file hasn't been truncated (yet.)
		if (type == FILEMOVE_END) {
			type = FILEMOVE_BEGIN;
			position = needsTrunc_ + position;
		}
	}

	size_t result;
#ifdef _WIN32
	DWORD moveMethod = 0;
	switch (type) {
	case FILEMOVE_BEGIN:    moveMethod = FILE_BEGIN;    break;
	case FILEMOVE_CURRENT:  moveMethod = FILE_CURRENT;  break;
	case FILEMOVE_END:      moveMethod = FILE_END;      break;
	}

	LARGE_INTEGER distance;
	distance.QuadPart = position;
	LARGE_INTEGER cursor;
	DWORD newPos = SetFilePointerEx(hFile, distance, &cursor, moveMethod);
	result = (size_t)cursor.QuadPart;
#else
	int moveMethod = 0;
	switch (type) {
	case FILEMOVE_BEGIN:    moveMethod = SEEK_SET;  break;
	case FILEMOVE_CURRENT:  moveMethod = SEEK_CUR;  break;
	case FILEMOVE_END:      moveMethod = SEEK_END;  break;
	}
	result = lseek(hFile, position, moveMethod);
#endif

	return replay_ ? (size_t)ReplayApplyDisk64(ReplayAction::FILE_SEEK, result, CoreTiming::GetGlobalTimeUs()) : result;
}

void DirectoryFileHandle::Close()
{
	if (needsTrunc_ != -1) {
#ifdef _WIN32
		Seek((s32)needsTrunc_, FILEMOVE_BEGIN);
		if (SetEndOfFile(hFile) == 0) {
			ERROR_LOG_REPORT(FILESYS, "Failed to truncate file.");
		}
#else
		if (ftruncate(hFile, (off_t)needsTrunc_) != 0) {
			ERROR_LOG_REPORT(FILESYS, "Failed to truncate file.");
		}
#endif
	}
#ifdef _WIN32
	if (hFile != (HANDLE)-1)
		CloseHandle(hFile);
#else
	if (hFile != -1)
		close(hFile);
#endif
}

void DirectoryFileSystem::CloseAll() {
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		iter->second.hFile.Close();
	}

	entries.clear();
}

DirectoryFileSystem::~DirectoryFileSystem() {
	CloseAll();
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
	bool result;
#if HOST_IS_CASE_SENSITIVE
	// Must fix case BEFORE attempting, because MkDir would create
	// duplicate (different case) directories

	std::string fixedCase = dirname;
	if (!FixPathCase(basePath,fixedCase, FPC_PARTIAL_ALLOWED))
		result = false;
	else
		result = File::CreateFullPath(GetLocalPath(fixedCase));
#else
	result = File::CreateFullPath(GetLocalPath(dirname));
#endif
	return ReplayApplyDisk(ReplayAction::MKDIR, result, CoreTiming::GetGlobalTimeUs()) != 0;
}

bool DirectoryFileSystem::RmDir(const std::string &dirname) {
	std::string fullName = GetLocalPath(dirname);

#if HOST_IS_CASE_SENSITIVE
	// Maybe we're lucky?
	if (File::DeleteDirRecursively(fullName))
		return (bool)ReplayApplyDisk(ReplayAction::RMDIR, true, CoreTiming::GetGlobalTimeUs());

	// Nope, fix case and try again.  Should we try again?
	fullName = dirname;
	if (!FixPathCase(basePath,fullName, FPC_FILE_MUST_EXIST))
		return (bool)ReplayApplyDisk(ReplayAction::RMDIR, false, CoreTiming::GetGlobalTimeUs());

	fullName = GetLocalPath(fullName);
#endif

/*#ifdef _WIN32
	return RemoveDirectory(fullName.c_str()) == TRUE;
#else
	return 0 == rmdir(fullName.c_str());
#endif*/
	bool result = File::DeleteDirRecursively(fullName);
	return ReplayApplyDisk(ReplayAction::RMDIR, result, CoreTiming::GetGlobalTimeUs()) != 0;
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
		return ReplayApplyDisk(ReplayAction::FILE_RENAME, SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS, CoreTiming::GetGlobalTimeUs());

	std::string fullFrom = GetLocalPath(from);

#if HOST_IS_CASE_SENSITIVE
	// In case TO should overwrite a file with different case.  Check error code?
	if (!FixPathCase(basePath,fullTo, FPC_PATH_MUST_EXIST))
		return ReplayApplyDisk(ReplayAction::FILE_RENAME, -1, CoreTiming::GetGlobalTimeUs());
#endif

	fullTo = GetLocalPath(fullTo);
	const char * fullToC = fullTo.c_str();

#ifdef _WIN32
	bool retValue = (MoveFileEx(ConvertUTF8ToWString(fullFrom).c_str(), ConvertUTF8ToWString(fullToC).c_str(), 0) == TRUE);
#else
	bool retValue = (0 == rename(fullFrom.c_str(), fullToC));
#endif

#if HOST_IS_CASE_SENSITIVE
	if (! retValue)
	{
		// May have failed due to case sensitivity on FROM, so try again.  Check error code?
		fullFrom = from;
		if (!FixPathCase(basePath,fullFrom, FPC_FILE_MUST_EXIST))
			return ReplayApplyDisk(ReplayAction::FILE_RENAME, -1, CoreTiming::GetGlobalTimeUs());
		fullFrom = GetLocalPath(fullFrom);

#ifdef _WIN32
		retValue = (MoveFile(fullFrom.c_str(), fullToC) == TRUE);
#else
		retValue = (0 == rename(fullFrom.c_str(), fullToC));
#endif
	}
#endif

	// TODO: Better error codes.
	int result = retValue ? 0 : (int)SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS;
	return ReplayApplyDisk(ReplayAction::FILE_RENAME, result, CoreTiming::GetGlobalTimeUs());
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
		// May have failed due to case sensitivity, so try again.  Try even if it fails?
		fullName = filename;
		if (!FixPathCase(basePath,fullName, FPC_FILE_MUST_EXIST))
			return (bool)ReplayApplyDisk(ReplayAction::FILE_REMOVE, false, CoreTiming::GetGlobalTimeUs());
		fullName = GetLocalPath(fullName);

#ifdef _WIN32
		retValue = (::DeleteFileA(fullName.c_str()) == TRUE);
#else
		retValue = (0 == unlink(fullName.c_str()));
#endif
	}
#endif

	return ReplayApplyDisk(ReplayAction::FILE_REMOVE, retValue, CoreTiming::GetGlobalTimeUs()) != 0;
}

u32 DirectoryFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	OpenFileEntry entry;
	u32 err = 0;
	bool success = entry.hFile.Open(basePath, filename, access, err);
	if (err == 0 && !success) {
		err = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	}

	err = ReplayApplyDisk(ReplayAction::FILE_OPEN, err, CoreTiming::GetGlobalTimeUs());
	if (err != 0) {
#ifdef _WIN32
		ERROR_LOG(FILESYS, "DirectoryFileSystem::OpenFile: FAILED, %i - access = %i", GetLastError(), (int)access);
#else
		ERROR_LOG(FILESYS, "DirectoryFileSystem::OpenFile: FAILED, %i - access = %i", errno, (int)access);
#endif
		//wwwwaaaaahh!!
		return err;
	} else {
#ifdef _WIN32
		if (access & FILEACCESS_APPEND)
			entry.hFile.Seek(0,FILEMOVE_END);
#endif

		u32 newHandle = hAlloc->GetNewHandle();

		entry.guestFilename = filename;
		entry.access = access;

		entries[newHandle] = entry;

		return newHandle;
	}
}

void DirectoryFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		hAlloc->FreeHandle(handle);
		iter->second.hFile.Close();
		entries.erase(iter);
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS,"Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool DirectoryFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

int DirectoryFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
}

int DirectoryFileSystem::DevType(u32 handle) {
	return PSP_DEV_TYPE_FILE;
}

size_t DirectoryFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t DirectoryFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		if (size < 0) {
			ERROR_LOG_REPORT(FILESYS, "Invalid read for %lld bytes from disk %s", size, iter->second.guestFilename.c_str());
			return 0;
		}

		size_t bytesRead = iter->second.hFile.Read(pointer,size);
		return bytesRead;
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS,"Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t DirectoryFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	int ignored;
	return WriteFile(handle, pointer, size, ignored);
}

size_t DirectoryFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		size_t bytesWritten = iter->second.hFile.Write(pointer,size);
		return bytesWritten;
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS,"Cannot write to file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t DirectoryFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		return iter->second.hFile.Seek(position,type);
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS,"Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}

PSPFileInfo DirectoryFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo x;
	x.name = filename;

	std::string fullName = GetLocalPath(filename);
	if (!File::Exists(fullName)) {
#if HOST_IS_CASE_SENSITIVE
		if (! FixPathCase(basePath,filename, FPC_FILE_MUST_EXIST))
			return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
		fullName = GetLocalPath(filename);

		if (! File::Exists(fullName))
			return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
#else
		return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
#endif
	}
	x.type = File::IsDirectory(fullName) ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
	x.exists = true;

	if (x.type != FILETYPE_DIRECTORY) {
		File::FileDetails details;
		if (!File::GetFileDetails(fullName, &details)) {
			ERROR_LOG(FILESYS, "DirectoryFileSystem::GetFileInfo: GetFileDetails failed: %s", fullName.c_str());
			x.size = 0;
			x.access = 0;
			memset(&x.atime, 0, sizeof(x.atime));
			memset(&x.ctime, 0, sizeof(x.ctime));
			memset(&x.mtime, 0, sizeof(x.mtime));
		} else {
			x.size = details.size;
			x.access = details.access;
			time_t atime = details.atime;
			time_t ctime = details.ctime;
			time_t mtime = details.mtime;

			localtime_r((time_t*)&atime, &x.atime);
			localtime_r((time_t*)&ctime, &x.ctime);
			localtime_r((time_t*)&mtime, &x.mtime);
		}
	}

	return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
}

bool DirectoryFileSystem::GetHostPath(const std::string &inpath, std::string &outpath) {
	outpath = GetLocalPath(inpath);
	return true;
}

#ifdef _WIN32
#define FILETIME_FROM_UNIX_EPOCH_US 11644473600000000ULL

static void tmFromFiletime(tm &dest, FILETIME &src) {
	u64 from_1601_us = (((u64) src.dwHighDateTime << 32ULL) + (u64) src.dwLowDateTime) / 10ULL;
	u64 from_1970_us = from_1601_us - FILETIME_FROM_UNIX_EPOCH_US;

	time_t t = (time_t) (from_1970_us / 1000000UL);
	localtime_r(&t, &dest);
}
#endif

// This simulates a bug in the PSP VFAT driver.
//
// Windows NT VFAT optimizes valid DOS filenames that are in lowercase.
// The PSP VFAT driver doesn't support this optimization, and behaves like Windows 98.
// Some homebrew depends on this bug in the PSP firmware.
//
// This essentially tries to simulate the "Windows 98 world view" on modern operating systems.
// Essentially all lowercase files are seen as UPPERCASE.
//
// Note: PSP-created files would stay lowercase, but this uppercases them too.
// Hopefully no PSP games read directories after they create files in them...
static std::string SimulateVFATBug(std::string filename) {
	// These are the characters allowed in DOS filenames.
	static const char *FAT_UPPER_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&'(){}-_`~";
	static const char *FAT_LOWER_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789!@#$%^&'(){}-_`~";

	bool apply_hack = false;
	size_t dot_pos = filename.find('.');
	if (dot_pos == filename.npos) {
		size_t badchar = filename.find_first_not_of(FAT_LOWER_CHARS);
		if (badchar == filename.npos) {
			// It's all lowercase.  Convert to upper.
			apply_hack = true;
		}
	} else {
		// There's a separate flag for each, so we compare separately.
		// But they both have to either be all upper or lowercase.
		std::string base = filename.substr(0, dot_pos);
		std::string ext = filename.substr(dot_pos + 1);

		// The filename must be short enough to fit.
		if (base.length() <= 8 && ext.length() <= 3) {
			size_t base_non_lower = base.find_first_not_of(FAT_LOWER_CHARS);
			size_t base_non_upper = base.find_first_not_of(FAT_UPPER_CHARS);
			size_t ext_non_lower = ext.find_first_not_of(FAT_LOWER_CHARS);
			size_t ext_non_upper = ext.find_first_not_of(FAT_UPPER_CHARS);

			// As long as neither is mixed, we apply the hack.
			bool base_apply_hack = base_non_lower == base.npos || base_non_upper == base.npos;
			bool ext_apply_hack = ext_non_lower == ext.npos || ext_non_upper == ext.npos;
			apply_hack = base_apply_hack && ext_apply_hack;
		}
	}

	if (apply_hack) {
		// In this situation, NT would write UPPERCASE, and just set a flag to say "actually lowercase".
		// That VFAT flag isn't read by the PSP firmware, so let's pretend to "not read it."
		std::transform(filename.begin(), filename.end(), filename.begin(), toupper);
	}

	return filename;
}

std::vector<PSPFileInfo> DirectoryFileSystem::GetDirListing(std::string path) {
	std::vector<PSPFileInfo> myVector;
#ifdef _WIN32
	WIN32_FIND_DATA findData;
	HANDLE hFind;

	std::string w32path = GetLocalPath(path) + "\\*.*";

	hFind = FindFirstFileEx(ConvertUTF8ToWString(w32path).c_str(), FindExInfoStandard, &findData, FindExSearchNameMatch, NULL, 0);

	if (hFind == INVALID_HANDLE_VALUE) {
		// Just return the empty array.
		return ReplayApplyDiskListing(myVector, CoreTiming::GetGlobalTimeUs());
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
		if (!wcscmp(findData.cFileName, L"..") )
			entry.size = 4096;
		else
			entry.size = findData.nFileSizeLow | ((u64)findData.nFileSizeHigh<<32);
		entry.name = SimulateVFATBug(ConvertWStringToUTF8(findData.cFileName));
		tmFromFiletime(entry.atime, findData.ftLastAccessTime);
		tmFromFiletime(entry.ctime, findData.ftCreationTime);
		tmFromFiletime(entry.mtime, findData.ftLastWriteTime);
		myVector.push_back(entry);

		int retval = FindNextFile(hFind, &findData);
		if (!retval)
			break;
	}
	FindClose(hFind);
#else
	dirent *dirp;
	std::string localPath = GetLocalPath(path);
	DIR *dp = opendir(localPath.c_str());

#if HOST_IS_CASE_SENSITIVE
	if (dp == NULL && FixPathCase(basePath,path, FPC_FILE_MUST_EXIST)) {
		// May have failed due to case sensitivity, try again
		localPath = GetLocalPath(path);
		dp = opendir(localPath.c_str());
	}
#endif

	if (dp == NULL) {
		ERROR_LOG(FILESYS,"Error opening directory %s\n",path.c_str());
		return ReplayApplyDiskListing(myVector, CoreTiming::GetGlobalTimeUs());
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
		entry.name = SimulateVFATBug(dirp->d_name);
		entry.size = s.st_size;
		localtime_r((time_t*)&s.st_atime,&entry.atime);
		localtime_r((time_t*)&s.st_ctime,&entry.ctime);
		localtime_r((time_t*)&s.st_mtime,&entry.mtime);
		myVector.push_back(entry);
	}
	closedir(dp);
#endif

	return ReplayApplyDiskListing(myVector, CoreTiming::GetGlobalTimeUs());
}

u64 DirectoryFileSystem::FreeSpace(const std::string &path) {
	uint64_t result = 0;
	if (free_disk_space(GetLocalPath(path), result)) {
		return ReplayApplyDisk64(ReplayAction::FREESPACE, result, CoreTiming::GetGlobalTimeUs());
	}

#if HOST_IS_CASE_SENSITIVE
	std::string fixedCase = path;
	if (FixPathCase(basePath, fixedCase, FPC_FILE_MUST_EXIST)) {
		// May have failed due to case sensitivity, try again.
		if (free_disk_space(GetLocalPath(fixedCase), result)) {
			return ReplayApplyDisk64(ReplayAction::FREESPACE, result, CoreTiming::GetGlobalTimeUs());
		}
	}
#endif

	// Just assume they're swimming in free disk space if we don't know otherwise.
	return ReplayApplyDisk64(ReplayAction::FREESPACE, std::numeric_limits<u64>::max(), CoreTiming::GetGlobalTimeUs());
}

void DirectoryFileSystem::DoState(PointerWrap &p) {
	auto s = p.Section("DirectoryFileSystem", 0, 2);
	if (!s)
		return;

	// Savestate layout:
	// u32: number of entries
	// per-entry:
	//     u32:              handle number
	//     std::string       filename (in guest's terms, untranslated)
	//     enum FileAccess   file access mode
	//     u32               seek position
	//     s64               current truncate position (v2+ only)

	u32 num = (u32) entries.size();
	p.Do(num);

	if (p.mode == p.MODE_READ) {
		CloseAll();
		u32 key;
		OpenFileEntry entry;
		for (u32 i = 0; i < num; i++) {
			p.Do(key);
			p.Do(entry.guestFilename);
			p.Do(entry.access);
			u32 err;
			if (!entry.hFile.Open(basePath,entry.guestFilename,entry.access, err)) {
				ERROR_LOG(FILESYS, "Failed to reopen file while loading state: %s", entry.guestFilename.c_str());
				continue;
			}
			u32 position;
			p.Do(position);
			if (position != entry.hFile.Seek(position, FILEMOVE_BEGIN)) {
				ERROR_LOG(FILESYS, "Failed to restore seek position while loading state: %s", entry.guestFilename.c_str());
				continue;
			}
			if (s >= 2) {
				p.Do(entry.hFile.needsTrunc_);
			}
			entries[key] = entry;
		}
	} else {
		for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
			u32 key = iter->first;
			p.Do(key);
			p.Do(iter->second.guestFilename);
			p.Do(iter->second.access);
			u32 position = (u32)iter->second.hFile.Seek(0, FILEMOVE_CURRENT);
			p.Do(position);
			p.Do(iter->second.hFile.needsTrunc_);
		}
	}
}



VFSFileSystem::VFSFileSystem(IHandleAllocator *_hAlloc, std::string _basePath) : basePath(_basePath) {
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

u32 VFSFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	if (access != FILEACCESS_READ) {
		ERROR_LOG(FILESYS, "VFSFileSystem only supports plain reading");
		return 0;
	}

	std::string fullName = GetLocalPath(filename);
	const char *fullNameC = fullName.c_str();
	VERBOSE_LOG(FILESYS,"VFSFileSystem actually opening %s (%s)", fullNameC, filename.c_str());

	size_t size;
	u8 *data = VFSReadFile(fullNameC, &size);
	if (!data) {
		ERROR_LOG(FILESYS, "VFSFileSystem failed to open %s", filename.c_str());
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
	FileInfo fo;
	if (VFSGetFileInfo(fullName.c_str(), &fo)) {
		x.exists = fo.exists;
		if (x.exists) {
			x.size = fo.size;
			x.type = fo.isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
		}
	} else {
		x.exists = false;
	}
	return x;
}

void VFSFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		delete [] iter->second.fileData;
		entries.erase(iter);
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS,"Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool VFSFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

int VFSFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
}

int VFSFileSystem::DevType(u32 handle) {
	return PSP_DEV_TYPE_FILE;
}

size_t VFSFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t VFSFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	DEBUG_LOG(FILESYS,"VFSFileSystem::ReadFile %08x %p %i", handle, pointer, (u32)size);
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		size_t bytesRead = size;
		memcpy(pointer, iter->second.fileData + iter->second.seekPos, size);
		iter->second.seekPos += size;
		return bytesRead;
	} else {
		ERROR_LOG(FILESYS,"Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t VFSFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	int ignored;
	return WriteFile(handle, pointer, size, ignored);
}

size_t VFSFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) {
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
		ERROR_LOG(FILESYS,"Cannot seek in file that hasn't been opened: %08x", handle);
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
	// Note: used interchangeably with DirectoryFileSystem for flash0:, so we use the same name.
	auto s = p.Section("DirectoryFileSystem", 0, 2);
	if (!s)
		return;

	u32 num = (u32) entries.size();
	p.Do(num);

	if (num != 0) {
		p.SetError(p.ERROR_WARNING);
		ERROR_LOG(FILESYS, "FIXME: Open files during savestate, could go badly.");
	}
}
