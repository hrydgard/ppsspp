#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <direct.h>
#if PPSSPP_PLATFORM(UWP)
#include <fileapifromapp.h>
#endif
#else
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#endif
#include <cstring>
#include <string>
#include <set>
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>
#include <ctype.h>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Common/Net/URL.h"
#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/File/AndroidStorage.h"

#if !defined(__linux__) && !defined(_WIN32) && !defined(__QNX__)
#define stat64 stat
#endif

#ifdef HAVE_LIBNX
// Far from optimal, but I guess it works...
#define fseeko fseek
#define ftello ftell
#define fileno
#endif // HAVE_LIBNX

namespace File {

#if PPSSPP_PLATFORM(WINDOWS)
static uint64_t FiletimeToStatTime(FILETIME ft) {
	const int windowsTickResolution = 10000000;
	const int64_t secToUnixEpoch = 11644473600LL;
	int64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	return (int64_t)(ticks / windowsTickResolution - secToUnixEpoch);
};
#endif

bool GetFileInfo(const Path &path, FileInfo * fileInfo) {
	switch (path.Type()) {
	case PathType::NATIVE:
		break;  // OK
	case PathType::CONTENT_URI:
		return Android_GetFileInfo(path.ToString(), fileInfo);
	default:
		return false;
	}

	// TODO: Expand relative paths?
	fileInfo->fullName = path;

#if PPSSPP_PLATFORM(WINDOWS)
	WIN32_FILE_ATTRIBUTE_DATA attrs;
#if PPSSPP_PLATFORM(UWP)
	if (!GetFileAttributesExFromAppW(path.ToWString().c_str(), GetFileExInfoStandard, &attrs)) {
#else
	if (!GetFileAttributesExW(path.ToWString().c_str(), GetFileExInfoStandard, &attrs)) {
#endif
		fileInfo->size = 0;
		fileInfo->isDirectory = false;
		fileInfo->exists = false;
		return false;
	}
	fileInfo->size = (uint64_t)attrs.nFileSizeLow | ((uint64_t)attrs.nFileSizeHigh << 32);
	fileInfo->isDirectory = (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	fileInfo->isWritable = (attrs.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;
	fileInfo->exists = true;
	fileInfo->atime = FiletimeToStatTime(attrs.ftLastAccessTime);
	fileInfo->mtime = FiletimeToStatTime(attrs.ftLastWriteTime);
	fileInfo->ctime = FiletimeToStatTime(attrs.ftCreationTime);
	if (attrs.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
		fileInfo->access = 0444;  // Read
	} else {
		fileInfo->access = 0666;  // Read/Write
	}
	if (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		fileInfo->access |= 0111;  // Execute
	}
#else

#if (defined __ANDROID__) && (__ANDROID_API__ < 21)
	struct stat file_info;
	int result = stat(path.c_str(), &file_info);
#else
	struct stat64 file_info;
	int result = stat64(path.c_str(), &file_info);
#endif
	if (result < 0) {
		fileInfo->exists = false;
		return false;
	}

	fileInfo->isDirectory = S_ISDIR(file_info.st_mode);
	fileInfo->isWritable = false;
	fileInfo->size = file_info.st_size;
	fileInfo->exists = true;
	fileInfo->atime = file_info.st_atime;
	fileInfo->mtime = file_info.st_mtime;
	fileInfo->ctime = file_info.st_ctime;
	fileInfo->access = file_info.st_mode & 0x1ff;
	// HACK: approximation
	if (file_info.st_mode & 0200)
		fileInfo->isWritable = true;
#endif
	return true;
}

bool GetModifTime(const Path &filename, tm & return_time) {
	memset(&return_time, 0, sizeof(return_time));
	FileInfo info;
	if (GetFileInfo(filename, &info)) {
		time_t t = info.mtime;
		localtime_r((time_t*)&t, &return_time);
		return true;
	} else {
		return false;
	}
}

bool FileInfo::operator <(const FileInfo & other) const {
	if (isDirectory && !other.isDirectory)
		return true;
	else if (!isDirectory && other.isDirectory)
		return false;
	if (strcasecmp(name.c_str(), other.name.c_str()) < 0)
		return true;
	else
		return false;
}

std::vector<File::FileInfo> ApplyFilter(std::vector<File::FileInfo> files, const char *filter) {
	std::set<std::string> filters;
	if (filter) {
		std::string tmp;
		while (*filter) {
			if (*filter == ':') {
				filters.insert("." + tmp);
				tmp.clear();
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
		if (!tmp.empty())
			filters.insert("." + tmp);
	}

	auto pred = [&](const File::FileInfo &info) {
		if (info.isDirectory || !filter)
			return false;
		std::string ext = info.fullName.GetFileExtension();
		return filters.find(ext) == filters.end();
	};
	files.erase(std::remove_if(files.begin(), files.end(), pred), files.end());
	return files;
}

bool GetFilesInDir(const Path &directory, std::vector<FileInfo> *files, const char *filter, int flags) {
	if (directory.Type() == PathType::CONTENT_URI) {
		std::vector<File::FileInfo> fileList = Android_ListContentUri(directory.ToString());
		*files = ApplyFilter(fileList, filter);
		std::sort(files->begin(), files->end());
		return true;
	}

	std::set<std::string> filters;
	if (filter) {
		std::string tmp;
		while (*filter) {
			if (*filter == ':') {
				filters.insert(std::move(tmp));
				tmp.clear();
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
		if (!tmp.empty())
			filters.insert(std::move(tmp));
	}

#if PPSSPP_PLATFORM(WINDOWS)
	if (directory.IsRoot()) {
		// Special path that means root of file system.
		std::vector<std::string> drives = File::GetWindowsDrives();
		for (auto drive = drives.begin(); drive != drives.end(); ++drive) {
			if (*drive == "A:/" || *drive == "B:/")
				continue;
			File::FileInfo fake;
			fake.fullName = Path(*drive);
			fake.name = *drive;
			fake.isDirectory = true;
			fake.exists = true;
			fake.size = 0;
			fake.isWritable = false;
			files->push_back(fake);
		}
		return files->size();
	}
	// Find the first file in the directory.
	WIN32_FIND_DATA ffd;
#if PPSSPP_PLATFORM(UWP)
	HANDLE hFind = FindFirstFileExFromAppW((directory.ToWString() + L"\\*").c_str(), FindExInfoStandard, &ffd, FindExSearchNameMatch, NULL, 0);
#else
	HANDLE hFind = FindFirstFileEx((directory.ToWString() + L"\\*").c_str(), FindExInfoStandard, &ffd, FindExSearchNameMatch, NULL, 0);
#endif
	if (hFind == INVALID_HANDLE_VALUE) {
		return 0;
	}
	do {
		const std::string virtualName = ConvertWStringToUTF8(ffd.cFileName);
		// check for "." and ".."
		if (!(flags & GETFILES_GET_NAVIGATION_ENTRIES) && (virtualName == "." || virtualName == ".."))
			continue;
		// Remove dotfiles (optional with flag.)
		if (!(flags & GETFILES_GETHIDDEN)) {
			if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0)
				continue;
		}

		FileInfo info;
		info.name = virtualName;
		info.fullName = directory / virtualName;
		info.exists = true;
		info.size = ((uint64_t)ffd.nFileSizeHigh << 32) | ffd.nFileSizeLow;
		info.isDirectory = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		info.isWritable = (ffd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;
		info.atime = FiletimeToStatTime(ffd.ftLastAccessTime);
		info.mtime = FiletimeToStatTime(ffd.ftLastWriteTime);
		info.ctime = FiletimeToStatTime(ffd.ftCreationTime);
		if (!info.isDirectory) {
			std::string ext = info.fullName.GetFileExtension();
			if (!ext.empty()) {
				ext = ext.substr(1);  // Remove the dot.
				if (filter && filters.find(ext) == filters.end()) {
					continue;
				}
			}
		}
		files->push_back(info);
	} while (FindNextFile(hFind, &ffd) != 0);
	FindClose(hFind);
#else
	struct dirent *result = NULL;
	DIR *dirp = opendir(directory.c_str());
	if (!dirp)
		return 0;
	while ((result = readdir(dirp))) {
		const std::string virtualName(result->d_name);
		// check for "." and ".."
		if (!(flags & GETFILES_GET_NAVIGATION_ENTRIES) && (virtualName == "." || virtualName == ".."))
			continue;

		// Remove dotfiles (optional with flag.)
		if (!(flags & GETFILES_GETHIDDEN)) {
			if (virtualName[0] == '.')
				continue;
		}

		// Let's just reuse GetFileInfo. We're calling stat anyway to get isDirectory information.
		Path fullName = directory / virtualName;

		FileInfo info;
		info.name = virtualName;
		if (!GetFileInfo(fullName, &info)) {
			continue;
		}
		if (!info.isDirectory) {
			std::string ext = info.fullName.GetFileExtension();
			if (!ext.empty()) {
				ext = ext.substr(1);  // Remove the dot.
				if (filter && filters.find(ext) == filters.end()) {
					continue;
				}
			}
		}
		files->push_back(info);
	}
	closedir(dirp);
#endif
	std::sort(files->begin(), files->end());
	return true;
}

#if PPSSPP_PLATFORM(WINDOWS)
// Returns a vector with the device names
std::vector<std::string> GetWindowsDrives()
{
	std::vector<std::string> drives;

#if PPSSPP_PLATFORM(UWP)
	DWORD logicaldrives = GetLogicalDrives();
	for (int i = 0; i < 26; i++)
	{
		if (logicaldrives & (1 << i))
		{
			CHAR driveName[] = { TEXT('A') + i, TEXT(':'), TEXT('\\'), TEXT('\0') };
			std::string str(driveName);
			drives.push_back(driveName);
		}
	}
	return drives;
#else
	const DWORD buffsize = GetLogicalDriveStrings(0, NULL);
	std::vector<wchar_t> buff(buffsize);
	if (GetLogicalDriveStrings(buffsize, buff.data()) == buffsize - 1)
	{
		auto drive = buff.data();
		while (*drive)
		{
			std::string str(ConvertWStringToUTF8(drive));
			str.pop_back();	// we don't want the final backslash
			str += "/";
			drives.push_back(str);

			// advance to next drive
			while (*drive++) {}
		}
	}
	return drives;
#endif  // PPSSPP_PLATFORM(UWP)
}
#endif  // PPSSPP_PLATFORM(WINDOWS)

}  // namespace File
