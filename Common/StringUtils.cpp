// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <cstring>

#include "ppsspp_config.h"

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

#if PPSSPP_PLATFORM(SWITCH)
#define _GNU_SOURCE
#include <cstdio>
#endif

#include <cstdarg>

#include <errno.h>

#include <string>
#include <sstream>
#include <limits.h>

#include <algorithm>
#include <iomanip>
#include <cctype>

#include "Common/Buffer.h"
#include "Common/StringUtils.h"

size_t truncate_cpy(char *dest, size_t destSize, const char *src) {
	size_t len = strlen(src);
	if (len >= destSize - 1) {
		memcpy(dest, src, destSize - 1);
		len = destSize - 1;
	} else {
		memcpy(dest, src, len);
	}
	dest[len] = '\0';
	return len;
}

size_t truncate_cpy(char *dest, size_t destSize, std::string_view src) {
	if (src.size() > destSize - 1) {
		memcpy(dest, src.data(), destSize - 1);
		dest[destSize - 1] = 0;
		return destSize - 1;
	} else {
		memcpy(dest, src.data(), src.size());
		dest[src.size()] = 0;
		return src.size();
	}
}

const char* safe_string(const char* s) {
	return s ? s : "(null)";
}

long parseHexLong(const std::string &s) {
	long value = 0;

	if (s.substr(0,2) == "0x") {
		//s = s.substr(2);
	}
	value = strtoul(s.c_str(),0, 0);
	return value;
}

long parseLong(std::string s) {
	long value = 0;
	if (s.substr(0,2) == "0x") {
		s = s.substr(2);
		value = strtol(s.c_str(),NULL, 16);
	} else {
		value = strtol(s.c_str(),NULL, 10);
	}
	return value;
}

bool containsNoCase(std::string_view haystack, std::string_view needle) {
	auto pred = [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); };
	auto found = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), pred);
	return found != haystack.end();
}

bool CharArrayFromFormatV(char* out, int outsize, const char* format, va_list args)
{
	int writtenCount = vsnprintf(out, outsize, format, args);

	if (writtenCount > 0 && writtenCount < outsize)
	{
		out[writtenCount] = '\0';
		return true;
	}
	else
	{
		out[outsize - 1] = '\0';
		return false;
	}
}

bool SplitPath(const std::string& full_path, std::string* _pPath, std::string* _pFilename, std::string* _pExtension)
{
	if (full_path.empty())
		return false;

	size_t dir_end = full_path.find_last_of("/"
	// windows needs the : included for something like just "C:" to be considered a directory
#ifdef _WIN32
		":"
#endif
	);
	if (std::string::npos == dir_end)
		dir_end = 0;
	else
		dir_end += 1;

	size_t fname_end = full_path.rfind('.');
	if (fname_end < dir_end || std::string::npos == fname_end)
		fname_end = full_path.size();

	if (_pPath)
		*_pPath = full_path.substr(0, dir_end);

	if (_pFilename)
		*_pFilename = full_path.substr(dir_end, fname_end - dir_end);

	if (_pExtension)
		*_pExtension = full_path.substr(fname_end);

	return true;
}

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

std::string IndentString(const std::string &str, const std::string &sep, bool skipFirst) {
	std::stringstream input(str);
	std::stringstream output;
	std::string line;

	bool doIndent = !skipFirst;
	while (std::getline(input, line)) {
		if (doIndent) {
			output << sep;
		}
		doIndent = true;
		output << line << "\n";
	}

	return output.str();
}

