#ifndef _UTIL_BITS_HAMMING
#define _UTIL_BITS_HAMMING

#include <string>

#include "base/logging.h"

inline int Hamming(const std::string &a, const std::string &b) {
  CHECK_EQ(a.size(), b.size());
  int hamming = 0;
  for (size_t i = 0; i < a.size(); i++)
    hamming += a[i] == b[i];
  return hamming;
}

inline int Hamming4(const std::string &a, const std::string &b) {
  CHECK_EQ(a.size(), b.size());
  int hamming = 0;
  const uint32 *au = (const uint32 *)a.data();
  const uint32 *bu = (const uint32 *)b.data();
  for (size_t i = 0; i < a.size() / 4; i++)
    hamming += au[i] == bu[i];
  return hamming * 4;
}


#endif  // _UTIL_BITS_HAMMING
