#pragma once

#include <string>
#include <vector>

#include <cstdio>

#include <inttypes.h>

#include "Common/File/Path.h"

namespace File {

struct FileInfo {
	std::string name;
	Path fullName;
	bool exists = false;
	bool isDirectory = false;
	bool isWritable = false;
	uint64_t size = 0;

	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
	uint32_t access;  // st_mode & 0x1ff

	// Currently only supported for Android storage files.
	// Other places use different methods to get this.
	uint64_t lastModified = 0;

	bool operator <(const FileInfo &other) const;
};

bool GetFileInfo(const Path &path, FileInfo *fileInfo);

enum {
	GETFILES_GETHIDDEN = 1,
};

size_t GetFilesInDir(const Path &directory, std::vector<FileInfo> *files, const char *filter = nullptr, int flags = 0);
int64_t GetDirectoryRecursiveSize(const Path &path, const char *filter = nullptr, int flags = 0);
std::vector<File::FileInfo> ApplyFilter(std::vector<File::FileInfo> files, const char *filter);

#ifdef _WIN32
std::vector<std::string> GetWindowsDrives();
#endif

}  // namespace File
