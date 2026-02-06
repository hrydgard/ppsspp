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
#include <ctime>
#include <limits>

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DiskFree.h"
#include "Common/File/VFS/VFS.h"
#include "Common/SysError.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HW/MemoryStick.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/Replay.h"
#include "Core/Reporting.h"
#include "Core/ELF/ParamSFO.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <sys/stat.h>
#if PPSSPP_PLATFORM(UWP)
#include <fileapifromapp.h>
#endif
#undef FILE_OPEN
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

DirectoryFileSystem::DirectoryFileSystem(IHandleAllocator *_hAlloc, const Path & _basePath, FileSystemFlags _flags) : basePath(_basePath), flags(_flags) {
	File::CreateFullPath(basePath);

	static const std::string_view mixedCase = "wJpCzSBNnZfxSgoS";
	static const std::string_view upperCase = "WJPCZSBNNZFXSGOS";

	// Check for case sensitivity
	bool checkSucceeded = false;
	File::CreateEmptyFile(basePath / mixedCase);
	if (File::Exists(basePath / mixedCase)) {
		checkSucceeded = true;
		if (!File::Exists(basePath / upperCase)) {
			flags |= FileSystemFlags::CASE_SENSITIVE;
		}
	}
	File::Delete(basePath / mixedCase);

	INFO_LOG(Log::IO, "Is file system case sensitive? %s (base: '%s') (checkOK: %d)", (flags & FileSystemFlags::CASE_SENSITIVE) ? "yes" : "no", _basePath.c_str(), checkSucceeded);

	hAlloc = _hAlloc;
}

DirectoryFileSystem::~DirectoryFileSystem() {
	CloseAll();
}

// TODO(scoped): Merge the two below functions somehow.

Path DirectoryFileHandle::GetLocalPath(const Path &basePath, std::string_view localPath) const {
	if (localPath.empty())
		return basePath;

	if (localPath[0] == '/')
		localPath = localPath.substr(1);

	if (fileSystemFlags_ & FileSystemFlags::STRIP_PSP) {
		if (localPath == "PSP") {
			localPath = "/";
		} else if (startsWithNoCase(localPath, "PSP/")) {
			localPath = localPath.substr(4);
		}
	}

	return basePath / localPath;
}

Path DirectoryFileSystem::GetLocalPath(std::string_view internalPath) const {
	if (internalPath.empty())
		return basePath;

	if (internalPath[0] == '/')
		internalPath = internalPath.substr(1);

	if (flags & FileSystemFlags::STRIP_PSP) {
		if (internalPath == "PSP") {
			internalPath = "/";
		} else if (startsWithNoCase(internalPath, "PSP/")) {
			internalPath = internalPath.substr(4);
		}
	}

	return basePath / internalPath;
}

