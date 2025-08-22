// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/GranularMixer.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/Math/math_util.h"
#include "Common/Swap.h"
#include "Core/HW/Display.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/Util/AudioFormat.h"  // for clamp_u16

// Something like a gaussian.
static const float g_GranuleWindow[256] = {
	0.0000016272f, 0.0000050749f, 0.0000113187f, 0.0000216492f, 0.0000377350f, 0.0000616906f,
	0.0000961509f, 0.0001443499f, 0.0002102045f, 0.0002984010f, 0.0004144844f, 0.0005649486f,
	0.0007573262f, 0.0010002765f, 0.0013036694f, 0.0016786636f, 0.0021377783f, 0.0026949534f,
	0.0033656000f, 0.0041666352f, 0.0051165029f, 0.0062351752f, 0.0075441359f, 0.0090663409f,
	0.0108261579f, 0.0128492811f, 0.0151626215f, 0.0177941726f, 0.0207728499f, 0.0241283062f,
	0.0278907219f, 0.0320905724f, 0.0367583739f, 0.0419244083f, 0.0476184323f, 0.0538693708f,
	0.0607049996f, 0.0681516192f, 0.0762337261f, 0.0849736833f, 0.0943913952f, 0.1045039915f,
	0.1153255250f, 0.1268666867f, 0.1391345431f, 0.1521323012f, 0.1658591025f, 0.1803098534f,
	0.1954750915f, 0.2113408944f, 0.2278888303f, 0.2450959552f, 0.2629348550f, 0.2813737361f,
	0.3003765625f, 0.3199032396f, 0.3399098438f, 0.3603488941f, 0.3811696664f, 0.4023185434f,
	0.4237393998f, 0.4453740162f, 0.4671625177f, 0.4890438330f, 0.5109561670f, 0.5328374823f,
	0.5546259838f, 0.5762606002f, 0.5976814566f, 0.6188303336f, 0.6396511059f, 0.6600901562f,
	0.6800967604f, 0.6996234375f, 0.7186262639f, 0.7370651450f, 0.7549040448f, 0.7721111697f,
	0.7886591056f, 0.8045249085f, 0.8196901466f, 0.8341408975f, 0.8478676988f, 0.8608654569f,
	0.8731333133f, 0.8846744750f, 0.8954960085f, 0.9056086048f, 0.9150263167f, 0.9237662739f,
	0.9318483808f, 0.9392950004f, 0.9461306292f, 0.9523815677f, 0.9580755917f, 0.9632416261f,
	0.9679094276f, 0.9721092781f, 0.9758716938f, 0.9792271501f, 0.9822058274f, 0.9848373785f,
	0.9871507189f, 0.9891738421f, 0.9909336591f, 0.9924558641f, 0.9937648248f, 0.9948834971f,
	0.9958333648f, 0.9966344000f, 0.9973050466f, 0.9978622217f, 0.9983213364f, 0.9986963306f,
	0.9989997235f, 0.9992426738f, 0.9994350514f, 0.9995855156f, 0.9997015990f, 0.9997897955f,
	0.9998556501f, 0.9999038491f, 0.9999383094f, 0.9999622650f, 0.9999783508f, 0.9999886813f,
	0.9999949251f, 0.9999983728f, 0.9999983728f, 0.9999949251f, 0.9999886813f, 0.9999783508f,
	0.9999622650f, 0.9999383094f, 0.9999038491f, 0.9998556501f, 0.9997897955f, 0.9997015990f,
	0.9995855156f, 0.9994350514f, 0.9992426738f, 0.9989997235f, 0.9986963306f, 0.9983213364f,
	0.9978622217f, 0.9973050466f, 0.9966344000f, 0.9958333648f, 0.9948834971f, 0.9937648248f,
	0.9924558641f, 0.9909336591f, 0.9891738421f, 0.9871507189f, 0.9848373785f, 0.9822058274f,
	0.9792271501f, 0.9758716938f, 0.9721092781f, 0.9679094276f, 0.9632416261f, 0.9580755917f,
	0.9523815677f, 0.9461306292f, 0.9392950004f, 0.9318483808f, 0.9237662739f, 0.9150263167f,
	0.9056086048f, 0.8954960085f, 0.8846744750f, 0.8731333133f, 0.8608654569f, 0.8478676988f,
	0.8341408975f, 0.8196901466f, 0.8045249085f, 0.7886591056f, 0.7721111697f, 0.7549040448f,
	0.7370651450f, 0.7186262639f, 0.6996234375f, 0.6800967604f, 0.6600901562f, 0.6396511059f,
	0.6188303336f, 0.5976814566f, 0.5762606002f, 0.5546259838f, 0.5328374823f, 0.5109561670f,
	0.4890438330f, 0.4671625177f, 0.4453740162f, 0.4237393998f, 0.4023185434f, 0.3811696664f,
	0.3603488941f, 0.3399098438f, 0.3199032396f, 0.3003765625f, 0.2813737361f, 0.2629348550f,
	0.2450959552f, 0.2278888303f, 0.2113408944f, 0.1954750915f, 0.1803098534f, 0.1658591025f,
	0.1521323012f, 0.1391345431f, 0.1268666867f, 0.1153255250f, 0.1045039915f, 0.0943913952f,
	0.0849736833f, 0.0762337261f, 0.0681516192f, 0.0607049996f, 0.0538693708f, 0.0476184323f,
	0.0419244083f, 0.0367583739f, 0.0320905724f, 0.0278907219f, 0.0241283062f, 0.0207728499f,
	0.0177941726f, 0.0151626215f, 0.0128492811f, 0.0108261579f, 0.0090663409f, 0.0075441359f,
	0.0062351752f, 0.0051165029f, 0.0041666352f, 0.0033656000f, 0.0026949534f, 0.0021377783f,
	0.0016786636f, 0.0013036694f, 0.0010002765f, 0.0007573262f, 0.0005649486f, 0.0004144844f,
	0.0002984010f, 0.0002102045f, 0.0001443499f, 0.0000961509f, 0.0000616906f, 0.0000377350f,
	0.0000216492f, 0.0000113187f, 0.0000050749f, 0.0000016272f
};

