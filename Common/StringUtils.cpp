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

#include "Common.h"
#include "StringUtils.h"

void truncate_cpy(char *dest, size_t destSize, const char *src) {
	size_t len = strlen(src);
	if (len >= destSize - 1) {
		memcpy(dest, src, destSize - 1);
		dest[destSize - 1] = '\0';
	} else {
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
}

long parseHexLong(std::string s) {
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

std::string GetFilenameFromPath(std::string full_path) {
	size_t pos;
#ifdef _WIN32
	pos = full_path.rfind('\\');
	if (pos != std::string::npos) {
		return full_path.substr(pos + 1);
	}
#endif
	pos = full_path.rfind('/');
	if (pos != std::string::npos) {
		return full_path.substr(pos + 1);
	}
	// No directory components, just return the full path.
	return full_path;
}
