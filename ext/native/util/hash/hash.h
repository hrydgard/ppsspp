#ifndef _UTIL_HASH_HASH_H
#define _UTIL_HASH_HASH_H

#include <cstdlib>

#include "base/basictypes.h"

namespace hash {

// Fairly decent function for hashing strings.
uint32_t Adler32(const uint8_t *data, size_t len);

}  // namespace hash

#endif
