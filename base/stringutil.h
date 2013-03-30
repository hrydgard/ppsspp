#pragma once

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#include "base/basictypes.h"

#ifdef _MSC_VER
#pragma warning (disable:4996)
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
	return str.substr(0, what.size()) == what;
}

inline bool endsWith(const std::string &str, const std::string &what) {
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
private:
  const char *ptr_;
};
