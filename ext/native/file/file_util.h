#pragma once

#include <string>
#include <vector>

#include <stdio.h>

#include <inttypes.h>

// Whole-file reading/writing
bool writeStringToFile(bool text_file, const std::string &str, const char *filename);
bool readFileToString(bool text_file, const char *filename, std::string &str);

bool writeDataToFile(bool text_file, const void* data, const unsigned int size, const char *filename);
bool readDataFromFile(bool text_file, unsigned char* &data, const unsigned int size, const char *filename);

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
size_t getFilesInDir(const char *directory, std::vector<FileInfo> *files, const char *filter = 0, int flags = 0);

#ifdef _WIN32
std::vector<std::string> getWindowsDrives();
#endif
