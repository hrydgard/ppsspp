// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#if defined(_MSC_VER)
#pragma warning(disable:4091)  // workaround bug in VS2015 headers
#ifndef UNICODE
#error Win32 build requires a unicode build
#endif
#else
#define _POSIX_SOURCE
#define _LARGE_TIME_API
#endif

#include "ppsspp_config.h"

#include "android/jni/app-android.h"

#include <cstring>
#include <ctime>
#include <memory>

#include <sys/types.h>

#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "Common/File/AndroidContentURI.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Common/SysError.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <sys/utime.h>
#include <shlobj.h>		// for SHGetFolderPath
#include <shellapi.h>
#include <commdlg.h>	// for GetSaveFileName
#include <io.h>
#include <direct.h>		// getcwd
#if PPSSPP_PLATFORM(UWP)
#include <fileapifromapp.h>
#include "UWP/UWPHelpers/StorageManager.h"
#endif
#else
#include <sys/param.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>
#endif

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__)
#include <sys/sysctl.h>		// KERN_PROC_PATHNAME
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFURL.h>
#include <CoreFoundation/CFBundle.h>
#if !PPSSPP_PLATFORM(IOS)
#include <mach-o/dyld.h>
#endif  // !PPSSPP_PLATFORM(IOS)
#endif  // __APPLE__

#include "Common/Data/Encoding/Utf8.h"

#include <sys/stat.h>

// NOTE: There's another one in DirListing.cpp.
#ifdef _WIN32
constexpr bool SIMULATE_SLOW_IO = false;
#else
constexpr bool SIMULATE_SLOW_IO = false;
#endif
constexpr bool LOG_IO = false;

#ifndef S_ISDIR
#define S_ISDIR(m)  (((m)&S_IFMT) == S_IFDIR)
#endif

#if !defined(__linux__) && !defined(_WIN32) && !defined(__QNX__)
#define stat64 stat
#define fstat64 fstat
#endif

#define DIR_SEP "/"
#ifdef _WIN32
#define DIR_SEP_CHRS "/\\"
#else
#define DIR_SEP_CHRS "/"
#endif

// This namespace has various generic functions related to files and paths.
// The code still needs a ton of cleanup.
// REMEMBER: strdup considered harmful!
namespace File {

FILE *OpenCFile(const Path &path, const char *mode) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "OpenCFile %s, %s", path.c_str(), mode);
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(300, "slow-io-sim");
	}
	switch (path.Type()) {
	case PathType::NATIVE:
		break;
	case PathType::CONTENT_URI:
		// We're gonna need some error codes..
		if (!strcmp(mode, "r") || !strcmp(mode, "rb") || !strcmp(mode, "rt")) {
			INFO_LOG(Log::Common, "Opening content file for read: '%s'", path.c_str());
			// Read, let's support this - easy one.
			int descriptor = Android_OpenContentUriFd(path.ToString(), Android_OpenContentUriMode::READ);
			if (descriptor < 0) {
				return nullptr;
			}
			return fdopen(descriptor, "rb");
		} else if (!strcmp(mode, "w") || !strcmp(mode, "wb") || !strcmp(mode, "wt") || !strcmp(mode, "at") || !strcmp(mode, "a")) {
			// Need to be able to create the file here if it doesn't exist.
			// Not exactly sure which abstractions are best, let's start simple.
			if (!File::Exists(path)) {
				INFO_LOG(Log::Common, "OpenCFile(%s): Opening content file for write. Doesn't exist, creating empty and reopening.", path.c_str());
				std::string name = path.GetFilename();
				if (path.CanNavigateUp()) {
					Path parent = path.NavigateUp();
					if (Android_CreateFile(parent.ToString(), name) != StorageError::SUCCESS) {
						WARN_LOG(Log::Common, "Failed to create file '%s' in '%s'", name.c_str(), parent.c_str());
						return nullptr;
					}
				} else {
					INFO_LOG_REPORT_ONCE(openCFileFailedNavigateUp, Log::Common, "Failed to navigate up to create file: %s", path.c_str());
					return nullptr;
				}
			} else {
				INFO_LOG(Log::Common, "OpenCFile(%s): Opening existing content file for write (truncating). Requested mode: '%s'", path.c_str(), mode);
			}

			// TODO: Support append modes and stuff... For now let's go with the most common one.
			Android_OpenContentUriMode openMode = Android_OpenContentUriMode::READ_WRITE_TRUNCATE;
			const char *fmode = "wb";
			if (!strcmp(mode, "at") || !strcmp(mode, "a")) {
				openMode = Android_OpenContentUriMode::READ_WRITE;
				fmode = "ab";
			}
			int descriptor = Android_OpenContentUriFd(path.ToString(), openMode);
			if (descriptor < 0) {
				INFO_LOG(Log::Common, "Opening '%s' for write failed", path.ToString().c_str());
				return nullptr;
			}
			FILE *f = fdopen(descriptor, fmode);
			if (f && (!strcmp(mode, "at") || !strcmp(mode, "a"))) {
				// Append mode - not sure we got a "true" append mode, so seek to the end.
				fseek(f, 0, SEEK_END);
			}
			return f;
		} else {
			ERROR_LOG(Log::Common, "OpenCFile(%s): Mode not yet supported: %s", path.c_str(), mode);
			return nullptr;
		}
		break;
	default:
		ERROR_LOG(Log::Common, "OpenCFile(%s): PathType not yet supported", path.c_str());
		return nullptr;
	}