bool DirectoryFileHandle::Open(const Path &basePath, std::string &fileName, FileAccess access, u32 &error) {
	error = 0;

	if (access == FILEACCESS_NONE) {
		error = SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		return false;
	}

	if (fileSystemFlags_ & FileSystemFlags::CASE_SENSITIVE) {
		if (access & (FILEACCESS_APPEND | FILEACCESS_CREATE | FILEACCESS_WRITE)) {
			DEBUG_LOG(Log::FileSystem, "Checking case for path %s", fileName.c_str());
			if (!FixPathCase(basePath, fileName, FPC_PATH_MUST_EXIST)) {
				error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
				return false;  // or go on and attempt (for a better error code than just 0?)
			}
		}
	}
	// else we try fopen first (in case we're lucky) before simulating case insensitivity

	Path fullName = GetLocalPath(basePath, fileName);

	// On the PSP, truncating doesn't lose data.  If you seek later, you'll recover it.
	// This is abnormal, so we deviate from the PSP's behavior and truncate on write/close.
	// This means it's incorrectly not truncated before the write.
	if (access & FILEACCESS_TRUNCATE) {
		needsTrunc_ = 0;
	}

	//TODO: tests, should append seek to end of file? seeking in a file opened for append?
#ifdef HAVE_LIBRETRO_VFS
	int flags = 0;
	if (access & FILEACCESS_READ && access & FILEACCESS_WRITE)
		flags = RETRO_VFS_FILE_ACCESS_READ_WRITE;
	else if (access & FILEACCESS_WRITE)
		flags = RETRO_VFS_FILE_ACCESS_WRITE;
	else if (access & FILEACCESS_READ && access & FILEACCESS_CREATE)
		flags = RETRO_VFS_FILE_ACCESS_READ_WRITE;
	else
		flags = RETRO_VFS_FILE_ACCESS_READ;
	bool success = false;
	if (!(access & FILEACCESS_CREATE)) {
		hFile = filestream_open(fullName.c_str(), flags & RETRO_VFS_FILE_ACCESS_WRITE ? flags | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING : flags, RETRO_VFS_FILE_ACCESS_HINT_NONE);
		success = hFile != nullptr;
	} else if (!(access & FILEACCESS_EXCL)) {
		hFile = filestream_open(fullName.c_str(), flags & RETRO_VFS_FILE_ACCESS_WRITE && File::Exists(fullName) ? flags | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING : flags, RETRO_VFS_FILE_ACCESS_HINT_NONE);
		success = hFile != nullptr;
	} else if (!File::Exists(fullName)) {
		hFile = filestream_open(fullName.c_str(), flags & RETRO_VFS_FILE_ACCESS_WRITE ? flags | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING : flags, RETRO_VFS_FILE_ACCESS_HINT_NONE);
		success = hFile != nullptr;
	}
#elif PPSSPP_PLATFORM(WINDOWS)
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
		sharemode |= FILE_SHARE_WRITE | FILE_SHARE_READ;
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
	hFile = CreateFile2FromAppW(fullName.ToWString().c_str(), desired, sharemode, openmode, nullptr);
#else
	hFile = CreateFile(fullName.ToWString().c_str(), desired, sharemode, 0, openmode, 0, 0);
#endif
	bool success = hFile != INVALID_HANDLE_VALUE;
	if (!success) {
		DWORD w32err = GetLastError();

		if (w32err == ERROR_SHARING_VIOLATION) {
			// Sometimes, the file is locked for write, let's try again.
			sharemode |= FILE_SHARE_WRITE;
#if PPSSPP_PLATFORM(UWP)
			hFile = CreateFile2FromAppW(fullName.ToWString().c_str(), desired, sharemode, openmode, nullptr);
#else
			hFile = CreateFile(fullName.ToWString().c_str(), desired, sharemode, 0, openmode, 0, 0);
#endif
			success = hFile != INVALID_HANDLE_VALUE;
			if (!success) {
				w32err = GetLastError();
			}
		}

		if (w32err == ERROR_DISK_FULL || w32err == ERROR_NOT_ENOUGH_QUOTA) {
			// This is returned when the disk is full.
			auto err = GetI18NCategory(I18NCat::ERRORS);
			g_OSD.Show(OSDType::MESSAGE_ERROR, err->T("Disk full while writing data"), 0.0f, "diskfull");
			error = SCE_KERNEL_ERROR_ERRNO_NO_PERM;
		} else if (!success) {
			error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
		}
	}
#else
	if (fullName.Type() == PathType::CONTENT_URI) {
		// Convert flags. Don't want to share this type, bad dependency.
		u32 flags = File::OPEN_NONE;
		if (access & FILEACCESS_READ)
			flags |= File::OPEN_READ;
		if (access & FILEACCESS_WRITE)
			flags |= File::OPEN_WRITE;
		if (access & FILEACCESS_APPEND)
			flags |= File::OPEN_APPEND;
		if (access & FILEACCESS_CREATE)
			flags |= File::OPEN_CREATE;
		// Important: don't pass TRUNCATE here, the PSP truncates weirdly.  See #579.
		// See above about truncate behavior.  Just add READ to preserve data here.
		if (access & FILEACCESS_TRUNCATE)
			flags |= File::OPEN_READ;

		int fd = File::OpenFD(fullName, (File::OpenFlag)flags);
		// Try to detect reads/writes to PSP/GAME to avoid them in replays.
		if (fullName.FilePathContainsNoCase("PSP/GAME/")) {
			inGameDir_ = true;
		}
		hFile = fd;
		if (fd != -1) {
			// Success
			return true;
		} else {
			ERROR_LOG(Log::FileSystem, "File::OpenFD returned an error");
			// TODO: Need better error codes from OpenFD so we can distinguish
			// disk full. Just set not found for now.
			error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
			return false;
		}
	}

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

	if (fileSystemFlags_ & FileSystemFlags::CASE_SENSITIVE) {
		if (!success && !(access & FILEACCESS_CREATE)) {
			if (!FixPathCase(basePath, fileName, FPC_PATH_MUST_EXIST)) {
				error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
				return false;
			}
			fullName = GetLocalPath(basePath, fileName);
			DEBUG_LOG(Log::FileSystem, "Case may have been incorrect, second try opening %s (%s)", fullName.c_str(), fileName.c_str());

			// And try again with the correct case this time
#ifdef HAVE_LIBRETRO_VFS
			success = false;
			if (!(access & FILEACCESS_CREATE)) {
				hFile = filestream_open(fullName.c_str(), flags & RETRO_VFS_FILE_ACCESS_WRITE ? flags | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING : flags, RETRO_VFS_FILE_ACCESS_HINT_NONE);
				success = hFile != nullptr;
			} else if (!(access & FILEACCESS_EXCL)) {
				hFile = filestream_open(fullName.c_str(), flags & RETRO_VFS_FILE_ACCESS_WRITE && File::Exists(fullName) ? flags | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING : flags, RETRO_VFS_FILE_ACCESS_HINT_NONE);
				success = hFile != nullptr;
			} else if (!File::Exists(fullName)) {
				hFile = filestream_open(fullName.c_str(), flags & RETRO_VFS_FILE_ACCESS_WRITE ? flags | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING : flags, RETRO_VFS_FILE_ACCESS_HINT_NONE);
				success = hFile != nullptr;
			}
#elif PPSSPP_PLATFORM(UWP)
			// Should never get here.
#elif PPSSPP_PLATFORM(WINDOWS)
			// Unlikely to get here, heh.
			hFile = CreateFile(fullName.ToWString().c_str(), desired, sharemode, 0, openmode, 0, 0);
			success = hFile != INVALID_HANDLE_VALUE;
#else
			hFile = open(fullName.c_str(), flags, 0666);
			success = hFile != -1;
#endif
		}
	}

#if !PPSSPP_PLATFORM(WINDOWS) && !defined(HAVE_LIBRETRO_VFS)
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
		auto err = GetI18NCategory(I18NCat::ERRORS);
		g_OSD.Show(OSDType::MESSAGE_ERROR, err->T("Disk full while writing data"), 0.0f, "diskfull");
		error = SCE_KERNEL_ERROR_ERRNO_NO_PERM;
	} else {
		error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	}
