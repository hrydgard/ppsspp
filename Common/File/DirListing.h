#pragma once

#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>

#include "Common/File/Path.h"

namespace File {

struct FileInfo {
	std::string name;
	Path fullName;
	bool exists = false;
	bool isDirectory = false;
	bool isWritable = false;
	uint64_t size = 0;

	uint64_t atime = 0;
	uint64_t mtime = 0;
	uint64_t ctime = 0;
	uint32_t access = 0;  // st_mode & 0x1ff

	bool operator <(const FileInfo &other) const;
};

bool GetFileInfo(const Path &path, FileInfo *fileInfo);

enum {
	GETFILES_GETHIDDEN = 1,
	GETFILES_GET_NAVIGATION_ENTRIES = 2,  // If you don't set this, "." and ".." will be skipped.
};

bool GetFilesInDir(const Path &directory, std::vector<FileInfo> *files, const char *filter = nullptr, int flags = 0);
std::vector<File::FileInfo> ApplyFilter(std::vector<File::FileInfo> files, const char *filter);

#ifdef _WIN32
std::vector<std::string> GetWindowsDrives();
#endif

}  // namespace File
