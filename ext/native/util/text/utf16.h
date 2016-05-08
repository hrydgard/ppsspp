#pragma once

#include "base/basictypes.h"

// Should optimize out.
#define UTF16_IS_LITTLE_ENDIAN (*(const uint16_t *)"\0\xff" >= 0x100)

template <bool is_little>
uint16_t UTF16_Swap(uint16_t u) {
	if (is_little) {
		return UTF16_IS_LITTLE_ENDIAN ? u : swap16(u);
	} else {
		return UTF16_IS_LITTLE_ENDIAN ? swap16(u) : u;
	}
}

template <bool is_little>
struct UTF16_Type {
public:
	static const uint32_t INVALID = (uint32_t)-1;

	UTF16_Type(const uint16_t *c) : c_(c), index_(0) {}

	uint32_t next() {
		const uint32_t u = UTF16_Swap<is_little>(c_[index_++]);

		// Surrogate pair.  UTF-16 is so simple.  We assume it's valid.
		if ((u & 0xF800) == 0xD800) {
			return 0x10000 + (((u & 0x3FF) << 10) | (UTF16_Swap<is_little>(c_[index_++]) & 0x3FF));
		}
		return u;
	}

	bool end() const {
		return c_[index_] == 0;
	}

	int length() const {
		int len = 0;
		for (UTF16_Type<is_little> dec(c_); !dec.end(); dec.next())
			++len;
		return len;
	}

	int shortIndex() const {
		return index_;
	}

	static int encode(uint16_t *dest, uint32_t u) {
		if (u >= 0x10000) {
			u -= 0x10000;
			*dest++ = UTF16_Swap<is_little>(0xD800 + ((u >> 10) & 0x3FF));
			*dest = UTF16_Swap<is_little>(0xDC00 + ((u >>  0) & 0x3FF));
			return 2;
		} else {
			*dest = UTF16_Swap<is_little>((uint16_t)u);
			return 1;
		}
	}

	static int encodeUnits(uint32_t u) {
		if (u >= 0x10000) {
			return 2;
		} else {
			return 1;
		}
	}

private:
	const uint16_t *c_;
	int index_;
};

typedef UTF16_Type<true> UTF16LE;
typedef UTF16_Type<false> UTF16BE;