#endif

	// Try to detect reads/writes to PSP/GAME to avoid them in replays.
	if (fullName.FilePathContainsNoCase("PSP/GAME/")) {
		inGameDir_ = true;
	}
	if (access & (FILEACCESS_APPEND | FILEACCESS_CREATE | FILEACCESS_WRITE)) {
		MemoryStick_NotifyWrite();
	}

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
			return replay_ ? ReplayApplyDiskRead(pointer, 0, (uint32_t)size, inGameDir_, CoreTiming::GetGlobalTimeUs()) : 0;
		}
		if (needsTrunc_ < off + size) {
			size = needsTrunc_ - off;
		}
	}
	if (size > 0) {
#ifdef HAVE_LIBRETRO_VFS
		bytesRead = fread(pointer, 1, size, hFile);
#elif defined(_WIN32)
		::ReadFile(hFile, (LPVOID)pointer, (DWORD)size, (LPDWORD)&bytesRead, 0);
#else
		bytesRead = read(hFile, pointer, size);
#endif
	}
	return replay_ ? ReplayApplyDiskRead(pointer, (uint32_t)bytesRead, (uint32_t)size, inGameDir_, CoreTiming::GetGlobalTimeUs()) : bytesRead;
}

size_t DirectoryFileHandle::Write(const u8* pointer, s64 size)
{
	size_t bytesWritten = 0;
	bool diskFull = false;

#ifdef HAVE_LIBRETRO_VFS
	bytesWritten = fwrite(pointer, 1, size, hFile);
#elif defined(_WIN32)
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

	if (replay_) {
		bytesWritten = ReplayApplyDiskWrite(pointer, (uint64_t)bytesWritten, (uint64_t)size, &diskFull, inGameDir_, CoreTiming::GetGlobalTimeUs());
	}

	MemoryStick_NotifyWrite();

	if (diskFull) {
		ERROR_LOG(Log::FileSystem, "Disk full");
		auto err = GetI18NCategory(I18NCat::ERRORS);
		g_OSD.Show(OSDType::MESSAGE_ERROR, err->T("Disk full while writing data"), 0.0f, "diskfull");
		// We only return an error when the disk is actually full.
		// When writing this would cause the disk to be full, so it wasn't written, we return 0.
		Path saveFolder = GetSysDirectory(DIRECTORY_SAVEDATA);
		int64_t space;
		if (free_disk_space(saveFolder, space)) {
			if (space < size) {
				// Sign extend to a 64-bit value.
				return (size_t)(s64)(s32)SCE_KERNEL_ERROR_ERRNO_DEVICE_NO_FREE_SPACE;
			}
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
			position = (s32)(needsTrunc_ + position);
		}
	}

	size_t result;
#ifdef HAVE_LIBRETRO_VFS
	int moveMethod = 0;
	switch (type) {
	case FILEMOVE_BEGIN:    moveMethod = SEEK_SET;  break;
	case FILEMOVE_CURRENT:  moveMethod = SEEK_CUR;  break;
	case FILEMOVE_END:      moveMethod = SEEK_END;  break;
	}
	result = File::Fseektell(hFile, position, moveMethod);
#elif defined(_WIN32)
	DWORD moveMethod = 0;
	switch (type) {
	case FILEMOVE_BEGIN:    moveMethod = FILE_BEGIN;    break;
	case FILEMOVE_CURRENT:  moveMethod = FILE_CURRENT;  break;
	case FILEMOVE_END:      moveMethod = FILE_END;      break;
	}

	LARGE_INTEGER distance;
	distance.QuadPart = position;
	LARGE_INTEGER cursor;
	SetFilePointerEx(hFile, distance, &cursor, moveMethod);
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

void DirectoryFileHandle::Close() {
	if (needsTrunc_ != -1) {
#ifdef HAVE_LIBRETRO_VFS
		if (filestream_truncate(hFile, needsTrunc_) != 0) {
			ERROR_LOG(Log::FileSystem, "Failed to truncate file to %d bytes", (int)needsTrunc_);
		}
#elif defined(_WIN32)
		Seek((s32)needsTrunc_, FILEMOVE_BEGIN);
		if (SetEndOfFile(hFile) == 0) {
			ERROR_LOG(Log::FileSystem, "Failed to truncate file to %d bytes", (int)needsTrunc_);
		}
#elif !PPSSPP_PLATFORM(SWITCH)
		// Note: it's not great that Switch cannot truncate appropriately...
		if (ftruncate(hFile, (off_t)needsTrunc_) != 0) {
			ERROR_LOG(Log::FileSystem, "Failed to truncate file to %d bytes", (int)needsTrunc_);
		}
#endif
	}

#ifdef HAVE_LIBRETRO_VFS
	if (hFile != nullptr)
		fclose(hFile);
#elif defined(_WIN32)
	if (hFile != (HANDLE)-1)
		CloseHandle(hFile);
#else
	if (hFile != -1)
		close(hFile);
#endif
}

void DirectoryFileSystem::CloseAll() {
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		INFO_LOG(Log::FileSystem, "DirectoryFileSystem::CloseAll(): Force closing %d (%s)", (int)iter->first, iter->second.guestFilename.c_str());
		iter->second.hFile.Close();
	}
	entries.clear();
}