#if defined(_WIN32) && defined(UNICODE)
#if PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
	// We shouldn't use _wfopen here,
	// this function is not allowed to read outside Local and Installation folders
	// FileSystem (broadFileSystemAccess) doesn't apply on _wfopen
	// if we have custom memory stick location _wfopen will return null
	// 'GetFileStreamFromApp' will convert 'mode' to [access, share, creationDisposition]
	// then it will call 'CreateFile2FromAppW' -> convert HANDLE to FILE*
	FILE* file = GetFileStreamFromApp(path.ToString(), mode);
	return file;
#else
	return _wfopen(path.ToWString().c_str(), ConvertUTF8ToWString(mode).c_str());
#endif
#else
	return fopen(path.c_str(), mode);
#endif
}

static std::string OpenFlagToString(OpenFlag flags) {
	std::string s;
	if (flags & OPEN_READ)
		s += "READ|";
	if (flags & OPEN_WRITE)
		s += "WRITE|";
	if (flags & OPEN_APPEND)
		s += "APPEND|";
	if (flags & OPEN_CREATE)
		s += "CREATE|";
	if (flags & OPEN_TRUNCATE)
		s += "TRUNCATE|";
	if (!s.empty()) {
		s.pop_back();  // Remove trailing separator.
	}
	return s;
}

int OpenFD(const Path &path, OpenFlag flags) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "OpenFD %s, %d", path.c_str(), flags);
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(300, "slow-io-sim");
	}

	switch (path.Type()) {
	case PathType::CONTENT_URI:
		break;
	default:
		ERROR_LOG(Log::Common, "OpenFD: Only supports Content URI paths. Not '%s' (%s)!", path.c_str(), OpenFlagToString(flags).c_str());
		// Not yet supported - use other paths.
		return -1;
	}

	if (flags & OPEN_CREATE) {
		if (!File::Exists(path)) {
			INFO_LOG(Log::Common, "OpenFD(%s): Creating file.", path.c_str());
			std::string name = path.GetFilename();
			if (path.CanNavigateUp()) {
				Path parent = path.NavigateUp();
				if (Android_CreateFile(parent.ToString(), name) != StorageError::SUCCESS) {
					WARN_LOG(Log::Common, "OpenFD: Failed to create file '%s' in '%s'", name.c_str(), parent.c_str());
					return -1;
				}
			} else {
				INFO_LOG(Log::Common, "Failed to navigate up to create file: %s", path.c_str());
				return -1;
			}
		} else {
			INFO_LOG(Log::Common, "OpenCFile(%s): Opening existing content file ('%s')", path.c_str(), OpenFlagToString(flags).c_str());
		}
	}

	Android_OpenContentUriMode mode;
	if (flags == OPEN_READ) {
		mode = Android_OpenContentUriMode::READ;
	} else if (flags & OPEN_WRITE) {
		if (flags & OPEN_TRUNCATE) {
			mode = Android_OpenContentUriMode::READ_WRITE_TRUNCATE;
		} else {
			mode = Android_OpenContentUriMode::READ_WRITE;
		}
		// TODO: Maybe better checking of additional flags here.
	} else {
		// TODO: Add support for more modes if possible.
		ERROR_LOG_REPORT_ONCE(openFlagNotSupported, Log::Common, "OpenFlag %s not yet supported", OpenFlagToString(flags).c_str());
		return -1;
	}

	INFO_LOG(Log::Common, "Android_OpenContentUriFd: %s (%s)", path.c_str(), OpenFlagToString(flags).c_str());
	int descriptor = Android_OpenContentUriFd(path.ToString(), mode);
	if (descriptor < 0) {
		ERROR_LOG(Log::Common, "Android_OpenContentUriFd failed: '%s'", path.c_str());
	}

	if (flags & OPEN_APPEND) {
		// Simply seek to the end of the file to simulate append mode.
		lseek(descriptor, 0, SEEK_END);
	}

	return descriptor;
}

#ifdef _WIN32
static bool ResolvePathVista(const std::wstring &path, wchar_t *buf, DWORD bufSize) {
	typedef DWORD(WINAPI *getFinalPathNameByHandleW_f)(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags);
	static getFinalPathNameByHandleW_f getFinalPathNameByHandleW = nullptr;

#if PPSSPP_PLATFORM(UWP)
	getFinalPathNameByHandleW = &GetFinalPathNameByHandleW;
#else
	if (!getFinalPathNameByHandleW) {
		HMODULE kernel32 = GetModuleHandle(L"kernel32.dll");
		if (kernel32)
			getFinalPathNameByHandleW = (getFinalPathNameByHandleW_f)GetProcAddress(kernel32, "GetFinalPathNameByHandleW");
	}
#endif

	if (getFinalPathNameByHandleW) {
#if PPSSPP_PLATFORM(UWP)
		HANDLE hFile = CreateFile2FromAppW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
#else
		HANDLE hFile = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
#endif
		if (hFile == INVALID_HANDLE_VALUE)
			return false;

		DWORD result = getFinalPathNameByHandleW(hFile, buf, bufSize - 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		CloseHandle(hFile);

		return result < bufSize && result != 0;
	}

	return false;
}
#endif

std::string ResolvePath(const std::string &path) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "ResolvePath %s", path.c_str());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(100, "slow-io-sim");
	}

	if (startsWith(path, "http://") || startsWith(path, "https://")) {
		return path;
	}

	if (Android_IsContentUri(path)) {
		// Nothing to do?
		return path;
	}

