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

#if PPSSPP_PLATFORM(ANDROID)
#include "android/jni/app-android.h"
#include "android/jni/AndroidContentURI.h"

#include <algorithm>
#include <ctime>
#include <limits>

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Common/File/DirListing.h"
#include "Core/FileSystems/AndroidStorageFileSystem.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HW/MemoryStick.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/Host.h"
#include "Core/Replay.h"
#include "Core/Reporting.h"

AndroidStorageFileSystem::AndroidStorageFileSystem(IHandleAllocator *_hAlloc, std::string _basePath, FileSystemFlags _flags) : basePath(_basePath), flags(_flags) {
	// File::CreateFullPath(basePath);
	hAlloc = _hAlloc;
}

AndroidStorageFileSystem::~AndroidStorageFileSystem() {
	CloseAll();
}

bool AndroidDirectoryFileHandle::Open(const std::string &basePath, std::string &fileName, FileAccess access, u32 &error) {
	error = 0;

	AndroidStorageContentURI contentUri = AndroidStorageContentURI(basePath).WithFilePath(fileName);

	// On the PSP, truncating doesn't lose data.  If you seek later, you'll recover it.
	// This is abnormal, so we deviate from the PSP's behavior and truncate on write/close.
	// This means it's incorrectly not truncated before the write.
	if (access & FILEACCESS_TRUNCATE) {
		needsTrunc_ = 0;
	}

	// Check for existance and directory, so we can make better decisions below.
	File::FileInfo fileInfo;
	if (!Android_GetFileInfo(contentUri.ToString(), &fileInfo)) {
		ERROR_LOG(FILESYS, "Failed to call Android_GetFileInfo on %s", contentUri.ToString().c_str());
		return false;
	}

	if (fileInfo.exists && fileInfo.isDirectory) {
		ERROR_LOG(FILESYS, "AndroidDirectoryFileHandle::Open on directory %s - not allowed", contentUri.ToString().c_str());
		return false;
	}

	// Android_OpenContentUriFd only has three modes: READ/READ_WRITE/READ_WRITE_TRUNCATE.
	// For the modes that can create files, we have to explicitly create the file first, instead.
	// My god, what a pain this is. Gonna need some sort of test suite.

	Android_OpenContentUriMode mode = Android_OpenContentUriMode::READ;

	// TODO: There are probably improvements to be made in this translation.
	if (access & FILEACCESS_WRITE) {
		mode = Android_OpenContentUriMode::READ_WRITE_TRUNCATE;
		// Should this require EXCL too?
		if (access & FILEACCESS_APPEND) {
			mode = Android_OpenContentUriMode::READ_WRITE;
			// Will also need to seek later.
		}
	} else if (access & FILEACCESS_READ) {
		mode = Android_OpenContentUriMode::READ;
	}

	bool success = true;

	if (access & FILEACCESS_CREATE) {
		AndroidStorageContentURI parentUri(basePath);

		// We're gonna have to explicitly create the file here.
		if (!Android_CreateFile(parentUri.ToString(), fileName)) {
			// Something went wrong.
			success = false;
		}
	} else {
		// Requested an existing file, doesn't exist.
		if (!fileInfo.exists) {
			error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
			return false;
		}
	}

	// Not sure what to do with FILEACCESS_EXCL.

	std::string uriString = contentUri.ToString();

	INFO_LOG(FILESYS, "AndroidDirectoryFileHandle::Open(%s)", uriString.c_str());

	hFile = Android_OpenContentUriFd(uriString, mode);
	if (hFile == -1) {
		ERROR_LOG(FILESYS, "Android_OpenContentUriFd(%s) failed", uriString.c_str());
		success = false;
	}

	// Seek to end if append mode.
	Seek(0, FILEMOVE_END);

	// Try to detect reads/writes to PSP/GAME to avoid them in replays.
	if (basePath.find("/PSP/GAME/") != std::string::npos) {
		inGameDir_ = true;
	}
	return success;
}

