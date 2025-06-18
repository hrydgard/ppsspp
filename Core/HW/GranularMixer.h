// Copyright 2009 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "Common/CommonTypes.h"
#include "Core/Config.h"

class PointerWrap;

// Replacement for std::countr_one from a later version of C++
constexpr u32 countr_one_replacement(u32 x) {
	u32 count = 0;
	while (x & 1) {
		++count;
		x >>= 1;
	}
	return count;
}

struct GranularStats {
	int queuedGranulesMin;
	int queuedGranulesMax;
	int queuedSamplesTarget;
	float smoothedQueuedGranules;
	int targetQueueSize;
	int maxQueuedGranules;
	float fadeVolume;
	bool looping;
	int overruns;
	int underruns;
	float smoothedReadSize;
	float frameTimeEstimate;
};

class GranularMixer final {
public:
	explicit GranularMixer();

	// Called from audio threads
	void Mix(s16 *samples, u32 numSamples, int outSampleRate, float vpsEstimate);

	// Called from emulation thread
	void PushSamples(const s32 *samples, u32 num_samples, float volume);

	void GetStats(GranularStats *stats);

	static constexpr u32 GRANULE_SIZE = 256;

private:
	static constexpr std::size_t MAX_GRANULE_QUEUE_SIZE = 128;
	static constexpr std::size_t GRANULE_QUEUE_MASK = MAX_GRANULE_QUEUE_SIZE - 1;

	struct StereoPair final {
		float l = 0.f;
		float r = 0.f;

		constexpr StereoPair() = default;
		constexpr StereoPair(const StereoPair&) = default;
		constexpr StereoPair& operator=(const StereoPair&) = default;
		constexpr StereoPair(StereoPair&&) = default;
		constexpr StereoPair& operator=(StereoPair&&) = default;

		constexpr StereoPair(float mono) : l(mono), r(mono) {}
		constexpr StereoPair(float left, float right) : l(left), r(right) {}
		constexpr StereoPair(s16 left, s16 right) : l(left), r(right) {}

		StereoPair operator+(const StereoPair& other) const {
			return StereoPair(l + other.l, r + other.r);
		}

		StereoPair operator*(float f) const {
			return StereoPair(l * f, r * f);
		}
	};

	static constexpr u32 GRANULE_OVERLAP = GRANULE_SIZE / 2;
	static constexpr u32 GRANULE_MASK = GRANULE_SIZE - 1;
	static constexpr u32 GRANULE_BITS = countr_one_replacement(GRANULE_MASK);
	static constexpr u32 GRANULE_FRAC_BITS = 32 - GRANULE_BITS;

	using Granule = std::array<StereoPair, GRANULE_SIZE>;

	Granule m_next_buffer{};
	u32 m_next_buffer_index = 0;

	u32 m_current_index = 0;
	Granule m_front;
	Granule m_back;

	std::atomic<u32> m_granule_queue_size{ 20 };
	float smoothedQueueSize_ = 0.0f;
	Granule m_queue[MAX_GRANULE_QUEUE_SIZE];

	// These are permitted to grow indefinitely and masked on use.
	std::atomic<u32> m_queue_head{};
	std::atomic<u32> m_queue_tail{};
	std::atomic<bool> m_queue_looping{};

	float m_fade_volume = 1.0;
	int underruns_ = 0;
	int overruns_ = 0;
	int queuedGranulesMin_ = 10000;
	int queuedGranulesMax_ = 0;
	int queuedSamplesTarget_ = 0;
	float smoothedReadSize_ = 0.0f;
	float frameTimeEstimate_ = 0.0f;

	void Enqueue();
	void Dequeue(Granule* granule);
};