inline s16 clampfloat_s16(float f) {
	if (f <= -32767.0f) return -32767;
	if (f >= 32767.0f) return 32767;
	return (s16)f;
}

GranularMixer::GranularMixer() {
	INFO_LOG(Log::Audio, "Mixer is initialized");
}

// Executed from sound stream thread
void GranularMixer::Mix(s16 *samples, u32 num_samples, int outSampleRate, float fpsEstimate) {
	_dbg_assert_(samples);
	if (!samples)
		return;
	memset(samples, 0, num_samples * 2 * sizeof(s16));
	frameTimeEstimate_ = 1.0f / fpsEstimate;

	smoothedReadSize_ = smoothedReadSize_ == 0 ? num_samples : (smoothedReadSize_ * 0.95f + num_samples * 0.05f);

	constexpr u32 INDEX_HALF = 0x80000000;
	constexpr double FADE_IN_RC = 0.008;
	constexpr double FADE_OUT_RC = 0.064;

	// We need at least a double because the index jump has 24 bits of fractional precision.
	const double out_sample_rate = outSampleRate;
	double inSampleRate = 44100;

	const double emulation_speed = 1.0f;  // TODO: Change when we're in slow-motion mode etc.
	if (0 < emulation_speed && emulation_speed != 1.0)
		inSampleRate *= emulation_speed;

	const double base = static_cast<double>(1 << GRANULE_FRAC_BITS);
	const u32 index_jump = std::lround(base * inSampleRate / out_sample_rate);

	// These fade in / out multiplier are tuned to match a constant
	// fade speed regardless of the input or the output sample rate.
	const float fade_in_mul = -std::expm1(-1.0 / (out_sample_rate * FADE_IN_RC));
	const float fade_out_mul = -std::expm1(-1.0 / (out_sample_rate * FADE_OUT_RC));

	// Calculate the ideal length of the granule queue.
	// NOTE: We must have enough room here for 20fps games, generating all their audio
	// in a burst each frame (since we can't force real clock sync). That means 16*3 = 48 or rather 50ms.
	// However, in case of faster framerates, we should apply some pressure to reduce this. And if real clock sync
	// is on, we should also be able to get away with a shorter buffer here.
	// const u32 buffer_size_ms = frameTimeEstimate_ * 44100.0f;
	const u32 buffer_size_samples = smoothedReadSize_ * 4 + std::llround(frameTimeEstimate_ * inSampleRate);
	queuedSamplesTarget_ = buffer_size_samples;

	// Limit the possible queue sizes to any number between 4 and 64.
	const u32 buffer_size_granules =
		std::clamp((buffer_size_samples) / (GRANULE_SIZE >> 1), static_cast<u32>(4),
			static_cast<u32>(MAX_GRANULE_QUEUE_SIZE));

	if (buffer_size_granules != m_granule_queue_size.load(std::memory_order_relaxed)) {
		INFO_LOG(Log::Audio, "Granule buffer size changed to %d", buffer_size_granules);
	}

	m_granule_queue_size.store(buffer_size_granules, std::memory_order_relaxed);

	int actualQueueSize = m_queue_head - m_queue_tail;
	if (smoothedQueueSize_ == 0) {
		smoothedQueueSize_ = actualQueueSize;
	} else {
		constexpr float factor = 0.95f;
		smoothedQueueSize_ = factor * smoothedQueueSize_ + (1.0f - factor) * (float)actualQueueSize;
	}
	if (actualQueueSize < queuedGranulesMin_) {
		queuedGranulesMin_ = actualQueueSize;
	}
	if (actualQueueSize > queuedGranulesMax_) {
		queuedGranulesMax_ = actualQueueSize;
	}

	// TODO: The performance of this could be greatly enhanced with SIMD but it won't be easy
	// due to wrapping of various buffers.
	bool queue_looping = m_queue_looping.load(std::memory_order_relaxed);
	while (num_samples-- > 0) {
		// The indexes for the front and back buffers are offset by 50% of the granule size.
		// We use the modular nature of 32-bit integers to wrap around the granule size.
		m_current_index += index_jump;
		const u32 front_index = m_current_index;
		const u32 back_index = m_current_index + INDEX_HALF;

		// If either index is less than the index jump, that means we reached
		// the end of the of the buffer and need to load the next granule.
		if (front_index < index_jump)
			Dequeue(&m_front);
		else if (back_index < index_jump)
			Dequeue(&m_back);

		// The Granules are pre-windowed, so we can just add them together. A bit of accidental wrapping doesn't matter
		// either since the tails are so weak.
		const u32 ft = front_index >> GRANULE_FRAC_BITS;
		const u32 bt = back_index >> GRANULE_FRAC_BITS;
		const StereoPair s0 = m_front[(ft - 2) & GRANULE_MASK] + m_back[(bt - 2) & GRANULE_MASK];
		const StereoPair s1 = m_front[(ft - 1) & GRANULE_MASK] + m_back[(bt - 1) & GRANULE_MASK];
		const StereoPair s2 = m_front[(ft + 0) & GRANULE_MASK] + m_back[(bt + 0) & GRANULE_MASK];
		const StereoPair s3 = m_front[(ft + 1) & GRANULE_MASK] + m_back[(bt + 1) & GRANULE_MASK];
		const StereoPair s4 = m_front[(ft + 2) & GRANULE_MASK] + m_back[(bt + 2) & GRANULE_MASK];
		const StereoPair s5 = m_front[(ft + 3) & GRANULE_MASK] + m_back[(bt + 3) & GRANULE_MASK];

		// Probably an overkill interpolator, but let's go with it for now.
		// Polynomial Interpolators for High-Quality Resampling of
		// Over Sampled Audio by Olli Niemitalo, October 2001.
		// Page 43 -- 6-point, 3rd-order Hermite:
		// https://yehar.com/blog/wp-content/uploads/2009/08/deip.pdf
		const u32 t_frac = m_current_index & ((1 << GRANULE_FRAC_BITS) - 1);
		const float t1 = t_frac / static_cast<float>(1 << GRANULE_FRAC_BITS);
		const float t2 = t1 * t1;
		const float t3 = t2 * t1;
		StereoPair sample = (
			s0 * ((+0.0f + 1.0f * t1 - 2.0f * t2 + 1.0f * t3) * (1.0f / 12.0f)) +
			s1 * ((+0.0f - 8.0f * t1 + 15.0f * t2 - 7.0f * t3) * (1.0f / 12.0f)) +
			s2 * ((+3.0f + 0.0f * t1 - 7.0f * t2 + 4.0f * t3) * (1.0f / 3.0f)) +
			s3 * ((+0.0f + 2.0f * t1 + 5.0f * t2 - 4.0f * t3) * (1.0f / 3.0f)) +
			s4 * ((+0.0f - 1.0f * t1 - 6.0f * t2 + 7.0f * t3) * (1.0f / 12.0f)) +
			s5 * ((+0.0f + 0.0f * t1 + 1.0f * t2 - 1.0f * t3) * (1.0f / 12.0f))
		);

		// Update the looping flag occasionally.
		if (!(num_samples & 31)) {
			queue_looping = m_queue_looping.load(std::memory_order_relaxed);
		}

		// Apply Fade In / Fade Out depending on if we are looping
		if (queue_looping)
			m_fade_volume += fade_out_mul * (0.0f - m_fade_volume);
		else
			m_fade_volume += fade_in_mul * (1.0f - m_fade_volume);

		samples[0] = (int16_t)clamp_value(sample.l * m_fade_volume, -32767.0f, 32767.0f);
		samples[1] = (int16_t)clamp_value(sample.r * m_fade_volume, -32767.0f, 32767.0f);

		samples += 2;
	}
}

