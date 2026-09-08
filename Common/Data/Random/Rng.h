#pragma once

#include <cstdint>
#include "Common/Swap.h"

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
	uint32_t R32() {
		m_z = 36969 * (m_z & 65535) + (m_z >> 16);
		m_w = 18000 * (m_w & 65535) + (m_w >> 16);
		return (m_z << 16) + m_w;
	}
	uint64_t R64() {
		const uint32_t bottom = R32();
		const uint32_t top = R32();
		return ((uint64_t)top << 32) | bottom;
	}
	float F() {
		return (float)R32() / (float)(0xFFFFFFFF);
	}

	// public for easy save/load. Yes a bit ugly but better than moving DoState into native.
	uint32_t m_w;
	uint32_t m_z;
};

// Data must consist only of the index and the twister array. This matches the PSP
// MT context exactly - see pspautotests hash/mt19937ctx, which records the whole 2500 byte
// context off a real PSP: the counter sits at offset 0 and the 624 word array at offset 4,
// already twisted once by the time Init returns.
class MersenneTwister {
public:
	MersenneTwister(uint32_t seed) : index_(0) {
		mt_[0] = seed;
		for (uint32_t i = 1; i < MT_SIZE; i++)
			mt_[i] = (1812433253UL * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + i);
		// The PSP twists during Init rather than lazily on the first draw, so a context that
		// has been seeded but not yet used already holds the twisted array.
		for (uint32_t i = 0; i < MT_SIZE; i++)
			twist(i);
	}

	uint32_t R32() {
		// Read first, then regenerate that word in place - done in index order this is the
		// same as twisting the whole array in one go, and it matches what hardware leaves
		// behind in the context after each draw.
		uint32_t y = mt_[index_];
		twist(index_);
		index_ = (index_ + 1) % MT_SIZE;
		y ^=  y >> 11;
		y ^= (y <<  7) & 2636928640UL;
		y ^= (y << 15) & 4022730752UL;
		y ^=  y >> 18;
		return y;
	}

private:
	enum {
		MT_SIZE = 624,
	};

	u32_le index_;
	u32_le mt_[MT_SIZE];

	void twist(uint32_t i) {
		// The low half has to come through the LOWER mask - masking both halves with
		// 0x80000000 makes this something other than MT19937, which is what it used to do.
		uint32_t y = (mt_[i] & 0x80000000) | (mt_[(i + 1) % MT_SIZE] & 0x7FFFFFFF);
		mt_[i] = mt_[(i + 397) % MT_SIZE] ^ (y >> 1);
		if (y & 1) mt_[i] = mt_[i] ^ 2567483615UL;
	}
};
