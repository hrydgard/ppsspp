#pragma once

#include <string>
#include <vector>

#include <stdio.h>

#include <inttypes.h>

// Beginnings of a directory utility system. TODO: Improve.

struct FileInfo {
	std::string name;
	std::string fullName;
	bool exists;
	bool isDirectory;
	bool isWritable;
	uint64_t size;

	bool operator <(const FileInfo &other) const;
};

std::string getFileExtension(const std::string &fn);
bool getFileInfo(const char *path, FileInfo *fileInfo);
FILE *openCFile(const std::string &filename, const char *mode);

enum {
	GETFILES_GETHIDDEN = 1
};
size_t getFilesInDir(const char *directory, std::vector<FileInfo> *files, const char *filter = nullptr, int flags = 0);
int64_t getDirectoryRecursiveSize(const std::string &path, const char *filter = nullptr, int flags = 0);

#ifdef _WIN32
std::vector<std::string> getWindowsDrives();
#endif