#ifdef _WIN32
	// Network paths. Ignore document aliases and stuff.
	if (startsWith(path, "//")) {
		return path;
	}

	static const int BUF_SIZE = 32768;
	wchar_t *buf = new wchar_t[BUF_SIZE] {};

	std::wstring input = ConvertUTF8ToWString(path);
	// Try to resolve symlinks (such as Documents aliases, etc.) if possible on Vista and higher.
	// For some paths and remote shares, this may fail, so fall back.
	if (!ResolvePathVista(input, buf, BUF_SIZE)) {
		wchar_t *longBuf = new wchar_t[BUF_SIZE] {};

		int result = GetLongPathNameW(input.c_str(), longBuf, BUF_SIZE - 1);
		if (result >= BUF_SIZE || result == 0)
			wcscpy_s(longBuf, BUF_SIZE - 1, input.c_str());

		result = GetFullPathNameW(longBuf, BUF_SIZE - 1, buf, nullptr);
		if (result >= BUF_SIZE || result == 0)
			wcscpy_s(buf, BUF_SIZE - 1, input.c_str());

		delete [] longBuf;
	}

	// Normalize slashes just in case.
	for (int i = 0; i < BUF_SIZE; ++i) {
		if (buf[i] == '\\')
			buf[i] = '/';
		else if (buf[i] == '\0')
			break;
	}

	// Undo the \\?\C:\ syntax that's normally returned (after normalization of slashes.)
	std::string output = ConvertWStringToUTF8(buf);
	if (buf[0] == '/' && buf[1] == '/' && buf[2] == '?' && buf[3] == '/' && isalpha(buf[4]) && buf[5] == ':')
		output = output.substr(4);
	delete [] buf;
	return output;

#elif PPSSPP_PLATFORM(IOS)
	// Resolving has wacky effects on documents paths.
	return path;
#else
	std::unique_ptr<char[]> buf(new char[PATH_MAX + 32768]);
	if (realpath(path.c_str(), buf.get()) == nullptr)
		return path;
	return buf.get();
#endif
}

static int64_t RecursiveSize(const Path &path) {
	// TODO: Some file systems can optimize this.
	std::vector<FileInfo> fileInfo;
	if (!GetFilesInDir(path, &fileInfo, nullptr, GETFILES_GETHIDDEN)) {
		return -1;
	}
	int64_t sizeSum = 0;
	for (const auto &file : fileInfo) {
		if (file.isDirectory) {
			sizeSum += RecursiveSize(file.fullName);
		} else {
			sizeSum += file.size;
		}
	}
	return sizeSum;
}

uint64_t ComputeRecursiveDirectorySize(const Path &path) {
	if (path.Type() == PathType::CONTENT_URI) {
		return Android_ComputeRecursiveDirectorySize(path.ToString());
	}

	// Generic solution.
	return RecursiveSize(path);
}

// Returns true if file filename exists. Will return true on directories.
bool ExistsInDir(const Path &path, const std::string &filename) {
	return Exists(path / filename);
}

bool Exists(const Path &path) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "Exists %s", path.ToVisualString().c_str());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(200, "slow-io-sim");
	}

	if (path.Type() == PathType::CONTENT_URI) {
		return Android_FileExists(path.c_str());
	}

#if defined(_WIN32)

	// Make sure Windows will no longer handle critical errors, which means no annoying "No disk" dialog
#if !PPSSPP_PLATFORM(UWP)
	int OldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
#endif
	WIN32_FILE_ATTRIBUTE_DATA data{};
#if PPSSPP_PLATFORM(UWP)
	if (!GetFileAttributesExFromAppW(path.ToWString().c_str(), GetFileExInfoStandard, &data) || data.dwFileAttributes == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
#else
	if (!GetFileAttributesEx(path.ToWString().c_str(), GetFileExInfoStandard, &data) || data.dwFileAttributes == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
#endif
#if !PPSSPP_PLATFORM(UWP)
	SetErrorMode(OldMode);
#endif
	return true;
#else  // !WIN32
	struct stat file_info{};
	return stat(path.c_str(), &file_info) == 0;
#endif
}

// Returns true if filename exists and is a directory
bool IsDirectory(const Path &path) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "IsDirectory %s", path.c_str());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(100, "slow-io-sim");
	}

	switch (path.Type()) {
	case PathType::NATIVE:
		break; // OK
	case PathType::CONTENT_URI:
	{
		FileInfo info;
		if (!Android_GetFileInfo(path.ToString(), &info)) {
			return false;
		}
		return info.exists && info.isDirectory;
	}
	default:
		return false;
	}

#if defined(_WIN32)
	WIN32_FILE_ATTRIBUTE_DATA data{};
