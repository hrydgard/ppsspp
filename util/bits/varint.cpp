#include "util/bits/varint.h"

namespace varint {

void Encode32(uint32_t value, char **dest) {
  // Simple varint
  char *p = *dest;
  while (value > 127) {
    *p++ = (value & 127);
    value >>= 7;
  }
  *p++ = value | 0x80;
  *dest = p;
}

uint32_t Decode32(const char **ptr) {
  uint32_t value = 0;
  const char *p = *ptr;
  while (true) {
    uint8 b = *p++;
    if (b & 0x80) {
      *ptr = p;
      return value | (b & 0x7F);
    } else {
      value |= *p++;
      value <<= 7;
    }
  }
  *ptr = p;
  return value;
}

}  // namespace varint


