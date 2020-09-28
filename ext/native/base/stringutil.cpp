#include "ppsspp_config.h"
#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif
#if PPSSPP_PLATFORM(SWITCH)
#define _GNU_SOURCE
#include <stdio.h>
#endif
#include <cstring>
#include <cstdarg>
#include <errno.h>
#include <string>
#include <sstream>
#include <limits.h>

#include <algorithm>
#include <iomanip>

#include "base/buffer.h"
#include "base/stringutil.h"

std::string LineNumberString(const std::string &str) {
	std::stringstream input(str);
	std::stringstream output;
	std::string line;

	int lineNumber = 1;
	while (std::getline(input, line)) {
		output << std::setw(4) << lineNumber++ << ":  " << line << std::endl;
	}

	return output.str();
}

void SkipSpace(const char **ptr) {
	while (**ptr && isspace(**ptr)) {
		(*ptr)++;
	}
}

void DataToHexString(const uint8_t *data, size_t size, std::string *output) {
	Buffer buffer;
	for (size_t i = 0; i < size; i++) {
		if (i && !(i & 15))
			buffer.Printf("\n");
		buffer.Printf("%02x ", data[i]);
	}
	buffer.TakeAll(output);
}

void DataToHexString(const char* prefix, uint32_t startAddr, const uint8_t* data, size_t size, std::string* output) {
	Buffer buffer;
	size_t i = 0;
	for (; i < size; i++) {
		if (i && !(i & 15)) {
			buffer.Printf(" ");
			for (size_t j = i - 16; j < i; j++) {
				buffer.Printf("%c", ((data[j] < 0x20) || (data[j] > 0x7e)) ? 0x2e : data[j]);
			}
			buffer.Printf("\n");
		}
		if (!(i & 15))
			buffer.Printf("%s%08x  ", prefix, startAddr + i);
		buffer.Printf("%02x ", data[i]);
	}
	if (size & 15) {
		size_t padded_size = ((size-1) | 15) + 1;
		for (size_t j = size; j < padded_size; j++) {
			buffer.Printf("   ");
		}
		buffer.Printf(" ");
		for (size_t j = size & ~UINT64_C(0xF); j < size; j++) {
			buffer.Printf("%c", ((data[j] < 0x20) || (data[j] > 0x7e)) ? 0x2e : data[j]);
		}
	}
	buffer.TakeAll(output);
}

std::string StringFromFormat(const char* format, ...)
{
	va_list args;
	std::string temp = "";
#ifdef _WIN32
	int required = 0;

	va_start(args, format);
	required = _vscprintf(format, args);
	// Using + 2 to be safe between MSVC versions.
	// In MSVC 2015 and later, vsnprintf counts the trailing zero (per c++11.)
	temp.resize(required + 2);
	if (vsnprintf(&temp[0], required + 1, format, args) < 0) {
		temp.resize(0);
	} else {
		temp.resize(required);
	}
	va_end(args);
#else
	char *buf = nullptr;

	va_start(args, format);
	if (vasprintf(&buf, format, args) < 0)
		buf = nullptr;
	va_end(args);

	if (buf != nullptr) {
		temp = buf;
		free(buf);
	}
#endif
	return temp;
}

std::string StringFromInt(int value)
{
	char temp[16];
	sprintf(temp, "%i", value);
	return temp;
}

// Turns "  hej " into "hej". Also handles tabs.
std::string StripSpaces(const std::string &str)
{
	const size_t s = str.find_first_not_of(" \t\r\n");

	if (str.npos != s)
		return str.substr(s, str.find_last_not_of(" \t\r\n") - s + 1);
	else
		return "";
}

// "\"hello\"" is turned to "hello"
// This one assumes that the string has already been space stripped in both
// ends, as done by StripSpaces above, for example.
std::string StripQuotes(const std::string& s)
{
	if (s.size() && '\"' == s[0] && '\"' == *s.rbegin())
		return s.substr(1, s.size() - 2);
	else
		return s;
}

void SplitString(const std::string& str, const char delim, std::vector<std::string>& output)
{
	size_t next = 0;
	for (size_t pos = 0, len = str.length(); pos < len; ++pos) {
		if (str[pos] == delim) {
			output.push_back(str.substr(next, pos - next));
			// Skip the delimiter itself.
			next = pos + 1;
		}
	}

	if (next == 0) {
		output.push_back(str);
	} else if (next < str.length()) {
		output.push_back(str.substr(next));
	}
}

void GetQuotedStrings(const std::string& str, std::vector<std::string>& output)
{
	size_t next = 0;
	bool even = 0;
	for (size_t pos = 0, len = str.length(); pos < len; ++pos) {
		if (str[pos] == '\"' || str[pos] == '\'') {
			if (even) {
				//quoted text
				output.push_back(str.substr(next, pos - next));
				even = 0;
			} else {
				//non quoted text
				even = 1;
			}
			// Skip the delimiter itself.
			next = pos + 1;
		}
	}
}

std::string ReplaceAll(std::string result, const std::string& src, const std::string& dest)
{
	size_t pos = 0;

	if (src == dest)
		return result;

	while(1)
	{
		pos = result.find(src, pos);
		if (pos == result.npos) 
			break;
		result.replace(pos, src.size(), dest);
		pos += dest.size();
	}
	return result;
}
