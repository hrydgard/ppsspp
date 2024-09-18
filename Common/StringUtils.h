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

#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>
#include <cstring>
#include <string_view>
#include <vector>

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

// Useful for shaders with error messages..
std::string LineNumberString(const std::string &str);
std::string IndentString(const std::string &str, const std::string &sep, bool skipFirst = false);

// Other simple string utilities.

inline bool startsWith(std::string_view str, std::string_view key) {
	if (str.size() < key.size())
		return false;
	return !memcmp(str.data(), key.data(), key.size());
}

inline bool endsWith(std::string_view str, std::string_view what) {
	if (str.size() < what.size())
		return false;
	return str.substr(str.size() - what.size()) == what;
}

// Only use on strings where you're only concerned about ASCII.
inline bool startsWithNoCase(std::string_view str, std::string_view key) {
	if (str.size() < key.size())
		return false;
	return strncasecmp(str.data(), key.data(), key.size()) == 0;
}

inline bool endsWithNoCase(std::string_view str, std::string_view key) {
	if (str.size() < key.size())
		return false;
	const size_t offset = str.size() - key.size();
	return strncasecmp(str.data() + offset, key.data(), key.size()) == 0;
}

inline bool equalsNoCase(std::string_view str, std::string_view key) {
	if (str.size() != key.size())
		return false;
	return strncasecmp(str.data(), key.data(), key.size()) == 0;
}

bool containsNoCase(std::string_view haystack, std::string_view needle);

void DataToHexString(const uint8_t *data, size_t size, std::string *output);
void DataToHexString(int indent, uint32_t startAddr, const uint8_t* data, size_t size, std::string* output);

std::string StringFromFormat(const char* format, ...);
std::string StringFromInt(int value);

std::string StripSpaces(const std::string &s);
std::string StripQuotes(const std::string &s);

std::string_view StripSpaces(std::string_view s);
std::string_view StripQuotes(std::string_view s);

std::string_view StripPrefix(std::string_view prefix, std::string_view s);

// NOTE: str must live at least as long as all uses of output.
void SplitString(std::string_view str, const char delim, std::vector<std::string_view> &output);
// Try to avoid this when possible, in favor of the string_view version.
void SplitString(std::string_view str, const char delim, std::vector<std::string> &output);

void GetQuotedStrings(std::string_view str, std::vector<std::string> &output);

std::string ReplaceAll(std::string_view input, std::string_view src, std::string_view dest);

// Takes something like R&eplace and returns Replace, plus writes 'e' to *shortcutChar
// if not nullptr. Useful for Windows menu strings.
std::string UnescapeMenuString(std::string_view input, char *shortcutChar);

void SkipSpace(const char **ptr);

size_t truncate_cpy(char *dest, size_t destSize, const char *src);
template<size_t Count>
inline size_t truncate_cpy(char(&out)[Count], const char *src) {
	return truncate_cpy(out, Count, src);
}
size_t truncate_cpy(char *dest, size_t destSize, std::string_view src);
template<size_t Count>
inline size_t truncate_cpy(char(&out)[Count], std::string_view src) {
	return truncate_cpy(out, Count, src);
}

const char* safe_string(const char* s);

long parseHexLong(const std::string &s);
long parseLong(std::string s);
std::string StringFromFormat(const char* format, ...);
// Cheap!
bool CharArrayFromFormatV(char* out, int outsize, const char* format, va_list args);

template<size_t Count>
inline void CharArrayFromFormat(char (& out)[Count], const char* format, ...)
{
	va_list args;
	va_start(args, format);
	CharArrayFromFormatV(out, Count, format, args);
	va_end(args);
}

// "C:/Windows/winhelp.exe" to "C:/Windows/", "winhelp", ".exe"
bool SplitPath(const std::string& full_path, std::string* _pPath, std::string* _pFilename, std::string* _pExtension);

// Replaces %1, %2, %3 in format with arg1, arg2, arg3.
// Much safer than snprintf and friends.
// For mixes of strings and ints, manually convert the ints to strings.
std::string ApplySafeSubstitutions(std::string_view format, std::string_view string1, std::string_view string2 = "", std::string_view string3 = "", std::string_view string4 = "");
std::string ApplySafeSubstitutions(std::string_view format, int i1, int i2 = 0, int i3 = 0, int i4 = 0);
