#ifndef _UTIL_HASH_HASH_H
#define _UTIL_HASH_HASH_H

#include <stdlib.h>

#include "base/basictypes.h"

namespace hash {

uint32 Fletcher(const uint8 *data_u8, size_t length);  // FAST. Length & 1 == 0.
uint32 Adler32(const uint8 *data, size_t len);         // Fairly accurate, slightly slower

}  // namespace hash

#endif
