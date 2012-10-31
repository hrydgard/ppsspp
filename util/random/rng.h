#pragma once

#include "base/basictypes.h"

// George Marsaglia-style random number generator.
class GMRng {
public:
	GMRng() {
		m_w = 0x23E866ED;
		m_z = 0x80FD5AF2;
	}
	void Init(int seed) {
		m_w = seed ^ (seed << 16);
		if (!m_w) m_w = 1337;
		m_z = ~seed;
		if (!m_z) m_z = 31337;
	}
	uint32 R32() {
		m_z = 36969 * (m_z & 65535) + (m_z >> 16);
		m_w = 18000 * (m_w & 65535) + (m_w >> 16);
		return (m_z << 16) + m_w;
	}
	float F() {
		return (float)R32() / (float)(0xFFFFFFFF);
	}

private:
	uint32 m_w;
	uint32 m_z;
};