#if PPSSPP_PLATFORM(UWP)
	if (!GetFileAttributesExFromAppW(path.ToWString().c_str(), GetFileExInfoStandard, &data) || data.dwFileAttributes == INVALID_FILE_ATTRIBUTES) {
#else
	if (!GetFileAttributesEx(path.ToWString().c_str(), GetFileExInfoStandard, &data) || data.dwFileAttributes == INVALID_FILE_ATTRIBUTES) {
#endif
		auto err = GetLastError();
		if (err != ERROR_FILE_NOT_FOUND) {
			WARN_LOG(Log::Common, "GetFileAttributes failed on %s: %08x %s", path.ToVisualString().c_str(), (uint32_t)err, GetStringErrorMsg(err).c_str());
		}
		return false;
	}
	DWORD result = data.dwFileAttributes;
	return (result & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
#else
	std::string copy = path.ToString();
	struct stat file_info{};
	int result = stat(copy.c_str(), &file_info);
	if (result < 0) {
		WARN_LOG(Log::Common, "IsDirectory: stat failed on %s: %s", copy.c_str(), GetLastErrorMsg().c_str());
		return false;
	}
	return S_ISDIR(file_info.st_mode);
#endif
}

// Deletes a given filename, return true on success
// Doesn't supports deleting a directory
bool Delete(const Path &filename) {
	if (SIMULATE_SLOW_IO) {
		sleep_ms(200, "slow-io-sim");
	}
	switch (filename.Type()) {
	case PathType::NATIVE:
		break; // OK
	case PathType::CONTENT_URI:
		return Android_RemoveFile(filename.ToString()) == StorageError::SUCCESS;
	default:
		return false;
	}

	INFO_LOG(Log::Common, "Delete: file %s", filename.c_str());

	// Return true because we care about the file no
	// being there, not the actual delete.
	if (!Exists(filename)) {
		WARN_LOG(Log::Common, "Delete: '%s' already does not exist", filename.c_str());
		return true;
	}

	// We can't delete a directory
	if (IsDirectory(filename)) {
		WARN_LOG(Log::Common, "Delete failed: '%s' is a directory", filename.c_str());
		return false;
	}

#ifdef _WIN32
#if PPSSPP_PLATFORM(UWP)
	if (!DeleteFileFromAppW(filename.ToWString().c_str())) {
		WARN_LOG(Log::Common, "Delete: DeleteFile failed on %s: %s", filename.c_str(), GetLastErrorMsg().c_str());
		return false;
	}
#else
	if (!DeleteFile(filename.ToWString().c_str())) {
		WARN_LOG(Log::Common, "Delete: DeleteFile failed on %s: %s", filename.c_str(), GetLastErrorMsg().c_str());
		return false;
	}
#endif
#else
	if (unlink(filename.c_str()) == -1) {
		WARN_LOG(Log::Common, "Delete: unlink failed on %s: %s",
				 filename.c_str(), GetLastErrorMsg().c_str());
		return false;
	}
#endif

	return true;
}

// Returns true if successful, or path already exists.
bool CreateDir(const Path &path) {
	if (SIMULATE_SLOW_IO) {
		sleep_ms(100, "slow-io-sim");
		INFO_LOG(Log::System, "CreateDir %s", path.c_str());
	}
	switch (path.Type()) {
	case PathType::NATIVE:
		break; // OK
	case PathType::CONTENT_URI:
	{
		// NOTE: The Android storage API will simply create a renamed directory (append a number) if it already exists.
		// We want to avoid that, so let's just return true if the directory already is there.
		if (File::Exists(path)) {
			return true;
		}

		// Convert it to a "CreateDirIn" call, if possible, since that's
		// what we can do with the storage API.
		AndroidContentURI uri(path.ToString());
		std::string newDirName = uri.GetLastPart();
		if (uri.NavigateUp()) {
			INFO_LOG(Log::Common, "Calling Android_CreateDirectory(%s, %s)", uri.ToString().c_str(), newDirName.c_str());
			return Android_CreateDirectory(uri.ToString(), newDirName) == StorageError::SUCCESS;
		} else {
			// Bad path - can't create this directory.
			WARN_LOG(Log::Common, "CreateDir failed: '%s'", path.c_str());
			return false;
		}
		break;
	}
	default:
		return false;
	}

	DEBUG_LOG(Log::Common, "CreateDir('%s')", path.c_str());
#ifdef _WIN32
#if PPSSPP_PLATFORM(UWP)
	if (CreateDirectoryFromAppW(path.ToWString().c_str(), NULL))
		return true;
#else
	if (::CreateDirectory(path.ToWString().c_str(), NULL))
		return true;
#endif

	DWORD error = GetLastError();
	if (error == ERROR_ALREADY_EXISTS) {
		DEBUG_LOG(Log::Common, "CreateDir: CreateDirectory failed on %s: already exists", path.c_str());
		return true;
	}
	ERROR_LOG(Log::Common, "CreateDir: CreateDirectory failed on %s: %08x %s", path.c_str(), (uint32_t)error, GetStringErrorMsg(error).c_str());
	return false;
#else
	if (mkdir(path.ToString().c_str(), 0755) == 0) {
		return true;
	}

	int err = errno;
	if (err == EEXIST) {
		DEBUG_LOG(Log::Common, "CreateDir: mkdir failed on %s: already exists", path.c_str());
		return true;
	}

	ERROR_LOG(Log::Common, "CreateDir: mkdir failed on %s: %s", path.c_str(), strerror(err));
	return false;
#endif
}

// Creates the full path of fullPath returns true on success
bool CreateFullPath(const Path &path) {
	if (File::Exists(path)) {
		DEBUG_LOG(Log::Common, "CreateFullPath: path exists %s", path.ToVisualString().c_str());
		return true;
	}

	switch (path.Type()) {
	case PathType::NATIVE:
	case PathType::CONTENT_URI:
		break; // OK
	default:
		ERROR_LOG(Log::Common, "CreateFullPath(%s): Not yet supported", path.ToVisualString().c_str());
		return false;
	}

	// The below code is entirely agnostic of path format.

	Path root = path.GetRootVolume();

	std::string diff;
	if (!root.ComputePathTo(path, diff)) {
		return false;
	}

	std::vector<std::string_view> parts;
	SplitString(diff, '/', parts);

	// Probably not necessary sanity check, ported from the old code.
	if (parts.size() > 100) {
		ERROR_LOG(Log::Common, "CreateFullPath: directory structure too deep");
		return false;
	}

	Path curPath = root;
	for (auto part : parts) {
		curPath /= part;
		File::CreateDir(curPath);
	}

	return true;
}

// renames file srcFilename to destFilename, returns true on success
bool Rename(const Path &srcFilename, const Path &destFilename) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "Rename %s -> %s", srcFilename.c_str(), destFilename.c_str());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(100, "slow-io-sim");
	}

	if (srcFilename.Type() != destFilename.Type()) {
		// Impossible. You're gonna need to make a copy, and delete the original. Not the responsibility
		// of Rename.
		return false;
	}

	// We've already asserted that they're the same Type, so only need to check either src or dest.
	switch (srcFilename.Type()) {
	case PathType::NATIVE:
		// OK, proceed with the regular code.
		break;
	case PathType::CONTENT_URI:
		// Content URI: Can only rename if in the same folder.
		// TODO: Fallback to move + rename? Or do we even care about that use case? We have MoveIfFast for such tricks.
		if (srcFilename.GetDirectory() != destFilename.GetDirectory()) {
			INFO_LOG(Log::Common, "Content URI rename: Directories not matching, failing. %s --> %s", srcFilename.c_str(), destFilename.c_str());
			return false;
		}
		INFO_LOG(Log::Common, "Content URI rename: %s --> %s", srcFilename.c_str(), destFilename.c_str());
		return Android_RenameFileTo(srcFilename.ToString(), destFilename.GetFilename()) == StorageError::SUCCESS;
	default:
		return false;
	}

	INFO_LOG(Log::Common, "Rename: %s --> %s", srcFilename.c_str(), destFilename.c_str());

