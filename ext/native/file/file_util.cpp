#include "ppsspp_config.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <direct.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#else
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

#include "base/logging.h"
#include "base/basictypes.h"
#include "file/file_util.h"
#include "util/text/utf8.h"

#if !defined(__linux__) && !defined(_WIN32) && !defined(__QNX__)
#define stat64 stat
#endif

FILE *openCFile(const std::string &filename, const char *mode)
{
#if defined(_WIN32) && defined(UNICODE)
	return _wfopen(ConvertUTF8ToWString(filename).c_str(), ConvertUTF8ToWString(mode).c_str());
#else
	return fopen(filename.c_str(), mode);
#endif
}

bool writeStringToFile(bool text_file, const std::string &str, const char *filename)
{
	FILE *f = openCFile(filename, text_file ? "w" : "wb");
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

bool writeDataToFile(bool text_file, const void* data, const unsigned int size, const char *filename)
{
	FILE *f = openCFile(filename, text_file ? "w" : "wb");
	if (!f)
		return false;
	size_t len = size;
	if (len != fwrite(data, 1, len, f))
	{
		fclose(f);
		return false;
	}
	fclose(f);
	return true;
}

uint64_t GetSize(FILE *f)
{
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
	return size;
#endif
}

bool readFileToString(bool text_file, const char *filename, std::string &str)
{
	FILE *f = openCFile(filename, text_file ? "r" : "rb");
	if (!f)
		return false;
	size_t len = (size_t)GetSize(f);
	char *buf = new char[len + 1];
	buf[fread(buf, 1, len, f)] = 0;
	str = std::string(buf, len);
	fclose(f);
	delete [] buf;
	return true;
}


bool readDataFromFile(bool text_file, unsigned char* &data, const unsigned int size, const char *filename)
{
	FILE *f = openCFile(filename, text_file ? "r" : "rb");
	if (!f)
		return false;
	size_t len = (size_t)GetSize(f);
	if(len < size) {
		fclose(f);
		return false;
	}
	data[fread(data, 1, size, f)] = 0;
	fclose(f);
	return true;
}

// Returns true if filename is a directory
bool isDirectory(const std::string &filename) {
	FileInfo info;
	getFileInfo(filename.c_str(), &info);
	return info.isDirectory;
}

bool getFileInfo(const char *path, FileInfo *fileInfo) {
	// TODO: Expand relative paths?
	fileInfo->fullName = path;

#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA attrs;
	if (!GetFileAttributesExW(ConvertUTF8ToWString(path).c_str(), GetFileExInfoStandard, &attrs)) {
		fileInfo->size = 0;
		fileInfo->isDirectory = false;
		fileInfo->exists = false;
		return false;
	}
	fileInfo->size = (uint64_t)attrs.nFileSizeLow | ((uint64_t)attrs.nFileSizeHigh << 32);
	fileInfo->isDirectory = (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	fileInfo->isWritable = (attrs.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;
	fileInfo->exists = true;
#else

	std::string copy(path);

#if (defined __ANDROID__) && (__ANDROID_API__ < 21)
	struct stat file_info;
	int result = stat(copy.c_str(), &file_info);
#else
	struct stat64 file_info;
	int result = stat64(copy.c_str(), &file_info);
#endif
	if (result < 0) {
		fileInfo->exists = false;
		return false;
	}

	fileInfo->isDirectory = S_ISDIR(file_info.st_mode);
	fileInfo->isWritable = false;
	fileInfo->size = file_info.st_size;
	fileInfo->exists = true;
	// HACK: approximation
	if (file_info.st_mode & 0200)
		fileInfo->isWritable = true;
#endif
	return true;
}

std::string getFileExtension(const std::string &fn) {
	int pos = (int)fn.rfind(".");
	if (pos < 0) return "";
	std::string ext = fn.substr(pos+1);
	for (size_t i = 0; i < ext.size(); i++) {
		ext[i] = tolower(ext[i]);
	}
	return ext;
}

bool FileInfo::operator <(const FileInfo &other) const {
	if (isDirectory && !other.isDirectory)
		return true;
	else if (!isDirectory && other.isDirectory)
		return false;
	if (strcasecmp(name.c_str(), other.name.c_str()) < 0)
		return true;
	else
		return false;
}

size_t getFilesInDir(const char *directory, std::vector<FileInfo> *files, const char *filter, int flags) {
	size_t foundEntries = 0;
	std::set<std::string> filters;
	std::string tmp;
	if (filter) {
		while (*filter) {
			if (*filter == ':') {
				filters.insert(tmp);
				tmp = "";
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
	}
	if (tmp.size())
		filters.insert(tmp);
#ifdef _WIN32
	// Find the first file in the directory.
	WIN32_FIND_DATA ffd;
	HANDLE hFind = FindFirstFileEx((ConvertUTF8ToWString(directory) + L"\\*").c_str(), FindExInfoStandard, &ffd, FindExSearchNameMatch, NULL, 0);
	if (hFind == INVALID_HANDLE_VALUE) {
		return 0;
	}
	// windows loop
	do
	{
		const std::string virtualName = ConvertWStringToUTF8(ffd.cFileName);
#else
	struct dirent *result = NULL;

	//std::string directoryWithSlash = directory;
	//if (directoryWithSlash.back() != '/')
	//	directoryWithSlash += "/";

	DIR *dirp = opendir(directory);
	if (!dirp)
		return 0;
	// non windows loop
	while ((result = readdir(dirp)))
	{
		const std::string virtualName(result->d_name);
#endif
		// check for "." and ".."
		if (((virtualName[0] == '.') && (virtualName[1] == '\0')) ||
			((virtualName[0] == '.') && (virtualName[1] == '.') && 
			(virtualName[2] == '\0')))
			continue;

		// Remove dotfiles (optional with flag.)
		if (!(flags & GETFILES_GETHIDDEN) && virtualName[0] == '.')
			continue;

		FileInfo info;
		info.name = virtualName;
		std::string dir = directory;

		// Only append a slash if there isn't one on the end.
		size_t lastSlash = dir.find_last_of("/");
		if (lastSlash != (dir.length() - 1))
			dir.append("/");

		info.fullName = dir + virtualName;
		info.isDirectory = isDirectory(info.fullName);
		info.exists = true;
		info.size = 0;
		if (!info.isDirectory) {
			std::string ext = getFileExtension(info.fullName);
			if (filter) {
				if (filters.find(ext) == filters.end())
					continue;
			}
		}

		if (files)
			files->push_back(info);
		foundEntries++;
#ifdef _WIN32
	} while (FindNextFile(hFind, &ffd) != 0);
	FindClose(hFind);
#else
	}
	closedir(dirp);
#endif
	if (files)
		std::sort(files->begin(), files->end());
	return foundEntries;
}

#ifdef _WIN32
// Returns a vector with the device names
std::vector<std::string> getWindowsDrives()
{
#if PPSSPP_PLATFORM(UWP)
	return std::vector<std::string>();  // TODO UWP http://stackoverflow.com/questions/37404405/how-to-get-logical-drives-names-in-windows-10
#else
	std::vector<std::string> drives;

	const DWORD buffsize = GetLogicalDriveStrings(0, NULL);
	std::vector<TCHAR> buff(buffsize);
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
#endif
}
#endif