void GranularMixer::PushSamples(const s32 *samples, u32 num_samples, float volume) {
	// TODO: This can be massively sped up. Although hardly likely to be a bottleneck.
	while (num_samples-- > 0) {
		const s16 l = clampfloat_s16(samples[0] * volume);
		const s16 r = clampfloat_s16(samples[1] * volume);
		samples += 2;

		m_next_buffer[m_next_buffer_index] = StereoPair(l, r);
		m_next_buffer_index = (m_next_buffer_index + 1) & GRANULE_MASK;

		// The granules overlap by 50%, so we need to enqueue the
		// next buffer every time we fill half of the samples.
		if (m_next_buffer_index == 0 || m_next_buffer_index == m_next_buffer.size() / 2) {
			Enqueue();
		}
	}
}

void GranularMixer::Enqueue() {
	const u32 head = m_queue_head.load(std::memory_order_acquire);

	// Check if we run out of space in the circular queue. (rare)
	u32 next_head = head + 1;
	if ((next_head & GRANULE_QUEUE_MASK) == (m_queue_tail.load(std::memory_order_acquire) & GRANULE_QUEUE_MASK)) {
		WARN_LOG(Log::Audio,
			"Granule Queue has completely filled and audio samples are being dropped. "
			"This should not happen unless the audio backend has stopped requesting audio.");
		return;
	}

	// The compiler (at least MSVC) fails at optimizing this loop using SIMD instructions.
	const u32 start_index = m_next_buffer_index;

	const u32 maskedHead = head & GRANULE_QUEUE_MASK;
	for (u32 i = 0; i < GRANULE_SIZE; ++i) {
		m_queue[maskedHead][i] = m_next_buffer[(i + start_index) & GRANULE_MASK] * g_GranuleWindow[i];
	}

	m_queue_head.store(next_head, std::memory_order_release);
	m_queue_looping.store(false, std::memory_order_relaxed);
}

