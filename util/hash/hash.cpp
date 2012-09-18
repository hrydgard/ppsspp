#include "base/basictypes.h"
#include "util/hash/hash.h"

namespace hash {

// uint32_t
// WARNING - may read one more byte!
// Implementation from Wikipedia.
uint32 HashFletcher(const uint8 *data_uint8, size_t length) {
  const uint16 *data = (const uint16 *)data_uint8;
  size_t len = (length + 1) / 2;
  uint32 sum1 = 0xffff, sum2 = 0xffff;

  while (len) {
    size_t tlen = len > 360 ? 360 : len;
    len -= tlen;

    do {
      sum1 += *data++;
      sum2 += sum1;
    } while (--tlen);

    sum1 = (sum1 & 0xffff) + (sum1 >> 16);
    sum2 = (sum2 & 0xffff) + (sum2 >> 16);
  }

  /* Second reduction step to reduce sums to 16 bits */
  sum1 = (sum1 & 0xffff) + (sum1 >> 16);
  sum2 = (sum2 & 0xffff) + (sum2 >> 16);
  return sum2 << 16 | sum1;
}

// Implementation from Wikipedia
// Slightly slower than Fletcher above, but slighly more reliable.
#define MOD_ADLER 65521
// data: Pointer to the data to be summed; len is in bytes
uint32 HashAdler32(const uint8 *data, size_t len) {
  uint32 a = 1, b = 0;
  while (len) {
    size_t tlen = len > 5550 ? 5550 : len;
    len -= tlen;
    do {
      a += *data++;
      b += a;
    } while (--tlen);

    a = (a & 0xffff) + (a >> 16) * (65536 - MOD_ADLER);
    b = (b & 0xffff) + (b >> 16) * (65536 - MOD_ADLER);
  }

  // It can be shown that a <= 0x1013a here, so a single subtract will do.
  if (a >= MOD_ADLER) {
    a -= MOD_ADLER;
  }

  // It can be shown that b can reach 0xfff87 here.
  b = (b & 0xffff) + (b >> 16) * (65536 - MOD_ADLER);

  if (b >= MOD_ADLER) {
    b -= MOD_ADLER;
  }
  return (b << 16) | a;
}

}  // namespace hash
