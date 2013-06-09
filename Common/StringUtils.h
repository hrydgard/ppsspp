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

#ifndef _STRINGUTIL_H_
#define _STRINGUTIL_H_

#include <iomanip>
#include <base/stringutil.h>

#include "Common.h"
long parseHexLong(std::string s);
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

// Thousand separator. Turns 12345678 into 12,345,678
template <typename I>
std::string ThousandSeparate(I value, int spaces = 0)
{
	std::ostringstream oss;

// std::locale("") seems to be broken on many platforms
#if defined _WIN32 || (defined __linux__ && !defined __clang__)
	oss.imbue(std::locale(""));
#endif
	oss << std::setw(spaces) << value;

	return oss.str();
}

std::string TabsToSpaces(int tab_size, const std::string &in);

// "C:/Windows/winhelp.exe" to "C:/Windows/", "winhelp", ".exe"
bool SplitPath(const std::string& full_path, std::string* _pPath, std::string* _pFilename, std::string* _pExtension);

void BuildCompleteFilename(std::string& _CompleteFilename, const std::string& _Path, const std::string& _Filename);

#endif // _STRINGUTIL_H_
