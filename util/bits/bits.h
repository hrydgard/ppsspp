#ifndef _UTIL_BITS_BITS
#define _UTil_BITS_BITS

#include "base/basictypes.h"

namespace bits {

int CountBits8(uint8 v);
int CountBits16(uint16 v);
int CountBits32(uint32 v);

// where mask is 0, the result is a.
// where mask is 1, the result is b.
inline uint32 MixBits(uint32 a, uint32 b, uint32 mask) {
  return a ^ ((a ^ b) & mask); 
}

inline uint32 ComputeParity(uint32 v) {
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