#if defined(_WIN32) && defined(UNICODE)
#if PPSSPP_PLATFORM(UWP)
	if (MoveFileFromAppW(srcFilename.ToWString().c_str(), destFilename.ToWString().c_str()))
		return true;
#else
	std::wstring srcw = srcFilename.ToWString();
	std::wstring destw = destFilename.ToWString();
	if (_wrename(srcw.c_str(), destw.c_str()) == 0)
		return true;
#endif
#else
	if (rename(srcFilename.c_str(), destFilename.c_str()) == 0)
		return true;
#endif

	ERROR_LOG(Log::Common, "Rename: failed %s --> %s: %s",
			  srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
	return false;
}

// copies file srcFilename to destFilename, returns true on success
bool Copy(const Path &srcFilename, const Path &destFilename) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "Copy %s -> %s", srcFilename.c_str(), destFilename.c_str());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(100, "slow-io-sim");
	}
	switch (srcFilename.Type()) {
	case PathType::NATIVE:
		break; // OK
	case PathType::CONTENT_URI:
		if (destFilename.Type() == PathType::CONTENT_URI && destFilename.CanNavigateUp()) {
			Path destParent = destFilename.NavigateUp();
			// Use native file copy.
			if (Android_CopyFile(srcFilename.ToString(), destParent.ToString()) == StorageError::SUCCESS) {
				return true;
			}
			INFO_LOG(Log::Common, "Android_CopyFile failed, falling back.");
			// Else fall through, and try using file I/O.
		}
		break;
	default:
		return false;
	}

	INFO_LOG(Log::Common, "Copy by OpenCFile: %s --> %s", srcFilename.c_str(), destFilename.c_str());
#ifdef _WIN32
#if PPSSPP_PLATFORM(UWP)
	if (CopyFileFromAppW(srcFilename.ToWString().c_str(), destFilename.ToWString().c_str(), FALSE))
		return true;
#else
	if (CopyFile(srcFilename.ToWString().c_str(), destFilename.ToWString().c_str(), FALSE))
		return true;
