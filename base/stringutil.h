#pragma once

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <stdarg.h>
#include <vector>

#include "base/basictypes.h"

#ifdef _MSC_VER
#pragma warning (disable:4996)
#endif

#ifdef BLACKBERRY
// QNX Does not have an implementation of vasprintf
static inline int vasprintf(char **rResult, const char *aFormat, va_list aAp)
{
	int rVal;
	char *result;
	va_list ap;

	result = (char *) malloc(16);
	if (result == NULL) return -1;

	va_copy(ap, aAp);
	rVal = vsnprintf(result, 16, aFormat, ap);
	va_end(ap);

	if (rVal == -1) 
	{
		free(result);
		return rVal;
	}
	else if (rVal >= 16)
	{
		free(result);
		result = (char *) malloc(rVal + 1);
		if (result == NULL) return -1;

		va_copy(ap, aAp);
		rVal = vsnprintf(result, rVal + 1, aFormat, aAp);
		va_end(ap);
	}

	*rResult = result;
	return rVal;
}
#endif

// Dumb wrapper around itoa, providing a buffer. Declare this on the stack.
class ITOA {
public:
  char buffer[16];
  const char *p(int i) {
    sprintf(buffer, "%i", i);
    return &buffer[0];
  }
};

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

void DataToHexString(const uint8 *data, size_t size, std::string *output);
inline void StringToHexString(const std::string &data, std::string *output) {
  DataToHexString((uint8_t *)(&data[0]), data.size(), output);
}


// highly unsafe and not recommended.
unsigned int parseHex(const char* _szValue);


// Suitable for inserting into maps, unlike char*, and cheaper than std::string.
// Strings must be constant and preferably be stored in the read-only part 
// of the binary.
class ConstString {
public:
  ConstString(const char *ptr) {
    ptr_ = ptr;
  }
  bool operator <(const ConstString &other) const {
    return strcmp(ptr_, other.ptr_) < 0;
  }
  bool operator ==(const ConstString &other) const {
    return ptr_ == other.ptr_ || !strcmp(ptr_, other.ptr_);
  }
	const char *get() const { return ptr_; }
private:
  const char *ptr_;
};

std::string StringFromFormat(const char* format, ...);
std::string StringFromInt(int value);
std::string StringFromBool(bool value);

std::string ArrayToString(const uint8_t *data, uint32_t size, int line_len = 20, bool spaces = true);

std::string StripSpaces(const std::string &s);
std::string StripQuotes(const std::string &s);

bool TryParse(const std::string &str, bool *const output);
bool TryParse(const std::string &str, uint32_t *const output);

template <typename N>
static bool TryParse(const std::string &str, N *const output)
{
	std::istringstream iss(str);

	N tmp = 0;
	if (iss >> tmp)
	{
		*output = tmp;
		return true;
	}
	else
		return false;
}
void SplitString(const std::string& str, const char delim, std::vector<std::string>& output);

std::string ReplaceAll(std::string input, const std::string& src, const std::string& dest);

template <typename N>
static std::string ValueToString(const N value)
{
	std::stringstream string;
	string << value;
	return string.str();
}

