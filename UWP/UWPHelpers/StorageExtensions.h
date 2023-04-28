// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

// Functions:
// replace(std::string& str, const std::string& from, const std::string& to)
// replace2(const std::string str, const std::string& from, const std::string& to)
// split(const std::string s, char seperator)
// 
// isChild(std::string parent, std::string child)
// isParent(std::string parent, std::string child, std::string childName)
// 
// iequals(const std::string a, const std::string b) case-insenstive
// equals(const std::string a, const std::string b) case-senstive
// ends_with(std::string const& value, std::string const& ending)
// 
// convert(const std::string input)
// convertToWString(const std::string input)
// convert(Platform::String^ input)
// convert(std::wstring input)
// convert(const char* input)
// convertToLPCWSTR(Platform::String^ input)
// convertToLPCWSTR(std::string input)
// convertToChar(Platform::String^ input)
// 
// tolower(std::string& input)
// tolower(Platform::String^ &input)
// toupper(std::string& input)
// toupper(Platform::String^& input)
// 
// windowsPath(std::string& path)
// windowsPath(Platform::String^ &path)
// merge(std::string targetFullPath, std::string subFullPath)
// 
// findInList(std::list<T>& inputList, T& str)
// isWriteMode(const char* mode)
// getSubRoot(std::string parent, std::string child)

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <list>

bool replace(std::string& str, const std::string& from, const std::string& to);
std::string replace2(const std::string str, const std::string& from, const std::string& to);
std::vector<std::string> split(const std::string s, char seperator);
// Parent full path, child full path
bool isChild(std::string parent, std::string child);
// Parent full path, child full path, child name only
bool isParent(std::string parent, std::string child, std::string childName);

bool iequals(const std::string a, const std::string b);
bool equals(const std::string a, const std::string b);
bool ends_with(std::string const& value, std::string const& ending);
bool starts_with(std::string str, std::string prefix);

Platform::String^ convert(const std::string input);
std::wstring convertToWString(const std::string input);
std::string convert(Platform::String^ input);
std::string convert(std::wstring input);
std::string convert(const char* input);
LPCWSTR convertToLPCWSTR(std::string input);
LPCWSTR convertToLPCWSTR(Platform::String^ input);
const char* convertToChar(Platform::String^ input);

void tolower(std::string& input);
void tolower(Platform::String^& input);
void toupper(std::string& input);
void toupper(Platform::String^& input);

void windowsPath(std::string& path);
void windowsPath(Platform::String^& path);

std::string merge(std::string targetFullPath, std::string subFullPath);

std::string& rtrim(std::string& s, const char* t = " \t\n\r\f\v");
std::string& ltrim(std::string& s, const char* t = " \t\n\r\f\v");
std::string& trim(std::string& s, const char* t = " \t\n\r\f\v");

template<typename T>
bool findInList(std::list<T>& inputList, T& str) {
	return (std::find(inputList.begin(), inputList.end(), str) != inputList.end());
};

bool isWriteMode(const char* mode);
bool isAppendMode(const char* mode);
// Parent and child full path
std::string getSubRoot(std::string parent, std::string child);

