#include "Common/Data/Encoding/Base64.h"

// TODO: This is a simple but not very efficient implementation.
std::string Base64Encode(const uint8_t *p, size_t sz) {
	static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

std::vector<uint8_t> Base64Decode(const char *s, size_t sz) {
	static const uint8_t lookup[256] = {
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  62,  63,  62, 255,  63,
		// '0' starts here.
		 52,  53,  54,  55,  56,  57,  58,  59,  60,  61, 255, 255, 255, 255, 255, 255,
		// 'A' after an invalid.
		255,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
		 15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25, 255, 255, 255, 255,  63,
		// 'a' after an invalid.
		255,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
		 41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51, 255, 255, 255, 255, 255,
	};

	const uint8_t *p = (const uint8_t *)s;
	std::vector<uint8_t> result;
	result.reserve(3 * sz / 4);

	for (size_t i = 0; i < sz; i += 4) {
		uint8_t quad[4] = {
			lookup[p[i]],
			i + 1 < sz ? lookup[p[i + 1]] : (uint8_t)255,
			i + 2 < sz ? lookup[p[i + 2]] : (uint8_t)255,
			i + 3 < sz ? lookup[p[i + 3]] : (uint8_t)255,
		};

		// First: ABCDEF GHXXXX XXXXXX XXXXXX.  Neither 6-bit value should be invalid.
		result.push_back((quad[0] << 2) | ((quad[1] & 0x30) >> 4));

		// Next: XXXXXX XXABCD EFGHXX XXXXXX.  Invalid if quad[2] is invalid.
		if (quad[2] == 255) {
			continue;
		}
		result.push_back(((quad[1] & 0x0F) << 4) | ((quad[2] & 0x3C) >> 2));

		// Last: XXXXXX XXXXXX XXXXAB CDEFGH.  Invalid only if quad[3] is.
		if (quad[3] == 255) {
			continue;
		}
		result.push_back(((quad[2] & 0x03) << 6) | (quad[3] & 0x3F));
	}

	return result;
}
