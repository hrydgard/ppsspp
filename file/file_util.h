#pragma once

#include <string>
#include <vector>

// Whole-file reading/writing
bool writeStringToFile(bool text_file, const std::string &str, const char *filename);
bool readFileToString(bool text_file, const char *filename, std::string &str);

bool writeDataToFile(bool text_file, const void* data, const unsigned int size, const char *filename);
bool readDataFromFile(bool text_file, unsigned char* &data, const unsigned int size, const char *filename);

// Beginnings of a directory utility system. TODO: Improve.

struct FileInfo
{
	std::string name;
	std::string fullName;
	bool exists;
	bool isDirectory;
	bool isWritable;
	size_t size;

	bool operator <(const FileInfo &other) const {
		if (isDirectory && !other.isDirectory)
			return true;
		else if (!isDirectory && other.isDirectory)
			return false;
		if (name < other.name)
			return true;
		else
			return false;
	}
};

std::string getFileExtension(const std::string &fn);
std::string getDir(const std::string &path);
std::string getFilename(std::string path);
bool getFileInfo(const char *path, FileInfo *fileInfo);
size_t getFilesInDir(const char *directory, std::vector<FileInfo> *files, const char *filter = 0);
void deleteFile(const char *file);
void deleteDir(const char *file);
bool exists(const std::string &filename);
void mkDir(const std::string &path);
std::string getDir(const std::string &path);
