#include "data/base64.h"

// TODO: This is a simple but not very efficient implementation.
std::string Base64Encode(const uint8_t *p, size_t sz) {
	const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	size_t unpaddedLength = (4 * sz + 2) / 3;
	std::string result;
	result.resize((unpaddedLength + 3) & ~3, '=');

	for (size_t i = 0; i < unpaddedLength; ++i) {
		// This is the index into the original string.
		size_t pos = (i * 3) / 4;
		int8_t off = 2 * ((i * 3) % 4);

		int c = p[pos];
		if (off > 2) {
			c <<= 8;
			off -= 8;

			// Grab more bits from the next character.
			if (pos + 1 < sz) {
				c |= p[pos + 1];
			}
		}

		// Since we take from the big end, off starts at 2 and goes down.
		int8_t shift = 2 - off;

		// Now take the bits at off and encode the character.
		result[i] = digits[(c >> shift) & 0x3F];
	}

	return result;
}