void GranularMixer::Dequeue(Granule *granule) {
	const u32 granule_queue_size = m_granule_queue_size.load(std::memory_order_relaxed);
	const u32 head = m_queue_head.load(std::memory_order_acquire);
	u32 tail = m_queue_tail.load(std::memory_order_acquire);

	// Checks to see if the queue has gotten too long.
	if ((head - tail) > granule_queue_size) {
		// Jump the playhead to half the queue size behind the head.
		const u32 gap = (granule_queue_size >> 1) + 1;
		tail = (head - gap);
		overruns_++;
	}

	// Checks to see if the queue is empty.
	u32 next_tail = tail + 1;

	bool looping = m_queue_looping.load();

	/*if (!looping && !smoothedQueueSize_ < granule_queue_size / 2) {
		// Repeat a single block occasionally to make sure we have a reasonably sized queue.
		next_tail = tail;
	} else*/ if (next_tail == head) {
		// Only fill gaps when running to prevent stutter on pause.
		CoreState state = coreState;
		const bool is_running = state == CORE_RUNNING_CPU || state == CORE_RUNNING_GE;
		if (g_Config.bFillAudioGaps && is_running) {
			// Jump the playhead to half the queue size behind the head.
			// This will repeat a few past granules I guess? They still contain sensible data.
			// This provides smoother audio playback than suddenly stopping.
			const u32 gap = std::max<u32>(2, granule_queue_size >> 1) - 1;
			next_tail = head - gap;
			underruns_++;
			m_queue_looping.store(true, std::memory_order_relaxed);
		} else {
			// Send a zero granule.
			std::fill(granule->begin(), granule->end(), StereoPair{ 0.0f, 0.0f });
			m_queue_looping.store(false, std::memory_order_relaxed);
			return;
		}
	}

	*granule = m_queue[tail & GRANULE_QUEUE_MASK];
	m_queue_tail.store(next_tail, std::memory_order_release);
}

void GranularMixer::GetStats(GranularStats *stats) {
	stats->queuedGranulesMin = queuedGranulesMin_;
	stats->queuedGranulesMax = queuedGranulesMax_;
	stats->smoothedQueuedGranules = smoothedQueueSize_;
	stats->targetQueueSize = m_granule_queue_size.load(std::memory_order_relaxed);
	stats->maxQueuedGranules = MAX_GRANULE_QUEUE_SIZE;
	stats->fadeVolume = m_fade_volume;
	stats->looping = m_queue_looping;
	stats->overruns = overruns_;
	stats->underruns = underruns_;
	stats->smoothedReadSize = smoothedReadSize_;
	stats->frameTimeEstimate = frameTimeEstimate_;
	stats->queuedSamplesTarget = queuedSamplesTarget_;
	queuedGranulesMin_ = 10000;
	queuedGranulesMax_ = 0;
}
