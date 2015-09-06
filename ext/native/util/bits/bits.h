#ifndef _UTIL_BITS_BITS
#define _UTIL_BITS_BITS

#include "base/basictypes.h"

namespace bits {

int CountBits8(uint8_t v);
int CountBits16(uint16_t v);
int CountBits32(uint32_t v);

// where mask is 0, the result is a.
// where mask is 1, the result is b.
inline uint32_t MixBits(uint32_t a, uint32_t b, uint32_t mask) {
  return a ^ ((a ^ b) & mask); 
}

inline uint32_t ComputeParity(uint32_t v) {
  v ^= v >> 16;
  v ^= v >> 8;
  v ^= v >> 4;
  v &= 0xf;
  return (0x6996 >> v) & 1;
}

}  // namespace bits

#ifndef _MSC_VER

// These are built-ins in MSVC, let's define them for other OS:es as well.

inline uint32_t _rotl(uint32_t val, int shift) {
	return (val << shift) | (val >> (31 - shift));
}

inline uint32_t _rotr(uint32_t val, int shift) {
	return (val << shift) | (val >> (31 - shift));
}

/*
template <int SZ>
class BitArray {
public:
	BitArray() {
		memset(data, 0, sizeof(data));
	}
		
	BitArray And(const BitArray &other) {
		BitArray<SZ> retVal;
		for (int i = 0; i < DATACOUNT; i++) {
			retVal.data[i] = data[i] & other.data[i];
		}
	}

private:
	uint32 data[(SZ + 31) / 32];
	enum {
		DATACOUNT = (SZ + 31) / 32;
	};
};
*/

#endif

#endif