#endif
	ERROR_LOG(Log::Common, "Copy: failed %s --> %s: %s",
			srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
	return false;
#else  // Non-Win32

	// buffer size
#define BSIZE 16384

	char buffer[BSIZE];

	// Open input file
	FILE *input = OpenCFile(srcFilename, "rb");
	if (!input) {
		ERROR_LOG(Log::Common, "Copy: input failed %s --> %s: %s",
				srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
		return false;
	}

	// open output file
	FILE *output = OpenCFile(destFilename, "wb");
	if (!output) {
		fclose(input);
		ERROR_LOG(Log::Common, "Copy: output failed %s --> %s: %s",
				srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
		return false;
	}

	int bytesWritten = 0;

	// copy loop
	while (!feof(input)) {
		// read input
		int rnum = fread(buffer, sizeof(char), BSIZE, input);
		if (rnum != BSIZE) {
			if (ferror(input) != 0) {
				ERROR_LOG(Log::Common,
						"Copy: failed reading from source, %s --> %s: %s",
						srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
				fclose(input);
				fclose(output);
				return false;
			}
		}

		// write output
		int wnum = fwrite(buffer, sizeof(char), rnum, output);
		if (wnum != rnum) {
			ERROR_LOG(Log::Common,
					"Copy: failed writing to output, %s --> %s: %s",
					srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
			fclose(input);
			fclose(output);
			return false;
		}

		bytesWritten += wnum;
	}

	if (bytesWritten == 0) {
		WARN_LOG(Log::Common, "Copy: No bytes written (must mean that input was empty)");
	}

	// close flushes
	fclose(input);
	fclose(output);
	return true;
#endif
}

// Will overwrite the target.
bool Move(const Path &srcFilename, const Path &destFilename) {
	if (SIMULATE_SLOW_IO) {
		sleep_ms(100, "slow-io-sim");
		INFO_LOG(Log::System, "Move %s -> %s", srcFilename.c_str(), destFilename.c_str());
	}
	bool fast = MoveIfFast(srcFilename, destFilename);
	if (fast) {
		return true;
	}
	// OK, that failed, so fall back on a copy.
	if (Copy(srcFilename, destFilename)) {
		return Delete(srcFilename);
	} else {
		return false;
	}
}

bool MoveIfFast(const Path &srcFilename, const Path &destFilename) {
	if (srcFilename.Type() != destFilename.Type()) {
		// No way it's gonna work.
		return false;
	}

	// Only need to check one type here, due to the above check.
	if (srcFilename.Type() == PathType::CONTENT_URI && srcFilename.CanNavigateUp() && destFilename.CanNavigateUp()) {
		if (srcFilename.GetFilename() == destFilename.GetFilename()) {
			Path srcParent = srcFilename.NavigateUp();
			Path dstParent = destFilename.NavigateUp();
			return Android_MoveFile(srcFilename.ToString(), srcParent.ToString(), dstParent.ToString()) == StorageError::SUCCESS;
			// If failed, fall through and try other ways.
		} else {
			// We do not handle simultaneous renames here.
			return false;
		}
	}

	// Try a traditional rename operation.
	return Rename(srcFilename, destFilename);
}

// Returns the size of file (64bit)
// TODO: Add a way to return an error.
uint64_t GetFileSize(const Path &filename) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "GetFileSize %s", filename.c_str());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(100, "slow-io-sim");
	}
	switch (filename.Type()) {
	case PathType::NATIVE:
		break; // OK
	case PathType::CONTENT_URI:
		{
			FileInfo info;
			if (Android_GetFileInfo(filename.ToString(), &info)) {
				return info.size;
			} else {
				return 0;
			}
		}
		break;
	default:
		return false;
	}

#if defined(_WIN32) && defined(UNICODE)
	WIN32_FILE_ATTRIBUTE_DATA attr;
#if PPSSPP_PLATFORM(UWP)
	if (!GetFileAttributesExFromAppW(filename.ToWString().c_str(), GetFileExInfoStandard, &attr))
#else
	if (!GetFileAttributesEx(filename.ToWString().c_str(), GetFileExInfoStandard, &attr))
#endif
		return 0;
	if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		return 0;
	return ((uint64_t)attr.nFileSizeHigh << 32) | (uint64_t)attr.nFileSizeLow;
#else
#if __ANDROID__ && __ANDROID_API__ < 21
	struct stat file_info;
	int result = stat(filename.c_str(), &file_info);
#else
	struct stat64 file_info;
	int result = stat64(filename.c_str(), &file_info);
#endif
	if (result != 0) {
		WARN_LOG(Log::Common, "GetSize: failed %s: No such file", filename.ToVisualString().c_str());
		return 0;
	}
	if (S_ISDIR(file_info.st_mode)) {
		WARN_LOG(Log::Common, "GetSize: failed %s: is a directory", filename.ToVisualString().c_str());
		return 0;
	}
	DEBUG_LOG(Log::Common, "GetSize: %s: %lld", filename.ToVisualString().c_str(), (long long)file_info.st_size);
	return file_info.st_size;
#endif
}

uint64_t GetFileSize(FILE *f) {
	// This will only support 64-bit when large file support is available.
	// That won't be the case on some versions of Android, at least.
#if defined(__ANDROID__) || (defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64)
	int fd = fileno(f);

	off64_t pos = lseek64(fd, 0, SEEK_CUR);
	off64_t size = lseek64(fd, 0, SEEK_END);
	if (size != pos && lseek64(fd, pos, SEEK_SET) != pos) {
		// Should error here.
		return 0;
	}
	if (size == -1)
		return 0;
	return size;
#else
#ifdef _WIN32
	uint64_t pos = _ftelli64(f);
#else
	uint64_t pos = ftello(f);
#endif
	if (fseek(f, 0, SEEK_END) != 0) {
		return 0;
	}
#ifdef _WIN32
	uint64_t size = _ftelli64(f);
	// Reset the seek position to where it was when we started.
	if (size != pos && _fseeki64(f, pos, SEEK_SET) != 0) {
#else
	uint64_t size = ftello(f);
	// Reset the seek position to where it was when we started.
	if (size != pos && fseeko(f, pos, SEEK_SET) != 0) {
#endif
		// Should error here.
		return 0;
	}
	if (size == -1)
		return 0;
	return size;
#endif
}

// creates an empty file filename, returns true on success
bool CreateEmptyFile(const Path &filename) {
	INFO_LOG(Log::Common, "CreateEmptyFile: %s", filename.c_str());
	FILE *pFile = OpenCFile(filename, "wb");
	if (!pFile) {
		ERROR_LOG(Log::Common, "CreateEmptyFile: failed to create '%s': %s", filename.c_str(), GetLastErrorMsg().c_str());
		return false;
	}
	fclose(pFile);
	return true;
}

// Deletes an empty directory, returns true on success
// WARNING: On Android with content URIs, it will delete recursively!
bool DeleteDir(const Path &path) {
	if (LOG_IO) {
		INFO_LOG(Log::System, "DeleteDir %s", path.c_str());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(100, "slow-io-sim");
	}
	switch (path.Type()) {
	case PathType::NATIVE:
		break; // OK
	case PathType::CONTENT_URI:
		return Android_RemoveFile(path.ToString()) == StorageError::SUCCESS;
	default:
		return false;
	}
	INFO_LOG(Log::Common, "DeleteDir: directory %s", path.c_str());

	// check if a directory
	if (!File::IsDirectory(path)) {
		ERROR_LOG(Log::Common, "DeleteDir: Not a directory %s", path.c_str());
		return false;
	}

#ifdef _WIN32
#if PPSSPP_PLATFORM(UWP)
	if (RemoveDirectoryFromAppW(path.ToWString().c_str()))
		return true;
#else
	if (::RemoveDirectory(path.ToWString().c_str()))
		return true;
#endif
#else
	if (rmdir(path.c_str()) == 0)
		return true;
#endif
	ERROR_LOG(Log::Common, "DeleteDir: %s: %s", path.c_str(), GetLastErrorMsg().c_str());

	return false;
}

// Deletes the given directory and anything under it. Returns true on success.
bool DeleteDirRecursively(const Path &path) {
	switch (path.Type()) {
	case PathType::NATIVE:
		break;
	case PathType::CONTENT_URI:
		// We make use of the dangerous auto-recursive property of Android_RemoveFile.
		return Android_RemoveFile(path.ToString()) == StorageError::SUCCESS;
	default:
		ERROR_LOG(Log::Common, "DeleteDirRecursively: Path type not supported");
		return false;
	}

	std::vector<FileInfo> files;
	GetFilesInDir(path, &files, nullptr, GETFILES_GETHIDDEN);
	for (const auto &file : files) {
		if (file.isDirectory) {
			DeleteDirRecursively(file.fullName);
		} else {
			Delete(file.fullName);
		}
	}
	return DeleteDir(path);
}

bool OpenFileInEditor(const Path &fileName) {
	switch (fileName.Type()) {
	case PathType::NATIVE:
		break;  // OK
	default:
		ERROR_LOG(Log::Common, "OpenFileInEditor(%s): Path type not supported", fileName.c_str());
		return false;
	}

#if PPSSPP_PLATFORM(WINDOWS)
#if PPSSPP_PLATFORM(UWP)
	OpenFile(fileName.ToString());
#else
	ShellExecuteW(nullptr, L"open", fileName.ToWString().c_str(), nullptr, nullptr, SW_SHOW);
#endif
#elif !defined(MOBILE_DEVICE)
	std::string iniFile;
#if defined(__APPLE__)
	iniFile = "open ";
#else
	iniFile = "xdg-open ";
#endif
	iniFile.append(fileName.ToString());
	NOTICE_LOG(Log::Boot, "Launching %s", iniFile.c_str());
	int retval = system(iniFile.c_str());
	if (retval != 0) {
		ERROR_LOG(Log::Common, "Failed to launch ini file");
	}
#endif
	return true;
}

const Path GetCurDirectory() {
#ifdef _WIN32
	wchar_t buffer[4096];
	size_t len = GetCurrentDirectory(sizeof(buffer) / sizeof(wchar_t), buffer);
	std::string curDir = ConvertWStringToUTF8(buffer);
	return Path(curDir);
#else
	char temp[4096]{};
	getcwd(temp, 4096);
	return Path(temp);
#endif
}

const Path &GetExeDirectory() {
	static Path ExePath;

	if (ExePath.empty()) {
#ifdef _WIN32
		std::wstring program_path;
		size_t sz;
		do {
			program_path.resize(program_path.size() + MAX_PATH);
			// On failure, this will return the same value as passed in, but success will always be one lower.
			sz = GetModuleFileNameW(nullptr, &program_path[0], (DWORD)program_path.size());
		} while (sz >= program_path.size());

		const wchar_t *last_slash = wcsrchr(&program_path[0], '\\');
		if (last_slash != nullptr)
			program_path.resize(last_slash - &program_path[0] + 1);
		else
			program_path.resize(sz);
		ExePath = Path(program_path);

#elif (defined(__APPLE__) && !PPSSPP_PLATFORM(IOS)) || defined(__linux__) || defined(KERN_PROC_PATHNAME)
		char program_path[4096]{};
		uint32_t program_path_size = sizeof(program_path) - 1;

#if defined(__linux__)
		if (readlink("/proc/self/exe", program_path, program_path_size) > 0)
#elif defined(__APPLE__) && !PPSSPP_PLATFORM(IOS)
		if (_NSGetExecutablePath(program_path, &program_path_size) == 0)
#elif defined(KERN_PROC_PATHNAME)
		int mib[4] = {
			CTL_KERN,
#if defined(__NetBSD__)
			KERN_PROC_ARGS,
			-1,
			KERN_PROC_PATHNAME,
#else
			KERN_PROC,
			KERN_PROC_PATHNAME,
			-1,
#endif
		};
		size_t sz = program_path_size;

		if (sysctl(mib, 4, program_path, &sz, NULL, 0) == 0)
#else
#error Unmatched ifdef.
#endif
		{
			program_path[sizeof(program_path) - 1] = '\0';
			char *last_slash = strrchr(program_path, '/');
			if (last_slash != nullptr)
				*last_slash = '\0';
			ExePath = Path(program_path);
		}
#endif
	}

	return ExePath;
}


IOFile::IOFile(const Path &filename, const char openmode[]) {
	Open(filename, openmode);
}

IOFile::~IOFile() {
	Close();
}

bool IOFile::Open(const Path& filename, const char openmode[])
{
	Close();
	m_file = File::OpenCFile(filename, openmode);
	m_good = IsOpen();
	return m_good;
}

bool IOFile::Close()
{
	if (!IsOpen() || 0 != std::fclose(m_file))
		m_good = false;

	m_file = NULL;
	return m_good;
}

std::FILE* IOFile::ReleaseHandle()
{
	std::FILE* const ret = m_file;
	m_file = NULL;
	return ret;
}

void IOFile::SetHandle(std::FILE* file)
{
	Close();
	Clear();
	m_file = file;
}

uint64_t IOFile::GetSize()
{
	if (IsOpen())
		return File::GetFileSize(m_file);
	else
		return 0;
}

bool IOFile::Seek(int64_t off, int origin)
{
	if (!IsOpen() || 0 != fseeko(m_file, off, origin))
		m_good = false;

	return m_good;
}

uint64_t IOFile::Tell()
{
	if (IsOpen())
		return ftello(m_file);
	else
		return -1;
}

bool IOFile::Flush()
{
	if (!IsOpen() || 0 != std::fflush(m_file))
		m_good = false;

	return m_good;
}

bool IOFile::Resize(uint64_t size)
{
	if (!IsOpen() || 0 !=
#ifdef _WIN32
		// ector: _chsize sucks, not 64-bit safe
		// F|RES: changed to _chsize_s. i think it is 64-bit safe
		_chsize_s(_fileno(m_file), size)
#else
		// TODO: handle 64bit and growing
		ftruncate(fileno(m_file), size)
#endif
	)
		m_good = false;

	return m_good;
}

bool ReadFileToStringOptions(bool textFile, bool allowShort, const Path &filename, std::string *str) {
	FILE *f = File::OpenCFile(filename, textFile ? "r" : "rb");
	if (!f)
		return false;
	// Warning: some files, like in /sys/, may return a fixed size like 4096.
	size_t len = (size_t)File::GetFileSize(f);
	bool success;
	if (len == 0) {
		// Just read until we can't read anymore.
		size_t totalSize = 1024;
		size_t totalRead = 0;
		do {
			totalSize *= 2;
			str->resize(totalSize);
			totalRead += fread(&(*str)[totalRead], 1, totalSize - totalRead, f);
		} while (totalRead == totalSize);
		str->resize(totalRead);
		success = true;
	} else {
		str->resize(len);
		size_t totalRead = fread(&(*str)[0], 1, len, f);
		str->resize(totalRead);
		// Allow less, because some system files will report incorrect lengths.
		// Also, when reading text with CRLF, the read length may be shorter.
		if (textFile) {
			// totalRead doesn't take \r into account since they might be skipped in this mode.
			// So let's just ask how far the cursor got.
			totalRead = ftell(f);
		}
		success = allowShort ? (totalRead <= len) : (totalRead == len);
	}
	fclose(f);
	return success;
}

uint8_t *ReadLocalFile(const Path &filename, size_t *size) {
	FILE *file = File::OpenCFile(filename, "rb");
	if (!file) {
		*size = 0;
		return nullptr;
	}
	fseek(file, 0, SEEK_END);
	size_t f_size = ftell(file);
	if ((long)f_size < 0) {
		*size = 0;
		fclose(file);
		return nullptr;
	}
	fseek(file, 0, SEEK_SET);
	// NOTE: If you find ~10 memory leaks from here, with very varying sizes, it might be the VFPU LUTs.
	uint8_t *contents = new uint8_t[f_size + 1];
	if (fread(contents, 1, f_size, file) != f_size) {
		delete[] contents;
		contents = nullptr;
		*size = 0;
	} else {
		contents[f_size] = 0;
		*size = f_size;
	}
	fclose(file);
	return contents;
}

bool WriteStringToFile(bool text_file, const std::string &str, const Path &filename) {
	FILE *f = File::OpenCFile(filename, text_file ? "w" : "wb");
	if (!f)
		return false;
	size_t len = str.size();
	if (len != fwrite(str.data(), 1, str.size(), f))
	{
		fclose(f);
		return false;
	}
	fclose(f);
	return true;
}

bool WriteDataToFile(bool text_file, const void* data, size_t size, const Path &filename) {
	FILE *f = File::OpenCFile(filename, text_file ? "w" : "wb");
	if (!f)
		return false;
	if (size != fwrite(data, 1, size, f))
	{
		fclose(f);
		return false;
	}
	fclose(f);
	return true;
}

void ChangeMTime(const Path &path, time_t mtime) {
	if (path.Type() == PathType::CONTENT_URI) {
		// No clue what to do here. There doesn't seem to be a way.
		return;
	}

#ifdef _WIN32
	_utimbuf buf{};
	buf.actime = mtime;
	buf.modtime = mtime;
	_utime(path.c_str(), &buf);
#else
	utimbuf buf{};
	buf.actime = mtime;
	buf.modtime = mtime;
	utime(path.c_str(), &buf);
#endif
}

bool IsProbablyInDownloadsFolder(const Path &filename) {
	INFO_LOG(Log::Common, "IsProbablyInDownloadsFolder: Looking at %s (%s)...", filename.c_str(), filename.ToVisualString().c_str());
	switch (filename.Type()) {
	case PathType::CONTENT_URI:
	{
		AndroidContentURI uri(filename.ToString());
		INFO_LOG(Log::Common, "Content URI provider: %s", uri.Provider().c_str());
		if (containsNoCase(uri.Provider(), "download")) {
			// like com.android.providers.downloads.documents
			return true;
		}
		break;
	}
	default:
		break;
	}
	return filename.FilePathContainsNoCase("download");
}

}  // namespace File
