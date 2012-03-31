#pragma once

#include <string>
#include <vector>

// Whole-file reading/writing
bool writeStringToFile(bool text_file, const std::string &str, const char *filename);
bool readFileToString(bool text_file, const char *filename, std::string &str);

// Beginnings of a directory utility system. TODO: Improve.
size_t getFilesInDir(const char *directory, std::vector<std::string> *files);
void deleteFile(const char *file);
