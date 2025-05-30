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

// Replacement for std::countr_one
constexpr std::size_t countr_one_replacement(std::size_t x) {
	std::size_t count = 0;
	while (x & 1) {
		++count;
		x >>= 1;
	}
	return count;
}

class GranularMixer final {
public:
	explicit GranularMixer();

	// Called from audio threads
	std::size_t Mix(s16* samples, std::size_t numSamples, int outSampleRate);

	// Called from emulation thread
	void PushSamples(const s32* samples, std::size_t num_samples, float volume);

private:
	const std::size_t SURROUND_CHANNELS = 6;

	class MixerFifo final {
		static constexpr std::size_t MAX_GRANULE_QUEUE_SIZE = 256;
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

			StereoPair operator+(const StereoPair& other) const
			{
				return StereoPair(l + other.l, r + other.r);
			}

			StereoPair operator*(const StereoPair& other) const
			{
				return StereoPair(l * other.l, r * other.r);
			}
		};

		static constexpr std::size_t GRANULE_SIZE = 256;
		static constexpr std::size_t GRANULE_OVERLAP = GRANULE_SIZE / 2;
		static constexpr std::size_t GRANULE_MASK = GRANULE_SIZE - 1;
		static constexpr std::size_t GRANULE_BITS = countr_one_replacement(GRANULE_MASK);
		static constexpr std::size_t GRANULE_FRAC_BITS = 32 - GRANULE_BITS;

		using Granule = std::array<StereoPair, GRANULE_SIZE>;

	public:
		MixerFifo(GranularMixer* mixer, bool little_endian) : m_mixer(mixer) {}
		void PushSamples(const s32* samples, std::size_t num_samples, float volume);
		void Mix(s16* samples, std::size_t num_samples, int outSampleRate);

	private:
		GranularMixer* m_mixer;

		Granule m_next_buffer{};
		std::size_t m_next_buffer_index = 0;

		u32 m_current_index = 0;
		Granule m_front, m_back;

		std::atomic<std::size_t> m_granule_queue_size{ 20 };
		std::array<Granule, MAX_GRANULE_QUEUE_SIZE> m_queue;
		std::atomic<std::size_t> m_queue_head{ 0 };
		std::atomic<std::size_t> m_queue_tail{ 0 };
		std::atomic<bool> m_queue_looping{ false };
		float m_fade_volume = 1.0;

		void Enqueue();
		void Dequeue(Granule* granule);

		StereoPair m_quantization_error;
	};

	void RefreshConfig();

	MixerFifo m_dma_mixer{ this, false };

	//int m_config_audio_buffer_ms;
};