bool DirectoryFileSystem::MkDir(const std::string &dirname) {
	bool result;
	if (flags & FileSystemFlags::CASE_SENSITIVE) {
		// Must fix case BEFORE attempting, because MkDir would create
		// duplicate (different case) directories
		std::string fixedCase = dirname;
		if (!FixPathCase(basePath, fixedCase, FPC_PARTIAL_ALLOWED)) {
			result = false;
		} else {
			result = File::CreateFullPath(GetLocalPath(fixedCase));
		}
	} else {
		result = File::CreateFullPath(GetLocalPath(dirname));
	}
	MemoryStick_NotifyWrite();
	return ReplayApplyDisk(ReplayAction::MKDIR, result, CoreTiming::GetGlobalTimeUs()) != 0;
}

bool DirectoryFileSystem::RmDir(const std::string &dirname) {
	Path fullName = GetLocalPath(dirname);

	if (flags & FileSystemFlags::CASE_SENSITIVE) {
		// Maybe we're lucky?
		if (File::DeleteDirRecursively(fullName)) {
			MemoryStick_NotifyWrite();
			return (bool)ReplayApplyDisk(ReplayAction::RMDIR, true, CoreTiming::GetGlobalTimeUs());
		}

		// Nope, fix case and try again.  Should we try again?
		std::string fullPath = dirname;
		if (!FixPathCase(basePath, fullPath, FPC_FILE_MUST_EXIST))
			return (bool)ReplayApplyDisk(ReplayAction::RMDIR, false, CoreTiming::GetGlobalTimeUs());

		fullName = GetLocalPath(fullPath);
	}

	bool result = File::DeleteDirRecursively(fullName);
	MemoryStick_NotifyWrite();
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

	Path fullFrom = GetLocalPath(from);

	if (flags & FileSystemFlags::CASE_SENSITIVE) {
		// In case TO should overwrite a file with different case.  Check error code?
		if (!FixPathCase(basePath, fullTo, FPC_PATH_MUST_EXIST))
			return ReplayApplyDisk(ReplayAction::FILE_RENAME, -1, CoreTiming::GetGlobalTimeUs());
	}

	Path fullToPath = GetLocalPath(fullTo);

	bool retValue = File::Rename(fullFrom, fullToPath);

	if (flags & FileSystemFlags::CASE_SENSITIVE) {
		if (!retValue) {
			// May have failed due to case sensitivity on FROM, so try again.  Check error code?
			std::string fullFromPath = from;
			if (!FixPathCase(basePath, fullFromPath, FPC_FILE_MUST_EXIST))
				return ReplayApplyDisk(ReplayAction::FILE_RENAME, -1, CoreTiming::GetGlobalTimeUs());
			fullFrom = GetLocalPath(fullFromPath);

			retValue = File::Rename(fullFrom, fullToPath);
		}
	}

	// TODO: Better error codes.
	int result = retValue ? 0 : (int)SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS;
	MemoryStick_NotifyWrite();
	return ReplayApplyDisk(ReplayAction::FILE_RENAME, result, CoreTiming::GetGlobalTimeUs());
}

