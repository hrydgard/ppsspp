#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <string>
#include <sstream>
#include <limits.h>

#include <algorithm>
#include <iomanip>

#include "base/buffer.h"
#include "base/stringutil.h"

#ifdef _WIN32
// Function Cross-Compatibility
#define strcasecmp _stricmp

void OutputDebugStringUTF8(const char *p) {
	wchar_t temp[4096];

	int len = std::min(4095, (int)strlen(p));
	int size = (int)MultiByteToWideChar(CP_UTF8, 0, p, len, NULL, 0);
	MultiByteToWideChar(CP_UTF8, 0, p, len, temp, size);
	temp[size] = 0;

	OutputDebugString(temp);
}

#else

void OutputDebugStringUTF8(const char *p) {
	ILOG("%s", p);
}

#endif

void StringTrimEndNonAlphaNum(char *str) {
	ssize_t n = strlen(str);
	while (!isalnum(str[n]) && n >= 0) {
		str[n--] = '\0';
	}
}

void SkipSpace(const char **ptr) {
	while (**ptr && isspace(**ptr)) {
		(*ptr)++;
	}
}

void StringUpper(char *str) {
	while (*str) {
		*str = toupper(*str);
		str++;
	}
}

void StringUpper(char *str, int len) {
	while (len--) {
		*str = toupper(*str);
		str++;
	}
}


unsigned int parseHex(const char *_szValue)
{
	int Value = 0;
	size_t Finish = strlen(_szValue);
	if (Finish > 8 ) { Finish = 8; }

	for (size_t Count = 0; Count < Finish; Count++) {
		Value = (Value << 4);
		switch( _szValue[Count] ) {
		case '0': break;
		case '1': Value += 1; break;
		case '2': Value += 2; break;
		case '3': Value += 3; break;
		case '4': Value += 4; break;
		case '5': Value += 5; break;
		case '6': Value += 6; break;
		case '7': Value += 7; break;
		case '8': Value += 8; break;
		case '9': Value += 9; break;
		case 'A': Value += 10; break;
		case 'a': Value += 10; break;
		case 'B': Value += 11; break;
		case 'b': Value += 11; break;
		case 'C': Value += 12; break;
		case 'c': Value += 12; break;
		case 'D': Value += 13; break;
		case 'd': Value += 13; break;
		case 'E': Value += 14; break;
		case 'e': Value += 14; break;
		case 'F': Value += 15; break;
		case 'f': Value += 15; break;
		default:
			Value = (Value >> 4);
			Count = Finish;
		}
	}
	return Value;
}

void DataToHexString(const uint8_t *data, size_t size, std::string *output) {
	Buffer buffer;
	for (size_t i = 0; i < size; i++) {
		buffer.Printf("%02x ", data[i]);
		if (i && !(i & 15))
			buffer.Printf("\n");
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

std::string StringFromBool(bool value)
{
	return value ? "True" : "False";
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

// For Debugging. Read out an u8 array.
std::string ArrayToString(const uint8_t *data, uint32_t size, int line_len, bool spaces)
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

bool TryParse(const std::string &str, uint32_t *const output)
{
	char *endptr = NULL;

	// Holy crap this is ugly.

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

	*output = static_cast<uint32_t>(value);
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

int strcmpIgnore(std::string str1, std::string str2, std::string ignorestr1, std::string ignorestr2) {
	str1 = ReplaceAll(str1, ignorestr1, ignorestr2);
	str2 = ReplaceAll(str2, ignorestr1, ignorestr2);
	return strcmp(str1.c_str(),str2.c_str());
}
