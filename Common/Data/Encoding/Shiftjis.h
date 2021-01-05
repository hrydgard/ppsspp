#pragma once

#include <cstdint>

// Warning: decodes/encodes JIS, not Unicode.
// Use a table to map.
struct ShiftJIS {
	static const uint32_t INVALID = (uint32_t) -1;

	ShiftJIS(const char *c) : c_(c), index_(0) {}

	uint32_t next() {
		uint32_t j = (uint8_t)c_[index_++];

		int row;
		bool emojiAdjust = false;
		switch (j >> 4) {
		case 0x8:
			if (j == 0x80) {
				return INVALID;
			}
			// Intentional fall-through.
		case 0x9:
		case 0xE:
			row = ((j & 0x3F) << 1) - 0x01;
			break;

		case 0xF:
			emojiAdjust = true;
			if (j < 0xF4) {
				row = ((j & 0x7F) << 1) - 0x59;
			} else if (j < 0xFD) {
				row = ((j & 0x7F) << 1) - 0x1B;
			} else {
				return j;
			}
			break;

		// Anything else (i.e. <= 0x7x, 0xAx, 0xBx, 0xCx, and 0xDx) is JIS X 0201, return directly.
		default:
			return j;
		}

		// Okay, if we didn't return, it's time for the second byte (the cell.)
		j = (uint8_t)c_[index_++];
		// Not a valid second byte.
		if (j < 0x40 || j == 0x7F || j >= 0xFD) {
			return INVALID;
		}

		if (j >= 0x9F) {
			// This range means the row was even.
			++row;
			j -= 0x7E;
		} else {
			if (j >= 0x80) {
				j -= 0x20;
			} else {
				// Yuck.  They wrapped around 0x7F, so we subtract one less.
				j -= 0x20 - 1;
			}

			if (emojiAdjust) {
				// These are shoved in where they'll fit.
				if (row == 0x87) {
					// First byte was 0xF0.
					row = 0x81;
				} else if (row == 0x8B) {
					// First byte was 0xF2.
					row = 0x85;
				} else if (row == 0xCD) {
					// First byte was 0xF4.
					row = 0x8F;
				}
			}
		}

		// j is already the cell + 0x20.
		return ((row + 0x20) << 8) | j;
	}

	bool end() const {
		return c_[index_] == 0;
	}

	int length() const {
		int len = 0;
		for (ShiftJIS dec(c_); !dec.end(); dec.next())
			++len;
		return len;
	}

	int byteIndex() const {
		return index_;
	}

	static int encode(char *dest, uint32_t j) {
		int row = (j >> 8) - 0x20;
		int offsetCell = j & 0xFF;

		// JIS X 0201.
		if ((j & ~0xFF) == 0) {
			*dest = j;
			return 1;
		}

		if (row < 0x3F) {
			*dest++ = 0x80 + ((row + 1) >> 1);
		} else if (row < 0x5F) {
			// Reduce by 0x40 to account for the above range.
			*dest++ = 0xE0 + ((row - 0x40 + 1) >> 1);
		} else if (row >= 0x80) {
			// TODO
		}

		if (row & 1) {
			if (offsetCell < 0x60) {
				// Subtract one to shift around 0x7F.
				*dest++ = offsetCell + 0x20 - 1;
			} else {
				*dest++ = offsetCell + 0x20;
			}
		} else {
			*dest++ = offsetCell + 0x7E;
		}

		return 2;
	}

	static int encodeUnits(uint32_t j) {
		if ((j & ~0xFF) == 0) {
			return 1;
		}
		return 2;
	}

private:
	const char *c_;
	int index_;
};