size_t AndroidDirectoryFileHandle::Read(u8* pointer, s64 size) {
	size_t bytesRead = 0;
	if (needsTrunc_ != -1) {
		// If the file was marked to be truncated, pretend there's nothing.
		// On a PSP. it actually is truncated, but the data wasn't erased.
		off_t off = (off_t)Seek(0, FILEMOVE_CURRENT);
		if (needsTrunc_ <= off) {
			return replay_ ? ReplayApplyDiskRead(pointer, 0, (uint32_t)size, inGameDir_, CoreTiming::GetGlobalTimeUs()) : 0;
		}
		if (needsTrunc_ < off + size) {
			size = needsTrunc_ - off;
		}
	}
	bytesRead = read(hFile, pointer, size);
	return replay_ ? ReplayApplyDiskRead(pointer, (uint32_t)bytesRead, (uint32_t)size, inGameDir_, CoreTiming::GetGlobalTimeUs()) : bytesRead;
}

size_t AndroidDirectoryFileHandle::Write(const u8* pointer, s64 size)
{
	size_t bytesWritten = 0;
	bool diskFull = false;

	bytesWritten = write(hFile, pointer, size);
	if (bytesWritten == (size_t)-1) {
		diskFull = errno == ENOSPC;
	}
	if (needsTrunc_ != -1) {
		off_t off = (off_t)Seek(0, FILEMOVE_CURRENT);
		if (needsTrunc_ < off) {
			needsTrunc_ = off;
		}
	}

	if (replay_) {
		bytesWritten = ReplayApplyDiskWrite(pointer, (uint64_t)bytesWritten, (uint64_t)size, &diskFull, inGameDir_, CoreTiming::GetGlobalTimeUs());
	}

	if (diskFull) {
		ERROR_LOG(FILESYS, "Disk full");
		auto err = GetI18NCategory("Error");
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

size_t AndroidDirectoryFileHandle::Seek(s32 position, FileMove type)
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

	int moveMethod = 0;
	switch (type) {
	case FILEMOVE_BEGIN:    moveMethod = SEEK_SET;  break;
	case FILEMOVE_CURRENT:  moveMethod = SEEK_CUR;  break;
	case FILEMOVE_END:      moveMethod = SEEK_END;  break;
	}
	result = lseek(hFile, position, moveMethod);

	return replay_ ? (size_t)ReplayApplyDisk64(ReplayAction::FILE_SEEK, result, CoreTiming::GetGlobalTimeUs()) : result;
}

void AndroidDirectoryFileHandle::Close()
{
	if (needsTrunc_ != -1) {
		// Note: it's not great that Switch cannot truncate appropriately...
		if (ftruncate(hFile, (off_t)needsTrunc_) != 0) {
			ERROR_LOG_REPORT(FILESYS, "Failed to truncate file.");
		}
	}
	if (hFile != -1)
		close(hFile);
}

void AndroidStorageFileSystem::CloseAll() {
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		INFO_LOG(FILESYS, "DirectoryFileSystem::CloseAll(): Force closing %d (%s)", (int)iter->first, iter->second.guestFilename.c_str());
		iter->second.hFile.Close();
	}
	entries.clear();
}

std::string AndroidStorageFileSystem::GetLocalPath(std::string localpath) {
	if (localpath.empty()) {
		return baseContentUri.ToString();
	}

	if (localpath[0] == '/')
		localpath.erase(0, 1);

	return baseContentUri.WithFilePath(localpath).ToString();
}

bool AndroidStorageFileSystem::MkDir(const std::string &dirname) {
	ERROR_LOG(FILESYS, "MkDir operation not yet supported.");
	// TODO: Figure out a way to create directories in Storage...
	// TODO: Use Android_CreateDirectory. Not sure how deep it can go...
	bool result = false;
	// bool result = File::CreateFullPath(GetLocalPath(dirname));
	return ReplayApplyDisk(ReplayAction::MKDIR, result, CoreTiming::GetGlobalTimeUs()) != 0;
}

