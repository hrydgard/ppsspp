#ifndef _UTIL_HASH_HASH_H
#define _UTIL_HASH_HASH_H

#include <stdlib.h>

#include "base/basictypes.h"

namespace hash {

uint32 Fletcher(const uint8 *data_u8, size_t length);  // FAST. Length & 1 == 0.
uint32 Adler32(const uint8 *data, size_t len);         // Fairly accurate, slightly slower

// WTF is this for?
class ConsistentHasher {
 public:
  ConsistentHasher() {
  	// TODO: really need a better seed here.
    uint32 orig_seed = rand();
    seed = Fletcher((const uint8 *)&orig_seed, 4);
  }
  uint32 Hash(uint32 value) {
    return value ^ seed;
  }
 private:
  uint32 seed;
};

}  // namespace hash

#endif