bool DirectoryFileSystem::RemoveFile(const std::string &filename) {
	Path localPath = GetLocalPath(filename);

	bool retValue = File::Delete(localPath);

	if (flags & FileSystemFlags::CASE_SENSITIVE) {
		if (!retValue) {
			// May have failed due to case sensitivity, so try again.  Try even if it fails?
			std::string fullNamePath = filename;
			if (!FixPathCase(basePath, fullNamePath, FPC_FILE_MUST_EXIST))
				return (bool)ReplayApplyDisk(ReplayAction::FILE_REMOVE, false, CoreTiming::GetGlobalTimeUs());
			localPath = GetLocalPath(fullNamePath);

			retValue = File::Delete(localPath);
		}
	}

	MemoryStick_NotifyWrite();
	return ReplayApplyDisk(ReplayAction::FILE_REMOVE, retValue, CoreTiming::GetGlobalTimeUs()) != 0;
}

int DirectoryFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	OpenFileEntry entry;
	entry.hFile.fileSystemFlags_ = flags;
	u32 err = 0;
	bool success = entry.hFile.Open(basePath, filename, (FileAccess)(access & FILEACCESS_PSP_FLAGS), err);
	if (err == 0 && !success) {
		err = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	}

	err = ReplayApplyDisk(ReplayAction::FILE_OPEN, err, CoreTiming::GetGlobalTimeUs());
	if (err != 0) {
		std::string errorString;
		int logError;
#ifdef _WIN32
		auto win32err = GetLastError();
		logError = (int)win32err;
		errorString = GetStringErrorMsg(win32err);
#else
		logError = (int)errno;
#endif
		if (!(access & FILEACCESS_PPSSPP_QUIET)) {
			ERROR_LOG(Log::FileSystem, "DirectoryFileSystem::OpenFile('%s'): FAILED, %d - access = %d '%s'", filename.c_str(), logError, (int)(access & FILEACCESS_PSP_FLAGS), errorString.c_str());
		}
		return err;
	} else {
#if defined(_WIN32) || defined(HAVE_LIBRETRO_VFS)
		if (access & FILEACCESS_APPEND) {
			entry.hFile.Seek(0, FILEMOVE_END);
		}
#endif

		u32 newHandle = hAlloc->GetNewHandle();

		entry.guestFilename = filename;
		entry.access = (FileAccess)(access & FILEACCESS_PSP_FLAGS);

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
		ERROR_LOG(Log::FileSystem,"Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool DirectoryFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

int DirectoryFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
}

PSPDevType DirectoryFileSystem::DevType(u32 handle) {
	return PSPDevType::FILE;
}

size_t DirectoryFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t DirectoryFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		if (size < 0) {
			ERROR_LOG(Log::FileSystem, "Invalid read for %lld bytes from disk %s", size, iter->second.guestFilename.c_str());
			return 0;
		}

		size_t bytesRead = iter->second.hFile.Read(pointer,size);
		return bytesRead;
	} else {
		// This shouldn't happen...
		ERROR_LOG(Log::FileSystem,"Cannot read file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t DirectoryFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	int ignored;
	return WriteFile(handle, pointer, size, ignored);
}

size_t DirectoryFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		size_t bytesWritten = iter->second.hFile.Write(pointer,size);
		return bytesWritten;
	} else {
		//This shouldn't happen...
		ERROR_LOG(Log::FileSystem,"Cannot write to file that hasn't been opened: %08x", handle);
		return 0;
	}
}

