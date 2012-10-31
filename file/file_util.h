#pragma once

#include <string>
#include <vector>

// Whole-file reading/writing
bool writeStringToFile(bool text_file, const std::string &str, const char *filename);
bool readFileToString(bool text_file, const char *filename, std::string &str);

// Beginnings of a directory utility system. TODO: Improve.

struct FileInfo
{
	std::string name;
	std::string fullName;
	bool isDirectory;
};

std::string getFileExtension(const std::string &fn);
size_t getFilesInDir(const char *directory, std::vector<FileInfo> *files, const char *filter = 0);
void deleteFile(const char *file);
bool exists(const std::string &filename);

std::string getDir(const std::string &path);
