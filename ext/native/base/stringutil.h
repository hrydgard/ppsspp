#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"

#ifdef _MSC_VER
#pragma warning (disable:4996)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

// Useful for shaders with error messages..
std::string LineNumberString(const std::string &str);

// Other simple string utilities.

inline bool startsWith(const std::string &str, const std::string &what) {
	if (str.size() < what.size())
		return false;
	return str.substr(0, what.size()) == what;
}

inline bool endsWith(const std::string &str, const std::string &what) {
	if (str.size() < what.size())
		return false;
	return str.substr(str.size() - what.size()) == what;
}

// Only use on strings where you're only concerned about ASCII.
inline bool startsWithNoCase(const std::string &str, const std::string &what) {
	if (str.size() < what.size())
		return false;
	return strncasecmp(str.c_str(), what.c_str(), what.size()) == 0;
}

inline bool endsWithNoCase(const std::string &str, const std::string &what) {
	if (str.size() < what.size())
		return false;
	const size_t offset = str.size() - what.size();
	return strncasecmp(str.c_str() + offset, what.c_str(), what.size()) == 0;
}

void DataToHexString(const uint8_t *data, size_t size, std::string *output);
void DataToHexString(const char* prefix, uint32_t startAddr, const uint8_t* data, size_t size, std::string* output);

std::string StringFromFormat(const char* format, ...);
std::string StringFromInt(int value);

std::string StripSpaces(const std::string &s);
std::string StripQuotes(const std::string &s);

void SplitString(const std::string& str, const char delim, std::vector<std::string>& output);

void GetQuotedStrings(const std::string& str, std::vector<std::string>& output);

std::string ReplaceAll(std::string input, const std::string& src, const std::string& dest);

void SkipSpace(const char **ptr);