size_t DirectoryFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		return iter->second.hFile.Seek(position,type);
	} else {
		//This shouldn't happen...
		ERROR_LOG(Log::FileSystem,"Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}

PSPFileInfo DirectoryFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo x;
	x.name = filename;

	File::FileInfo info;
	Path fullName = GetLocalPath(filename);
	if (!File::GetFileInfo(fullName, &info)) {
		if (flags & FileSystemFlags::CASE_SENSITIVE) {
			if (!FixPathCase(basePath, filename, FPC_FILE_MUST_EXIST))
				return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
			fullName = GetLocalPath(filename);

			if (!File::GetFileInfo(fullName, &info))
				return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
		} else {
			return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
		}
	}

	x.type = info.isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
	x.exists = true;

	if (x.type != FILETYPE_DIRECTORY) {
		x.size = info.size;
	}
	x.access = info.access;
	time_t atime = info.atime;
	time_t ctime = info.ctime;
	time_t mtime = info.mtime;

	localtime_r((time_t*)&atime, &x.atime);
	localtime_r((time_t*)&ctime, &x.ctime);
	localtime_r((time_t*)&mtime, &x.mtime);

	return ReplayApplyDiskFileInfo(x, CoreTiming::GetGlobalTimeUs());
}

PSPFileInfo DirectoryFileSystem::GetFileInfoByHandle(u32 handle) {
	WARN_LOG(Log::FileSystem, "GetFileInfoByHandle not yet implemented for DirectoryFileSystem");
	return PSPFileInfo();
}

#ifdef _WIN32
#define FILETIME_FROM_UNIX_EPOCH_US 11644473600000000ULL

static void tmFromFiletime(tm &dest, const FILETIME &src) {
	u64 from_1601_us = (((u64) src.dwHighDateTime << 32ULL) + (u64) src.dwLowDateTime) / 10ULL;
	u64 from_1970_us = from_1601_us - FILETIME_FROM_UNIX_EPOCH_US;

	time_t t = (time_t) (from_1970_us / 1000000UL);
	localtime_s(&dest, &t);
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
	static const char * const FAT_UPPER_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&'(){}-_`~";
	static const char * const FAT_LOWER_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789!@#$%^&'(){}-_`~";
	static const char * const LOWER_CHARS = "abcdefghijklmnopqrstuvwxyz";

	// To avoid logging/comparing, skip all this if it has no lowercase chars to begin with.
	size_t lowerchar = filename.find_first_of(LOWER_CHARS);
	if (lowerchar == filename.npos) {
		return filename;
	}

	bool apply_hack = false;
	size_t dot_pos = filename.find('.');
	if (dot_pos == filename.npos && filename.length() <= 8) {
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
		VERBOSE_LOG(Log::FileSystem, "Applying VFAT hack to filename: %s", filename.c_str());
		// In this situation, NT would write UPPERCASE, and just set a flag to say "actually lowercase".
		// That VFAT flag isn't read by the PSP firmware, so let's pretend to "not read it."
		std::transform(filename.begin(), filename.end(), filename.begin(), toupper);
	}

	return filename;
}

bool DirectoryFileSystem::ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) {
	Path localPath = GetLocalPath(path);

	int64_t sizeTemp = File::ComputeRecursiveDirectorySize(localPath);
	if (sizeTemp >= 0) {
		*size = sizeTemp;
		return true;
	} else {
		return false;
	}
}

