#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#include <cstring>
#include <string>
#include <set>
#include <algorithm>
#include <stdio.h>
#include <sys/stat.h>
#include <ctype.h>

#include "base/logging.h"
#include "base/basictypes.h"
#include "file/file_util.h"

#if defined(__FreeBSD__) || defined(__APPLE__)
#define stat64 stat
#endif

bool writeStringToFile(bool text_file, const std::string &str, const char *filename)
{
	FILE *f = fopen(filename, text_file ? "w" : "wb");
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

uint64_t GetSize(FILE *f)
{
	// can't use off_t here because it can be 32-bit
	uint64_t pos = ftell(f);
	if (fseek(f, 0, SEEK_END) != 0) {
		return 0;
	}
	uint64_t size = ftell(f);
	// Reset the seek position to where it was when we started.
	if ((size != pos) && (fseek(f, pos, SEEK_SET) != 0)) {
		// Should error here
		return 0;
	}
	return size;
}

bool ReadFileToString(bool text_file, const char *filename, std::string &str)
{
	FILE *f = fopen(filename, text_file ? "r" : "rb");
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

#define DIR_SEP "/"
#define DIR_SEP_CHR '\\'

#ifndef METRO

// Remove any ending forward slashes from directory paths
// Modifies argument.
static void stripTailDirSlashes(std::string &fname)
{
	if (fname.length() > 1)
	{
		size_t i = fname.length() - 1;
		while (fname[i] == DIR_SEP_CHR)
			fname[i--] = '\0';
	}
	return;
}

// Returns true if file filename exists
bool exists(const std::string &filename)
{
#ifdef _WIN32
	return GetFileAttributes(filename.c_str()) != 0xFFFFFFFF;
#else
	struct stat64 file_info;

	std::string copy(filename);
	stripTailDirSlashes(copy);

	int result = stat64(copy.c_str(), &file_info);

	return (result == 0);
#endif
}

// Returns true if filename is a directory
bool isDirectory(const std::string &filename)
{
#ifdef _WIN32
	return (GetFileAttributes(filename.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	struct stat64 file_info;

	std::string copy(filename);
	stripTailDirSlashes(copy);

	int result = stat64(copy.c_str(), &file_info);

	if (result < 0) {
		WLOG("IsDirectory: stat failed on %s", filename.c_str());
		return false;
	}

	return S_ISDIR(file_info.st_mode);
#endif
}

std::string getFileExtension(const std::string &fn) {
	int pos = fn.rfind(".");
	if (pos < 0) return "";
	std::string ext = fn.substr(pos+1);
	for (size_t i = 0; i < ext.size(); i++) {
		ext[i] = tolower(ext[i]);
	}
	return ext;
}

size_t getFilesInDir(const char *directory, std::vector<FileInfo> *files, const char *filter) {
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
#ifdef _WIN32
	// Find the first file in the directory.
	WIN32_FIND_DATA ffd;
#ifdef UNICODE
	HANDLE hFind = FindFirstFile((std::wstring(directory) + "\\*").c_str(), &ffd);
#else
	HANDLE hFind = FindFirstFile((std::string(directory) + "\\*").c_str(), &ffd);
#endif
	if (hFind == INVALID_HANDLE_VALUE) {
		FindClose(hFind);
		return 0;
	}
	// windows loop
	do
	{
		const std::string virtualName(ffd.cFileName);
#else
	struct dirent_large { struct dirent entry; char padding[FILENAME_MAX+1]; };
	struct dirent_large diren;
	struct dirent *result = NULL;

	DIR *dirp = opendir(directory);
	if (!dirp)
		return 0;
	// non windows loop
	while (!readdir_r(dirp, (dirent*) &diren, &result) && result)
	{
		const std::string virtualName(result->d_name);
#endif
		// check for "." and ".."
		if (((virtualName[0] == '.') && (virtualName[1] == '\0')) ||
			((virtualName[0] == '.') && (virtualName[1] == '.') && 
			(virtualName[2] == '\0')))
			continue;

		// Remove dotfiles (should be made optional?)
		if (virtualName[0] == '.')
			continue;

		FileInfo info;
		info.name = virtualName;
		info.fullName = std::string(directory) + "/" + virtualName;
		info.isDirectory = isDirectory(info.fullName);
		info.exists = true;
		if (!info.isDirectory) {
			std::string ext = getFileExtension(info.fullName);
			if (filter) {
				if (filters.find(ext) == filters.end())
					continue;
			}
		}

		files->push_back(info);
#ifdef _WIN32
	} while (FindNextFile(hFind, &ffd) != 0);
	FindClose(hFind);
#else
	}
	closedir(dirp);
#endif
	std::sort(files->begin(), files->end());
	return foundEntries;
}

void deleteFile(const char *file)
{
#ifdef _WIN32
	if (!DeleteFile(file)) {
		ELOG("Error deleting %s: %i", file, GetLastError());
	}
#else
	int err = unlink(file);
	if (err) {
		ELOG("Error unlinking %s: %i", file, err);
	}
#endif
}
#endif

std::string getDir(const std::string &path)
{
	if (path == "/")
		return path;
	int n = path.size() - 1;
	while (n >= 0 && path[n] != '\\' && path[n] != '/')
		n--;
	std::string cutpath = path.substr(0, n);
	for (size_t i = 0; i < cutpath.size(); i++)
	{
		if (cutpath[i] == '\\') cutpath[i] = '/';
	}
#ifndef _WIN32
	if (!cutpath.size()) {
		return "/";
	}
#endif
	return cutpath;
}
