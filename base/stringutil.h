#pragma once

#include <stdlib.h>
#include <stdio.h>
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

inline bool endsWith(const std::string &str, const std::string &what) {
  return str.substr(str.size() - what.size()) == what;
}

void DataToHexString(const uint8 *data, size_t size, std::string *output);
inline void StringToHexString(const std::string &data, std::string *output) {
  DataToHexString((uint8_t *)(&data[0]), data.size(), output);
}


// highly unsafe and not recommended.
unsigned int parseHex(const char* _szValue);
