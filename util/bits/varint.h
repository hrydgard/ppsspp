#ifndef _UTIL_BITS_VARINT
#define _UTIL_BITS_VARINT

#include "base/base.h"

namespace varint {

void Encode32(uint32 value, char **dest);
uint32 Decode32(const char **ptr);

}  // namespace varint

#endif  // _UTIL_BITS_VARINT