bool AndroidStorageFileSystem::RmDir(const std::string &dirname) {
	
	ERROR_LOG(FILESYS, "RmDir operation not yet supported.");

	return false; // ReplayApplyDisk(ReplayAction::RMDIR, result, CoreTiming::GetGlobalTimeUs()) != 0;
}

int AndroidStorageFileSystem::RenameFile(const std::string &from, const std::string &to) {
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

	fullTo = GetLocalPath(fullTo);
	const char * fullToC = fullTo.c_str();

	bool retValue = (0 == rename(fullFrom.c_str(), fullToC));

	// TODO: Better error codes.
	int result = retValue ? 0 : (int)SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS;
	return ReplayApplyDisk(ReplayAction::FILE_RENAME, result, CoreTiming::GetGlobalTimeUs());
}

bool AndroidStorageFileSystem::RemoveFile(const std::string &filename) {
	std::string fullName = GetLocalPath(filename);
	bool retValue = (0 == unlink(fullName.c_str()));
	return ReplayApplyDisk(ReplayAction::FILE_REMOVE, retValue, CoreTiming::GetGlobalTimeUs()) != 0;
}

int AndroidStorageFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	OpenFileEntry entry;
	u32 err = 0;
	bool success = entry.hFile.Open(basePath, filename, access, err);
	if (err == 0 && !success) {
		err = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	}

	err = ReplayApplyDisk(ReplayAction::FILE_OPEN, err, CoreTiming::GetGlobalTimeUs());
	if (err != 0) {
		ERROR_LOG(FILESYS, "DirectoryFileSystem::OpenFile: FAILED, %i - access = %i", errno, (int)access);
		return err;
	} else {
		if (access & FILEACCESS_APPEND)
			entry.hFile.Seek(0, FILEMOVE_END);

		u32 newHandle = hAlloc->GetNewHandle();

		entry.guestFilename = filename;
		entry.access = access;

		entries[newHandle] = entry;

		return newHandle;
	}
}

void AndroidStorageFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		hAlloc->FreeHandle(handle);
		iter->second.hFile.Close();
		entries.erase(iter);
	} else {
		// This shouldn't happen...
		ERROR_LOG(FILESYS, "Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool AndroidStorageFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

int AndroidStorageFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
}

PSPDevType AndroidStorageFileSystem::DevType(u32 handle) {
	return PSPDevType::FILE;
}

size_t AndroidStorageFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t AndroidStorageFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		if (size < 0) {
			ERROR_LOG_REPORT(FILESYS, "Invalid read for %lld bytes from disk %s", size, iter->second.guestFilename.c_str());
			return 0;
		}

		size_t bytesRead = iter->second.hFile.Read(pointer, size);
		return bytesRead;
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS, "Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t AndroidStorageFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	int ignored;
	return WriteFile(handle, pointer, size, ignored);
}

