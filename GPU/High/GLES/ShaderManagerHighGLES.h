#pragma once

#include <stdlib.h>
#include "Common/CommonTypes.h"

namespace HighGpu {

struct ShaderID {
	ShaderID() { d[0] = 0xFFFFFFFF; }
	void clear() { d[0] = 0xFFFFFFFF; }
	u32 d[2];
	bool operator < (const ShaderID &other) const {
		for (size_t i = 0; i < sizeof(d) / sizeof(u32); i++) {
			if (d[i] < other.d[i])
				return true;
			if (d[i] > other.d[i])
				return false;
		}
		return false;
	}
	bool operator == (const ShaderID &other) const {
		for (size_t i = 0; i < sizeof(d) / sizeof(u32); i++) {
			if (d[i] != other.d[i])
				return false;
		}
		return true;
	}

	int Bit(int bit) const {
		return (d[bit >> 5] >> (bit & 31)) & 1;
	}
	// Does not handle crossing 32-bit boundaries
	int Bits(int bit, int count) const {
		const int mask = (1 << count) - 1;
		return (d[bit >> 5] >> bit) & mask;
	}

	void SetBit(int bit, bool value = true) {
		if (value) {
			d[bit >> 5] |= 1 << (bit & 0x1f);
		}
	}
	void SetBits(int bit, int count, int value) {
		if (value != 0) {
			d[bit >> 5] |= value << (bit & 0x1f);
		}
	}
};

// Pre-defined attribute indices
enum {
	ATTR_POSITION = 0,
	ATTR_TEXCOORD = 1,
	ATTR_NORMAL = 2,
	ATTR_W1 = 3,
	ATTR_W2 = 4,
	ATTR_COLOR0 = 5,
	ATTR_COLOR1 = 6,

	ATTR_COUNT,
};

class ShaderManagerGLES {
public:
	void ClearCache(bool);
	void DirtyShader();
	void DirtyLastShader();
};

}  // namespace
