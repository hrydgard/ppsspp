#pragma once

#include <string>
#include <vector>

#include <stdio.h>

#include <inttypes.h>

// Beginnings of a directory utility system. TODO: Improve.

namespace File {

struct FileInfo {
	std::string name;
	std::string fullName;
	bool exists;
	bool isDirectory;
	bool isWritable;
	uint64_t size;

	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
	uint32_t access;  // st_mode & 0x1ff

	bool operator <(const FileInfo &other) const;
};

bool GetFileInfo(const char *path, FileInfo *fileInfo);

enum {
	GETFILES_GETHIDDEN = 1
};
size_t GetFilesInDir(const char *directory, std::vector<FileInfo> *files, const char *filter = nullptr, int flags = 0);
int64_t getDirectoryRecursiveSize(const std::string &path, const char *filter = nullptr, int flags = 0);

#ifdef _WIN32
std::vector<std::string> getWindowsDrives();
#endif

}
