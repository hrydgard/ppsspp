#include <cstdint>
#include "Common/Data/Hash/Hash.h"

namespace hash {

// Implementation from Wikipedia
#define MOD_ADLER 65521
// data: Pointer to the data to be summed; len is in bytes
uint32_t Adler32(const uint8_t *data, size_t len) {
	uint32_t a = 1, b = 0;
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