std::vector<PSPFileInfo> DirectoryFileSystem::GetDirListing(const std::string &path, bool *exists) {
	std::vector<PSPFileInfo> myVector;

	std::vector<File::FileInfo> files;
	Path localPath = GetLocalPath(path);
	const int flags = File::GETFILES_GETHIDDEN | File::GETFILES_GET_NAVIGATION_ENTRIES;
	bool success = File::GetFilesInDir(localPath, &files, nullptr, flags);

	if (this->flags & FileSystemFlags::CASE_SENSITIVE) {
		if (!success) {
			// TODO: Case sensitivity should be checked on a file system basis, right?
			std::string fixedPath = path;
			if (FixPathCase(basePath, fixedPath, FPC_FILE_MUST_EXIST)) {
				// May have failed due to case sensitivity, try again
				localPath = GetLocalPath(fixedPath);
				success = File::GetFilesInDir(localPath, &files, nullptr, flags);
			}
		}
	}

	if (!success) {
		if (exists)
			*exists = false;
		return ReplayApplyDiskListing(myVector, CoreTiming::GetGlobalTimeUs());
	}

	bool hideISOFiles = PSP_CoreParameter().compat.flags().HideISOFiles;

	// Then apply transforms to match PSP idiosynchrasies, as we convert the entries.
	for (auto &file : files) {
		PSPFileInfo entry;
		if (Flags() & FileSystemFlags::SIMULATE_FAT32) {
			entry.name = SimulateVFATBug(file.name);
		} else {
			entry.name = file.name;
		}
		if (hideISOFiles) {
			if (endsWithNoCase(entry.name, ".cso") || endsWithNoCase(entry.name, ".iso") || endsWithNoCase(entry.name, ".chd")) {  // chd not really necessary, but let's hide them too.
				// Workaround for DJ Max Portable, see compat.ini.
				continue;
			} else if (file.isDirectory) {
				if (endsWithNoCase(path, "SAVEDATA")) {
					// Don't let it see savedata from other games, it can misinterpret stuff.
					std::string gameID = g_paramSFO.GetDiscID();
					if (entry.name.size() > 2 && !startsWithNoCase(entry.name, gameID)) {
						continue;
					}
				} else if (file.name == "GAME" || file.name == "TEXTURES" || file.name == "PPSSPP_STATE" || file.name == "PLUGINS" || file.name == "SYSTEM" || equalsNoCase(file.name, "Cheats")) {
					// The game scans these folders on startup which can take time. Skip them.
					continue;
				}
			}
		}
		if (file.name == "..") {
			entry.size = 4096;
		} else {
			entry.size = file.size;
		}
		if (file.isDirectory) {
			entry.type = FILETYPE_DIRECTORY;
		} else {
			entry.type = FILETYPE_NORMAL;
		}
		entry.access = file.access;
		entry.exists = file.exists;

		localtime_r((time_t*)&file.atime, &entry.atime);
		localtime_r((time_t*)&file.ctime, &entry.ctime);
		localtime_r((time_t*)&file.mtime, &entry.mtime);

		myVector.push_back(entry);
	}

	if (this->flags & FileSystemFlags::STRIP_PSP) {
		if (path == "/") {
			// Artificially add the /PSP directory to the root listing.
			PSPFileInfo pspInfo{};
			pspInfo.name = "PSP";
			pspInfo.type = FILETYPE_DIRECTORY;
			pspInfo.size = 4096;
			pspInfo.access = 0x777;
			pspInfo.exists = true;
			myVector.push_back(pspInfo);
		}
	}

	if (exists)
		*exists = true;
	return ReplayApplyDiskListing(myVector, CoreTiming::GetGlobalTimeUs());
}

