#pragma once

#include <string>
#include <vector>

bool writeStringToFile(bool text_file, const std::string &str, const char *filename);
bool readFileToString(bool text_file, const char *filename, std::string &str);


size_t getFilesInDir(const char *directory, std::vector<std::string> *files);
void deleteFile(const char *file);