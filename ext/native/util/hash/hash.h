#ifndef _UTIL_HASH_HASH_H
#define _UTIL_HASH_HASH_H

#include <stdlib.h>

#include "base/basictypes.h"

namespace hash {

uint32_t Fletcher(const uint8_t *data_u8, size_t length);  // FAST. Length & 1 == 0.
uint32_t Adler32(const uint8_t *data, size_t len);         // Fairly accurate, slightly slower

}  // namespace hash

#endif