std::string_view StripPrefix(std::string_view prefix, std::string_view s) {
	if (startsWith(s, prefix)) {
		return s.substr(prefix.size(), s.size() - prefix.size());
	} else {
		return s;
	}
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

void DataToHexString(int indent, uint32_t startAddr, const uint8_t* data, size_t size, std::string* output) {
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
			buffer.Printf("%*s%08x  ", indent, "", startAddr + i);
		buffer.Printf("%02x ", data[i]);
	}
	if (size & 15) {
		size_t padded_size = ((size - 1) | 15) + 1;
		for (size_t j = size; j < padded_size; j++) {
			buffer.Printf("   ");
		}
	}
	if (size > 0) {
		buffer.Printf(" ");
		for (size_t j = (size - 1ULL) & ~UINT64_C(0xF); j < size; j++) {
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

std::string StringFromInt(int value) {
	char temp[16];
	snprintf(temp, sizeof(temp), "%d", value);
	return temp;
}

// Turns "  hej " into "hej". Also handles tabs.
std::string StripSpaces(const std::string &str) {
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

// Turns "  hej " into "hej". Also handles tabs.
std::string_view StripSpaces(std::string_view str) {
	const size_t s = str.find_first_not_of(" \t\r\n");
	if (str.npos != s)
		return str.substr(s, str.find_last_not_of(" \t\r\n") - s + 1);
	else
		return "";
}

// "\"hello\"" is turned to "hello"
// This one assumes that the string has already been space stripped in both
// ends, as done by StripSpaces above, for example.
std::string_view StripQuotes(std::string_view s) {
	if (s.size() && '\"' == s[0] && '\"' == *s.rbegin())
		return s.substr(1, s.size() - 2);
	else
		return s;
}

// NOTE: str must live at least as long as all uses of output.
void SplitString(std::string_view str, const char delim, std::vector<std::string_view> &output) {
	size_t next = 0;
	for (size_t pos = 0, len = str.length(); pos < len; ++pos) {
		if (str[pos] == delim) {
			output.emplace_back(str.substr(next, pos - next));
			// Skip the delimiter itself.
			next = pos + 1;
		}
	}

	if (next == 0) {
		output.push_back(str);
	} else if (next < str.length()) {
		output.emplace_back(str.substr(next));
	}
}

void SplitString(std::string_view str, const char delim, std::vector<std::string> &output) {
	size_t next = 0;
	size_t pos = 0;
	while (pos < str.length()) {
		size_t delimPos = str.find(delim, pos);
		if (delimPos == std::string_view::npos) {
			break;
		}
		output.emplace_back(str.substr(next, delimPos - next));
		next = delimPos + 1;
		pos = delimPos + 1;
	}

	if (next == 0) {
		output.emplace_back(str);
	} else if (next < str.length()) {
		output.emplace_back(str.substr(next));
	}
}

static std::string ApplyHtmlEscapes(std::string_view str_view) {
	struct Repl {
		const char *a;
		const char *b;
	};

	static const Repl replacements[] = {
		{ "&amp;", "&" },
		// Easy to add more cases.
	};

	std::string str(str_view);
	for (const Repl &r : replacements) {
		str = ReplaceAll(str, r.a, r.b);
	}
	return str;
}

// Meant for HTML listings and similar, so supports some HTML escapes.
void GetQuotedStrings(std::string_view str, std::vector<std::string> &output) {
	size_t next = 0;
	bool even = 0;
	for (size_t pos = 0, len = str.length(); pos < len; ++pos) {
		if (str[pos] == '\"' || str[pos] == '\'') {
			if (even) {
				//quoted text
				output.emplace_back(ApplyHtmlEscapes(str.substr(next, pos - next)));
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

// TODO: this is quite inefficient.
std::string ReplaceAll(std::string_view input, std::string_view src, std::string_view dest) {
	size_t pos = 0;

	std::string result(input);
	if (src == dest)
		return result;

	// TODO: Don't mutate the input, just append stuff to the output instead.
	while (true) {
		pos = result.find(src, pos);
		if (pos == result.npos)
			break;
		result.replace(pos, src.size(), dest);
		pos += dest.size();
	}
	return result;
}

std::string UnescapeMenuString(std::string_view input, char *shortcutChar) {
	size_t len = input.length();
	std::string output;
	output.reserve(len);
	bool escaping = false;
	bool escapeFound = false;
	for (size_t i = 0; i < len; i++) {
		if (input[i] == '&') {
			if (escaping) {
				output.push_back(input[i]);
				escaping = false;
			} else {
				escaping = true;
			}
		} else {
			output.push_back(input[i]);
			if (escaping && shortcutChar && !escapeFound) {
				*shortcutChar = input[i];
				escapeFound = true;
			}
			escaping = false;
		}
	}
	return output;
}

std::string ApplySafeSubstitutions(std::string_view format, std::string_view string1, std::string_view string2, std::string_view string3, std::string_view string4) {
	size_t formatLen = format.length();
	std::string output;
	output.reserve(formatLen + 20);
	for (size_t i = 0; i < formatLen; i++) {
		char c = format[i];
		if (c != '%') {
			output.push_back(c);
			continue;
		}
		if (i >= formatLen - 1) {
			break;
		}
		switch (format[i + 1]) {
		case '1':
			output += string1; i++;
			break;
		case '2':
			output += string2; i++;
			break;
		case '3':
			output += string3; i++;
			break;
		case '4':
			output += string4; i++;
			break;
		}
	}
	return output;
}

std::string ApplySafeSubstitutions(std::string_view format, int i1, int i2, int i3, int i4) {
	size_t formatLen = format.length();
	std::string output;
	output.reserve(formatLen + 20);
	for (size_t i = 0; i < formatLen; i++) {
		char c = format[i];
		if (c != '%') {
			output.push_back(c);
			continue;
		}
		if (i >= formatLen - 1) {
			break;
		}
		switch (format[i + 1]) {
		case '1':
			output += StringFromInt(i1); i++;
			break;
		case '2':
			output += StringFromInt(i2); i++;
			break;
		case '3':
			output += StringFromInt(i3); i++;
			break;
		case '4':
			output += StringFromInt(i4); i++;
			break;
		}
	}
	return output;
}