u64 DirectoryFileSystem::FreeDiskSpace(const std::string &path) {
	int64_t result = 0;
	if (free_disk_space(GetLocalPath(path), result)) {
		return ReplayApplyDisk64(ReplayAction::FREESPACE, (uint64_t)result, CoreTiming::GetGlobalTimeUs());
	}

	if (flags & FileSystemFlags::CASE_SENSITIVE) {
		std::string fixedCase = path;
		if (FixPathCase(basePath, fixedCase, FPC_FILE_MUST_EXIST)) {
			// May have failed due to case sensitivity, try again.
			if (free_disk_space(GetLocalPath(fixedCase), result)) {
				return ReplayApplyDisk64(ReplayAction::FREESPACE, result, CoreTiming::GetGlobalTimeUs());
			}
		}
	}

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
	Do(p, num);

	if (p.mode == p.MODE_READ) {
		CloseAll();
		u32 key;
		OpenFileEntry entry;
		entry.hFile.fileSystemFlags_ = flags;
		for (u32 i = 0; i < num; i++) {
			Do(p, key);
			Do(p, entry.guestFilename);
			Do(p, entry.access);
			u32 err;
			bool brokenFile = false;
			if (!entry.hFile.Open(basePath,entry.guestFilename,entry.access, err)) {
				ERROR_LOG(Log::FileSystem, "Failed to reopen file while loading state: %s", entry.guestFilename.c_str());
				brokenFile = true;
			}
			u32 position;
			Do(p, position);
			if (position != entry.hFile.Seek(position, FILEMOVE_BEGIN)) {
				ERROR_LOG(Log::FileSystem, "Failed to restore seek position while loading state: %s", entry.guestFilename.c_str());
				brokenFile = true;
			}
			if (s >= 2) {
				Do(p, entry.hFile.needsTrunc_);
			}
			// Let's hope that things don't go that badly with the file mysteriously auto-closed.
			// Better than not loading the save state at all, hopefully.
			if (!brokenFile) {
				entries[key] = entry;
			}
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



VFSFileSystem::VFSFileSystem(IHandleAllocator *_hAlloc, std::string _basePath) : basePath(_basePath) {
	hAlloc = _hAlloc;
}

VFSFileSystem::~VFSFileSystem() {
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		delete [] iter->second.fileData;
	}
	entries.clear();
}

std::string VFSFileSystem::GetLocalPath(std::string_view localPath) const {
	return join(basePath, localPath);
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

int VFSFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	if (access != FILEACCESS_READ) {
		ERROR_LOG(Log::FileSystem, "VFSFileSystem only supports plain reading");
		return SCE_KERNEL_ERROR_ERRNO_INVALID_FLAG;
	}

	std::string fullName = GetLocalPath(filename);
	const char *fullNameC = fullName.c_str();
	VERBOSE_LOG(Log::FileSystem, "VFSFileSystem actually opening %s (%s)", fullNameC, filename.c_str());

	size_t size;
	u8 *data = g_VFS.ReadFile(fullNameC, &size);
	if (!data) {
		ERROR_LOG(Log::FileSystem, "VFSFileSystem failed to open %s", filename.c_str());
		return SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
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
	File::FileInfo fo;
	if (g_VFS.GetFileInfo(fullName.c_str(), &fo)) {
		x.exists = fo.exists;
		if (x.exists) {
			x.size = fo.size;
			x.type = fo.isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
			x.access = fo.isWritable ? 0666 : 0444;
		}
	} else {
		x.exists = false;
	}
	return x;
}

PSPFileInfo VFSFileSystem::GetFileInfoByHandle(u32 handle) {
	return PSPFileInfo();
}


void VFSFileSystem::CloseFile(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end()) {
		delete [] iter->second.fileData;
		entries.erase(iter);
	} else {
		//This shouldn't happen...
		ERROR_LOG(Log::FileSystem,"Cannot close file that hasn't been opened: %08x", handle);
	}
}

bool VFSFileSystem::OwnsHandle(u32 handle) {
	EntryMap::iterator iter = entries.find(handle);
	return (iter != entries.end());
}

int VFSFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
}

PSPDevType VFSFileSystem::DevType(u32 handle) {
	return PSPDevType::FILE;
}

size_t VFSFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t VFSFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	DEBUG_LOG(Log::FileSystem,"VFSFileSystem::ReadFile %08x %p %i", handle, pointer, (u32)size);
	EntryMap::iterator iter = entries.find(handle);
	if (iter != entries.end())
	{
		if(iter->second.seekPos + size > iter->second.size)
			size = iter->second.size - iter->second.seekPos;
		if(size < 0) size = 0;
		size_t bytesRead = size;
		memcpy(pointer, iter->second.fileData + iter->second.seekPos, size);
		iter->second.seekPos += size;
		return bytesRead;
	} else {
		ERROR_LOG(Log::FileSystem,"Cannot read file that hasn't been opened: %08x", handle);
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
		ERROR_LOG(Log::FileSystem,"Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
}

std::vector<PSPFileInfo> VFSFileSystem::GetDirListing(const std::string &path, bool *exists) {
	std::vector<PSPFileInfo> myVector;
	// TODO
	if (exists)
		*exists = false;
	return myVector;
}

void VFSFileSystem::DoState(PointerWrap &p) {
	// Note: used interchangeably with DirectoryFileSystem for flash0:, so we use the same name.
	auto s = p.Section("DirectoryFileSystem", 0, 2);
	if (!s)
		return;

	u32 num = (u32) entries.size();
	Do(p, num);

	if (num != 0) {
		p.SetError(p.ERROR_WARNING);
		ERROR_LOG(Log::FileSystem, "FIXME: Open files during savestate, could go badly.");
	}
}