size_t AndroidStorageFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		size_t bytesWritten = iter->second.hFile.Write(pointer, size);
		return bytesWritten;
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS, "Cannot write to file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t AndroidStorageFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		return iter->second.hFile.Seek(position, type);
	} else {
		//This shouldn't happen...
		ERROR_LOG(FILESYS, "Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}

PSPFileInfo AndroidStorageFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo x;
	x.name = filename;

	std::string uri = GetLocalPath(filename);

	File::FileInfo info;
	if (!Android_GetFileInfo(uri, &info)) {
		return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
	}

	x.type = info.isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
	x.exists = true;

	if (x.type != FILETYPE_DIRECTORY) {
		x.size = info.size;
		x.access = info.isWritable ? 0777 : 0666;

		// The only time value we get from the storage API.
		int64_t lastModified = info.lastModified / 1000;

		time_t atime = lastModified;
		time_t ctime = lastModified;
		time_t mtime = lastModified;

		localtime_r((time_t*)&atime, &x.atime);
		localtime_r((time_t*)&ctime, &x.ctime);
		localtime_r((time_t*)&mtime, &x.mtime);
	}

	return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
}

// See comment in DirectoryFileSystem.
extern std::string SimulateVFATBug(std::string filename);

std::vector<PSPFileInfo> AndroidStorageFileSystem::GetDirListing(std::string path) {
	std::vector<PSPFileInfo> myVector;
	bool listingRoot = path == "/" || path == "\\";

	std::string uri = GetLocalPath(path);

	std::vector<File::FileInfo> fileInfo = Android_ListContentUri(uri);

	bool hideISOFiles = PSP_CoreParameter().compat.flags().HideISOFiles;
	for (auto &info : fileInfo) {
		PSPFileInfo entry;
		if (info.isDirectory)
			entry.type = FILETYPE_DIRECTORY;
		else
			entry.type = FILETYPE_NORMAL;
		entry.access = info.isWritable ? 0777 : 0666;
		entry.name = info.name;
		if (Flags() & FileSystemFlags::SIMULATE_FAT32) {
			entry.name = SimulateVFATBug(entry.name);
		}
		entry.size = info.size;

		bool hideFile = false;
		if (hideISOFiles && (endsWithNoCase(entry.name, ".cso") || endsWithNoCase(entry.name, ".iso"))) {
			// Workaround for DJ Max Portable, see compat.ini.
			hideFile = true;
		}

		// TODO: Maybe do this conversion in the Android wrapper layer...
		int64_t lastModified = info.lastModified / 1000;

		time_t atime = lastModified;
		time_t ctime = lastModified;
		time_t mtime = lastModified;

		localtime_r((time_t*)&atime, &entry.atime);
		localtime_r((time_t*)&ctime, &entry.ctime);
		localtime_r((time_t*)&mtime, &entry.mtime);
		if (!hideFile && (!listingRoot || (strcmp(info.name.c_str(), "..") && strcmp(info.name.c_str(), "."))))
			myVector.push_back(entry);
	}

	return ReplayApplyDiskListing(myVector, CoreTiming::GetGlobalTimeUs());
}

u64 AndroidStorageFileSystem::FreeSpace(const std::string &path) {
	// Can't get this, I think.

	return ReplayApplyDisk64(ReplayAction::FREESPACE, std::numeric_limits<u64>::max(), CoreTiming::GetGlobalTimeUs());
}

// WARNING! This must be kept compatible with DirectoryFileSystem for save state portability.
void AndroidStorageFileSystem::DoState(PointerWrap &p) {
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

	u32 num = (u32)entries.size();
	Do(p, num);

	if (p.mode == p.MODE_READ) {
		CloseAll();
		u32 key;
		OpenFileEntry entry;
		for (u32 i = 0; i < num; i++) {
			Do(p, key);
			Do(p, entry.guestFilename);
			Do(p, entry.access);
			u32 err;
			if (!entry.hFile.Open(basePath, entry.guestFilename, entry.access, err)) {
				ERROR_LOG(FILESYS, "Failed to reopen file while loading state: %s", entry.guestFilename.c_str());
				continue;
			}
			u32 position;
			Do(p, position);
			if (position != entry.hFile.Seek(position, FILEMOVE_BEGIN)) {
				ERROR_LOG(FILESYS, "Failed to restore seek position while loading state: %s", entry.guestFilename.c_str());
				continue;
			}
			if (s >= 2) {
				Do(p, entry.hFile.needsTrunc_);
			}
			entries[key] = entry;
		}
	} else {
		for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
			u32 key = iter->first;
			Do(p, key);
			Do(p, iter->second.guestFilename);
			Do(p, iter->second.access);
			u32 position = (u32)iter->second.hFile.Seek(0, FILEMOVE_CURRENT);
			Do(p, position);
			Do(p, iter->second.hFile.needsTrunc_);
		}
	}
}

#endif  // PPSSPP_PLATFORM(ANDROID)
