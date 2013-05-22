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

	// public for easy save/load. Yes a bit ugly but better than moving DoState into native.
	uint32 m_w;
	uint32 m_z;
};


// Data must consist only of the index and the twister array. This matches the PSP
// MT context exactly.
class MersenneTwister {
public:
	MersenneTwister(uint32_t seed) : index_(0) {
		mt_[0] = seed;
		for (uint32_t i = 1; i < MT_SIZE; i++)
			mt_[i] = (1812433253UL * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + i);
	}

	uint32_t R32() {
		if (index_ == 0)
			gen();
		uint32_t y = mt_[index_];
		y ^=  y >> 11;
		y ^= (y <<  7) & 2636928640UL;
		y ^= (y << 15) & 4022730752UL;
		y ^=  y >> 18;
		index_ = (index_ + 1) % MT_SIZE;
		return y;
	}

private:
	enum {
		MT_SIZE = 624,
	};

	uint32_t index_;
	uint32_t mt_[MT_SIZE];

	void gen() {
		for(uint32_t i = 0; i < MT_SIZE; i++){
			uint32_t y = (mt_[i] & 0x80000000) + (mt_[(i + 1) % MT_SIZE] & 0x80000000);
			mt_[i] = mt_[(i + 397) % MT_SIZE] ^ (y >> 1);
			if (y % 2) mt_[i] ^= 2567483615UL;
		}
		return;
	}
};
