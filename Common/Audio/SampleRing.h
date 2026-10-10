// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

// Hands 16-bit samples from one producer thread (a host audio callback) to one consumer thread.
// Lock-free and allocation-free, so it's safe to push from a real-time callback. Input that
// doesn't fit is dropped.
template <uint32_t SIZE>
class SampleRing {
	static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be a power of two");

public:
	// Producer. Null samples means silence.
	void Push(const int16_t *samples, uint32_t count) {
		const uint32_t write = writePos_.load(std::memory_order_relaxed);
		const uint32_t read = readPos_.load(std::memory_order_acquire);
		count = std::min(count, SIZE - (write - read));
		for (uint32_t i = 0; i < count; i++) {
			ring_[(write + i) & (SIZE - 1)] = samples ? samples[i] : 0;
		}
		writePos_.store(write + count, std::memory_order_release);
	}

	// Consumer. Calls func(const int16_t *samples, uint32_t count) for what's buffered, in up to
	// two pieces if it wraps around the end of the ring.
	template <class F>
	void Drain(F func) {
		const uint32_t read = readPos_.load(std::memory_order_relaxed);
		const uint32_t write = writePos_.load(std::memory_order_acquire);
		const uint32_t count = write - read;
		if (count == 0) {
			return;
		}
		const uint32_t start = read & (SIZE - 1);
		const uint32_t first = std::min(count, SIZE - start);
		func(&ring_[start], first);
		if (count > first) {
			func(&ring_[0], count - first);
		}
		readPos_.store(read + count, std::memory_order_release);
	}

	// Consumer. Drops whatever is buffered.
	void Clear() {
		readPos_.store(writePos_.load(std::memory_order_acquire), std::memory_order_release);
	}

private:
	int16_t ring_[SIZE];
	std::atomic<uint32_t> writePos_{0};
	std::atomic<uint32_t> readPos_{0};
};
