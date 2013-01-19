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

#include <stdlib.h>
#include <stdio.h>

#include "Common.h"
#include "CommonPaths.h"
#include "StringUtil.h"

// faster than sscanf
bool AsciiToHex(const char* _szValue, u32& result)
{
	char *endptr = NULL;
	const u32 value = strtoul(_szValue, &endptr, 16);

	if (!endptr || *endptr)
		return false;

	result = value;
	return true;
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

std::string StringFromFormat(const char* format, ...)
{
	va_list args;
	char *buf = NULL;
	std::string temp = "";
#ifdef _WIN32
	int required = 0;

	va_start(args, format);
	required = _vscprintf(format, args);
	buf = new char[required + 1];
	if(vsnprintf(buf, required, format, args) < 0)
		buf[0] = '\0';
	va_end(args);

	buf[required] = '\0';
	temp = buf;
	delete[] buf;
#else
	va_start(args, format);
	if(vasprintf(&buf, format, args) < 0)
		buf = NULL;
	va_end(args);

	if(buf != NULL) {
		temp = buf;
		free(buf);
	}
#endif
	return temp;
}

// For Debugging. Read out an u8 array.
std::string ArrayToString(const u8 *data, u32 size, int line_len, bool spaces)
{
	std::ostringstream oss;
	oss << std::setfill('0') << std::hex;

	for (int line = 0; size; ++data, --size)
	{
		oss << std::setw(2) << (int)*data;
		if (line_len == ++line)
		{
			oss << '\n';
			line = 0;
		}
		else if (spaces)
			oss << ' ';
	}

	return oss.str();
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

bool TryParse(const std::string &str, u32 *const output)
{
	char *endptr = NULL;

	// Reset errno to a value other than ERANGE
	errno = 0;

	unsigned long value = strtoul(str.c_str(), &endptr, 0);
	
	if (!endptr || *endptr)
		return false;

	if (errno == ERANGE)
		return false;

	if (ULONG_MAX > UINT_MAX) {
#ifdef _MSC_VER
#pragma warning (disable:4309)
#endif
		// Note: The typecasts avoid GCC warnings when long is 32 bits wide.
		if (value >= static_cast<unsigned long>(0x100000000ull)
				&& value <= static_cast<unsigned long>(0xFFFFFFFF00000000ull))
			return false;
	}

	*output = static_cast<u32>(value);
	return true;
}

bool TryParse(const std::string &str, bool *const output)
{
	if ("1" == str || !strcasecmp("true", str.c_str()))
		*output = true;
	else if ("0" == str || !strcasecmp("false", str.c_str()))
		*output = false;
	else
		return false;

	return true;
}

std::string StringFromInt(int value)
{
	char temp[16];
	sprintf(temp, "%i", value);
	return temp;
}

std::string StringFromBool(bool value)
{
	return value ? "True" : "False";
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

void BuildCompleteFilename(std::string& _CompleteFilename, const std::string& _Path, const std::string& _Filename)
{
	_CompleteFilename = _Path;

	// check for seperator
	if (!strchr(DIR_SEP_CHRS, *_CompleteFilename.rbegin()))
		_CompleteFilename += DIR_SEP;

	// add the filename
	_CompleteFilename += _Filename;
}

void SplitString(const std::string& str, const char delim, std::vector<std::string>& output)
{
	std::istringstream iss(str);
	output.resize(1);

	while (std::getline(iss, *output.rbegin(), delim))
		output.push_back("");

	output.pop_back();
}

std::string TabsToSpaces(int tab_size, const std::string &in)
{
	const std::string spaces(tab_size, ' ');
	std::string out(in);

	size_t i = 0;
	while (out.npos != (i = out.find('\t')))
		out.replace(i, 1, spaces);

	return out;
}

std::string ReplaceAll(std::string result, const std::string& src, const std::string& dest)
{
	while(1)
	{
		const size_t pos = result.find(src);
		if (pos == result.npos) break;
		result.replace(pos, src.size(), dest);
	}
	return result;
}

// UriDecode and UriEncode are from http://www.codeguru.com/cpp/cpp/string/conversions/print.php/c12759
// by jinq0123 (November 2, 2006)

// Uri encode and decode.
// RFC1630, RFC1738, RFC2396

// Some compilers don't like to assume (int)-1 will safely cast to (char)-1 as
// the MSBs aren't 0's. Workaround the issue while maintaining table spacing.
#define N1 (char)-1
const char HEX2DEC[256] =
{
    /*       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F */
    /* 0 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* 1 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* 2 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* 3 */  0, 1, 2, 3,  4, 5, 6, 7,  8, 9,N1,N1, N1,N1,N1,N1,

    /* 4 */ N1,10,11,12, 13,14,15,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* 5 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* 6 */ N1,10,11,12, 13,14,15,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* 7 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,

    /* 8 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* 9 */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* A */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* B */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,

    /* C */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* D */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* E */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1,
    /* F */ N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1, N1,N1,N1,N1
};

std::string UriDecode(const std::string & sSrc)
{
	// Note from RFC1630:  "Sequences which start with a percent sign
	// but are not followed by two hexadecimal characters (0-9, A-F) are reserved
	// for future extension"

	const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
	const size_t SRC_LEN = sSrc.length();
	const unsigned char * const SRC_END = pSrc + SRC_LEN;
	const unsigned char * const SRC_LAST_DEC = SRC_END - 2;   // last decodable '%' 

	char * const pStart = new char[SRC_LEN];
	char * pEnd = pStart;

	while (pSrc < SRC_LAST_DEC)
	{
		if (*pSrc == '%')
		{
			char dec1, dec2;
			if (-1 != (dec1 = HEX2DEC[*(pSrc + 1)])
				&& -1 != (dec2 = HEX2DEC[*(pSrc + 2)]))
			{
				*pEnd++ = (dec1 << 4) + dec2;
				pSrc += 3;
				continue;
			}
		}

		*pEnd++ = *pSrc++;
	}

	// the last 2- chars
	while (pSrc < SRC_END)
		*pEnd++ = *pSrc++;

	std::string sResult(pStart, pEnd);
	delete [] pStart;
	return sResult;
}

// Only alphanum is safe.
const char SAFE[256] =
{
	/*      0 1 2 3  4 5 6 7  8 9 A B  C D E F */
	/* 0 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 1 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 2 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 3 */ 1,1,1,1, 1,1,1,1, 1,1,0,0, 0,0,0,0,

	/* 4 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
	/* 5 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,
	/* 6 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
	/* 7 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,

	/* 8 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 9 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* A */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* B */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,

	/* C */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* D */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* E */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* F */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
};

std::string UriEncode(const std::string & sSrc)
{
	const char DEC2HEX[16 + 1] = "0123456789ABCDEF";
	const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
	const size_t SRC_LEN = sSrc.length();
	unsigned char * const pStart = new unsigned char[SRC_LEN * 3];
	unsigned char * pEnd = pStart;
	const unsigned char * const SRC_END = pSrc + SRC_LEN;

	for (; pSrc < SRC_END; ++pSrc)
	{
		if (SAFE[*pSrc]) 
			*pEnd++ = *pSrc;
		else
		{
			// escape this char
			*pEnd++ = '%';
			*pEnd++ = DEC2HEX[*pSrc >> 4];
			*pEnd++ = DEC2HEX[*pSrc & 0x0F];
		}
	}

	std::string sResult((char *)pStart, (char *)pEnd);
	delete [] pStart;
	return sResult;
}

bool StringEndsWith(std::string const &fullString, std::string const &ending) {
	if (fullString.length() >= ending.length()) {
		return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
	} else {
		return false;
	}
}
